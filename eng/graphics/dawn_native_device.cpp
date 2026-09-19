#include "dawn_native_device_internal.h"
#include "webscene/dawn_native_device.h"

#include <cstring>
#include <mutex>
#include <unordered_map>

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
    std::unordered_map<WGPUDevice,dawn::native::DeviceBase*> values;
};

live_devices& devices() {
    // Dawn devices may outlive other process statics. Keep the registry alive
    // until process teardown rather than racing its destructor at shutdown.
    static auto* value=new live_devices;
    return *value;
}

dawn::Ref<dawn::native::DeviceBase> find_device(WGPUDevice token) noexcept {
    auto& registry=devices();
    std::lock_guard lock(registry.mutex);
    const auto found=registry.values.find(token);
    if(found==registry.values.end()||!found->second->TryAddRef())return {};
    return dawn::AcquireRef(found->second);
}

bool nonzero_uuid(const uint8_t* value) noexcept {
    for(size_t index=0;index<VK_UUID_SIZE;++index)if(value[index])return true;
    return false;
}
}

namespace webscene::dawn_bridge {
void register_device(dawn::native::DeviceBase* device) noexcept {
    if(!device)return;
    try {
        auto& registry=devices();
        std::lock_guard lock(registry.mutex);
        registry.values[dawn::native::ToAPI(device)]=device;
    } catch(...) {
        // Device creation remains usable, while the exact-device query fails
        // closed because an unregistered token is indistinguishable from one
        // created by another WebGPU implementation.
    }
}

void unregister_device(dawn::native::DeviceBase* device) noexcept {
    if(!device)return;
    auto& registry=devices();
    std::lock_guard lock(registry.mutex);
    const auto token=dawn::native::ToAPI(device);
    const auto found=registry.values.find(token);
    if(found!=registry.values.end()&&found->second==device)registry.values.erase(found);
}
}

extern "C" webscene_dawn_native_device_status_v1 websceneDawnQueryVulkanDeviceV1(
    WGPUDevice token,webscene_dawn_native_device_v1* result) {
    if(!token||!result)return WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_ARGUMENT_V1;
    if(result->struct_size<sizeof(webscene_dawn_native_device_v1)||
        result->version!=WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION)
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
    value.version=WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION;
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
