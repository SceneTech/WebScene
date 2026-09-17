# Native Spotify blank-window regression — 2026-09-17

Base: `14ca6837` on PR #245. This is a startup correction, not CSS/resize
performance acceptance.

## Cause and controlled reproduction

The current SDK loaded Spotify's scripts and reached `document.readyState ===
'complete'`, but retained only 80 DOM elements and an empty React root. Inspector
showed React's root with `pendingLanes: 16`, `callbackPriority: 16`, and a pending
callback. A new MessageChannel round-trip worked. Querying live MessagePort
objects found only the sender with no handler: the receiving wrapper had been
collected.

A diagnostic-only constructor wrapper, installed at a web-player breakpoint in
the actual navigated realm, retained the channel. That changed the same runtime
to 2,724 elements and cleared React's pending lanes. An earlier injection in the
initial, pre-navigation realm was overwritten by navigation and was not a valid
retention experiment. No diagnostic wrapper is present in the shipped demo.

The minimal native control reproduces the failure without Spotify: keep only
`channel.port2`, put a handler on `port1`, force `notify_low_memory()`, then post.
The unpatched runtime exits 1 with `Entangled receiver was lost during GC`.

## Correction and scope

Local entangled wrappers have private V8 references to each other rather than
independent weak native roots. Closing or transferring a port removes those
edges; deserializing a same-isolate transfer reconnects the new wrapper. Dead
cycles remain collectible. This conservatively retains a local peer even without
a listener, but does not permanently root channels in the runtime.

This addresses the same-isolate scheduler regression. It does not claim complete
cross-worker/queued-port garbage-collection conformance or change queue-start
semantics. The [HTML port lifetime rules](https://html.spec.whatwg.org/multipage/web-messaging.html#ports-and-garbage-collection)
require an entangled receiver with a message listener to remain reachable from
its peer.

## Validation

- Native forced-GC cases: `onmessage`, `addEventListener` plus `start`, and a
  structured-cloned sender.
- Worker/MessagePort suite: transfer, 10,000 round trips, queue bounds, navigation,
  termination, iframe bootstrap and memory reclamation. Repeated discarded,
  unclosed channel batches exceed binding capacity and remain reclaimable;
  reachable closed senders do not retain their former peers.
- New required WPT-style contract `messageport-scheduler-lifetime.html`: 3/3
  checks in Chrome and native WebScene. Portable coverage does not force GC;
  the native companion does.
- Rebuilt the installed SDK and unmodified native demo with pinned LLVM 22.1.8.
  A 25-second probe of Spotify client `1.3.2.208.ga5cc8c4a` reported 2,724 DOM
  elements, 68 visible catalog cards and 15 image elements. First image decoded
  as 300×300 with `complete: true`.
- Native-window screenshot confirms catalog text and cards now render. Artwork
  still appears as coloured placeholders and the header is clipped. These remain
  visual blockers; neither smooth resizing nor Chrome performance parity is
  accepted by this check.

Local evidence: `/tmp/css-messageport-{tests,spotify-probe}-20260917.log`,
`/tmp/css-messageport-native-20260917/results.json`, and
`/tmp/css-messageport-chrome-20260917/results.json`.
