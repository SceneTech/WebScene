#include "graphics/dawn_linux_external_provider.h"
#include <stdexcept>

using namespace webscene::graphics;
namespace {
void require(bool value){if(!value)throw std::runtime_error("Dawn Linux external provider contract failed");}
}
int main(){
    auto* first=reinterpret_cast<WGPUDevice>(uintptr_t{1});
    auto* second=reinterpret_cast<WGPUDevice>(uintptr_t{2});
    require(valid_dawn_linux_external_binding(first,first,first));
    require(!valid_dawn_linux_external_binding(nullptr,first,first));
    require(!valid_dawn_linux_external_binding(first,second,first));
    require(!valid_dawn_linux_external_binding(first,first,second));

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
