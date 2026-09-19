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
unsupported provider. A concrete Linux producer must allocate exportable
Vulkan images and memory, import those allocations into Dawn, preserve the
device UUID and Vulkan create info, run `BeginAccess`/`EndAccess`, and publish
the returned layout and fence state through `linux_external_image_provider`.
Until that allocator exists, acquisition returns
`WEBSCENE_GPU_LINUX_SHARED_UNSUPPORTED_PROVIDER_V3`.

ANGLE Vulkan/GL interop remains independently unimplemented. No GL/EGL route is
advertised by this contract.

## Authored gates

- C11 ABI size/offset contracts cover all exported views.
- The provider contract covers exact metadata, memory/layout/queue validation,
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

Implement the external Vulkan allocation provider described above using the
pinned Dawn/Vulkan profile. Hardware support must be negotiated per format,
modifier, handle type and synchronization route. Unsupported routes remain a
specific failure and cannot fall back to CPU presentation under the GPU-image
capability.
