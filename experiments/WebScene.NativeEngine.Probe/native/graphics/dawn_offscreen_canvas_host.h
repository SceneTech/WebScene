#pragma once
#include "native_webgpu_offscreen_surface.h"
#include "webgpu_canvas_host.h"
#include <array>
#include <algorithm>

namespace webscene::graphics {
// Runtime canvases use the device selected by JavaScript, never a second
// application device. The three-slot pool and completion worker are shared with
// native offscreen authoring. Only an explicit capture performs CPU readback.
class dawn_offscreen_canvas_host final {
public:
    struct submission {
        dawn_canvas_images::submitted_frame frame;
        std::shared_ptr<offscreen_gpu_dependencies> dependencies;
        image_metadata metadata;
        submission(dawn_canvas_images::submitted_frame value,
                   std::shared_ptr<offscreen_gpu_dependencies> owner)
            : frame(std::move(value)), dependencies(std::move(owner)),
              metadata(frame.image.describe()) {}
    };
    struct snapshot { std::shared_ptr<submission> value; };
    struct ready_image {
        owned_image_pool::retained image;
        std::shared_ptr<offscreen_gpu_dependencies> dependencies;
        image_metadata describe() const { return image.describe(); }
    };
private:
    const std::thread::id thread_ = std::this_thread::get_id();
    const uint64_t budget_;
    std::shared_ptr<completion_wake> wake_;
    wgpu::Instance instance_;
    std::shared_ptr<offscreen_gpu_dependencies> dependencies_;
    std::unique_ptr<dawn_canvas_images> images_;
    std::optional<dawn_canvas_images::frame> active_;
    std::array<std::shared_ptr<submission>, 3> pending_{};
    std::weak_ptr<submission> latest_;
    size_t active_slot_{};

    void check_thread() const {
        if (thread_ != std::this_thread::get_id())
            throw std::logic_error("Offscreen Runtime canvas requires its engine thread");
    }
    void clear_failed() {
        for (auto& value : pending_)
            if (value && value->frame.status->load(std::memory_order_acquire)
                             == dawn_canvas_images::submission_status::failed)
                value.reset();
    }
public:
    explicit dawn_offscreen_canvas_host(uint64_t budget,
                                       std::shared_ptr<completion_wake> wake = {})
        : budget_(budget), wake_(std::move(wake)) {
        if (!budget) throw std::invalid_argument("Canvas byte budget must be positive");
    }
    dawn_offscreen_canvas_host(const dawn_offscreen_canvas_host&) = delete;
    dawn_offscreen_canvas_host& operator=(const dawn_offscreen_canvas_host&) = delete;
    ~dawn_offscreen_canvas_host() {
        // GPUCanvasContext retires even an unpresented texture before teardown.
        if (active_) std::terminate();
    }
    void set_instance(const wgpu::Instance& instance) {
        check_thread();
        if (!instance || images_) throw std::logic_error("Set canvas instance before configuration");
        instance_ = instance;
    }
    wgpu::Texture acquire(image_metadata metadata, const wgpu::Device& device,
                          const wgpu::TextureDescriptor& descriptor) {
        check_thread();
        if (active_) throw std::logic_error("Canvas already has a current texture");
        if (!instance_ || !device) throw std::logic_error("Missing Runtime canvas instance/device");
        clear_failed();
        size_t slot = 0;
        while (slot < pending_.size() && pending_[slot]) ++slot;
        if (slot == pending_.size()) return {};
        if (!dependencies_ || dependencies_->gpu->device.Get() != device.Get()) {
            // Do not multiply the budget when JavaScript reconfigures devices.
            // Previously submitted/retained images must drain before switching.
            if (images_ && images_->busy_images()) return {};
            images_.reset();
            dependencies_.reset();
            auto gpu = std::make_shared<native_webgpu_device>();
            gpu->instance = instance_;
            gpu->device = device;
            dependencies_ = std::make_shared<offscreen_gpu_dependencies>(std::move(gpu));
            images_ = std::make_unique<dawn_canvas_images>(device, budget_, 128, wake_);
        }
        auto frame = images_->acquire(metadata, &descriptor);
        if (!frame) return {};
        auto texture = frame->texture;
        active_ = std::move(frame);
        active_slot_ = slot;
        return texture;
    }
    void retire(const wgpu::Texture& texture, bool present) {
        check_thread();
        if (!active_ || texture.Get() != active_->texture.Get())
            throw std::invalid_argument("Retirement requires the current Runtime canvas texture");
        wgpu::Future future;
        auto retired = images_->retire_submitted(std::move(*active_), wake_, &future);
        active_.reset();
        // The callback retains the producer even if image admission failed or
        // the document discarded this frame. Completion is not RAF-dependent.
        dependencies_->completion.enqueue(future);
        latest_.reset();
        if (retired && present) {
            auto value = std::make_shared<submission>(std::move(*retired), dependencies_);
            pending_[active_slot_] = value;
            latest_ = value;
        }
    }
    std::unique_ptr<snapshot> capture_latest_submission() {
        check_thread();
        if (active_) throw std::logic_error("Retire the rendering opportunity before snapshotting");
        auto value = latest_.lock();
        return value ? std::make_unique<snapshot>(snapshot{std::move(value)}) : nullptr;
    }
    std::optional<ready_image> take_ready() {
        check_thread();
        clear_failed();
        for (auto& value : pending_) {
            if (!value || value->frame.status->load(std::memory_order_acquire)
                              != dawn_canvas_images::submission_status::success) continue;
            auto image = value->frame.image.retain();
            if (!image) return {};
            auto dependencies = value->dependencies;
            value.reset();
            return ready_image{std::move(*image), std::move(dependencies)};
        }
        return {};
    }
    bool has_completed_retirements() const {
        check_thread();
        for (const auto& value : pending_)
            if (value && value->frame.status->load(std::memory_order_acquire)
                             != dawn_canvas_images::submission_status::pending) return true;
        return false;
    }
    bool idle() {
        check_thread();
        clear_failed();
        return !active_ && std::none_of(pending_.begin(), pending_.end(), [](const auto& p) { return bool(p); });
    }
    bool can_acquire() {
        check_thread();
        clear_failed();
        return !active_ && (!images_ || images_->busy_images() < 3)
            && std::any_of(pending_.begin(), pending_.end(), [](const auto& p) { return !p; });
    }
    size_t busy_images() const { check_thread(); return images_ ? images_->busy_images() : 0; }
    image_lease_pool::occupancy inspect_occupancy() const {
        check_thread(); return images_ ? images_->inspect_occupancy() : image_lease_pool::occupancy{};
    }
};

class dawn_offscreen_scene_snapshot final : public webscene_gpu_image_snapshot {
    std::unique_ptr<dawn_offscreen_canvas_host::snapshot> ticket_;
    std::shared_ptr<const webscene_gpu_image_lease_v3> resolved_;
public:
    explicit dawn_offscreen_scene_snapshot(std::unique_ptr<dawn_offscreen_canvas_host::snapshot> ticket)
        : ticket_(std::move(ticket)) {
        if (!ticket_ || !ticket_->value) throw std::invalid_argument("Missing offscreen snapshot ticket");
    }
    image_metadata describe() const override { return ticket_->value->metadata; }
    status state() const override {
        switch (ticket_->value->frame.status->load(std::memory_order_acquire)) {
            case dawn_canvas_images::submission_status::pending: return status::pending;
            case dawn_canvas_images::submission_status::success: return status::ready;
            default: return status::failed;
        }
    }
    std::shared_ptr<const webscene_gpu_image_lease_v3> resolve() override {
        if (state() != status::ready) return {};
        if (!resolved_) {
            auto image = ticket_->value->frame.image.retain();
            if (!image) return {};
            resolved_ = std::make_shared<webscene_gpu_image_lease_v3>(
                std::move(*image), ticket_->value->dependencies);
        }
        return resolved_;
    }
};

inline webgpu_canvas_host make_offscreen_webgpu_canvas_host(
    std::shared_ptr<dawn_offscreen_canvas_host> provider,
    std::function<image_metadata()> next_metadata) {
    if (!provider || !next_metadata) throw std::invalid_argument("Missing canvas provider/identity source");
    webgpu_canvas_host result;
    result.validate = [](const webgpu_canvas_configuration& config) {
        if ((config.format != wgpu::TextureFormat::BGRA8Unorm && config.format != wgpu::TextureFormat::RGBA8Unorm)
            || config.color_space != "srgb" || config.tone_mapping != "standard")
            throw std::invalid_argument("Offscreen capture requires RGBA8/BGRA8, sRGB and standard tone mapping");
        if (!config.device.HasFeature(wgpu::FeatureName::ImplicitDeviceSynchronization))
            throw std::invalid_argument("Runtime offscreen device lacks implicit synchronization");
    };
    result.acquire = [provider, next_metadata = std::move(next_metadata)](
        const webgpu_canvas_configuration& config, const webgpu_texture_descriptor& descriptor) {
        auto metadata = next_metadata();
        metadata.width = descriptor.size.width; metadata.height = descriptor.size.height;
        metadata.format = config.format == wgpu::TextureFormat::BGRA8Unorm ? image_format::bgra8_unorm : image_format::rgba8_unorm;
        metadata.color_space = image_color_space::srgb;
        metadata.alpha = config.alpha_mode == "opaque" ? image_alpha::opaque : image_alpha::premultiplied;
        metadata.orientation = image_orientation::top_left;
        wgpu::Texture texture;
        descriptor.with_native([&](const auto& native) { texture = provider->acquire(metadata, config.device, native); });
        return texture;
    };
    result.retire = [provider](const wgpu::Texture& texture, bool present) { provider->retire(texture, present); };
    return result;
}
} // namespace webscene::graphics
