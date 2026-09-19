# Exact Dawn Vulkan instance query

Issue: #649. Parent: WebScene #28. This is the WebScene prerequisite for
AppScene #184, #262 and #130.

The pinned Linux Dawn monolith now exports
`websceneDawnQueryVulkanDeviceV2` beside the unchanged v1 entry point. For a
live Vulkan `WGPUDevice`, v2 returns the exact borrowed `VkInstance`,
`VkPhysicalDevice`, `VkDevice`, `VkQueue`, graphics queue family and
device/driver UUIDs. It also returns the exact `vkGetInstanceProcAddr` function
which Dawn loaded for that instance. The public C header represents Vulkan
handles and procedures with opaque ABI-compatible values; all private Dawn and
Vulkan implementation headers remain inside the pinned bridge source.

## Validation and ownership

The exported function validates arguments and the v2 size/version before it
looks up the token. Lookup scans the authoritative live registry while holding
its mutex and acquires a Dawn reference before releasing the mutex. Only then
does the query access private device state. Foreign and stale tokens therefore
fail before query-side private dereference. The bridge also rejects lost and
non-Vulkan devices, null native identities, zero UUIDs and an absent resolver.
It fills a local result and copies it to the caller only on success, leaving the
caller's bytes unchanged on every failure.

V2 reports three required capabilities: enabled `VK_KHR_surface`, enabled
`VK_KHR_xlib_surface`, and the complete set of loaded generic surface and Xlib
creation/presentation-query procedures. A missing extension, build flag or
procedure returns `XLIB_PRESENTATION_UNAVAILABLE`; the provider publishes no
lifetime. The host supplies the X11 `Display` and visual ID and must call
`vkGetPhysicalDeviceXlibPresentationSupportKHR` through the returned resolver
before it creates an Xlib surface.

Issue #651 subsequently advances `bind_dawn_linux_external_device` to v3 so it
can require balanced access to Dawn's exact queue; direct v2 consumers remain
ABI compatible. The binding copies the public `wgpu::Device` into the existing
native owner. That reference retains the Dawn backend and every borrowed Vulkan
identity. Allocator identity checks include the instance, resolver,
presentation capability mask and v3 queue-access capability. No Vulkan object
returned by this API may be destroyed by the consumer, and no second
instance/device or CPU transfer path is permitted.

## SDK and build contracts

The Linux version script and post-build symbol inspection require both v1 and
v2 while rejecting every other non-WebGPU export. The installed and provenance
copies of `webscene/dawn_native_device.h`, bridge source and internal hook remain
content-hashed by the graphics manifest and `verify-sdk.py`. The graphics SDK
cache identity already hashes those files plus the symbol policy and export
inspector, so this ABI change cannot reuse a v1-only binary. Linux SDK CMake
targets publish the legacy v1 macro and the explicit v2 macro.

The bridge registry was also made compatible with Dawn's `-fno-exceptions`
build. Registration uses a non-throwing intrusive-list allocation and fails
closed if allocation fails. Unregistration removes and frees the exact node;
lookup still retains the live Dawn device under the registry mutex.

Per the active fast policy, authored builds and tests were not run. Validation
for this checkpoint is limited to `git diff --check`.

## Remaining acceptance

AppScene can now create its Xlib `VkSurfaceKHR` and Graphite backend against the
same Dawn instance/device/queue. Physical Linux acceptance must still prove the
display/visual presentation query, surface creation and swapchain format/mode,
rendered pixels, resize and device-loss teardown, queue-completion retirement,
the 30-minute FD/residency plateau, and zero CPU readback/upload in ordinary
frames.
