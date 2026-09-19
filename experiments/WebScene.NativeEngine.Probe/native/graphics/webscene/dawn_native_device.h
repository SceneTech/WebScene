#pragma once
#include <stdint.h>
#include <webgpu/webgpu.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION
#define WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION 1U
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

#if defined(_WIN32)
#define WEBSCENE_DAWN_NATIVE_DEVICE_EXPORT __declspec(dllimport)
#elif defined(__GNUC__)
#define WEBSCENE_DAWN_NATIVE_DEVICE_EXPORT __attribute__((visibility("default")))
#else
#define WEBSCENE_DAWN_NATIVE_DEVICE_EXPORT
#endif

/* result must carry sizeof(webscene_dawn_native_device_v1) and ABI version 1.
 * Failure leaves the caller's result unchanged. Foreign tokens are rejected
 * from a live-device registry before Dawn object memory is dereferenced. */
WEBSCENE_DAWN_NATIVE_DEVICE_EXPORT webscene_dawn_native_device_status_v1
websceneDawnQueryVulkanDeviceV1(
    WGPUDevice device, webscene_dawn_native_device_v1* result);

#undef WEBSCENE_DAWN_NATIVE_DEVICE_EXPORT

#ifdef __cplusplus
}
#endif
