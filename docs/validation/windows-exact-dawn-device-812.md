# Windows exact Dawn D3D12 device bridge (#812)

## Contract

The sealed `win-x64` Dawn SDK exports `websceneDawnQueryD3D12DeviceV1` and
installs `webscene/dawn_native_device.h`. A successful query returns borrowed
identities for the exact live Dawn D3D12 device and its direct command queue,
plus the adapter LUID and the public adapter/device tokens which own them.

The caller keeps its `WGPUDevice` alive, does not release either COM object,
and orders its native submissions on the returned direct queue. Querying adds
no frame-loop work and creates no second D3D12 device or queue.

## Source and package gates

- Null and incompatible result structures fail before registry lookup.
- Foreign tokens fail before a Dawn private object is dereferenced.
- Lost, non-D3D12, missing-queue, missing-device, and zero-LUID cases fail and
  leave the caller's result unchanged.
- The result advertises exact-device, direct-queue, and adapter-LUID
  capabilities together; partial success is forbidden.
- The bridge is compiled inside the pinned Dawn monolith and only the
  size/versioned C function is exported from `webgpu_dawn.dll`.
- The installed header, implementation, internal registry header, symbol
  policy, and export inventory are checksummed by the SDK manifest.
- `verify-sdk.py` rejects a stale, missing, or locally modified bridge.

## Deferred Windows runner and VM acceptance

Execution is deferred by the current fast implementation direction. Before
qualification, the cumulative Windows stack must demonstrate:

1. Build and install the sealed `win-x64` Dawn SDK on `windows-2022` and pass
   its export audit and SDK verifier.
2. Query a live D3D12 `WGPUDevice`; compare the returned device pointer, direct
   queue pointer, adapter LUID, adapter token, and device token with the pinned
   Dawn object's authoritative values.
3. Exercise every failure status, including a foreign non-null token, another
   backend where available, loss/closing, invalid size/version, and null
   arguments. Prove an initialized output sentinel is unchanged on failure.
4. Consume the tuple in AppScene's Graphite/DXGI host and show one shared
   device/queue, GPU-only presentation, resize/recreation, device-loss cleanup,
   and bounded resource counts.
5. Capture warm visible-idle CPU/GPU, frame latency, resize latency, allocation
   plateau, and copied bytes. The acceptance threshold is zero CPU pixel
   readback/upload and no per-frame device, queue, or context creation.

This evidence belongs in an immutable dated directory and must record the exact
WebScene, AppScene, Dawn, Windows SDK, driver, adapter, and VM/host revisions.
