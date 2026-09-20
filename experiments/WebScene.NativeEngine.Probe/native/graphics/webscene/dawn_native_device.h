#pragma once
#include <stdint.h>
#include <webgpu/webgpu.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION_V1
#define WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION_V1 1U
#endif
#ifndef WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION_V2
#define WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION_V2 2U
#endif
#ifndef WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION_V3
#define WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION_V3 3U
#endif
/* The original name remains the v1 value for source compatibility. New code
 * selects the explicit versioned constant matching the query it calls. */
#ifndef WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION
#define WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION_V1
#endif

typedef uint32_t webscene_dawn_native_device_status_v1;
enum {
    WEBSCENE_DAWN_NATIVE_DEVICE_SUCCESS_V1 = 0,
    WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_ARGUMENT_V1 = 1,
    WEBSCENE_DAWN_NATIVE_DEVICE_INCOMPATIBLE_ABI_V1 = 2,
    WEBSCENE_DAWN_NATIVE_DEVICE_FOREIGN_DEVICE_V1 = 3,
    WEBSCENE_DAWN_NATIVE_DEVICE_NOT_VULKAN_V1 = 4,
    WEBSCENE_DAWN_NATIVE_DEVICE_LOST_V1 = 5,
    WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_IDENTITY_V1 = 6
};

/* Exact native identities borrowed from one live WGPUDevice. The caller keeps
 * its WGPUDevice reference alive while using or retaining these values. A
 * Vulkan-aware host may cast the opaque values to the matching dispatchable
 * Vulkan handle types; it must never destroy Dawn-owned objects or queues. */
typedef struct webscene_dawn_native_device_v1 {
    uint32_t struct_size;
    uint32_t version;
    WGPUAdapter adapter;
    WGPUDevice device;
    void* vk_physical_device;
    void* vk_device;
    void* vk_queue;
    uint32_t queue_family;
    uint32_t reserved;
    uint8_t device_uuid[16];
    uint8_t driver_uuid[16];
} webscene_dawn_native_device_v1;

typedef uint32_t webscene_dawn_native_device_status_v2;
enum {
    WEBSCENE_DAWN_NATIVE_DEVICE_SUCCESS_V2 = 0,
    WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_ARGUMENT_V2 = 1,
    WEBSCENE_DAWN_NATIVE_DEVICE_INCOMPATIBLE_ABI_V2 = 2,
    WEBSCENE_DAWN_NATIVE_DEVICE_FOREIGN_DEVICE_V2 = 3,
    WEBSCENE_DAWN_NATIVE_DEVICE_NOT_VULKAN_V2 = 4,
    WEBSCENE_DAWN_NATIVE_DEVICE_LOST_V2 = 5,
    WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_IDENTITY_V2 = 6,
    WEBSCENE_DAWN_NATIVE_DEVICE_XLIB_PRESENTATION_UNAVAILABLE_V2 = 7
};

typedef void (*webscene_dawn_vk_proc_v2)(void);
typedef webscene_dawn_vk_proc_v2 (*webscene_dawn_vk_get_instance_proc_addr_v2)(
    void* vk_instance, const char* name);

typedef uint32_t webscene_dawn_vulkan_instance_capabilities_v2;
enum {
    WEBSCENE_DAWN_VULKAN_SURFACE_EXTENSION_ENABLED_V2 = 1U << 0,
    WEBSCENE_DAWN_VULKAN_XLIB_SURFACE_EXTENSION_ENABLED_V2 = 1U << 1,
    WEBSCENE_DAWN_VULKAN_XLIB_PRESENTATION_PROCS_AVAILABLE_V2 = 1U << 2,
    WEBSCENE_DAWN_VULKAN_XLIB_PRESENTATION_REQUIRED_V2 =
        WEBSCENE_DAWN_VULKAN_SURFACE_EXTENSION_ENABLED_V2 |
        WEBSCENE_DAWN_VULKAN_XLIB_SURFACE_EXTENSION_ENABLED_V2 |
        WEBSCENE_DAWN_VULKAN_XLIB_PRESENTATION_PROCS_AVAILABLE_V2
};

/* V2 adds the VkInstance which owns the returned device and the exact resolver
 * Dawn loaded for that instance. All Vulkan identities and the resolver are
 * borrowed from the live WGPUDevice and become invalid when it is released.
 * The resolver is ABI-compatible with PFN_vkGetInstanceProcAddr on Linux. */
typedef struct webscene_dawn_native_device_v2 {
    uint32_t struct_size;
    uint32_t version;
    WGPUAdapter adapter;
    WGPUDevice device;
    void* vk_instance;
    void* vk_physical_device;
    void* vk_device;
    void* vk_queue;
    webscene_dawn_vk_get_instance_proc_addr_v2 vk_get_instance_proc_addr;
    uint32_t queue_family;
    webscene_dawn_vulkan_instance_capabilities_v2 instance_capabilities;
    uint8_t device_uuid[16];
    uint8_t driver_uuid[16];
} webscene_dawn_native_device_v2;

typedef uint32_t webscene_dawn_native_device_status_v3;
enum {
    WEBSCENE_DAWN_NATIVE_DEVICE_SUCCESS_V3 = 0,
    WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_ARGUMENT_V3 = 1,
    WEBSCENE_DAWN_NATIVE_DEVICE_INCOMPATIBLE_ABI_V3 = 2,
    WEBSCENE_DAWN_NATIVE_DEVICE_FOREIGN_DEVICE_V3 = 3,
    WEBSCENE_DAWN_NATIVE_DEVICE_NOT_VULKAN_V3 = 4,
    WEBSCENE_DAWN_NATIVE_DEVICE_LOST_OR_CLOSING_V3 = 5,
    WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_IDENTITY_V3 = 6,
    WEBSCENE_DAWN_NATIVE_DEVICE_XLIB_PRESENTATION_UNAVAILABLE_V3 = 7,
    WEBSCENE_DAWN_NATIVE_DEVICE_QUEUE_ACCESS_UNAVAILABLE_V3 = 8,
    WEBSCENE_DAWN_NATIVE_DEVICE_QUEUE_ACCESS_NESTED_V3 = 9,
    WEBSCENE_DAWN_NATIVE_DEVICE_INVALID_QUEUE_ACCESS_V3 = 10,
    WEBSCENE_DAWN_NATIVE_DEVICE_QUEUE_ACCESS_WRONG_THREAD_V3 = 11
};

typedef webscene_dawn_vk_proc_v2 webscene_dawn_vk_proc_v3;
typedef webscene_dawn_vk_get_instance_proc_addr_v2
    webscene_dawn_vk_get_instance_proc_addr_v3;
typedef webscene_dawn_vulkan_instance_capabilities_v2
    webscene_dawn_vulkan_instance_capabilities_v3;

typedef uint32_t webscene_dawn_vulkan_queue_access_capabilities_v3;
enum {
    WEBSCENE_DAWN_VULKAN_QUEUE_ACCESS_DEVICE_GUARD_V3 = 1U << 0,
    WEBSCENE_DAWN_VULKAN_QUEUE_ACCESS_REQUIRED_V3 =
        WEBSCENE_DAWN_VULKAN_QUEUE_ACCESS_DEVICE_GUARD_V3
};

/* V3 preserves the v2 native tuple and proves that the exported balanced
 * access functions serialize with Dawn submissions and presentation. */
typedef struct webscene_dawn_native_device_v3 {
    uint32_t struct_size;
    uint32_t version;
    WGPUAdapter adapter;
    WGPUDevice device;
    void* vk_instance;
    void* vk_physical_device;
    void* vk_device;
    void* vk_queue;
    webscene_dawn_vk_get_instance_proc_addr_v3 vk_get_instance_proc_addr;
    uint32_t queue_family;
    webscene_dawn_vulkan_instance_capabilities_v3 instance_capabilities;
    webscene_dawn_vulkan_queue_access_capabilities_v3 queue_access_capabilities;
    uint32_t reserved;
    uint8_t device_uuid[16];
    uint8_t driver_uuid[16];
} webscene_dawn_native_device_v3;

/* Opaque balanced access to the exact queue reported by the v3 query. One
 * access may be active per thread. Acquire and release must occur on the same
 * thread, and access scopes must not nest. The caller must not copy or modify
 * an active value. */
typedef struct webscene_dawn_vulkan_queue_access_v3 {
    uint32_t struct_size;
    uint32_t version;
    WGPUDevice device;
    void* vk_queue;
    uint32_t queue_family;
    uint32_t reserved;
    void* access_token;
} webscene_dawn_vulkan_queue_access_v3;

typedef uint32_t webscene_dawn_d3d12_device_status_v1;
#define WEBSCENE_DAWN_D3D12_DEVICE_ABI_VERSION_V1 1U
enum {
    WEBSCENE_DAWN_D3D12_DEVICE_SUCCESS_V1 = 0,
    WEBSCENE_DAWN_D3D12_DEVICE_INVALID_ARGUMENT_V1 = 1,
    WEBSCENE_DAWN_D3D12_DEVICE_INCOMPATIBLE_ABI_V1 = 2,
    WEBSCENE_DAWN_D3D12_DEVICE_FOREIGN_DEVICE_V1 = 3,
    WEBSCENE_DAWN_D3D12_DEVICE_NOT_D3D12_V1 = 4,
    WEBSCENE_DAWN_D3D12_DEVICE_LOST_V1 = 5,
    WEBSCENE_DAWN_D3D12_DEVICE_INVALID_IDENTITY_V1 = 6
};

enum {
    WEBSCENE_DAWN_D3D12_EXACT_DEVICE_V1 = 1U << 0,
    WEBSCENE_DAWN_D3D12_DIRECT_QUEUE_V1 = 1U << 1,
    WEBSCENE_DAWN_D3D12_ADAPTER_LUID_V1 = 1U << 2,
    WEBSCENE_DAWN_D3D12_REQUIRED_V1 =
        WEBSCENE_DAWN_D3D12_EXACT_DEVICE_V1 |
        WEBSCENE_DAWN_D3D12_DIRECT_QUEUE_V1 |
        WEBSCENE_DAWN_D3D12_ADAPTER_LUID_V1
};

/* Exact D3D12 identities borrowed from one live Dawn WGPUDevice. The caller
 * retains that public device while using the returned device and direct queue,
 * never releases the native COM objects, and serializes native queue work with
 * its own Dawn submissions. Failure leaves the result unchanged. */
typedef struct webscene_dawn_d3d12_device_v1 {
    uint32_t struct_size;
    uint32_t version;
    WGPUAdapter adapter;
    WGPUDevice device;
    void* d3d12_device;
    void* d3d12_direct_queue;
    uint32_t adapter_luid_low;
    int32_t adapter_luid_high;
    uint32_t capabilities;
    uint32_t reserved;
} webscene_dawn_d3d12_device_v1;

#if defined(_WIN32) && defined(WEBSCENE_DAWN_NATIVE_DEVICE_IMPLEMENTATION)
#define WEBSCENE_DAWN_NATIVE_DEVICE_EXPORT __declspec(dllexport)
#elif defined(_WIN32)
#define WEBSCENE_DAWN_NATIVE_DEVICE_EXPORT __declspec(dllimport)
#elif defined(__GNUC__)
#define WEBSCENE_DAWN_NATIVE_DEVICE_EXPORT __attribute__((visibility("default")))
#else
#define WEBSCENE_DAWN_NATIVE_DEVICE_EXPORT
#endif

#if !defined(_WIN32)
/* result must carry sizeof(webscene_dawn_native_device_v1) and ABI version 1.
 * Failure leaves the caller's result unchanged. Foreign tokens are rejected
 * from a live-device registry before Dawn object memory is dereferenced. */
WEBSCENE_DAWN_NATIVE_DEVICE_EXPORT webscene_dawn_native_device_status_v1
websceneDawnQueryVulkanDeviceV1(
    WGPUDevice device, webscene_dawn_native_device_v1* result);

/* result must carry sizeof(webscene_dawn_native_device_v2) and ABI version 2.
 * Success guarantees all XLIB_PRESENTATION_REQUIRED capability bits. The host
 * must still call vkGetPhysicalDeviceXlibPresentationSupportKHR with its exact
 * Display and visual before creating a surface. Failure leaves result
 * unchanged, and foreign tokens are rejected before private dereference. */
WEBSCENE_DAWN_NATIVE_DEVICE_EXPORT webscene_dawn_native_device_status_v2
websceneDawnQueryVulkanDeviceV2(
    WGPUDevice device, webscene_dawn_native_device_v2* result);

/* V3 succeeds only when Dawn's device-wide synchronization guard protects the
 * exact Vulkan queue. Failure leaves result unchanged. */
WEBSCENE_DAWN_NATIVE_DEVICE_EXPORT webscene_dawn_native_device_status_v3
websceneDawnQueryVulkanDeviceV3(
    WGPUDevice device, webscene_dawn_native_device_v3* result);

/* Acquire retains the WGPUDevice and Dawn's internal device until release.
 * It fails for foreign, stale, lost or closing devices before private state is
 * used. Release always ends a valid same-thread scope, including after loss. */
WEBSCENE_DAWN_NATIVE_DEVICE_EXPORT webscene_dawn_native_device_status_v3
websceneDawnAcquireVulkanQueueV3(
    WGPUDevice device, webscene_dawn_vulkan_queue_access_v3* result);
WEBSCENE_DAWN_NATIVE_DEVICE_EXPORT webscene_dawn_native_device_status_v3
websceneDawnReleaseVulkanQueueV3(
    webscene_dawn_vulkan_queue_access_v3* access);
#endif

#if defined(_WIN32)
/* result must carry sizeof(webscene_dawn_d3d12_device_v1) and version 1.
 * Foreign tokens are rejected from the live registry before Dawn private
 * object memory is dereferenced. */
WEBSCENE_DAWN_NATIVE_DEVICE_EXPORT webscene_dawn_d3d12_device_status_v1
websceneDawnQueryD3D12DeviceV1(
    WGPUDevice device, webscene_dawn_d3d12_device_v1* result);
#endif

#undef WEBSCENE_DAWN_NATIVE_DEVICE_EXPORT

#ifdef __cplusplus
}
#endif
