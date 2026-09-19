#pragma once
#include "dawn_shared_image.h"
#include "linux_external_image.h"
#include "webscene/dawn_native_device.h"
#include <atomic>
#include <span>

namespace webscene::graphics {
#ifndef WEBSCENE_DAWN_LINUX_EXTERNAL_FACTORY_VERSION
#define WEBSCENE_DAWN_LINUX_EXTERNAL_FACTORY_VERSION 1U
#endif
// The WebGPU C ABI bundled with WebScene exposes Vulkan shared-memory import,
// but deliberately does not expose Dawn's VkDevice/VkPhysicalDevice.  A native
// allocator therefore has to be installed by the code which created the exact
// Dawn device.  The raw WGPUDevice token below prevents accidental cross-device
// use; the allocator remains responsible for proving the native Vulkan identity.
enum class dawn_linux_external_status : uint32_t {
    success,
    invalid_argument,
    not_vulkan,
    missing_memory_feature,
    missing_fence_feature,
    native_device_provider_required,
    device_mismatch,
    allocation_failed,
    invalid_allocation,
    import_failed,
    begin_failed,
    end_failed,
    uninitialized_handoff,
    unsupported_fence,
    fence_export_failed,
    invalid_device_factory,
    native_device_query_failed,
    device_lost
};

struct dawn_linux_external_device_lifetime {
    WGPUAdapter dawn_adapter_token{};
    WGPUDevice dawn_device_token{};
    // Opaque borrowed identities for the exact Vulkan tuple. They are never
    // called or cast by WebScene; native_owner controls their typed lifetime.
    const void* vk_physical_device{};
    const void* vk_device{};
    const void* vk_queue{};
    std::array<uint8_t,16> device_uuid{};
    std::array<uint8_t,16> driver_uuid{};
    uint32_t dawn_queue_family{UINT32_MAX};
    // Set by the Dawn device-lost callback before the host releases its device.
    std::shared_ptr<std::atomic<bool>> device_lost;
    // Host-defined owner for the exact native device/physical-device/queue
    // tuple. It must outlive every allocation made from those Vulkan handles.
    std::shared_ptr<void> native_owner;
};

struct dawn_linux_external_native_device_owner {
    explicit dawn_linux_external_native_device_owner(const wgpu::Device& value):device(value) {}
    wgpu::Device device;
};

// Populate the factory lifetime directly from the WebScene-pinned Dawn build.
// The copied wgpu::Device keeps every returned borrowed Vulkan identity alive.
inline dawn_linux_external_status bind_dawn_linux_external_device(
    const wgpu::Adapter& adapter,const wgpu::Device& device,
    std::shared_ptr<std::atomic<bool>> device_lost,
    std::shared_ptr<const dawn_linux_external_device_lifetime>& result) {
    result.reset();
    if(!adapter||!device||!device_lost)
        return dawn_linux_external_status::invalid_argument;
    if(device_lost->load(std::memory_order_acquire))
        return dawn_linux_external_status::device_lost;
    webscene_dawn_native_device_v1 native{};
    native.struct_size=sizeof(native);
    native.version=WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION;
    const auto status=websceneDawnQueryVulkanDeviceV1(device.Get(),&native);
    if(status==WEBSCENE_DAWN_NATIVE_DEVICE_LOST_V1)
        return dawn_linux_external_status::device_lost;
    if(status==WEBSCENE_DAWN_NATIVE_DEVICE_NOT_VULKAN_V1)
        return dawn_linux_external_status::not_vulkan;
    if(status!=WEBSCENE_DAWN_NATIVE_DEVICE_SUCCESS_V1)
        return dawn_linux_external_status::native_device_query_failed;
    if(native.adapter!=adapter.Get()||native.device!=device.Get()||
        !native.vk_physical_device||!native.vk_device||!native.vk_queue||
        native.queue_family==UINT32_MAX)
        return dawn_linux_external_status::native_device_query_failed;
    auto lifetime=std::make_shared<dawn_linux_external_device_lifetime>();
    lifetime->dawn_adapter_token=native.adapter;
    lifetime->dawn_device_token=native.device;
    lifetime->vk_physical_device=native.vk_physical_device;
    lifetime->vk_device=native.vk_device;
    lifetime->vk_queue=native.vk_queue;
    for(size_t index=0;index<lifetime->device_uuid.size();++index) {
        lifetime->device_uuid[index]=native.device_uuid[index];
        lifetime->driver_uuid[index]=native.driver_uuid[index];
    }
    lifetime->dawn_queue_family=native.queue_family;
    lifetime->device_lost=std::move(device_lost);
    lifetime->native_owner=std::make_shared<dawn_linux_external_native_device_owner>(device);
    if(!nonzero_uuid(lifetime->device_uuid)||!nonzero_uuid(lifetime->driver_uuid))
        return dawn_linux_external_status::native_device_query_failed;
    if(lifetime->device_lost->load(std::memory_order_acquire))
        return dawn_linux_external_status::device_lost;
    result=std::move(lifetime);
    return dawn_linux_external_status::success;
}

struct dawn_linux_external_allocator;

// Atomic result of host-owned Dawn/Vulkan device creation. The pinned public
// Dawn API cannot build this object: it exposes VkInstance but no supported
// VkDevice, VkPhysicalDevice or VkQueue accessor and cannot wrap a host VkDevice.
// AppScene (or another native host with an exact-device integration) creates
// the Dawn device and allocator together and publishes one shared lifetime.
struct dawn_linux_external_device {
    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    std::shared_ptr<const dawn_linux_external_device_lifetime> lifetime;
    std::shared_ptr<dawn_linux_external_allocator> allocator;
};

struct dawn_linux_external_device_request {
    std::span<const wgpu::FeatureName> required_features;
    bool force_fallback_adapter{};
};

struct dawn_linux_external_device_factory {
    virtual ~dawn_linux_external_device_factory()=default;
    virtual dawn_linux_external_status create(
        const dawn_linux_external_device_request&,
        std::shared_ptr<dawn_linux_external_device>&)=0;
};

struct dawn_linux_external_capabilities {
    bool vulkan{};
    bool dma_buf{};
    bool opaque_fd{};
    bool dedicated_allocation{};
    bool sync_fd{};
    bool semaphore_opaque_fd{};
    uint32_t vulkan_driver_version{};
    bool has_memory(linux_memory_handle kind) const noexcept {
        return kind==linux_memory_handle::dma_buf ? dma_buf
            : kind==linux_memory_handle::opaque_fd && opaque_fd;
    }
    bool has_fence() const noexcept{return sync_fd||semaphore_opaque_fd;}
};

inline dawn_linux_external_status inspect_dawn_linux_external_capabilities(
    const wgpu::Adapter& adapter,const wgpu::Device& device,
    dawn_linux_external_capabilities& result) {
    result={};
    if(!adapter||!device)return dawn_linux_external_status::invalid_argument;
    wgpu::AdapterPropertiesVk vk{};
    wgpu::AdapterInfo info{};
    info.nextInChain=&vk;
    if(adapter.GetInfo(&info)!=wgpu::Status::Success||info.backendType!=wgpu::BackendType::Vulkan)
        return dawn_linux_external_status::not_vulkan;
    result.vulkan=true;
    result.vulkan_driver_version=vk.driverVersion;
    result.dma_buf=device.HasFeature(wgpu::FeatureName::SharedTextureMemoryDmaBuf);
    result.opaque_fd=device.HasFeature(wgpu::FeatureName::SharedTextureMemoryOpaqueFD);
    result.dedicated_allocation=device.HasFeature(
        wgpu::FeatureName::SharedTextureMemoryVkDedicatedAllocation);
    result.sync_fd=device.HasFeature(wgpu::FeatureName::SharedFenceSyncFD);
    result.semaphore_opaque_fd=device.HasFeature(
        wgpu::FeatureName::SharedFenceVkSemaphoreOpaqueFD);
    if(!result.dma_buf&&!result.opaque_fd)
        return dawn_linux_external_status::missing_memory_feature;
    if(!result.has_fence())return dawn_linux_external_status::missing_fence_feature;
    // A feature-complete public Dawn device can import external allocations,
    // but cannot allocate/export one or reveal the native device UUID/queues.
    return dawn_linux_external_status::native_device_provider_required;
}

struct dawn_linux_external_allocation : linux_external_image_provider {
    // Must be the same public handle used when the native allocator was bound.
    virtual WGPUDevice dawn_device_token() const noexcept=0;
    // Retaining the exact shared object is part of allocation ownership. A
    // token/UUID copy alone cannot keep the native Vulkan device alive.
    virtual std::shared_ptr<const dawn_linux_external_device_lifetime>
        device_lifetime() const noexcept=0;
    // Required only for opaque-FD. It points to a VkImageCreateInfo retained by
    // this allocation through ImportSharedTextureMemory.
    virtual const void* vk_image_create_info() const noexcept=0;
    // Maps each Dawn output fence to a stable, nonzero synchronization domain.
    virtual uint64_t fence_ordering_domain(size_t index,wgpu::SharedFenceType) const noexcept=0;
    virtual bool fence_is_timeline(size_t index,wgpu::SharedFenceType) const noexcept=0;
};

struct dawn_linux_external_allocator {
    virtual ~dawn_linux_external_allocator()=default;
    // Capture device.Get() when the allocator is installed next to Dawn device
    // creation. Ordinary Dawn textures are never accepted as external storage.
    virtual WGPUDevice dawn_device_token() const noexcept=0;
    virtual std::shared_ptr<const dawn_linux_external_device_lifetime>
        device_lifetime() const noexcept=0;
    virtual dawn_linux_external_status allocate(const image_metadata&,
        wgpu::TextureFormat,wgpu::TextureUsage,
        std::shared_ptr<dawn_linux_external_allocation>&)=0;
};

inline bool same_dawn_linux_external_identity(
    const std::shared_ptr<const dawn_linux_external_device_lifetime>& expected,
    const std::shared_ptr<const dawn_linux_external_device_lifetime>& actual) noexcept {
    return expected&&expected==actual&&expected->native_owner&&
        expected->device_lost&&
        !expected->device_lost->load(std::memory_order_acquire)&&
        expected->dawn_adapter_token&&expected->dawn_device_token&&
        expected->dawn_adapter_token==actual->dawn_adapter_token&&
        expected->dawn_device_token==actual->dawn_device_token&&
        expected->vk_physical_device&&expected->vk_device&&expected->vk_queue&&
        nonzero_uuid(expected->device_uuid)&&nonzero_uuid(expected->driver_uuid)&&
        expected->dawn_queue_family!=UINT32_MAX;
}

inline bool dawn_linux_external_device_is_lost(
    const std::shared_ptr<const dawn_linux_external_device_lifetime>& value) noexcept {
    return value&&value->device_lost&&
        value->device_lost->load(std::memory_order_acquire);
}

inline bool dawn_linux_snapshot_matches_device(
    const linux_external_image_snapshot& snapshot,
    const dawn_linux_external_device_lifetime& device) noexcept {
    if(snapshot.device_uuid!=device.device_uuid||snapshot.driver_uuid!=device.driver_uuid)
        return false;
    if(snapshot.queue_sharing==linux_queue_sharing::exclusive)
        return snapshot.consumer_queue_family==device.dawn_queue_family;
    if(snapshot.queue_sharing!=linux_queue_sharing::concurrent)return false;
    for(uint32_t index=0;index<snapshot.vk_queue_family_index_count;++index)
        if(snapshot.vk_queue_family_indices[index]==device.dawn_queue_family)return true;
    return false;
}

inline bool valid_dawn_linux_external_binding(WGPUDevice expected,
    WGPUDevice allocator,WGPUDevice allocation) noexcept;

inline bool valid_dawn_linux_external_device(
    const dawn_linux_external_device& value) noexcept {
    return value.instance&&value.adapter&&value.device&&value.allocator&&value.lifetime&&
        same_dawn_linux_external_identity(value.lifetime,
            value.allocator->device_lifetime())&&
        value.adapter.Get()==value.lifetime->dawn_adapter_token&&
        valid_dawn_linux_external_binding(value.device.Get(),
            value.lifetime->dawn_device_token,value.allocator->dawn_device_token());
}

#if defined(__linux__)
// Concrete ownership boundary returned by an exact-device Vulkan allocator.
// native_allocation_owner owns the VkImage/VkDeviceMemory (and any allocator
// state); this object owns the exported memory/wait FDs. SharedTextureMemory is
// destroyed before the provider releases this allocation.
class owned_dawn_linux_external_allocation final : public dawn_linux_external_allocation {
    std::shared_ptr<const dawn_linux_external_device_lifetime> device_;
    std::shared_ptr<void> native_allocation_owner_;
    std::shared_ptr<const void> vk_image_create_info_;
    owned_posix_fd memory_fd_;
    std::vector<owned_posix_fd> wait_fds_;
    linux_external_image_snapshot snapshot_;
    uint64_t first_fence_ordering_domain_{};
    bool opaque_fences_are_timeline_{};
public:
    WGPUDevice dawn_device_token() const noexcept override {
        return device_?device_->dawn_device_token:nullptr;
    }
    std::shared_ptr<const dawn_linux_external_device_lifetime>
        device_lifetime() const noexcept override {return device_;}
    const void* vk_image_create_info() const noexcept override {
        return vk_image_create_info_.get();
    }
    uint64_t fence_ordering_domain(size_t index,wgpu::SharedFenceType) const noexcept override {
        if(index>UINT64_MAX-first_fence_ordering_domain_)return 0;
        return first_fence_ordering_domain_+index;
    }
    bool fence_is_timeline(size_t,wgpu::SharedFenceType type) const noexcept override {
        return type==wgpu::SharedFenceType::VkSemaphoreOpaqueFD&&opaque_fences_are_timeline_;
    }
    bool export_image(const image_metadata& expected,
        linux_external_image_snapshot& result) const override {
        if(!same_dawn_linux_external_identity(device_,device_)||
            !same_image_metadata(expected,snapshot_.metadata))return false;
        result=snapshot_;return true;
    }

    static dawn_linux_external_status adopt(
        std::shared_ptr<const dawn_linux_external_device_lifetime> device,
        linux_external_image_snapshot snapshot,owned_posix_fd memory_fd,
        std::vector<owned_posix_fd> wait_fds,
        std::shared_ptr<void> native_allocation_owner,
        std::shared_ptr<const void> vk_image_create_info,
        uint64_t first_fence_ordering_domain,bool opaque_fences_are_timeline,
        std::shared_ptr<dawn_linux_external_allocation>& result) {
        result.reset();
        if(!same_dawn_linux_external_identity(device,device)||!memory_fd||!native_allocation_owner||
            !first_fence_ordering_domain||snapshot.planes.empty()||
            snapshot.waits.size()!=wait_fds.size())
            return dawn_linux_external_status::invalid_allocation;
        if(snapshot.memory_handle==linux_memory_handle::opaque_fd) {
            if(snapshot.planes.size()!=1||!vk_image_create_info)
                return dawn_linux_external_status::invalid_allocation;
        } else if(snapshot.memory_handle==linux_memory_handle::dma_buf) {
            // The pinned Dawn import supports multiple plane layouts only when
            // every plane names the same DMA-BUF FD.
            if(snapshot.planes.size()>3||vk_image_create_info)
                return dawn_linux_external_status::invalid_allocation;
        } else return dawn_linux_external_status::invalid_allocation;
        for(auto& plane:snapshot.planes)plane.borrowed_fd=memory_fd.get();
        for(size_t index=0;index<snapshot.waits.size();++index) {
            if(!wait_fds[index])return dawn_linux_external_status::invalid_allocation;
            snapshot.waits[index].borrowed_fd=wait_fds[index].get();
        }
        if(!valid_linux_external_snapshot(snapshot,snapshot.metadata)||
            !dawn_linux_snapshot_matches_device(snapshot,*device))
            return dawn_linux_external_status::invalid_allocation;
        auto owned=std::make_shared<owned_dawn_linux_external_allocation>();
        owned->device_=std::move(device);
        owned->native_allocation_owner_=std::move(native_allocation_owner);
        owned->vk_image_create_info_=std::move(vk_image_create_info);
        owned->memory_fd_=std::move(memory_fd);
        owned->wait_fds_=std::move(wait_fds);
        owned->snapshot_=std::move(snapshot);
        owned->first_fence_ordering_domain_=first_fence_ordering_domain;
        owned->opaque_fences_are_timeline_=opaque_fences_are_timeline;
        result=std::move(owned);
        return dawn_linux_external_status::success;
    }
};
#endif

inline bool valid_dawn_linux_external_binding(WGPUDevice expected,
    WGPUDevice allocator,WGPUDevice allocation) noexcept {
    return expected&&expected==allocator&&expected==allocation;
}
inline bool valid_dawn_linux_fence_handoff(linux_sync_handle kind,bool timeline,
    uint64_t value,uint64_t ordering_domain) noexcept {
    if(!ordering_domain)return false;
    if(kind==linux_sync_handle::sync_fd)return !timeline&&value==1;
    if(kind==linux_sync_handle::vk_semaphore_opaque_fd)
        return timeline ? value>0 : value==1;
    return false;
}
inline bool apply_dawn_linux_layout_handoff(linux_external_image_snapshot& value,
    int32_t old_layout,int32_t new_layout) noexcept {
    if(old_layout<=0||new_layout<=0)return false;
    value.producer_layout=old_layout;value.consumer_layout=new_layout;
    std::swap(value.producer_queue_family,value.consumer_queue_family);
    return true;
}
inline bool same_dawn_linux_external_allocation(const linux_external_image_snapshot& a,
    const linux_external_image_snapshot& b) noexcept {
    if(a.memory_handle!=b.memory_handle||a.queue_sharing!=b.queue_sharing||
        a.allocation_size!=b.allocation_size||a.drm_modifier!=b.drm_modifier||
        a.drm_format!=b.drm_format||a.memory_type_index!=b.memory_type_index||
        a.vk_format!=b.vk_format||a.vk_image_type!=b.vk_image_type||
        a.vk_tiling!=b.vk_tiling||a.vk_usage!=b.vk_usage||
        a.vk_create_flags!=b.vk_create_flags||a.vk_sharing_mode!=b.vk_sharing_mode||
        a.vk_initial_layout!=b.vk_initial_layout||
        a.vk_queue_family_index_count!=b.vk_queue_family_index_count||
        a.sample_count!=b.sample_count||a.mip_level_count!=b.mip_level_count||
        a.array_layer_count!=b.array_layer_count||a.vk_queue_family_indices!=b.vk_queue_family_indices||
        a.device_uuid!=b.device_uuid||a.driver_uuid!=b.driver_uuid||
        a.dedicated_allocation!=b.dedicated_allocation||a.planes.size()!=b.planes.size())return false;
    for(size_t index=0;index<a.planes.size();++index)
        if(a.planes[index].borrowed_fd!=b.planes[index].borrowed_fd||
            a.planes[index].offset!=b.planes[index].offset||
            a.planes[index].stride!=b.planes[index].stride)return false;
    return true;
}

// Imported external allocation plus the complete BeginAccess/EndAccess cycle.
// Call all methods on the Dawn device owner thread.  export_image starts
// succeeding only after a fully initialized EndAccess handoff has been encoded.
class dawn_linux_external_provider final : public linux_external_image_provider {
    std::shared_ptr<dawn_linux_external_device> exact_device_;
    std::shared_ptr<const dawn_linux_external_device_lifetime> device_lifetime_;
    wgpu::Device device_;
    std::shared_ptr<dawn_linux_external_allocation> allocation_;
    std::shared_ptr<dawn_shared_image> shared_;
    linux_external_image_snapshot published_;
    std::vector<owned_posix_fd> output_fds_;
    std::vector<wgpu::SharedFence> input_fences_;
    std::vector<uint64_t> input_values_;
    bool active_{},complete_{};

    static wgpu::TextureDescriptor texture_description(const image_metadata& metadata,
        wgpu::TextureFormat format,wgpu::TextureUsage usage) {
        wgpu::TextureDescriptor result{};
        result.dimension=wgpu::TextureDimension::e2D;
        result.size={metadata.width,metadata.height,1};
        result.format=format;
        result.usage=usage;
        result.mipLevelCount=1;
        result.sampleCount=1;
        return result;
    }
    dawn_linux_external_status import_input_fences(
        const linux_external_image_snapshot& source) {
        input_fences_.clear();input_values_.clear();
        input_fences_.reserve(source.waits.size());input_values_.reserve(source.waits.size());
        for(const auto& wait:source.waits) {
            wgpu::SharedFenceDescriptor descriptor{};
            wgpu::SharedFenceSyncFDDescriptor sync_fd{};
            wgpu::SharedFenceVkSemaphoreOpaqueFDDescriptor opaque_fd{};
            if(wait.handle==linux_sync_handle::sync_fd) {
                if(!device_.HasFeature(wgpu::FeatureName::SharedFenceSyncFD))
                    return dawn_linux_external_status::missing_fence_feature;
                sync_fd.handle=wait.borrowed_fd;descriptor.nextInChain=&sync_fd;
            } else if(wait.handle==linux_sync_handle::vk_semaphore_opaque_fd) {
                if(!device_.HasFeature(wgpu::FeatureName::SharedFenceVkSemaphoreOpaqueFD))
                    return dawn_linux_external_status::missing_fence_feature;
                opaque_fd.handle=wait.borrowed_fd;descriptor.nextInChain=&opaque_fd;
            } else return dawn_linux_external_status::unsupported_fence;
            auto fence=device_.ImportSharedFence(&descriptor);
            if(!fence)return dawn_linux_external_status::begin_failed;
            input_fences_.push_back(std::move(fence));
            input_values_.push_back(wait.signaled_value);
        }
        return dawn_linux_external_status::success;
    }
    dawn_linux_external_status publish_handoff(
        const wgpu::SharedTextureMemoryEndAccessState& handoff,
        const wgpu::SharedTextureMemoryVkImageLayoutEndState& layout) {
        if(!handoff.initialized||handoff.fenceCount==0||
            handoff.fenceCount!=handoff.signaledValueCount||layout.oldLayout<=0||layout.newLayout<=0)
            return dawn_linux_external_status::uninitialized_handoff;
        linux_external_image_snapshot next;
        if(!allocation_->export_image(published_.metadata,next)||
            !same_dawn_linux_external_allocation(next,published_))
            return dawn_linux_external_status::invalid_allocation;
        if(!apply_dawn_linux_layout_handoff(next,layout.oldLayout,layout.newLayout))
            return dawn_linux_external_status::uninitialized_handoff;
        next.producer_complete=true;
        next.waits.clear();
        output_fds_.clear();output_fds_.reserve(handoff.fenceCount);
        next.waits.reserve(handoff.fenceCount);
        try {
            for(size_t index=0;index<handoff.fenceCount;++index) {
                wgpu::SharedFenceExportInfo type_info{};
                handoff.fences[index].ExportInfo(&type_info);
                int exported=-1;
                linux_sync_handle kind{};
                if(type_info.type==wgpu::SharedFenceType::SyncFD) {
                    wgpu::SharedFenceSyncFDExportInfo details{};
                    wgpu::SharedFenceExportInfo info{};info.nextInChain=&details;
                    handoff.fences[index].ExportInfo(&info);
                    exported=details.handle;kind=linux_sync_handle::sync_fd;
                } else if(type_info.type==wgpu::SharedFenceType::VkSemaphoreOpaqueFD) {
                    wgpu::SharedFenceVkSemaphoreOpaqueFDExportInfo details{};
                    wgpu::SharedFenceExportInfo info{};info.nextInChain=&details;
                    handoff.fences[index].ExportInfo(&info);
                    exported=details.handle;kind=linux_sync_handle::vk_semaphore_opaque_fd;
                } else return dawn_linux_external_status::unsupported_fence;
                auto owned=owned_posix_fd::adopt(exported);
                const auto domain=allocation_->fence_ordering_domain(index,type_info.type);
                const auto timeline=allocation_->fence_is_timeline(index,type_info.type);
                const auto value=handoff.signaledValues[index];
                if(!valid_dawn_linux_fence_handoff(kind,timeline,value,domain))
                    return dawn_linux_external_status::fence_export_failed;
                output_fds_.push_back(std::move(owned));
                next.waits.push_back({kind,output_fds_.back().get(),domain,value,timeline});
            }
        } catch(const std::exception&) {
            return dawn_linux_external_status::fence_export_failed;
        }
        if(!valid_linux_external_snapshot(next,next.metadata))
            return dawn_linux_external_status::invalid_allocation;
        published_=std::move(next);complete_=true;
        return dawn_linux_external_status::success;
    }
public:
    bool export_image(const image_metadata& expected,
        linux_external_image_snapshot& result) const override {
        if(!same_dawn_linux_external_identity(device_lifetime_,device_lifetime_)||
            !complete_||!same_image_metadata(expected,published_.metadata))return false;
        result=published_;return true;
    }
    const wgpu::Texture& texture() const noexcept{return shared_->texture();}

    static dawn_linux_external_status create(const wgpu::Adapter& adapter,
        const wgpu::Device& device,dawn_linux_external_allocator& allocator,
        const image_metadata& metadata,wgpu::TextureFormat format,wgpu::TextureUsage usage,
        std::shared_ptr<dawn_linux_external_provider>& result) {
        result.reset();
        dawn_linux_external_capabilities capabilities;
        const auto support=inspect_dawn_linux_external_capabilities(adapter,device,capabilities);
        if(support!=dawn_linux_external_status::native_device_provider_required)return support;
        const auto lifetime=allocator.device_lifetime();
        if(dawn_linux_external_device_is_lost(lifetime))
            return dawn_linux_external_status::device_lost;
        if(!same_dawn_linux_external_identity(lifetime,lifetime)||
            adapter.Get()!=lifetime->dawn_adapter_token||
            !valid_dawn_linux_external_binding(device.Get(),
                lifetime->dawn_device_token,allocator.dawn_device_token()))
            return dawn_linux_external_status::device_mismatch;
        std::shared_ptr<dawn_linux_external_allocation> allocation;
        auto status=allocator.allocate(metadata,format,usage,allocation);
        if(status!=dawn_linux_external_status::success)return status;
        if(dawn_linux_external_device_is_lost(lifetime))
            return dawn_linux_external_status::device_lost;
        if(!allocation||!same_dawn_linux_external_identity(
                lifetime,allocation->device_lifetime())||
            !valid_dawn_linux_external_binding(device.Get(),
                allocator.dawn_device_token(),allocation->dawn_device_token()))
            return dawn_linux_external_status::device_mismatch;
        linux_external_image_snapshot snapshot;
        if(!allocation->export_image(metadata,snapshot)||
            !valid_linux_external_snapshot(snapshot,metadata)||
            !dawn_linux_snapshot_matches_device(snapshot,*lifetime)||
            !capabilities.has_memory(snapshot.memory_handle)||
            (snapshot.dedicated_allocation&&!capabilities.dedicated_allocation))
            return dawn_linux_external_status::invalid_allocation;

        wgpu::SharedTextureMemoryDescriptor import{};
        std::vector<wgpu::SharedTextureMemoryDmaBufPlane> planes;
        wgpu::SharedTextureMemoryDmaBufDescriptor dma_buf{};
        wgpu::SharedTextureMemoryOpaqueFDDescriptor opaque_fd{};
        if(snapshot.memory_handle==linux_memory_handle::dma_buf) {
            planes.reserve(snapshot.planes.size());
            for(const auto& plane:snapshot.planes)
                planes.push_back({plane.borrowed_fd,plane.offset,plane.stride});
            dma_buf.size={metadata.width,metadata.height,1};
            dma_buf.drmFormat=snapshot.drm_format;dma_buf.drmModifier=snapshot.drm_modifier;
            dma_buf.planeCount=planes.size();dma_buf.planes=planes.data();
            import.nextInChain=&dma_buf;
        } else {
            const auto create_info=allocation->vk_image_create_info();
            if(!create_info)return dawn_linux_external_status::invalid_allocation;
            opaque_fd.vkImageCreateInfo=create_info;
            opaque_fd.memoryFD=snapshot.planes.front().borrowed_fd;
            opaque_fd.memoryTypeIndex=snapshot.memory_type_index;
            opaque_fd.allocationSize=snapshot.allocation_size;
            opaque_fd.dedicatedAllocation=snapshot.dedicated_allocation;
            import.nextInChain=&opaque_fd;
        }
        const auto description=texture_description(metadata,format,usage);
        auto shared=dawn_shared_image::import(device,import,description,allocation);
        if(!shared)return dawn_linux_external_status::import_failed;
        auto created=std::shared_ptr<dawn_linux_external_provider>(
            new dawn_linux_external_provider);
        created->device_lifetime_=lifetime;created->device_=device;
        created->allocation_=std::move(allocation);
        created->shared_=std::move(shared);created->published_=std::move(snapshot);
        result=std::move(created);
        return dawn_linux_external_status::success;
    }

    static dawn_linux_external_status create(dawn_linux_external_device_factory& factory,
        const dawn_linux_external_device_request& request,const image_metadata& metadata,
        wgpu::TextureFormat format,wgpu::TextureUsage usage,
        std::shared_ptr<dawn_linux_external_provider>& result) {
        result.reset();
        std::shared_ptr<dawn_linux_external_device> exact;
        const auto status=factory.create(request,exact);
        if(status!=dawn_linux_external_status::success)return status;
        if(!exact||!valid_dawn_linux_external_device(*exact))
            return dawn_linux_external_status::invalid_device_factory;
        auto provider_status=create(exact->adapter,exact->device,*exact->allocator,
            metadata,format,usage,result);
        if(provider_status==dawn_linux_external_status::success)
            result->exact_device_=std::move(exact);
        return provider_status;
    }

    dawn_linux_external_status begin_access() {
        if(!same_dawn_linux_external_identity(device_lifetime_,device_lifetime_))
            return dawn_linux_external_status::device_lost;
        if(active_||complete_)return dawn_linux_external_status::begin_failed;
        linux_external_image_snapshot source;
        if(!allocation_->export_image(published_.metadata,source)||
            !valid_linux_external_snapshot(source,published_.metadata)||
            !same_dawn_linux_external_allocation(source,published_))
            return dawn_linux_external_status::invalid_allocation;
        auto status=import_input_fences(source);
        if(status!=dawn_linux_external_status::success)return status;
        wgpu::SharedTextureMemoryVkImageLayoutBeginState layout{};
        layout.oldLayout=source.producer_layout;layout.newLayout=source.consumer_layout;
        wgpu::SharedTextureMemoryBeginAccessDescriptor access{};
        access.nextInChain=&layout;access.initialized=source.producer_complete;
        access.fenceCount=input_fences_.size();access.fences=input_fences_.data();
        access.signaledValueCount=input_values_.size();access.signaledValues=input_values_.data();
        if(!shared_->begin(access))return dawn_linux_external_status::begin_failed;
        active_=true;return dawn_linux_external_status::success;
    }

    dawn_linux_external_status end_access() {
        if(!active_)return dawn_linux_external_status::end_failed;
        if(!same_dawn_linux_external_identity(device_lifetime_,device_lifetime_)) {
            active_=false;
            return dawn_linux_external_status::device_lost;
        }
        wgpu::SharedTextureMemoryVkImageLayoutEndState layout{};
        wgpu::SharedTextureMemoryEndAccessState handoff{};handoff.nextInChain=&layout;
        if(!shared_->end(handoff)){active_=false;return dawn_linux_external_status::end_failed;}
        active_=false;
        if(!shared_->expire_texture())return dawn_linux_external_status::end_failed;
        return publish_handoff(handoff,layout);
    }
};
} // namespace webscene::graphics
