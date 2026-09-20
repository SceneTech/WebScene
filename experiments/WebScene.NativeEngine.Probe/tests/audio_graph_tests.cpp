#include "audio_graph.h"
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace webscene::media;
void check(bool value, const char *why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "Expected rejection");
}
int main() {
    try {
        audio_graph graph(false);
        auto source = graph.create(audio_graph::kind::source), gain = graph.create(audio_graph::kind::gain),
             meter = graph.create(audio_graph::kind::analyser),
             capture = graph.create(audio_graph::kind::stream);
        auto pcm = std::make_shared<audio_buffer>();
        pcm->channels = 2;
        pcm->sample_rate = 48000;
        pcm->samples.resize(48000 * 2);
        for (size_t i = 0; i < 48000; ++i) {
            pcm->samples[i * 2] = .5f;
            pcm->samples[i * 2 + 1] = -.25f;
        }
        auto control = std::make_shared<playback_control>();
        control->set(0, 1, true, 1, false);
        graph.set_source(source, pcm, control);
        graph.connect(source, gain);
        graph.connect(gain, meter);
        graph.connect(meter, 0);
        graph.connect(meter, capture);
        auto recording = graph.capture(capture), sibling = recording->clone();
        graph.set_gain(gain, .5f, 0, 0);
        graph.resume();
        std::array<float, 256> output{};
        graph.render(output.data(), 128);
        for (size_t i = 0; i < 128; ++i) {
            check(std::abs(output[i * 2] - .25) < 1e-6, "Left gain channel");
            check(std::abs(output[i * 2 + 1] + .125) < 1e-6, "Right gain channel");
        }
        std::array<float, 128> samples{};
        graph.analyser(meter, samples);
        for (float s : samples)
            check(std::abs(s - .0625) < 1e-6, "Analyser did not contain actual mix");
        graph.analyser(capture, samples);
        for (float s : samples)
            check(std::abs(s - .0625) < 1e-6, "Capture bus did not contain actual mix");
        std::array<float, 256> recorded{};
        auto packet = recording->read(recorded);
        check(packet.frames == 128 && packet.first_frame == 0 && packet.dropped == 0,
              "Capture timestamp/frame count");
        for (size_t i = 0; i < 128; ++i) {
            check(std::abs(recorded[i * 2] - .25) < 1e-6, "Recorded left channel");
            check(std::abs(recorded[i * 2 + 1] + .125) < 1e-6, "Recorded right channel");
        }
        recording->stop();
        check(recording->ended() && !sibling->ended(), "Track stop affected sibling");
        check(sibling->read(recorded).frames == 128, "Independent capture cursor");
        const auto before = graph.time();
        graph.suspend();
        graph.render(output.data(), 128);
        for (float f : output)
            check(f == 0, "Suspended context not silent");
        check(graph.time() == before, "Suspended clock advanced");
        graph.resume();
        graph.set_gain(gain, 0, graph.time(), .01);
        graph.render(output.data(), 128);
        check(output[0] > .24 && output[254] < output[0] && output[254] > 0,
              "Target automation did not ramp");
        control->set(0, 1, true, 1, true);
        graph.render(output.data(), 128);
        for (float f : output)
            check(f == 0, "Muted media not silent");
        rejects([&] { graph.connect(meter, source); });
        rejects([&] { graph.set_gain(gain, 0, -1, .1); });
        rejects([&] { graph.connect(999, 0); });
        graph.suspend();
        for (int i = 0; i < 32; ++i)
            graph.set_gain(gain, 1, 100 + i, 0);
        rejects([&] { graph.set_gain(gain, 1, 200, 0); });
        graph.close();
        check(sibling->ended(), "Context close did not end capture");
        rejects([&] { graph.resume(); });
        {
            audio_graph g(false);
            auto input = g.create(audio_graph::kind::source);
            auto data = std::make_shared<audio_buffer>();
            data->channels = 1;
            data->sample_rate = 48000;
            data->samples.resize(2048);
            for (size_t i = 0; i < data->samples.size(); ++i)
                data->samples[i] = float(i) / 2048;
            auto clock = std::make_shared<playback_control>();
            clock->set(0, 2, true, 1, false);
            clock->epoch = 0;
            g.set_source(input, data, clock);
            g.connect(input, 0);
            g.resume();
            std::array<float, 512> block{};
            g.render_at(block.data(), 256, 0);
            for (size_t i = 0; i < 256; ++i) {
                check(std::abs(block[i * 2] - float(i * 2) / 2048) < 1e-6, "Rate/quantum continuity");
                check(block[i * 2] == block[i * 2 + 1], "Mono to stereo conversion");
            }
            clock->set(.01, 1, true, 1, false);
            clock->epoch = 0;
            g.render_at(block.data(), 128, 0);
            check(std::abs(block[0] - 480.f / 2048) < 1e-6, "Seek did not reset audio cursor");
        }
        {
            audio_capture ring(48000);
            uint64_t cursor = 0;
            std::array<float, 256> signal{};
            signal.fill(.2f);
            for (int i = 0; i < 130; ++i)
                ring.write(signal);
            auto data = ring.read(cursor, signal);
            check(data.dropped == 256 && data.first_frame == 256 && data.frames == 128,
                  "Capture overrun not reported");
        }
        {
            audio_capture ring(48000);
            const std::array<float, 3> mono{.25F, -.5F, .75F};
            ring.write_interleaved(mono.data(), mono.size(), 1U);
            uint64_t cursor = 0U;
            std::array<float, 6> stereo{};
            const auto data = ring.read(cursor, stereo);
            check(data.frames == mono.size(), "Interleaved capture frame count changed");
            for (size_t index = 0; index < mono.size(); ++index)
                check(stereo[index * 2U] == mono[index]
                          && stereo[index * 2U + 1U] == mono[index],
                    "Interleaved mono capture was not duplicated to stereo");
        }
        {
            audio_graph live(false, 48000);
            auto source = std::make_shared<audio_capture>(16000);
            auto track = std::make_shared<audio_track>(source);
            const auto input = live.create(audio_graph::kind::track);
            const auto meter = live.create(audio_graph::kind::analyser);
            live.set_track(input, track);
            live.connect(input, meter);
            live.resume();
            std::array<float, 64> mono{};
            mono.fill(.4F);
            source->write_interleaved(mono.data(), mono.size(), 1U);
            std::array<float, 256> output{};
            live.render(output.data(), 128U);
            for (const auto sample : output)
                check(sample == 0.F, "Unconnected microphone source echoed to output");
            std::array<float, 128> analysed{};
            live.analyser(meter, analysed);
            for (const auto sample : analysed)
                check(std::abs(sample - .4F) < 1e-6F,
                    "16 kHz microphone source did not resample into analyser");

            track->enabled = false;
            source->write_interleaved(mono.data(), mono.size(), 1U);
            live.render(output.data(), 128U);
            live.analyser(meter, analysed);
            for (const auto sample : analysed)
                check(sample == 0.F, "Disabled microphone track was not silent");
            track->enabled = true;
            source->set_muted(true);
            source->write_interleaved(mono.data(), mono.size(), 1U);
            live.render(output.data(), 128U);
            live.analyser(meter, analysed);
            for (const auto sample : analysed)
                check(sample == 0.F, "Muted microphone capture was not silent");
            source->set_muted(false);

            for (size_t index = 0U; index < 300U; ++index)
                source->write_interleaved(mono.data(), mono.size(), 1U);
            live.render(output.data(), 128U);
            auto metrics = live.track_metrics(input);
            check(metrics.rendered_frames == 512U && metrics.dropped_frames != 0U,
                "Microphone graph source did not report its bounded-ring overrun");
            source->end();
            live.render(output.data(), 128U);
            metrics = live.track_metrics(input);
            check(metrics.ended, "Microphone graph source did not observe track end");
            live.close();
        }
        {
            audio_graph gate(false, 48000);
            auto source = std::make_shared<audio_capture>(48000);
            auto track = std::make_shared<audio_track>(source);
            const auto input = gate.create(audio_graph::kind::track);
            const auto meter = gate.create(audio_graph::kind::analyser);
            gate.set_track(input, track);
            gate.connect(input, meter);
            gate.resume();
            std::array<float, 128> mono{};
            std::array<float, 256> output{};
            const auto started = std::chrono::steady_clock::now();
            for (size_t quantum = 0U; quantum < 4096U; ++quantum) {
                source->write_interleaved(mono.data(), mono.size(), 1U);
                gate.render(output.data(), 128U);
            }
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            check(elapsed < 2000,
                "Live microphone graph gate exceeded two seconds");
            gate.close();
        }
        {
            audio_graph worklet(false, 16000);
            auto source = std::make_shared<audio_capture>(16000);
            auto track = std::make_shared<audio_track>(source);
            const auto input = worklet.create(audio_graph::kind::track);
            const auto processor = worklet.create(audio_graph::kind::worklet);
            worklet.set_track(input, track);
            worklet.connect(input, processor);
            worklet.connect(processor, 0U);
            auto captured = worklet.capture_worklet(processor);
            std::atomic<uint32_t> wakes{};
            worklet.set_worklet_available([&wakes] { ++wakes; });
            worklet.resume();
            std::array<float, 128> mono{};
            mono.fill(.3F);
            source->write_interleaved(mono.data(), mono.size(), 1U);
            std::array<float, 256> output{};
            worklet.render(output.data(), 128U);
            for (const auto sample : output)
                check(sample == 0.F, "PCM worklet input echoed to destination");
            std::array<float, 256> quantum{};
            const auto packet = captured->read(quantum);
            check(packet.frames == 128U && wakes.load() == 1U,
                "PCM worklet did not publish one fixed quantum and one wake");
            for (const auto sample : quantum)
                check(std::abs(sample - .3F) < 1e-6F,
                    "PCM worklet capture changed input samples");
            worklet.begin_worklet_drain();
            source->write_interleaved(mono.data(), mono.size(), 1U);
            worklet.render(output.data(), 128U);
            check(wakes.load() == 2U,
                "PCM worklet wake did not rearm after owner-thread drain");
            worklet.close();
        }
        for (size_t cycle = 0U; cycle < 100U; ++cycle) {
            audio_graph graph_cycle(false, 48000);
            auto source = std::make_shared<audio_capture>(48000);
            auto track = std::make_shared<audio_track>(source);
            const auto input = graph_cycle.create(audio_graph::kind::track);
            graph_cycle.set_track(input, track);
            graph_cycle.close();
        }
        std::cout << "Audio graph: native mixing, gain automation, analyser/capture, suspend/mute, "
                     "cycle/limit and teardown passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
