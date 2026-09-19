#include "../webscene_native_engine.h"
#include "image_lease_abi.h"
#include <cstring>
#if defined(__linux__)
#include "linux_external_image.h"
#include <new>
#include <system_error>
#endif

webscene_gpu_linux_shared_status_v3 webscene_gpu_linux_acquire_shared_v3(
    const webscene_gpu_image_consumer_v3* consumer,void** owner,
    webscene_gpu_linux_shared_image_view_v3* result){
    if(owner)*owner=nullptr;
    if(!owner||!result||result->struct_size<sizeof(*result)||result->version!=3||!consumer)
        return WEBSCENE_GPU_LINUX_SHARED_INVALID_ARGUMENT_V3;
    const auto size=result->struct_size;
    std::memset(result,0,sizeof(*result));result->struct_size=size;result->version=3;
#if defined(__linux__)
    try{
        auto candidate=webscene::graphics::linux_shared_image_owner::create(consumer->value);
        if(!candidate)return WEBSCENE_GPU_LINUX_SHARED_UNSUPPORTED_PROVIDER_V3;
        const auto& source=candidate->snapshot();
        const auto& metadata=source.metadata;
        uint64_t capabilities=WEBSCENE_GPU_LINUX_CAP_DEVICE_UUID_V3
            |WEBSCENE_GPU_LINUX_CAP_EXPLICIT_LAYOUT_V3
            |WEBSCENE_GPU_LINUX_CAP_EXPLICIT_RETIREMENT_V3;
        capabilities|=source.memory_handle==webscene::graphics::linux_memory_handle::opaque_fd
            ?WEBSCENE_GPU_LINUX_CAP_OPAQUE_FD_V3:WEBSCENE_GPU_LINUX_CAP_DMA_BUF_V3;
        if(source.queue_sharing==webscene::graphics::linux_queue_sharing::exclusive)
            capabilities|=WEBSCENE_GPU_LINUX_CAP_QUEUE_FAMILY_OWNERSHIP_V3;
        if(source.dedicated_allocation)capabilities|=WEBSCENE_GPU_LINUX_CAP_DEDICATED_ALLOCATION_V3;
        if(source.producer_complete)capabilities|=WEBSCENE_GPU_LINUX_CAP_PRODUCER_COMPLETE_V3;
        for(const auto& wait:candidate->waits()){
            capabilities|=wait.handle==webscene::graphics::linux_sync_handle::sync_fd
                ?WEBSCENE_GPU_LINUX_CAP_SYNC_FD_V3:WEBSCENE_GPU_LINUX_CAP_VK_SEMAPHORE_OPAQUE_FD_V3;
            if(wait.timeline)capabilities|=WEBSCENE_GPU_LINUX_CAP_TIMELINE_SEMAPHORE_V3;
        }
        result->capabilities=capabilities;
        result->allocation_size=source.allocation_size;result->drm_modifier=source.drm_modifier;
        result->allocation=metadata.allocation;result->allocation_generation=metadata.allocation_generation;
        result->content_serial=metadata.content_serial;result->producer_timeline=metadata.producer_timeline;
        result->producer_value=metadata.producer_value;result->width=metadata.width;result->height=metadata.height;
        result->format=static_cast<uint32_t>(metadata.format);result->alpha=static_cast<uint32_t>(metadata.alpha);
        result->color_space=static_cast<uint32_t>(metadata.color_space);
        result->orientation=static_cast<uint32_t>(metadata.orientation);
        result->memory_handle_type=static_cast<uint32_t>(source.memory_handle);
        result->queue_sharing=static_cast<uint32_t>(source.queue_sharing);
        result->drm_format=source.drm_format;result->memory_type_index=source.memory_type_index;
        result->vk_format=source.vk_format;result->vk_image_type=source.vk_image_type;
        result->vk_tiling=source.vk_tiling;result->vk_usage=source.vk_usage;
        result->vk_create_flags=source.vk_create_flags;result->vk_sharing_mode=source.vk_sharing_mode;
        result->vk_initial_layout=source.vk_initial_layout;
        result->vk_queue_family_index_count=source.vk_queue_family_index_count;
        result->sample_count=source.sample_count;
        result->mip_level_count=source.mip_level_count;result->array_layer_count=source.array_layer_count;
        result->producer_layout=source.producer_layout;result->consumer_layout=source.consumer_layout;
        result->producer_queue_family=source.producer_queue_family;
        result->consumer_queue_family=source.consumer_queue_family;
        std::memcpy(result->vk_queue_family_indices,source.vk_queue_family_indices.data(),
            sizeof(result->vk_queue_family_indices));
        std::memcpy(result->device_uuid,source.device_uuid.data(),source.device_uuid.size());
        std::memcpy(result->driver_uuid,source.driver_uuid.data(),source.driver_uuid.size());
        result->plane_count=static_cast<uint32_t>(candidate->planes().size());
        result->producer_wait_count=static_cast<uint32_t>(candidate->waits().size());
        *owner=candidate.release();
        return WEBSCENE_GPU_LINUX_SHARED_SUCCESS_V3;
    }catch(const std::bad_alloc&){return WEBSCENE_GPU_LINUX_SHARED_OUT_OF_MEMORY_V3;}
    catch(const std::system_error&){return WEBSCENE_GPU_LINUX_SHARED_FD_DUPLICATION_FAILED_V3;}
    catch(const std::invalid_argument&){return WEBSCENE_GPU_LINUX_SHARED_INVALID_DESCRIPTOR_V3;}
    catch(...){return WEBSCENE_GPU_LINUX_SHARED_INTERNAL_ERROR_V3;}
#else
    return WEBSCENE_GPU_LINUX_SHARED_UNSUPPORTED_PROVIDER_V3;
#endif
}

uint8_t webscene_gpu_linux_get_plane_v3(const void* owner,uint32_t index,
    webscene_gpu_linux_plane_view_v3* result){
    if(!result||result->struct_size<sizeof(*result)||result->version!=3)return 0;
    const auto size=result->struct_size;std::memset(result,0,sizeof(*result));
    result->struct_size=size;result->version=3;result->borrowed_fd=-1;
#if defined(__linux__)
    if(!owner)return 0;
    const auto& planes=static_cast<const webscene::graphics::linux_shared_image_owner*>(owner)->planes();
    if(index>=planes.size())return 0;
    result->borrowed_fd=planes[index].fd.get();result->stride=planes[index].stride;
    result->offset=planes[index].offset;return 1;
#else
    return 0;
#endif
}

uint8_t webscene_gpu_linux_get_producer_wait_v3(const void* owner,uint32_t index,
    webscene_gpu_linux_sync_view_v3* result){
    if(!result||result->struct_size<sizeof(*result)||result->version!=3)return 0;
    const auto size=result->struct_size;std::memset(result,0,sizeof(*result));
    result->struct_size=size;result->version=3;result->borrowed_fd=-1;
#if defined(__linux__)
    if(!owner)return 0;
    const auto& waits=static_cast<const webscene::graphics::linux_shared_image_owner*>(owner)->waits();
    if(index>=waits.size())return 0;
    const auto& wait=waits[index];result->handle_type=static_cast<uint32_t>(wait.handle);
    result->flags=wait.timeline?WEBSCENE_GPU_LINUX_SYNC_TIMELINE_V3:0;
    result->borrowed_fd=wait.fd.get();result->ordering_domain=wait.ordering_domain;
    result->signaled_value=wait.value;return 1;
#else
    return 0;
#endif
}

void webscene_gpu_linux_release_shared_v3(void* owner){
#if defined(__linux__)
    delete static_cast<webscene::graphics::linux_shared_image_owner*>(owner);
#else
    (void)owner;
#endif
}
