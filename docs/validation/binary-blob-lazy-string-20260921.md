# Binary Blob lazy-string validation — 2026-09-21

## Owner

WebScene #280 and the unchanged Code OSS workspace acceptance parent #252.

## Problem

The native WebSocket bridge delivers binary messages as `Uint8Array` values. The default WebSocket `binaryType` is `blob`, so each message constructs a WebScene Blob. Blob construction copied the bytes correctly, then eagerly called `String(part)` for every part to support the runtime's existing compatibility `Blob#toString()` behavior.

For a `Uint8Array`, JavaScript stringification expands every element into comma-separated decimal text. The exact Code OSS startup stream receives about 11.87 MiB on its management socket, mostly in 262,144-byte frames. None of the remote protocol consumers request `String(blob)`, so this expansion was discarded work on every frame.

## Change

Both top-level and child-frame Blob implementations retain typed-array string descriptors and materialize the current compatibility string only when `String(blob)` is explicitly requested. Byte construction, Blob size, source snapshotting, `arrayBuffer()`, FileReader order, slicing, and object-URL behavior remain unchanged.

The optimization is deliberately limited to ordinary `Uint8Array` parts. Other views retain their prior immediate string coercion semantics.

## Focused gates

Release native engine, macOS arm64:

- 48 × 262,144-byte Blob construction (12 MiB total): **0.867 ms**
- the same construction plus explicit string expansion: **55.793 ms**
- deferred path is **64.4× faster**
- source mutation after construction retains `1,3,5,7`, proving byte/string snapshot behavior
- WebSocket cross-socket marker remains event **2**
- 100-cycle WebSocket p95: **0.469 ms**
- 20-cycle protocol Ready p95: **1.775 ms**
- 20-cycle protocol Initialized p95: **8.962 ms**
- FileReader p95: **0.030 ms**
- FileReader under a 4,096-timer backlog completes in **0.318 ms** after one competing timer
- protocol heap returns to the pre-run value

Command:

```sh
WEBSCENE_NATIVE_ENGINE_TEST_FILTER=websocket-file-reader \
  /private/tmp/webscene-882-build/webscene_native_engine_tests
```

## Product gate

Rebuild the exact AppScene SDK and unchanged Code OSS package from the merged WebScene commit. Retain the exact remote root/provider/five resolved/model/rendered children and require first-child paint below two seconds before closing #280 or #252.

