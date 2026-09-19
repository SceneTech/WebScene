#pragma once
#include "owned_image_pool.h"
#include "posix_fd.h"
#include <array>
#include <memory>
#include <vector>

namespace webscene::graphics {
enum class linux_memory_handle : uint32_t { opaque_fd=1, dma_buf=2 };
enum class linux_sync_handle : uint32_t { sync_fd=1, vk_semaphore_opaque_fd=2 };
enum class linux_queue_sharing : uint32_t { exclusive=1, concurrent=2 };

struct linux_external_plane {
    int borrowed_fd{-1};
    uint64_t offset{};
    uint32_t stride{};
};
struct linux_external_wait {
    linux_sync_handle handle{};
    int borrowed_fd{-1};
    uint64_t ordering_domain{};
    uint64_t signaled_value{};
    bool timeline{};
};
struct linux_external_image_snapshot {
    image_metadata metadata{};
    linux_memory_handle memory_handle{};
    linux_queue_sharing queue_sharing{};
    uint64_t allocation_size{};
    uint64_t drm_modifier{};
    uint32_t drm_format{};
    uint32_t memory_type_index{UINT32_MAX};
    uint32_t vk_format{};
    uint32_t vk_image_type{};
    uint32_t vk_tiling{};
    uint32_t vk_usage{};
    uint32_t vk_create_flags{};
    uint32_t vk_sharing_mode{};
    int32_t vk_initial_layout{};
    uint32_t vk_queue_family_index_count{};
    uint32_t sample_count{};
    uint32_t mip_level_count{};
    uint32_t array_layer_count{};
    int32_t producer_layout{};
    int32_t consumer_layout{};
    uint32_t producer_queue_family{UINT32_MAX};
    uint32_t consumer_queue_family{UINT32_MAX};
    std::array<uint32_t,4> vk_queue_family_indices{};
    std::array<uint8_t,16> device_uuid{};
    std::array<uint8_t,16> driver_uuid{};
    bool dedicated_allocation{};
    bool producer_complete{};
    std::vector<linux_external_plane> planes;
    std::vector<linux_external_wait> waits;
};

// Implemented only by a storage allocator that owns Linux external memory.
// export_image borrows its FDs for the duration of the call. The ABI adapter
// duplicates every accepted FD before returning to the host.
struct linux_external_image_provider : image_provider_lifetime {
    image_provider_kind kind()const noexcept final{return image_provider_kind::linux_external;}
    virtual bool export_image(const image_metadata&,linux_external_image_snapshot&)const=0;
};

inline bool same_image_metadata(const image_metadata& a,const image_metadata& b)noexcept{
    return a.canvas==b.canvas&&a.allocation==b.allocation
        &&a.allocation_generation==b.allocation_generation&&a.content_serial==b.content_serial
        &&a.producer_timeline==b.producer_timeline&&a.producer_value==b.producer_value
        &&a.width==b.width&&a.height==b.height&&a.format==b.format&&a.alpha==b.alpha
        &&a.color_space==b.color_space&&a.orientation==b.orientation;
}
inline bool nonzero_uuid(const std::array<uint8_t,16>& value)noexcept{
    for(const auto byte:value)if(byte)return true;
    return false;
}
inline bool valid_linux_external_snapshot(const linux_external_image_snapshot& value,
    const image_metadata& expected)noexcept{
    if(!same_image_metadata(value.metadata,expected)||!nonzero_uuid(value.device_uuid)
        ||value.planes.empty()||value.planes.size()>4||value.waits.size()>UINT32_MAX
        ||value.sample_count!=1||value.mip_level_count!=1||value.array_layer_count!=1
        ||!value.vk_format||value.vk_image_type!=1||!value.vk_usage
        ||(value.vk_initial_layout!=0&&value.vk_initial_layout!=8)
        ||value.producer_layout<=0||value.consumer_layout<=0)return false;
    if(value.queue_sharing==linux_queue_sharing::exclusive){
        if(value.vk_sharing_mode!=0||value.vk_queue_family_index_count
            ||value.producer_queue_family==UINT32_MAX||value.consumer_queue_family==UINT32_MAX)return false;
    }else if(value.queue_sharing==linux_queue_sharing::concurrent){
        if(value.vk_sharing_mode!=1||value.vk_queue_family_index_count<2
            ||value.vk_queue_family_index_count>value.vk_queue_family_indices.size()
            ||value.producer_queue_family!=UINT32_MAX||value.consumer_queue_family!=UINT32_MAX)return false;
        for(uint32_t i=0;i<value.vk_queue_family_index_count;++i){
            if(value.vk_queue_family_indices[i]==UINT32_MAX)return false;
            for(uint32_t j=0;j<i;++j)
                if(value.vk_queue_family_indices[i]==value.vk_queue_family_indices[j])return false;
        }
    }else return false;
    if(!value.allocation_size)return false;
    if(value.memory_handle==linux_memory_handle::opaque_fd){
        if(value.planes.size()!=1||value.memory_type_index>=32
            ||value.drm_format||value.drm_modifier)return false;
    }else if(value.memory_handle==linux_memory_handle::dma_buf){
        if(!value.drm_format||value.memory_type_index!=UINT32_MAX)return false;
    }else return false;
    for(const auto& plane:value.planes){
        if(plane.borrowed_fd<0)return false;
        if(value.memory_handle==linux_memory_handle::dma_buf&&!plane.stride)return false;
    }
    if(value.producer_complete!=value.waits.empty())return false;
    for(size_t index=0;index<value.waits.size();++index){
        const auto& wait=value.waits[index];
        if(wait.borrowed_fd<0||!wait.ordering_domain)return false;
        if(wait.handle==linux_sync_handle::sync_fd){
            if(wait.timeline||wait.signaled_value!=1)return false;
        }else if(wait.handle==linux_sync_handle::vk_semaphore_opaque_fd){
            if(wait.timeline?!wait.signaled_value:wait.signaled_value!=1)return false;
        }else return false;
        for(size_t prior=0;prior<index;++prior)
            if(value.waits[prior].ordering_domain==wait.ordering_domain)return false;
    }
    return true;
}

template<class FdOps> class basic_linux_shared_image_owner {
public:
    using owned_fd=unique_posix_fd<FdOps>;
    struct plane { owned_fd fd; uint64_t offset{}; uint32_t stride{}; };
    struct wait { linux_sync_handle handle{}; owned_fd fd; uint64_t ordering_domain{}; uint64_t value{}; bool timeline{}; };
private:
    linux_external_image_snapshot snapshot_;
    std::vector<plane> planes_;
    std::vector<wait> waits_;
public:
    const linux_external_image_snapshot& snapshot()const noexcept{return snapshot_;}
    const std::vector<plane>& planes()const noexcept{return planes_;}
    const std::vector<wait>& waits()const noexcept{return waits_;}
    static std::unique_ptr<basic_linux_shared_image_owner> create(
        const owned_image_pool::consumer& consumer){
        const auto metadata=consumer.describe();
        const auto anchor=consumer.provider();
        if(!anchor||anchor->kind()!=image_provider_kind::linux_external)return {};
        const auto provider=std::static_pointer_cast<linux_external_image_provider>(anchor);
        linux_external_image_snapshot snapshot;
        if(!provider->export_image(metadata,snapshot)||!valid_linux_external_snapshot(snapshot,metadata))
            throw std::invalid_argument("invalid Linux external image export");
        auto result=std::make_unique<basic_linux_shared_image_owner>();
        result->planes_.reserve(snapshot.planes.size());
        result->waits_.reserve(snapshot.waits.size());
        for(const auto& source:snapshot.planes)
            result->planes_.push_back({owned_fd::duplicate(source.borrowed_fd),source.offset,source.stride});
        for(const auto& source:snapshot.waits)
            result->waits_.push_back({source.handle,owned_fd::duplicate(source.borrowed_fd),
                source.ordering_domain,source.signaled_value,source.timeline});
        // Never retain the provider's borrowed descriptor integers in the
        // immutable metadata copy exposed by owner inspection.
        snapshot.planes.clear();snapshot.waits.clear();
        result->snapshot_=std::move(snapshot);
        return result;
    }
};
#if defined(__linux__)
using linux_shared_image_owner=basic_linux_shared_image_owner<linux_fd_ops>;
#endif
} // namespace webscene::graphics
