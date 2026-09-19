# Exact Dawn Vulkan queue access

Issue #651 is a prerequisite of AppScene #265/#264 under WebScene #28. It
extends the Linux-only native-device bridge at Dawn revision
`2ca8cbfe0f8275aa0f739e7b6b4345a16e2f0378` without changing the v1 or v2 ABI.

## Dawn synchronization seam

The pinned Dawn source enables `Feature::ImplicitDeviceSynchronization` in
`PhysicalDevice.cpp`. Device initialization consequently creates the
ref-counted recursive `DeviceMutex`. Generated native API entry points acquire
`DeviceBase::GetGuard()` before queue operations. Vulkan
`Queue::SubmitPendingCommandsImpl` reaches `vkQueueSubmit` inside that guard,
and `Surface::APIPresent` explicitly holds the same guard through
`SwapChain::Present` and `vkQueuePresentKHR`.

The bridge therefore needs no private queue replacement and no patch to
`QueueVk`. `websceneDawnAcquireVulkanQueueV3` retains both an external
`WGPUDevice` reference and an internal `DeviceBase` reference, then holds the
exact `DeviceGuard`. `websceneDawnReleaseVulkanQueueV3` drops the guard before
either reference. External work in that interval is serialized with all Dawn
submit and present work on the returned exact queue.

## ABI and lifecycle

`websceneDawnQueryVulkanDeviceV3` preserves the v2 instance, physical device,
device, queue, resolver, Xlib capability, queue family and UUID identity. It
also requires Dawn's device-guard capability. Query and acquire validate the
public size/version and authoritative live registry before private device
memory is dereferenced. Foreign and stale tokens fail at lookup; non-Vulkan,
lost, disconnecting, disconnected and destroyed devices fail before access is
published.

Access scopes do not nest and acquire/release must run on the same thread. A
live-token registry rejects random, stale and double-release tokens without
dereferencing caller memory. Release removes the token from that registry
before unlocking, and unlocking occurs without the registry mutex held so Dawn
deferred work cannot invert the lock order. The C++ wrapper is non-copyable and
non-movable and keeps the complete public Linux device lifetime until its
same-thread destructor releases the token. Native operations inside the scope
must not call WebGPU or invoke user callbacks.

An active token prevents last-reference teardown. Explicit destroy or device
loss on another thread waits for the bounded external operation, then observes
the released guard; an access request already waiting on the guard rechecks the
device state and fails if teardown won. The release path remains available
after loss so the mutex and retained device references cannot be stranded.

## SDK boundary and remaining acceptance

The Linux symbol allowlist requires v1, v2, v3, acquire and release together.
The installed public header, provenance bridge sources, export inventory and
SDK manifest remain content-hashed, and their inputs participate in the exact
graphics SDK cache key. No private Dawn header enters the SDK.

Only `git diff --check` was run. Builds and authored tests remain unexecuted
under the directed fast policy. Physical Linux acceptance still requires the
AppScene external-memory and semaphore path to hold one access scope across
Graphite insertion/submission and native present, retire frames from private
completion primitives, and prove loss/teardown bounds and race-free
interleaving with Dawn submissions under validation layers.
