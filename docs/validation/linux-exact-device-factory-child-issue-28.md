# Child issue scope draft: connect AppScene to the exact Dawn Vulkan device

Parent: WebScene #28.

## Problem

WebScene now accepts an atomic `dawn_linux_external_device_factory` result and
owns external Vulkan allocations through
`owned_dawn_linux_external_allocation`. The pinned public Dawn revision still
does not expose its `VkDevice`, `VkPhysicalDevice`, `VkQueue` or queue family,
and it cannot wrap a host-created `VkDevice`. AppScene cannot implement the
factory correctly by matching only the adapter UUID or Vulkan instance.

## Focused implementation

- Add one supported, pinned Dawn SDK hook that returns the exact Vulkan
  device/physical-device/queue tuple and queue family for a created
  `WGPUDevice`, with lifetime and device-loss rules. Do not include Dawn private
  headers in AppScene.
- Create the Dawn instance, adapter and device plus the AppScene allocator as
  one factory result. Publish one shared lifetime object containing the public
  adapter/device tokens, Vulkan device/driver UUIDs, Dawn queue family and the
  native tuple's opaque identities and owner.
- Allocate one single-sample BGRA/RGBA Vulkan image with a negotiated external
  opaque-FD or single-FD DMA-BUF route on that exact device. Own the
  `VkImage`/`VkDeviceMemory`, exported memory FD and retained create-info chain,
  then adopt them through `owned_dawn_linux_external_allocation`.
- Feed the returned WebScene texture through the existing BeginAccess,
  rendering and EndAccess path. Preserve initialized contents, layouts, queue
  ownership and exported fence ordering domains.
- Return an explicit unsupported/error status for every missing capability or
  identity mismatch. Do not create an ordinary Dawn texture, CPU bitmap upload
  or readback fallback.

## Acceptance

- The allocator's `WGPUDevice`, native lifetime, UUIDs and Dawn queue family
  match the factory device exactly; copied/foreign identities are rejected.
- Vulkan external-image format and handle queries admit the selected format,
  usage, tiling/modifier, dedicated-allocation and synchronization combination.
- A rendered hardware pixel pattern survives Dawn EndAccess and AppScene GPU
  import with the recorded layout and queue-family transition.
- Invalid UUID, queue family, modifier and fence imports fail without publishing
  a frame or leaking Vulkan objects/FDs.
- Resize, device loss and teardown keep FD and GPU residency bounded; ordinary
  frames show no CPU readback/upload.

## Evidence required before closing

Record exact WebScene/AppScene/Dawn revisions, physical adapter and driver,
device/driver UUIDs, queue families, Vulkan external-memory/semaphore capability
queries, rendered pixels, negative cases, a 30-minute FD/residency plateau and
an installed-SDK consumer run. Software Vulkan coverage is separate and does
not satisfy the physical-hardware gate.
