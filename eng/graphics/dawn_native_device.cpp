#include "dawn_native_device_internal.h"
#include "webscene/dawn_native_device.h"

#include <cstring>
#include <mutex>
#include <new>

#include "src/dawn/common/Ref.h"
#include "src/dawn/native/Adapter.h"
#include "src/dawn/native/Device.h"
#include "src/dawn/native/PhysicalDevice.h"
#include "src/dawn/native/vulkan/DeviceVk.h"
#include "src/dawn/native/vulkan/Forward.h"
#include "src/dawn/native/vulkan/PhysicalDeviceVk.h"
#include "src/dawn/native/vulkan/QueueVk.h"

namespace {
struct live_devices {
    std::mutex mutex;
    struct entry {
        WGPUDevice token;
        dawn::native::DeviceBase* device;
        entry* next;
    };
    entry* first{};
};

live_devices& devices() {
    // Construct in static storage without registering a destructor. Dawn
    // devices may outlive ordinary process statics during loader teardown.
    alignas(live_devices) static unsigned char storage[sizeof(live_devices)];
    static auto* value=::new(static_cast<void*>(storage)) live_devices;
    return *value;
}

dawn::Ref<dawn::native::DeviceBase> find_device(WGPUDevice token) noexcept {
    auto& registry=devices();
    std::lock_guard lock(registry.mutex);
    for(auto* current=registry.first;current;current=current->next) {
        if(current->token!=token)continue;
        if(!current->device->TryAddRef())return {};
        return dawn::AcquireRef(current->device);
    }
    return {};
}

bool nonzero_uuid(const uint8_t* value) noexcept {
    for(size_t index=0;index<VK_UUID_SIZE;++index)if(value[index])return true;
    return false;
}
}

namespace webscene::dawn_bridge {
void register_device(dawn::native::DeviceBase* device) noexcept {
    if(!device)return;
    const auto token=dawn::native::ToAPI(device);
    auto* value=new(std::nothrow) live_devices::entry{token,device,nullptr};
    if(!value)return;
    auto& registry=devices();
    std::lock_guard lock(registry.mutex);
    for(auto* current=registry.first;current;current=current->next) {
        if(current->token!=token)continue;
        // A duplicate authoritative registration is harmless. A token
        // collision remains bound to the first live object and therefore
        // makes a query for the second object fail closed.
        delete value;
        return;
    }
    value->next=registry.first;
    registry.first=value;
}

void unregister_device(dawn::native::DeviceBase* device) noexcept {
    if(!device)return;
    auto& registry=devices();
    std::lock_guard lock(registry.mutex);
    const auto token=dawn::native::ToAPI(device);
    auto** link=&registry.first;
    while(*link) {
        auto* current=*link;
        if(current->token==token&&current->device==device) {
            *link=current->next;
            delete current;
            return;
        }
        link=&current->next;
    }
}
}

extern "C" webscene_dawn_native_device_status_v1 websceneDawnQueryVulkanDeviceV1(
    WGPUDevice token,webscene_dawn_native_device_v1* result) {
    if(!token||!result)return WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_ARGUMENT_V1;
    if(result->struct_size<sizeof(webscene_dawn_native_device_v1)||
        result->version!=WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION_V1)
        return WEBSCENE_DAWN_NATIVE_DEVICE_INCOMPATIBLE_ABI_V1;
    auto base=find_device(token);
    if(!base)return WEBSCENE_DAWN_NATIVE_DEVICE_FOREIGN_DEVICE_V1;
    auto guard=base->GetGuard();
    if(base->GetState()!=dawn::native::DeviceBase::State::Alive||!base->GetQueue())
        return WEBSCENE_DAWN_NATIVE_DEVICE_LOST_V1;
    if(base->GetPhysicalDevice()->GetBackendType()!=wgpu::BackendType::Vulkan)
        return WEBSCENE_DAWN_NATIVE_DEVICE_NOT_VULKAN_V1;

    auto* device=dawn::native::vulkan::ToBackend(base.Get());
    auto* physical=dawn::native::vulkan::ToBackend(base->GetPhysicalDevice());
    auto* queue=dawn::native::vulkan::ToBackend(base->GetQueue());
    VkPhysicalDeviceIDProperties ids{};
    ids.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    VkPhysicalDeviceProperties2 properties{};
    properties.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext=&ids;
    device->fn.GetPhysicalDeviceProperties2(physical->GetVkPhysicalDevice(),&properties);
    if(!nonzero_uuid(ids.deviceUUID)||!nonzero_uuid(ids.driverUUID))
        return WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_IDENTITY_V1;

    webscene_dawn_native_device_v1 value{};
    value.struct_size=sizeof(value);
    value.version=WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION_V1;
    value.adapter=dawn::native::ToAPI(base->GetAdapter());
    value.device=token;
    value.vk_physical_device=reinterpret_cast<void*>(physical->GetVkPhysicalDevice());
    value.vk_device=reinterpret_cast<void*>(device->GetVkDevice());
    value.vk_queue=reinterpret_cast<void*>(queue->GetVkQueue());
    value.queue_family=device->GetGraphicsQueueFamily();
    std::memcpy(value.device_uuid,ids.deviceUUID,VK_UUID_SIZE);
    std::memcpy(value.driver_uuid,ids.driverUUID,VK_UUID_SIZE);
    *result=value;
    return WEBSCENE_DAWN_NATIVE_DEVICE_SUCCESS_V1;
}

extern "C" webscene_dawn_native_device_status_v2 websceneDawnQueryVulkanDeviceV2(
    WGPUDevice token,webscene_dawn_native_device_v2* result) {
    if(!token||!result)return WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_ARGUMENT_V2;
    if(result->struct_size<sizeof(webscene_dawn_native_device_v2)||
        result->version!=WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION_V2)
        return WEBSCENE_DAWN_NATIVE_DEVICE_INCOMPATIBLE_ABI_V2;
    auto base=find_device(token);
    if(!base)return WEBSCENE_DAWN_NATIVE_DEVICE_FOREIGN_DEVICE_V2;
    auto guard=base->GetGuard();
    if(base->GetState()!=dawn::native::DeviceBase::State::Alive||!base->GetQueue())
        return WEBSCENE_DAWN_NATIVE_DEVICE_LOST_V2;
    if(base->GetPhysicalDevice()->GetBackendType()!=wgpu::BackendType::Vulkan)
        return WEBSCENE_DAWN_NATIVE_DEVICE_NOT_VULKAN_V2;

    auto* device=dawn::native::vulkan::ToBackend(base.Get());
    auto* physical=dawn::native::vulkan::ToBackend(base->GetPhysicalDevice());
    auto* queue=dawn::native::vulkan::ToBackend(base->GetQueue());
    const auto instance=device->GetVkInstance();
    const auto resolver=device->fn.GetInstanceProcAddr;
    if(instance==VK_NULL_HANDLE||!resolver)
        return WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_IDENTITY_V2;

    VkPhysicalDeviceIDProperties ids{};
    ids.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    VkPhysicalDeviceProperties2 properties{};
    properties.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext=&ids;
    device->fn.GetPhysicalDeviceProperties2(physical->GetVkPhysicalDevice(),&properties);
    if(!nonzero_uuid(ids.deviceUUID)||!nonzero_uuid(ids.driverUUID))
        return WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_IDENTITY_V2;

    uint32_t capabilities=0;
    const auto& global=device->GetGlobalInfo();
    if(global.HasExt(dawn::native::vulkan::InstanceExt::Surface))
        capabilities|=WEBSCENE_DAWN_VULKAN_SURFACE_EXTENSION_ENABLED_V2;
#if defined(DAWN_USE_X11)
    if(global.HasExt(dawn::native::vulkan::InstanceExt::XlibSurface))
        capabilities|=WEBSCENE_DAWN_VULKAN_XLIB_SURFACE_EXTENSION_ENABLED_V2;
    if(device->fn.DestroySurfaceKHR&&device->fn.GetPhysicalDeviceSurfaceSupportKHR&&
        device->fn.GetPhysicalDeviceSurfaceCapabilitiesKHR&&
        device->fn.GetPhysicalDeviceSurfaceFormatsKHR&&
        device->fn.GetPhysicalDeviceSurfacePresentModesKHR&&
        device->fn.CreateXlibSurfaceKHR&&
        device->fn.GetPhysicalDeviceXlibPresentationSupportKHR)
        capabilities|=WEBSCENE_DAWN_VULKAN_XLIB_PRESENTATION_PROCS_AVAILABLE_V2;
#endif
    if((capabilities&WEBSCENE_DAWN_VULKAN_XLIB_PRESENTATION_REQUIRED_V2)!=
        WEBSCENE_DAWN_VULKAN_XLIB_PRESENTATION_REQUIRED_V2)
        return WEBSCENE_DAWN_NATIVE_DEVICE_XLIB_PRESENTATION_UNAVAILABLE_V2;

    webscene_dawn_native_device_v2 value{};
    value.struct_size=sizeof(value);
    value.version=WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION_V2;
    value.adapter=dawn::native::ToAPI(base->GetAdapter());
    value.device=token;
    value.vk_instance=reinterpret_cast<void*>(instance);
    value.vk_physical_device=reinterpret_cast<void*>(physical->GetVkPhysicalDevice());
    value.vk_device=reinterpret_cast<void*>(device->GetVkDevice());
    value.vk_queue=reinterpret_cast<void*>(queue->GetVkQueue());
    value.vk_get_instance_proc_addr=
        reinterpret_cast<webscene_dawn_vk_get_instance_proc_addr_v2>(resolver);
    value.queue_family=device->GetGraphicsQueueFamily();
    value.instance_capabilities=capabilities;
    std::memcpy(value.device_uuid,ids.deviceUUID,VK_UUID_SIZE);
    std::memcpy(value.driver_uuid,ids.driverUUID,VK_UUID_SIZE);
    *result=value;
    return WEBSCENE_DAWN_NATIVE_DEVICE_SUCCESS_V2;
}
