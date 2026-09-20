# Origin-partitioned BroadcastChannel

Issue #764 adds the missing `BroadcastChannel` primitive used directly by
unchanged Code OSS Markdown diff previews and indirectly by IndexedDB-backed
file changes, profile changes, and voice-window ownership.

The native V8 provider routes structured-cloned messages only between open
channels with the same storage partition, security origin, and channel name.
It excludes the sending object, preserves FIFO task delivery, publishes
`MessageEvent` origin/source/ports metadata, checkpoints microtasks, and joins
the existing fair asynchronous task-source rotation.

The process registry owns only weak endpoint references. JavaScript wrappers
become weak unless an open channel has a `message` or `messageerror` listener.
Close, garbage collection, frame retirement, navigation, and runtime shutdown
clear queued clones and unregister the endpoint without retaining its V8
context.

Boundaries are explicit: at most 4,096 live channel bindings, a 4 KiB UTF-8
name, 256 queued messages and 16 MiB queued data per receiver, and 64 MiB total
fanout per post. Capacity failure throws instead of silently dropping or
delivering a partial recipient set.

Authored coverage consists of the direct browser contract
`contracts/broadcast-channel-origin-partition.html` and the native companion
`test_origin_partitioned_broadcast_channel_contract`. They cover API shape,
same-realm and same-origin iframe routing, separate runtime instances,
structured data and ArrayBuffer copying, sender exclusion, FIFO order,
MessageEvent metadata, clone failure, idempotent close, closed-state failure,
and process-registration retirement. These gates were authored but not run
under the active implementation-throughput directive.
