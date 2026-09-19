#include "graphics/dawn_linux_external_provider.h"
#include <stdexcept>

using namespace webscene::graphics;
namespace {
void require(bool value){if(!value)throw std::runtime_error("Dawn Linux external provider contract failed");}
}
int main(){
    static_assert(WEBSCENE_DAWN_LINUX_EXTERNAL_FACTORY_VERSION==1);
    static_assert(WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION==1);
    webscene_dawn_native_device_v1 query{};
    query.struct_size=sizeof(query);
    query.version=WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION;
    require(websceneDawnQueryVulkanDeviceV1(nullptr,&query)==
        WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_ARGUMENT_V1);
    auto* first=reinterpret_cast<WGPUDevice>(uintptr_t{1});
    auto* second=reinterpret_cast<WGPUDevice>(uintptr_t{2});
    require(websceneDawnQueryVulkanDeviceV1(first,&query)==
        WEBSCENE_DAWN_NATIVE_DEVICE_FOREIGN_DEVICE_V1);
    query.version=WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION+1;
    require(websceneDawnQueryVulkanDeviceV1(first,&query)==
        WEBSCENE_DAWN_NATIVE_DEVICE_INCOMPATIBLE_ABI_V1);
    require(valid_dawn_linux_external_binding(first,first,first));
    require(!valid_dawn_linux_external_binding(nullptr,first,first));
    require(!valid_dawn_linux_external_binding(first,second,first));
    require(!valid_dawn_linux_external_binding(first,first,second));

    auto native_owner=std::make_shared<int>(42);
    auto device=std::make_shared<dawn_linux_external_device_lifetime>();
    device->dawn_adapter_token=reinterpret_cast<WGPUAdapter>(uintptr_t{6});
    device->dawn_device_token=first;
    device->vk_physical_device=reinterpret_cast<void*>(uintptr_t{3});
    device->vk_device=reinterpret_cast<void*>(uintptr_t{4});
    device->vk_queue=reinterpret_cast<void*>(uintptr_t{5});
    device->device_uuid[0]=1;
    device->driver_uuid[0]=2;device->dawn_queue_family=4;
    device->device_lost=std::make_shared<std::atomic<bool>>(false);
    device->native_owner=native_owner;
    require(same_dawn_linux_external_identity(device,device));
    auto copied=std::make_shared<dawn_linux_external_device_lifetime>(*device);
    require(!same_dawn_linux_external_identity(device,copied));
    device->device_lost->store(true,std::memory_order_release);
    require(!same_dawn_linux_external_identity(device,device));
    require(dawn_linux_external_device_is_lost(device));
    device->device_lost->store(false,std::memory_order_release);

    linux_external_image_snapshot exact;
    exact.device_uuid=device->device_uuid;exact.driver_uuid=device->driver_uuid;
    exact.queue_sharing=linux_queue_sharing::exclusive;
    exact.consumer_queue_family=device->dawn_queue_family;
    require(dawn_linux_snapshot_matches_device(exact,*device));
    exact.consumer_queue_family=5;
    require(!dawn_linux_snapshot_matches_device(exact,*device));
    exact.queue_sharing=linux_queue_sharing::concurrent;
    exact.vk_queue_family_index_count=2;
    exact.vk_queue_family_indices={3,4,0,0};
    require(dawn_linux_snapshot_matches_device(exact,*device));
    exact.vk_queue_family_indices={3,5,0,0};
    require(!dawn_linux_snapshot_matches_device(exact,*device));

    require(valid_dawn_linux_fence_handoff(linux_sync_handle::sync_fd,false,1,7));
    require(!valid_dawn_linux_fence_handoff(linux_sync_handle::sync_fd,true,1,7));
    require(!valid_dawn_linux_fence_handoff(linux_sync_handle::sync_fd,false,2,7));
    require(valid_dawn_linux_fence_handoff(
        linux_sync_handle::vk_semaphore_opaque_fd,true,9,8));
    require(valid_dawn_linux_fence_handoff(
        linux_sync_handle::vk_semaphore_opaque_fd,false,1,8));
    require(!valid_dawn_linux_fence_handoff(
        linux_sync_handle::vk_semaphore_opaque_fd,true,0,8));
    require(!valid_dawn_linux_fence_handoff(
        linux_sync_handle::vk_semaphore_opaque_fd,false,1,0));

    linux_external_image_snapshot image;
    image.producer_layout=2;image.consumer_layout=3;
    image.producer_queue_family=4;image.consumer_queue_family=5;
    require(apply_dawn_linux_layout_handoff(image,6,7));
    require(image.producer_layout==6&&image.consumer_layout==7);
    require(image.producer_queue_family==5&&image.consumer_queue_family==4);
    require(!apply_dawn_linux_layout_handoff(image,0,7));
    require(image.producer_layout==6&&image.consumer_layout==7);

    linux_external_image_snapshot same=image;
    same.planes={{11,0,64}};image.planes=same.planes;
    require(same_dawn_linux_external_allocation(image,same));
    same.planes.front().offset=4;
    require(!same_dawn_linux_external_allocation(image,same));
    same=image;same.device_uuid[0]=1;
    require(!same_dawn_linux_external_allocation(image,same));
    same=image;same.producer_layout=99;same.consumer_queue_family=99;
    require(same_dawn_linux_external_allocation(image,same));
}
