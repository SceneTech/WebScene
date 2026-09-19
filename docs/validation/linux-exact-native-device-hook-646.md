# Exact Dawn Vulkan device hook

Issue: #646. The pinned Linux Dawn build exports
`websceneDawnQueryVulkanDeviceV1`, a product-neutral C ABI that accepts only a
live `WGPUDevice` created by that build. It returns borrowed opaque
`VkPhysicalDevice`, `VkDevice` and `VkQueue` identities, the graphics queue
family, exact public adapter/device tokens, and Vulkan device/driver UUIDs.

Dawn's existing instance device list is the lifecycle authority. A transient,
pin-specific build patch registers and unregisters devices at the same add/remove
methods. The query checks the registry before dereferencing a token, retains the
device during inspection, holds its guard, and rejects foreign, lost,
non-Vulkan or incomplete identities. SDK verification covers the installed
header, bridge sources, export allowlist and cache inputs. AppScene includes no
Dawn private header.

The first graphics SDK build exposed one deterministic compatibility defect:
Dawn compiles native objects with `-fno-exceptions`, while the initial registry
used an exception handler around `unordered_map` insertion. Issue #649 replaces
that insertion with a mutex-protected intrusive list whose nodes use
`new(std::nothrow)`. Allocation failure leaves the device unregistered, so the
query continues to reject it as foreign before dereference. The registry itself
uses destructor-free static storage to preserve the original teardown rule.
No exception syntax remains in the bridge.

`bind_dawn_linux_external_device` is the factory consumption point. It copies
the `wgpu::Device` into the lifetime anchor and requires the same shared atomic
loss flag used by the host's Dawn device-lost callback. Allocation, begin,
end and export operations fail closed after loss. Native handles remain
Dawn-owned and borrowed; allocators may use them but never destroy them.

Per the directed checkpoint policy, the authored build and contract gates were
not run. Validation for this change is limited to `git diff --check`.

## Focused follow-up issue draft

**Title:** Implement exact-device Linux Vulkan external-memory allocator

Create the AppScene `dawn_linux_external_device_factory` around a Dawn Vulkan
device bound through `bind_dawn_linux_external_device`. Allocate exportable
opaque-FD and supported DMA-BUF images on the returned exact `VkDevice` and
queue family; retain `VkImage`, memory and create-info chains; export owned
memory and synchronization FDs; and adopt them through
`owned_dawn_linux_external_allocation`. Reject unsupported formats, modifiers,
handle types, queue ownership, UUID mismatches and device loss. Do not accept
ordinary Dawn textures and do not add CPU readback/upload fallback.

Acceptance requires real Linux Vulkan hardware evidence for both supported
handle routes, BeginAccess/EndAccess layout and synchronization rotation,
negative foreign-device and device-loss cases, X11 and Wayland presentation,
resize teardown, a 30-minute FD/residency plateau, and zero CPU transfer in
ordinary frames.
