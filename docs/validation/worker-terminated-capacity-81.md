# Terminated Worker capacity lifecycle (#81)

## Gap

The runtime retained every dedicated Worker state in `dedicated_workers` until
navigation or engine teardown. The 64-worker safety bound used the container
size, so a page that created and terminated 64 workers could never create a
replacement. Erasing a stopped entry was unsafe because the retained
JavaScript wrapper's `postMessage()` and `terminate()` functions still carried
a native pointer to that entry.

This affects product-neutral worker pools and restart/recovery flows. It does
not depend on VS Code source changes and does not move worker execution into the
document realm.

## Ownership contract

- At most 64 live worker threads/isolates can run concurrently.
- `terminate()` joins the worker before its parent wrapper becomes weak.
- A worker-side `close()`, terminal startup failure, navigation, and engine
  teardown wake their owners and converge on the same stopped state.
- A stopped wrapper retains its callback record while JavaScript can still
  reach it. Its methods remain safe and idempotent, but it no longer consumes a
  live execution slot.
- Worker methods resolve native state from their branded receiver. Extracted
  methods reject with `TypeError` and cannot outlive a collected wrapper while
  carrying a dangling native callback pointer.
- Weak collection clears the wrapper and realm handles before the native record
  is erased at a parent-realm task or construction boundary.
- Retained stopped wrappers have a separate 256-record bound. At the boundary,
  the runtime requests one V8 collection and then fails explicitly rather than
  growing native records without limit.
- Queue contents and byte accounting are cleared by the existing stop path
  before a stopped record can be reclaimed.

## Focused gates

- `WEBSCENE_HYBRID_V8_RUNTIME_TEST_FILTER=worker-lifecycle-capacity` creates and
  terminates 80 workers while retaining all wrappers, confirms stopped methods
  remain safe, confirms detached methods reject, then completes a request/reply
  on a replacement isolated worker. Its second reduction starts 80 workers
  whose top-level scripts fail, retains their wrappers, and proves those
  terminal failures also release capacity for a successful replacement.
- `webscene-worker-terminated-capacity-profile.json` carries the same behavior
  as a browser-shaped contract.
- Existing termination, message-error, structured-clone, MessagePort lifetime,
  nested-worker, and navigation teardown gates remain the adjacent regression
  set.

## Validation status

Per the active throughput directive, no build, runtime test, WPT run, benchmark,
or packaged-product validation was executed for this slice. The authored diff
must pass `git diff --check` before commit. The focused commands above remain
required before promotion.

## Remaining #81 scope

This slice does not promote complete Worker support. Cross-realm transfer and
teardown acceptance across Window, iframe, dedicated Worker, and ServiceWorker;
module/classic loading failures; queue/heap/RSS throughput budgets; selected
upstream Worker/structured-clone WPT; and unchanged packaged Code OSS Monaco and
browser-extension worker acceptance remain open.
