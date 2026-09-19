# Linux external-image interop contract

Issue: #28. This checkpoint defines the product-neutral Linux memory and
synchronization boundary needed by native presenters. It does not claim a
working Vulkan producer, a successful Skia import, or hardware qualification.

## Delivered boundary

The v3 scene image lease remains the lifetime root. A presenter starts one
`webscene_gpu_image_consumer_v3`, then calls
`webscene_gpu_linux_acquire_shared_v3`. A successful acquisition returns an
immutable descriptor and a `shared_owner` containing close-on-exec duplicates
of every memory-plane and producer-wait file descriptor.

The descriptor carries:

- opaque-FD or DMA-BUF memory type, up to four planes, offsets, strides, DRM
  format and modifier;
- exact allocation/generation/content identity, producer timeline/value and
  portable image metadata;
- Vulkan format, type, tiling, usage, flags, initial layout, sharing mode,
  creation queue-family list, single-sample/mip/layer counts, memory type,
  allocation size and dedicated-allocation state;
- 16-byte Vulkan device and driver UUIDs;
- initialized producer/consumer layouts, exclusive queue-family transfer or
  explicit concurrent sharing;
- SyncFD or Vulkan opaque-FD semaphore waits, stable ordering-domain identity,
  binary value `1`, and positive timeline values.

Acquisition rejects zero UUIDs, undefined layouts, incomplete queue ownership,
invalid memory combinations, invalid plane descriptors, missing producer
completion/waits, and metadata that does not exactly match the retained lease.
It never substitutes CPU pixels. FD duplication is atomic: partial failure
closes all duplicates and publishes no owner. `close(2)` is deliberately not
retried after `EINTR`, because the descriptor number may already have been
consumed and reused.

The presenter imports or duplicates all borrowed handles before calling
`webscene_gpu_linux_release_shared_v3`. Releasing that owner closes only
WebScene's duplicates. It does not complete image consumption. The presenter
must call `webscene_gpu_image_complete_consumer_v3` only after its GPU queue has
retired every read, including failed imports that submitted no GPU work.

## Dawn and Vulkan boundary

The pinned Dawn revision exposes `SharedTextureMemoryDmaBuf`,
`SharedTextureMemoryOpaqueFD`, `SharedFenceSyncFD`,
`SharedFenceVkSemaphoreOpaqueFD`, and Vulkan begin/end image-layout chains.
Those APIs import externally allocated memory and export synchronization from
`EndAccess`. They do not turn an ordinary Dawn-created texture into exportable
external memory.

For that reason, generic `dawn_canvas_images` deliberately remains an
unsupported provider. `dawn_linux_external_provider` now supplies the exact
provider seam around the pinned public Dawn ABI:

- it proves Vulkan backend and enabled DMA-BUF/opaque-FD plus SyncFD/opaque
  semaphore features before asking a native allocator for storage;
- the allocator and every allocation must carry the exact `WGPUDevice` token
  captured beside Dawn device creation, so allocations cannot cross devices;
- DMA-BUF planes or opaque-FD memory plus retained `VkImageCreateInfo` are
  imported directly. Ordinary Dawn textures and CPU copies are never accepted;
- producer waits, Vulkan old/new layouts and initialized state feed
  `BeginAccess`; `EndAccess` must return initialized contents, layouts and an
  equal fence/value count;
- returned fences are exported to owned SyncFD/opaque-semaphore FDs, checked
  against binary/timeline ordering rules and published through the existing v3
  `linux_external_image_provider`. Failed or partial handoffs publish nothing
  and RAII closes every already-exported FD.

The installed Dawn C ABI exposes only Vulkan `driverVersion` in
`AdapterPropertiesVk`. The pinned `dawn/native/VulkanBackend.h` adds only
`GetInstance` and `GetInstanceProcAddr`; it exposes no `VkDevice`,
`VkPhysicalDevice`, `VkQueue`, queue family or device/driver UUID, and Dawn's
adapter creates its own `VkDevice` rather than wrapping one supplied by a host.
The implementations that expose these objects live under Dawn's private
`src/dawn/native/vulkan` tree and are not an SDK contract. WebScene therefore
does not infer device identity from the Vulkan instance, adapter properties or
matching UUIDs.

The SDK now exposes `dawn_linux_external_device_factory`. An AppScene host with
an exact-device Dawn integration creates the WebGPU instance/adapter/device and
native allocator atomically, and returns one
`dawn_linux_external_device_lifetime` shared by the allocator and every
allocation. That lifetime carries the exact public `WGPUAdapter`/`WGPUDevice` tokens,
opaque identities for the native device/physical-device/queue, device/driver
UUIDs, Dawn queue family and a host-owned lifetime anchor for that tuple.
Provider creation rejects a copied lifetime object, mismatched device token,
UUID or queue family.

`owned_dawn_linux_external_allocation` is the concrete handoff boundary. It
retains the exact device lifetime, the host's `VkImage`/`VkDeviceMemory` owner,
the retained opaque-FD `VkImageCreateInfo` graph when applicable, and owned
memory/wait FDs. It rewrites exported snapshots to borrow only those owned FDs
and restricts the pinned Dawn DMA-BUF route to its supported single-FD plane
model. Provider member order destroys `SharedTextureMemory` before releasing
the native allocation and exact device. A factory-backed provider can therefore
advance past `native_device_provider_required`; an absent or invalid host
factory still fails closed.

ANGLE Vulkan/GL interop remains independently unimplemented. No GL/EGL route is
advertised by this contract.

## Authored gates

- C11 ABI size/offset contracts cover all exported views.
- The provider contract covers exact Dawn-device binding, binary/timeline fence
  rules, shared exact-device lifetime identity, UUID/queue-family admission,
  concrete native-allocation/FD ownership, layout ownership rotation, metadata,
  memory/layout/queue validation, producer readiness, timeline rules, FD
  duplication, closure and rollback.
- Dense Linux linking exports all `webscene_gpu_linux_*_v3` symbols through the
  existing version script; macOS export lists include the additive symbols for
  stable cross-platform loading and return unsupported outside Linux.
- Linux CMake consumers of `webscene_native_engine` receive
  `WEBSCENE_GPU_LINUX_SHARED_ABI_VERSION=3`; the installed runtime C header
  carries the same version macro and complete ABI.

Per the active fast-throughput policy, these gates were authored but not run.
Only `git diff --check` is expected for this checkpoint. Required promotion
evidence still includes Linux compilation, ABI/symbol checks, real Vulkan/Dawn
imports, X11 and Wayland Skia pixels, device/driver capability manifests,
invalid modifier/UUID/layout cases, resize/device-loss teardown, a 30-minute
FD/residency plateau, and proof of zero CPU readback/upload in ordinary frames.

## Remaining concrete blocker

Implement `dawn_linux_external_device_factory` in AppScene against a supported
exact-device Dawn integration. The current pinned public Dawn library cannot do
this by itself, so the host needs a narrowly supported Dawn SDK hook (or an
equivalent host-owned Dawn build) that returns the exact native device,
physical-device, queue and queue family for the created `WGPUDevice`. The host
then allocates/exports opaque-FD or DMA-BUF storage on those handles, adopts it
through `owned_dawn_linux_external_allocation`, and negotiates hardware support
per format, modifier, handle type and synchronization route. Real Linux Vulkan
hardware/driver execution is still required for pixels, negative identity and
layout cases, device loss, FD/residency plateau and zero-readback evidence.
Unsupported routes remain explicit and cannot fall back to CPU presentation
under the GPU-image capability.
