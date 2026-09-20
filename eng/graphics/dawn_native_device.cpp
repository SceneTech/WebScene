#include "dawn_native_device_internal.h"
#include "webscene/dawn_native_device.h"

#include <cstring>
#include <mutex>
#include <new>
#include <thread>
#include <utility>

#include "src/dawn/common/Ref.h"
#include "src/dawn/native/Adapter.h"
#include "src/dawn/native/Device.h"
#include "src/dawn/native/PhysicalDevice.h"
#if defined(_WIN32)
#include "src/dawn/native/d3d12/DeviceD3D12.h"
#include "src/dawn/native/d3d12/Forward.h"
#else
#include "src/dawn/native/vulkan/DeviceVk.h"
#include "src/dawn/native/vulkan/Forward.h"
#include "src/dawn/native/vulkan/PhysicalDeviceVk.h"
#include "src/dawn/native/vulkan/QueueVk.h"
#endif

namespace {
#if !defined(_WIN32)
struct queue_access_token;
#endif

struct live_devices {
    std::mutex mutex;
    struct entry {
        WGPUDevice token;
        dawn::native::DeviceBase* device;
        entry* next;
    };
    entry* first{};
#if !defined(_WIN32)
    queue_access_token* first_access{};
#endif
};

#if !defined(_WIN32)
class external_device_reference final {
  public:
    explicit external_device_reference(dawn::native::DeviceBase* device) noexcept
        : device_(device) {
        device_->APIAddRef();
    }
    ~external_device_reference() { device_->APIRelease(); }
    external_device_reference(const external_device_reference&)=delete;
    external_device_reference& operator=(const external_device_reference&)=delete;

  private:
    dawn::native::DeviceBase* device_;
};

struct queue_access_token final {
    queue_access_token(dawn::Ref<dawn::native::DeviceBase> device,
                       dawn::native::DeviceGuard guard,VkQueue queue,
                       uint32_t family) noexcept
        : device(std::move(device)),external_reference(this->device.Get()),
          guard(std::move(guard)),queue(queue),family(family),
          owner(std::this_thread::get_id()) {}

    // Destruction order is significant: the guard unlocks before the external
    // and internal device references are released.
    dawn::Ref<dawn::native::DeviceBase> device;
    external_device_reference external_reference;
    dawn::native::DeviceGuard guard;
    VkQueue queue;
    uint32_t family;
    std::thread::id owner;
    queue_access_token* next{};
};

thread_local queue_access_token* current_queue_access{};
#endif

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

#if !defined(_WIN32)
bool nonzero_uuid(const uint8_t* value) noexcept {
    for(size_t index=0;index<VK_UUID_SIZE;++index)if(value[index])return true;
    return false;
}

uint32_t instance_capabilities(dawn::native::vulkan::Device* device) noexcept {
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
    return capabilities;
}
#endif
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

#if !defined(_WIN32)
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

    const auto capabilities=instance_capabilities(device);
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

extern "C" webscene_dawn_native_device_status_v3 websceneDawnQueryVulkanDeviceV3(
    WGPUDevice token,webscene_dawn_native_device_v3* result) {
    if(!token||!result)return WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_ARGUMENT_V3;
    if(result->struct_size<sizeof(webscene_dawn_native_device_v3)||
        result->version!=WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION_V3)
        return WEBSCENE_DAWN_NATIVE_DEVICE_INCOMPATIBLE_ABI_V3;
    auto base=find_device(token);
    if(!base)return WEBSCENE_DAWN_NATIVE_DEVICE_FOREIGN_DEVICE_V3;
    auto guard=base->GetGuard();
    if(base->GetState()!=dawn::native::DeviceBase::State::Alive||!base->GetQueue())
        return WEBSCENE_DAWN_NATIVE_DEVICE_LOST_OR_CLOSING_V3;
    if(base->GetPhysicalDevice()->GetBackendType()!=wgpu::BackendType::Vulkan)
        return WEBSCENE_DAWN_NATIVE_DEVICE_NOT_VULKAN_V3;
    if(!base->HasFeature(dawn::native::Feature::ImplicitDeviceSynchronization))
        return WEBSCENE_DAWN_NATIVE_DEVICE_QUEUE_ACCESS_UNAVAILABLE_V3;

    auto* device=dawn::native::vulkan::ToBackend(base.Get());
    auto* physical=dawn::native::vulkan::ToBackend(base->GetPhysicalDevice());
    auto* queue=dawn::native::vulkan::ToBackend(base->GetQueue());
    const auto instance=device->GetVkInstance();
    const auto resolver=device->fn.GetInstanceProcAddr;
    if(instance==VK_NULL_HANDLE||!resolver||queue->GetVkQueue()==VK_NULL_HANDLE||
        device->GetGraphicsQueueFamily()==UINT32_MAX)
        return WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_IDENTITY_V3;
    VkPhysicalDeviceIDProperties ids{};
    ids.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    VkPhysicalDeviceProperties2 properties{};
    properties.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext=&ids;
    device->fn.GetPhysicalDeviceProperties2(physical->GetVkPhysicalDevice(),&properties);
    if(!nonzero_uuid(ids.deviceUUID)||!nonzero_uuid(ids.driverUUID))
        return WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_IDENTITY_V3;
    const auto capabilities=instance_capabilities(device);
    if((capabilities&WEBSCENE_DAWN_VULKAN_XLIB_PRESENTATION_REQUIRED_V2)!=
        WEBSCENE_DAWN_VULKAN_XLIB_PRESENTATION_REQUIRED_V2)
        return WEBSCENE_DAWN_NATIVE_DEVICE_XLIB_PRESENTATION_UNAVAILABLE_V3;

    webscene_dawn_native_device_v3 value{};
    value.struct_size=sizeof(value);
    value.version=WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION_V3;
    value.adapter=dawn::native::ToAPI(base->GetAdapter());
    value.device=token;
    value.vk_instance=reinterpret_cast<void*>(instance);
    value.vk_physical_device=reinterpret_cast<void*>(physical->GetVkPhysicalDevice());
    value.vk_device=reinterpret_cast<void*>(device->GetVkDevice());
    value.vk_queue=reinterpret_cast<void*>(queue->GetVkQueue());
    value.vk_get_instance_proc_addr=
        reinterpret_cast<webscene_dawn_vk_get_instance_proc_addr_v3>(resolver);
    value.queue_family=device->GetGraphicsQueueFamily();
    value.instance_capabilities=capabilities;
    value.queue_access_capabilities=
        WEBSCENE_DAWN_VULKAN_QUEUE_ACCESS_DEVICE_GUARD_V3;
    std::memcpy(value.device_uuid,ids.deviceUUID,VK_UUID_SIZE);
    std::memcpy(value.driver_uuid,ids.driverUUID,VK_UUID_SIZE);
    *result=value;
    return WEBSCENE_DAWN_NATIVE_DEVICE_SUCCESS_V3;
}

extern "C" webscene_dawn_native_device_status_v3 websceneDawnAcquireVulkanQueueV3(
    WGPUDevice public_device,webscene_dawn_vulkan_queue_access_v3* result) {
    if(!public_device||!result)
        return WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_ARGUMENT_V3;
    if(result->struct_size<sizeof(webscene_dawn_vulkan_queue_access_v3)||
        result->version!=WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION_V3)
        return WEBSCENE_DAWN_NATIVE_DEVICE_INCOMPATIBLE_ABI_V3;
    if(result->access_token)
        return WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_ARGUMENT_V3;
    if(current_queue_access)
        return WEBSCENE_DAWN_NATIVE_DEVICE_QUEUE_ACCESS_NESTED_V3;
    auto base=find_device(public_device);
    if(!base)return WEBSCENE_DAWN_NATIVE_DEVICE_FOREIGN_DEVICE_V3;
    auto guard=base->GetGuard();
    if(base->GetState()!=dawn::native::DeviceBase::State::Alive||!base->GetQueue())
        return WEBSCENE_DAWN_NATIVE_DEVICE_LOST_OR_CLOSING_V3;
    if(base->GetPhysicalDevice()->GetBackendType()!=wgpu::BackendType::Vulkan)
        return WEBSCENE_DAWN_NATIVE_DEVICE_NOT_VULKAN_V3;
    if(!base->HasFeature(dawn::native::Feature::ImplicitDeviceSynchronization))
        return WEBSCENE_DAWN_NATIVE_DEVICE_QUEUE_ACCESS_UNAVAILABLE_V3;
    auto* device=dawn::native::vulkan::ToBackend(base.Get());
    auto* queue=dawn::native::vulkan::ToBackend(base->GetQueue());
    const auto queue_handle=queue->GetVkQueue();
    const auto queue_family=device->GetGraphicsQueueFamily();
    if(queue_handle==VK_NULL_HANDLE||queue_family==UINT32_MAX)
        return WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_IDENTITY_V3;
    auto* access=new(std::nothrow) queue_access_token(
        std::move(base),std::move(guard),queue_handle,queue_family);
    if(!access)return WEBSCENE_DAWN_NATIVE_DEVICE_QUEUE_ACCESS_UNAVAILABLE_V3;
    {
        auto& registry=devices();
        std::lock_guard lock(registry.mutex);
        access->next=registry.first_access;
        registry.first_access=access;
    }
    current_queue_access=access;
    webscene_dawn_vulkan_queue_access_v3 value{};
    value.struct_size=sizeof(value);
    value.version=WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION_V3;
    value.device=public_device;
    value.vk_queue=reinterpret_cast<void*>(access->queue);
    value.queue_family=access->family;
    value.access_token=access;
    *result=value;
    return WEBSCENE_DAWN_NATIVE_DEVICE_SUCCESS_V3;
}

extern "C" webscene_dawn_native_device_status_v3 websceneDawnReleaseVulkanQueueV3(
    webscene_dawn_vulkan_queue_access_v3* value) {
    if(!value)
        return WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_ARGUMENT_V3;
    if(value->struct_size<sizeof(webscene_dawn_vulkan_queue_access_v3)||
        value->version!=WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION_V3)
        return WEBSCENE_DAWN_NATIVE_DEVICE_INCOMPATIBLE_ABI_V3;
    if(!value->access_token)
        return WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_ARGUMENT_V3;
    auto& registry=devices();
    queue_access_token* access{};
    {
        std::lock_guard lock(registry.mutex);
        auto** link=&registry.first_access;
        while(*link&&static_cast<void*>(*link)!=value->access_token)
            link=&(*link)->next;
        if(!*link)return WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_QUEUE_ACCESS_V3;
        access=*link;
        if(access->owner!=std::this_thread::get_id())
            return WEBSCENE_DAWN_NATIVE_DEVICE_QUEUE_ACCESS_WRONG_THREAD_V3;
        if(current_queue_access!=access)
            return WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_QUEUE_ACCESS_V3;
        *link=access->next;
    }
    const bool lost=access->device->GetState()!=dawn::native::DeviceBase::State::Alive;
    current_queue_access=nullptr;
    *value={};
    delete access;
    return lost ? WEBSCENE_DAWN_NATIVE_DEVICE_LOST_OR_CLOSING_V3
                : WEBSCENE_DAWN_NATIVE_DEVICE_SUCCESS_V3;
}
#else
extern "C" webscene_dawn_d3d12_device_status_v1
websceneDawnQueryD3D12DeviceV1(
    WGPUDevice token,webscene_dawn_d3d12_device_v1* result) {
    if(!token||!result)
        return WEBSCENE_DAWN_D3D12_DEVICE_INVALID_ARGUMENT_V1;
    if(result->struct_size<sizeof(webscene_dawn_d3d12_device_v1)||
        result->version!=WEBSCENE_DAWN_D3D12_DEVICE_ABI_VERSION_V1)
        return WEBSCENE_DAWN_D3D12_DEVICE_INCOMPATIBLE_ABI_V1;
    auto base=find_device(token);
    if(!base)return WEBSCENE_DAWN_D3D12_DEVICE_FOREIGN_DEVICE_V1;
    auto guard=base->GetGuard();
    if(base->GetState()!=dawn::native::DeviceBase::State::Alive||!base->GetQueue())
        return WEBSCENE_DAWN_D3D12_DEVICE_LOST_V1;
    if(base->GetPhysicalDevice()->GetBackendType()!=wgpu::BackendType::D3D12)
        return WEBSCENE_DAWN_D3D12_DEVICE_NOT_D3D12_V1;

    auto* device=dawn::native::d3d12::ToBackend(base.Get());
    auto queue=device->GetD3D12CommandQueue();
    auto* native_device=device->GetD3D12Device();
    const auto luid=native_device?native_device->GetAdapterLuid():LUID{};
    if(!native_device||!queue||(luid.LowPart==0U&&luid.HighPart==0))
        return WEBSCENE_DAWN_D3D12_DEVICE_INVALID_IDENTITY_V1;

    webscene_dawn_d3d12_device_v1 value{};
    value.struct_size=sizeof(value);
    value.version=WEBSCENE_DAWN_D3D12_DEVICE_ABI_VERSION_V1;
    value.adapter=dawn::native::ToAPI(base->GetAdapter());
    value.device=token;
    value.d3d12_device=native_device;
    value.d3d12_direct_queue=queue.Get();
    value.adapter_luid_low=luid.LowPart;
    value.adapter_luid_high=luid.HighPart;
    value.capabilities=WEBSCENE_DAWN_D3D12_REQUIRED_V1;
    *result=value;
    return WEBSCENE_DAWN_D3D12_DEVICE_SUCCESS_V1;
}
#endif
