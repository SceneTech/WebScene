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
`AdapterPropertiesVk`. It exposes no `VkDevice`, `VkPhysicalDevice`, queue
family or device/driver UUID. Consequently WebScene cannot construct or
independently verify a Vulkan allocator from this SDK surface. Capability
inspection returns `native_device_provider_required` even when all import
features exist. The host that creates Dawn's native Vulkan device must install
an allocator bound to that exact device token and populate the UUID, create
info, modifier and queue-family fields. Without it, acquisition continues to
return `WEBSCENE_GPU_LINUX_SHARED_UNSUPPORTED_PROVIDER_V3`.

ANGLE Vulkan/GL interop remains independently unimplemented. No GL/EGL route is
advertised by this contract.

## Authored gates

- C11 ABI size/offset contracts cover all exported views.
- The provider contract covers exact Dawn-device binding, binary/timeline fence
  rules, layout ownership rotation, metadata, memory/layout/queue validation,
  producer readiness, timeline rules, FD duplication, closure and rollback.
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

Implement the host-side Vulkan allocation object behind
`dawn_linux_external_allocator`. This requires either a Dawn SDK addition that
exposes a supported native Vulkan device/physical-device/queue identity, or
device creation owned by a Linux host component that can bind those native
handles and the public `WGPUDevice` token atomically. Then negotiate hardware
support per format, modifier, handle type and synchronization route and run the
authored contract on a real Vulkan device. Unsupported routes remain explicit
and cannot fall back to CPU presentation under the GPU-image capability.
