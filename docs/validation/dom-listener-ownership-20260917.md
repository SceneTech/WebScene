# DOM listener ownership after structural CSS integration

Base: `4040058e74d76b4485961d65dad74a77c46072fb` (merged #245).
Workstream: #237 lifecycle support for #258; full CSS epic #235 remains open.

## Reproducer and cause

The original #258 oracle commit `8deca42d4bfd86d6ce0cc841a8e0a2b72d0cbf64`
is still unavailable in this local object database and has no matching remote
branch. This report does not claim that oracle or its broader workload is fixed.

The local 512-row, 100-detach/reattach-cycle reduction was strengthened to:

- Observe a nonzero baseline and an actually populated node snapshot.
- Wait for cleanup script completion, not an arbitrary scene publication.
- Assert each requested low-memory notification completed.
- Keep reachable wrappers, style/classList objects, expandos and listeners valid
  through detached GC and reattachment, including a listener added while detached.
- Require reclamation without a forced redraw and zero extra layouts/publications
  over a subsequent 250 ms idle interval.

On unchanged merged runtime code, cyclic listener closures retained **2,565 nodes**
after cleanup (baseline **1**). Explicitly unregistering the listeners before
cleanup, with all other fixture behavior unchanged, passed **3/3** times with
baseline/populated/final counts **1 / 2,565 / 1**. The normal registered case failed
twice. This is distinct from the earlier reproducer's synchronization mistake.

`frame_event_listeners` stored strong V8 callback handles. A callback closing over
panel state rooted that state and its DOM wrappers even after the final user root
was removed. Native subtree release could not run because those wrappers remained
live, so its eventual listener cleanup could never break the cycle.

## Implementation

Node targets in the element-event registry now own private listener-entry maps. Each entry retains its
callback and optional signal; the native dispatch index holds only weak handles
to those values and to the target/entry. Connected wrappers remain rooted by the
existing DOM policy. Reachable detached wrappers retain their tree and listeners
through the existing detached-wrapper graph, while unreachable cycles can collect.

Removal, once delivery and abortion delete the ownership entry as well as resetting
the native registration. Native input, programmatic event and resize dispatch paths
share that retirement helper. Detached frame-context and native subtree retirement
also clear ownership entries. Window, document, standalone EventTarget and MediaQueryList
ownership are not redesigned by this change.

No per-detach scan of the global listener table was introduced. No site-specific
handling, event dropping, cache timeout, resize deferral or stretched frame is used.

Chromium reference: its garbage-collected
[RegisteredEventListener](https://raw.githubusercontent.com/chromium/chromium/main/third_party/blink/renderer/core/dom/events/registered_event_listener.cc)
traces the callback, and
[EventListenerMap](https://raw.githubusercontent.com/chromium/chromium/main/third_party/blink/renderer/core/dom/events/event_listener_map.cc)
traces the registrations. This is an ownership-design reference, not a claim that
WebScene implements Blink/Oilpan or shares its performance characteristics.

## Verification

- Final `webscene_native_panel_listener_lifecycle` CTest: **5/5 repeated passes**.
  The panel returns to baseline 1 without explicitly unregistering its listeners;
  reachable listener/wrapper identity and styles survive GC first.
- Connected-target retirement gate: **four routes × 128 callbacks**. Functions and
  object listeners survive GC while registered; `removeEventListener`, once delivery
  and AbortSignal removal allow every corresponding WeakRef to clear while the target
  remains connected. Included in the repeated CTest and default native suite.
- Required product-neutral `dom-detached-listener-lifecycle.html`: **5/5 Chrome and
  5/5 native**, covering 100-cycle propagation, capture variants, once/abort, object
  listeners, reentrant removal and installation after detachment.
- Adjacent native WPT selection `event-`: **8/8 documents, 20/20 subtests**.
- Adjacent Chrome propagation/window-lifecycle/expando contracts: **3/3 documents,
  5/5 subtests**.
- Native `event-listener-options`, `detached-dom-gc`, `desktop-host-capabilities`,
  `navigation-realm` and `wrapper-retention-recascade`: pass.
- Cumulative `css-invalidation-scaling` gate: pass. This is a work-counter/correctness
  check; concurrent builds mean its incidental timing output is not performance evidence.
- Certification shared-library/test build: pass. Production SDK static runtime
  build with certification disabled and Inspector enabled: pass. Inspector runtime
  behavior was not separately qualified in this turn.

Local evidence:

- `/tmp/css-listener-lifecycle-chrome-final-20260917/results.json`
- `/tmp/css-listener-lifecycle-native-qualified-20260917/results.json`
- `/tmp/css-listener-adjacent-chrome-20260917/results.json`
- `/tmp/css-listener-adjacent-native-20260917/results.json`
- `/Volumes/SSD/builds/webscene-spotify-tests/Testing/Temporary/LastTest.log`

Artifact hashes and machine-readable result summaries are recorded in
[`evidence/dom-listener-ownership-20260917.json`](evidence/dom-listener-ownership-20260917.json).
The local test executable also contains the preserved, uncommitted IndexedDB fixture
edit; none of these focused lifecycle groups executes that fixture. The shared and
production runtime binaries contain no IndexedDB edits.

Commands from `/tmp/webscene-spotify-probe`:

```sh
ctest --test-dir /Volumes/SSD/builds/webscene-spotify-tests \
  -R '^webscene_native_panel_listener_lifecycle$' --output-on-failure --repeat until-fail:5
WEBSCENE_NATIVE_ENGINE_TEST_FILTER=panel-subtree-reclamation-unregister-control \
  /Volumes/SSD/builds/webscene-spotify-tests/webscene_native_engine_tests
```

The debug-live workflow was used with a focused-test fallback because its debugger
connector was unavailable. No interactive debugger session was started.

## Remaining scope

Obtain #258's exact original oracle and requalify its workload, RSS, visual accounting,
latency and idle CPU budgets. These tests prove bounded node/callback lifetime for
this reduction, not a general native-allocation/RSS bound, a faster Spotify resize,
or unchanged VS Code acceptance. Listener-table registration/search complexity and
non-DOM EventTarget lifetime remain separate work.

The prior failed package checks were explicitly left outstanding when #245 merged;
this is not cross-RID/release qualification. IndexedDB remains assigned to its owner;
the unrelated local fixture repair is deliberately excluded from this change. No CI
monitoring, installed SDK replacement or manual Spotify app update was performed.
