#if defined(WEBSCENE_NATIVE_ENGINE_ENABLE_MEDIA)
struct audio_context_binding {
    std::unique_ptr<webscene::media::audio_graph> graph;
    std::unordered_map<uint64_t, uint32_t> sources;
    std::unordered_map<uint64_t, const webscene::media::audio_buffer *> attached;
};
std::unordered_map<uint32_t, audio_context_binding> audio_contexts;
std::unordered_set<uint64_t> media_audio_source_elements;
v8::Global<v8::Private> audio_track_key;
uint32_t next_audio_context{1}, next_audio_track{1};
std::unordered_map<uint32_t, std::shared_ptr<webscene::media::audio_track>> audio_tracks;
// Recording adapters retain these native PCM consumers, independently of
// JS/context lifetime. Each track has its own cursor and stop state.
struct native_audio_input_binding {
    uint64_t capture_id{}, generation{};
    uint32_t sample_rate{}, channels{};
    std::string origin;
    std::shared_ptr<webscene::media::audio_capture> source;
    uint64_t next_frame{}, last_timestamp{};
    bool muted{}, ended{};
    uint32_t live_tracks{};
};
struct native_audio_input_track {
    std::shared_ptr<native_audio_input_binding> input;
    v8::Global<v8::Object> object;
    bool stopped{};
};
struct pending_audio_input_event {
    uint64_t capture_id{}, generation{};
    uint32_t kind{}, detail{};
};
std::mutex native_audio_input_mutex;
std::unordered_map<uint64_t, std::shared_ptr<native_audio_input_binding>>
    native_audio_inputs;
std::unordered_map<uint32_t, native_audio_input_track> native_audio_input_tracks;
std::deque<pending_audio_input_event> pending_audio_input_events;
static constexpr size_t maximum_native_audio_inputs = 16U;
static constexpr size_t maximum_pending_audio_input_events = 256U;

void queue_audio_input_stop(const native_audio_input_binding& input) {
    auto request = std::make_unique<native_host_request>();
    request->view.kind = WEBSCENE_HOST_REQUEST_MEDIA_STOP_CAPTURE_V1;
    request->content_type = "application/vnd.webscene.media-capture-stop+json";
    request->url = input.origin;
    const auto json = std::string{"{\"version\":1,\"captureId\":\""}
        + std::to_string(input.capture_id) + "\",\"generation\":\""
        + std::to_string(input.generation) + "\"}";
    request->bytes.assign(json.begin(), json.end());
    (void)enqueue_typed_host_request(std::move(request));
}

void release_audio_input_track(uint32_t id) {
    const auto found = native_audio_input_tracks.find(id);
    if (found == native_audio_input_tracks.end() || found->second.stopped) return;
    auto input = found->second.input;
    found->second.stopped = true;
    found->second.object.Reset();
    bool stop = false;
    {
        std::lock_guard lock(native_audio_input_mutex);
        if (input->live_tracks) --input->live_tracks;
        if (input->live_tracks == 0U && !input->ended) {
            input->ended = true;
            input->source->end();
            native_audio_inputs.erase(input->capture_id);
            stop = true;
        }
    }
    if (stop) queue_audio_input_stop(*input);
}

std::unique_ptr<webscene::media::audio_graph> media_default_audio;
std::unordered_map<uint64_t, uint32_t> media_default_sources;
void start_default_media_audio(uint64_t key, media_binding &b) {
    if (!b.frame.audio || !b.control->playing.load())
        return;
    if (media_audio_source_elements.contains(key))
        return;
    if (!media_default_audio)
        media_default_audio = std::make_unique<webscene::media::audio_graph>();
    if (!media_default_sources.contains(key)) {
        auto id = media_default_audio->create(webscene::media::audio_graph::kind::source);
        media_default_audio->set_source(id, b.frame.audio, b.control);
        media_default_audio->connect(id, 0);
        media_default_sources.emplace(key, id);
    }
    media_default_audio->resume();
}
void release_default_media_audio(uint64_t key) {
    auto found = media_default_sources.find(key);
    if (found != media_default_sources.end()) {
        media_default_audio->disconnect(found->second);
        media_default_sources.erase(found);
        if (media_default_sources.empty())
            media_default_audio.reset();
    }
}

struct audio_decode_binding {
    std::future<webscene::media::audio_buffer> result;
    v8::Global<v8::Context> realm;
    v8::Global<v8::Promise::Resolver> promise;
};
std::vector<audio_decode_binding> audio_decodes;
std::unique_ptr<webscene::media::decode_service> audio_decoder;
void clear_audio_bindings() {
    std::vector<std::shared_ptr<native_audio_input_binding>> inputs;
    {
        std::lock_guard lock(native_audio_input_mutex);
        inputs.reserve(native_audio_inputs.size());
        for (auto& [id, input] : native_audio_inputs) {
            static_cast<void>(id);
            if (!input->ended) {
                input->ended = true;
                input->source->end();
                inputs.push_back(input);
            }
        }
        native_audio_inputs.clear();
        pending_audio_input_events.clear();
    }
    for (const auto& input : inputs) queue_audio_input_stop(*input);
    native_audio_input_tracks.clear();
    audio_track_key.Reset();
    audio_tracks.clear();
    media_audio_source_elements.clear();
    media_default_audio.reset();
    media_default_sources.clear();
    audio_decoder.reset();
    audio_decodes.clear();
    audio_contexts.clear();
}

void drain_audio_input_events() {
    std::deque<pending_audio_input_event> events;
    {
        std::lock_guard lock(native_audio_input_mutex);
        events.swap(pending_audio_input_events);
    }
    for (const auto& event : events) {
        const char* type = nullptr;
        {
            std::lock_guard lock(native_audio_input_mutex);
            const auto binding = native_audio_inputs.find(event.capture_id);
            if (binding == native_audio_inputs.end()
                || binding->second->generation != event.generation) continue;
            if (event.kind == WEBSCENE_MEDIA_CAPTURE_EVENT_MUTE_V1) {
                binding->second->muted = true;
                type = "mute";
            } else if (event.kind == WEBSCENE_MEDIA_CAPTURE_EVENT_UNMUTE_V1) {
                binding->second->muted = false;
                type = "unmute";
            } else {
                binding->second->ended = true;
                binding->second->source->end();
                native_audio_inputs.erase(binding);
                type = "ended";
            }
        }
        for (auto& [id, track] : native_audio_input_tracks) {
            static_cast<void>(id);
            if (track.input->capture_id != event.capture_id
                || track.input->generation != event.generation
                || track.stopped) continue;
            auto object = track.object.Get(isolate);
            if (object.IsEmpty()) continue;
            v8::Local<v8::Context> realm;
            if (!object->GetCreationContext().ToLocal(&realm)) continue;
            v8::Context::Scope scope(realm);
            v8::Local<v8::Value> dispatcher;
            if (object->Get(realm, js_string(isolate, "dispatchEvent")).ToLocal(&dispatcher)
                && dispatcher->IsFunction()) {
                auto instance = create_event_instance(realm);
                instance->Set(realm, js_string(isolate, "type"),
                              js_string(isolate, type)).Check();
                v8::Local<v8::Value> args[] = {instance};
                (void)dispatcher.As<v8::Function>()->Call(realm, object, 1, args);
            }
            if (event.kind == WEBSCENE_MEDIA_CAPTURE_EVENT_ENDED_V1
                || event.kind == WEBSCENE_MEDIA_CAPTURE_EVENT_ERROR_V1)
                track.object.Reset();
        }
    }
}
void refresh_audio_source(uint64_t key, media_binding &b) {
    if (!b.frame.audio)
        return;
    for (auto &[id, c] : audio_contexts) {
        auto found = c.sources.find(key);
        if (found != c.sources.end() && c.attached[key] != b.frame.audio.get()) {
            c.graph->set_source(found->second, b.frame.audio, b.control);
            c.attached[key] = b.frame.audio.get();
        }
    }
}
void drain_audio_decodes() {
    for (size_t i = 0; i < audio_decodes.size();) {
        if (audio_decodes[i].result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++i;
            continue;
        }
        auto job = std::move(audio_decodes[i]);
        audio_decodes.erase(audio_decodes.begin() + i);
        auto realm = job.realm.Get(isolate);
        v8::Context::Scope scope(realm);
        auto resolver = job.promise.Get(isolate);
        try {
            auto pcm = job.result.get();
            auto value = v8::Object::New(isolate);
            value
                ->CreateDataProperty(realm, js_string(isolate, "sampleRate"),
                                     v8::Integer::NewFromUnsigned(isolate, pcm.sample_rate))
                .Check();
            auto channels = v8::Array::New(isolate, pcm.channels);
            for (uint32_t ch = 0; ch < pcm.channels; ++ch) {
                auto buffer = v8::ArrayBuffer::New(isolate, pcm.frames() * sizeof(float));
                auto data = static_cast<float *>(buffer->GetBackingStore()->Data());
                for (uint64_t f = 0; f < pcm.frames(); ++f)
                    data[f] = pcm.samples[f * pcm.channels + ch];
                channels->Set(realm, ch, v8::Float32Array::New(buffer, 0, pcm.frames())).Check();
            }
            value->CreateDataProperty(realm, js_string(isolate, "channels"), channels).Check();
            resolver->Resolve(realm, value).Check();
        } catch (const std::exception &e) {
            resolver->Reject(realm, v8::Exception::Error(js_string(isolate, e.what()))).Check();
        }
    }
}
std::shared_ptr<webscene::media::audio_track> resolve_audio_track(v8::Local<v8::Value> value) {
    if (!value->IsObject() || audio_track_key.IsEmpty())
        throw std::invalid_argument("Native audio track required");
    auto realm = isolate->GetCurrentContext();
    v8::Local<v8::Value> id;
    if (!value.As<v8::Object>()->GetPrivate(realm, audio_track_key.Get(isolate)).ToLocal(&id) ||
        !id->IsUint32())
        throw std::invalid_argument("Native audio track required");
    auto found = audio_tracks.find(id.As<v8::Uint32>()->Value());
    if (found == audio_tracks.end())
        throw std::invalid_argument("Audio track has been released");
    return found->second;
}
static void audio_native_call(const v8::FunctionCallbackInfo<v8::Value> &info) {
    auto *self = current(info.GetIsolate());
    if (!self)
        return;
    auto realm = info.GetIsolate()->GetCurrentContext();
    try {
        if (info.Length() < 1)
            throw std::invalid_argument("Audio operation required");
        auto op = to_utf8(info.GetIsolate(), info[0]);
        if (op == "enumerateDevices" || op == "openCapture") {
            auto resolver = v8::Promise::Resolver::New(realm).ToLocalChecked();
            info.GetReturnValue().Set(resolver->GetPromise());
            const auto reject = [&](const char* message, const char* name) {
                auto error = v8::Exception::Error(js_string(info.GetIsolate(), message));
                if (error->IsObject())
                    error.As<v8::Object>()->Set(realm, js_string(info.GetIsolate(), "name"),
                                                js_string(info.GetIsolate(), name)).Check();
                resolver->Reject(realm, error).Check();
            };
            v8::Local<v8::Value> secure;
            if (realm != self->context.Get(info.GetIsolate())
                || !realm->Global()->Get(realm, js_string(info.GetIsolate(), "isSecureContext")).ToLocal(&secure)
                || !secure->IsTrue()) {
                reject("Media-device enumeration requires a secure top-level context", "SecurityError");
                return;
            }
            constexpr size_t maximum_pending_media_requests = 16U;
            if (self->host_promise_targets.size()
                >= maximum_pending_media_requests) {
                reject("Too many pending media-device requests", "QuotaExceededError");
                return;
            }
            const auto request_id = ++self->next_host_request_id;
            auto request = std::make_unique<native_host_request>();
            request->view.request_id = request_id;
            request->view.kind = op == "openCapture"
                ? WEBSCENE_HOST_REQUEST_MEDIA_OPEN_CAPTURE_V1
                : WEBSCENE_HOST_REQUEST_MEDIA_ENUMERATE_DEVICES_V1;
            request->content_type = op == "openCapture"
                ? "application/vnd.webscene.media-capture-request+json"
                : "application/vnd.webscene.media-devices+json";
            request->url = self->current_security_origin();
            if (op == "openCapture") {
                if (info.Length() < 2 || !info[1]->IsString()) {
                    reject("A canonical media-capture request is required", "TypeError");
                    return;
                }
                auto json = to_utf8(info.GetIsolate(), info[1]);
                if (json.empty() || json.size() > 16U * 1024U) {
                    reject("The media-capture request is too large", "QuotaExceededError");
                    return;
                }
                request->bytes.assign(json.begin(), json.end());
            }
            host_promise_target target;
            target.operation = op == "openCapture"
                ? "openMediaCapture" : "enumerateMediaDevices";
            target.context.Reset(info.GetIsolate(), realm);
            target.resolver.Reset(info.GetIsolate(), resolver);
            self->host_promise_targets.emplace(request_id, std::move(target));
            if (!self->enqueue_typed_host_request(std::move(request))) {
                self->host_promise_targets.erase(request_id);
                reject("The host request queue is full", "QuotaExceededError");
            }
            return;
        }
        if (op == "releaseCapture") {
            if (info.Length() < 3 || !info[1]->IsString() || !info[2]->IsString())
                throw std::invalid_argument("Native capture identity required");
            const auto parse_identity = [](const std::string& text) {
                uint64_t result{};
                const auto parsed = std::from_chars(
                    text.data(), text.data() + text.size(), result);
                if (text.empty() || parsed.ec != std::errc{}
                    || parsed.ptr != text.data() + text.size() || result == 0U)
                    throw std::invalid_argument("Invalid native capture identity");
                return result;
            };
            native_audio_input_binding input;
            input.capture_id = parse_identity(to_utf8(info.GetIsolate(), info[1]));
            input.generation = parse_identity(to_utf8(info.GetIsolate(), info[2]));
            input.origin = self->current_security_origin();
            self->queue_audio_input_stop(input);
            return;
        }
        if (op == "bindCapture") {
            if (info.Length() < 5 || !info[1]->IsString() || !info[2]->IsString())
                throw std::invalid_argument("Native capture identity required");
            const auto capture_text = to_utf8(info.GetIsolate(), info[1]);
            const auto generation_text = to_utf8(info.GetIsolate(), info[2]);
            const auto parse_identity = [](const std::string& text) {
                uint64_t result{};
                const auto parsed = std::from_chars(
                    text.data(), text.data() + text.size(), result);
                if (text.empty() || parsed.ec != std::errc{}
                    || parsed.ptr != text.data() + text.size() || result == 0U)
                    throw std::invalid_argument("Invalid native capture identity");
                return result;
            };
            const auto capture_id = parse_identity(capture_text);
            const auto generation = parse_identity(generation_text);
            const auto rate = info[3]->Uint32Value(realm).FromMaybe(0U);
            const auto channels = info[4]->Uint32Value(realm).FromMaybe(0U);
            if (rate < 8000U || rate > 192000U || channels < 1U || channels > 8U)
                throw std::invalid_argument("Invalid native capture format");
            if (self->audio_tracks.size() >= 256U)
                throw std::length_error("Audio track limit");
            auto source = std::make_shared<webscene::media::audio_capture>(rate);
            auto input = std::make_shared<native_audio_input_binding>();
            input->capture_id = capture_id;
            input->generation = generation;
            input->sample_rate = rate;
            input->channels = channels;
            input->origin = self->current_security_origin();
            input->source = source;
            input->live_tracks = 1U;
            {
                std::lock_guard lock(self->native_audio_input_mutex);
                if (self->native_audio_inputs.size() >= maximum_native_audio_inputs)
                    throw std::length_error("Native audio input limit");
                if (self->native_audio_inputs.contains(capture_id))
                    throw std::invalid_argument("Native capture identity is already active");
                self->native_audio_inputs.emplace(capture_id, input);
            }
            const auto track_id = self->next_audio_track++;
            self->audio_tracks.emplace(
                track_id, std::make_shared<webscene::media::audio_track>(source));
            self->native_audio_input_tracks.emplace(
                track_id, native_audio_input_track{input, {}, false});
            info.GetReturnValue().Set(track_id);
            return;
        }
        if (op == "trackBind" || op == "trackClone" || op == "trackStop"
            || op == "trackEnded" || op == "trackEnabled" || op == "trackMuted") {
            auto id = info[1]->Uint32Value(realm).FromMaybe(0);
            auto found = self->audio_tracks.find(id);
            if (found == self->audio_tracks.end())
                throw std::invalid_argument("Audio track unavailable");
            auto track = found->second;
            if (op == "trackBind") {
                if (info.Length() < 3 || !info[2]->IsObject())
                    throw std::invalid_argument("Audio track object required");
                if (self->audio_track_key.IsEmpty())
                    self->audio_track_key.Reset(info.GetIsolate(), v8::Private::New(info.GetIsolate()));
                info[2]
                    .As<v8::Object>()
                    ->SetPrivate(realm, self->audio_track_key.Get(info.GetIsolate()),
                                 v8::Integer::NewFromUnsigned(info.GetIsolate(), id))
                    .Check();
                if (auto input = self->native_audio_input_tracks.find(id);
                    input != self->native_audio_input_tracks.end())
                    input->second.object.Reset(info.GetIsolate(), info[2].As<v8::Object>());
                return;
            }
            if (op == "trackStop") {
                track->stop();
                self->release_audio_input_track(id);
                return;
            }
            if (op == "trackEnded") {
                info.GetReturnValue().Set(track->ended());
                return;
            }
            if (op == "trackEnabled") {
                track->enabled = info[2]->BooleanValue(info.GetIsolate());
                return;
            }
            if (op == "trackMuted") {
                const auto input = self->native_audio_input_tracks.find(id);
                if (input == self->native_audio_input_tracks.end()) {
                    info.GetReturnValue().Set(false);
                    return;
                }
                std::lock_guard lock(self->native_audio_input_mutex);
                info.GetReturnValue().Set(input->second.input->muted);
                return;
            }
            if (self->audio_tracks.size() >= 256)
                throw std::length_error("Audio track limit");
            auto clone = self->next_audio_track++;
            self->audio_tracks.emplace(clone, track->clone());
            if (const auto input = self->native_audio_input_tracks.find(id);
                input != self->native_audio_input_tracks.end()) {
                {
                    std::lock_guard lock(self->native_audio_input_mutex);
                    ++input->second.input->live_tracks;
                }
                self->native_audio_input_tracks.emplace(
                    clone, native_audio_input_track{input->second.input, {}, false});
            }
            info.GetReturnValue().Set(clone);
            return;
        }
        if (op == "create") {
            if (self->audio_contexts.size() >= 8)
                throw std::length_error("Audio context limit");
            auto id = self->next_audio_context++;
            auto rate = info.Length() > 1 ? info[1]->Uint32Value(realm).FromMaybe(48000) : 48000;
            self->audio_contexts.emplace(
                id,
                audio_context_binding{std::make_unique<webscene::media::audio_graph>(true, rate), {}, {}});
            info.GetReturnValue().Set(id);
            return;
        }
        if (op == "decode") {
            if (info.Length() < 2 || !info[1]->IsArrayBuffer())
                throw std::invalid_argument("decodeAudioData requires ArrayBuffer");
            if (self->audio_decodes.size() >= 8)
                throw std::length_error("Audio decode queue full");
            if (!self->audio_decoder)
                self->audio_decoder = std::make_unique<webscene::media::decode_service>([self] {
                    self->media_work_ready.store(true, std::memory_order_release);
                    if (self->runtime_work_available)
                        self->runtime_work_available();
                });
            auto buffer = info[1].As<v8::ArrayBuffer>();
            if (buffer->WasDetached())
                throw std::invalid_argument("Detached audio data");
            auto bytes = buffer->GetBackingStore();
            if (bytes->ByteLength() > webscene::media::decode_limits{}.encoded_bytes)
                throw std::length_error("Audio input too large");
            auto source = std::make_shared<webscene::media::encoded_source>();
            auto p = static_cast<const uint8_t *>(bytes->Data());
            if (bytes->ByteLength())
                source->bytes.assign(p, p + bytes->ByteLength());
            auto resolver = v8::Promise::Resolver::New(realm).ToLocalChecked();
            auto future = self->audio_decoder->audio(
                source, {}, info.Length() > 2 ? info[2]->Uint32Value(realm).FromMaybe(0) : 0);
            buffer->Detach(v8::Local<v8::Value>()).Check();
            self->audio_decodes.push_back({std::move(future),
                                           v8::Global<v8::Context>(info.GetIsolate(), realm),
                                           v8::Global<v8::Promise::Resolver>(info.GetIsolate(), resolver)});
            info.GetReturnValue().Set(resolver->GetPromise());
            return;
        }
        if (info.Length() < 2)
            throw std::invalid_argument("Audio context required");
        auto id = info[1]->Uint32Value(realm).FromMaybe(0);
        auto found = self->audio_contexts.find(id);
        if (found == self->audio_contexts.end())
            throw std::invalid_argument("Audio context unavailable");
        auto &c = found->second;
        auto number = [&](int i, double fallback = 0.) {
            return info.Length() > i ? info[i]->NumberValue(realm).FromMaybe(fallback) : fallback;
        };
        if (op == "close") {
            self->audio_contexts.erase(found);
            return;
        }
        if (op == "resume") {
            c.graph->resume();
            return;
        }
        if (op == "suspend") {
            c.graph->suspend();
            return;
        }
        if (op == "time") {
            info.GetReturnValue().Set(c.graph->time());
            return;
        }
        if (op == "node") {
            auto kind = static_cast<webscene::media::audio_graph::kind>(static_cast<int>(number(2)));
            if (number(2) < 1 || number(2) > 5)
                throw std::invalid_argument("Invalid audio node kind");
            info.GetReturnValue().Set(c.graph->create(kind));
            return;
        }
        if (op == "trackSource") {
            if (info.Length() < 4)
                throw std::invalid_argument("MediaStreamTrack required");
            c.graph->set_track(number(2), self->resolve_audio_track(info[3]));
            return;
        }
        if (op == "connect") {
            c.graph->connect(number(2), number(3));
            return;
        }
        if (op == "disconnect") {
            c.graph->disconnect(number(2));
            return;
        }
        if (op == "gain") {
            c.graph->set_gain(number(2), number(3), number(4), number(5));
            return;
        }
        if (op == "capture") {
            if (self->audio_tracks.size() >= 256)
                throw std::length_error("Audio track limit");
            auto track = c.graph->capture(number(2));
            auto track_id = self->next_audio_track++;
            self->audio_tracks.emplace(track_id, std::move(track));
            info.GetReturnValue().Set(track_id);
            return;
        }
        if (op == "validateSource") {
            if (info.Length() < 3 || !info[2]->IsObject())
                throw std::invalid_argument("Media element required");
            auto *element = unwrap_node(info[2].As<v8::Object>());
            if (!element || (element->tag != "audio" && element->tag != "video"))
                throw std::invalid_argument("Media element required");
            const auto media = self->media_bindings.find(self->wrapper_key(*element));
            if (media != self->media_bindings.end() && media->second->stream)
                throw std::invalid_argument("Native URL media audio cannot be routed through Web Audio");
            if (self->media_audio_source_elements.contains(self->wrapper_key(*element)))
                throw std::invalid_argument("Media element already has an audio source node");
            return;
        }
        if (op == "source") {
            if (info.Length() < 4 || !info[3]->IsObject())
                throw std::invalid_argument("Media source required");
            auto *element = unwrap_node(info[3].As<v8::Object>());
            if (!element)
                throw std::invalid_argument("Media source required");
            auto key = self->wrapper_key(*element);
            if (self->media_audio_source_elements.contains(key))
                throw std::invalid_argument("Media element already has an audio source node");
            self->media_audio_source_elements.insert(key);
            self->release_default_media_audio(key);
            c.sources.emplace(key, number(2));
            auto media = self->media_bindings.find(key);
            if (media != self->media_bindings.end() && media->second->frame.audio) {
                c.graph->set_source(number(2), media->second->frame.audio, media->second->control);
                c.attached[key] = media->second->frame.audio.get();
            }
            return;
        }
        if (op == "samples") {
            if (info.Length() < 4 || !info[3]->IsFloat32Array())
                throw std::invalid_argument("Analyser requires Float32Array");
            auto array = info[3].As<v8::Float32Array>();
            auto backing = array->Buffer()->GetBackingStore();
            if (array->Buffer()->WasDetached())
                throw std::invalid_argument("Detached analyser array");
            c.graph->analyser(number(2), std::span<float>(reinterpret_cast<float *>(
                                                              static_cast<uint8_t *>(backing->Data()) +
                                                              array->ByteOffset()),
                                                          array->Length()));
            return;
        }
        throw std::invalid_argument("Unknown audio operation");
    } catch (const std::exception &e) {
        info.GetIsolate()->ThrowException(v8::Exception::TypeError(js_string(info.GetIsolate(), e.what())));
    }
}
void install_audio_globals(v8::Local<v8::Context> realm) {
    realm->Global()
        ->Set(realm, js_string(isolate, "__websceneAudio"),
              v8::Function::New(realm, audio_native_call).ToLocalChecked())
        .Check();
    constexpr std::string_view source =
#include "media/audio_platform.js.inc"
        ;
    auto script = v8::Script::Compile(realm, js_dom_string(isolate, std::string(source))).ToLocalChecked();
    script->Run(realm).ToLocalChecked();
}
#endif
