# Worker options and script-load policy (#701, #81, #227)

## Implemented boundary

Dedicated workers now convert the first `WorkerOptions` dictionary members for
`type`, `credentials`, and `name`. The defaults are `classic`, `same-origin`,
and the empty string. Invalid worker types, invalid credential modes, and
non-dictionary option values throw `TypeError` before a worker slot is
reserved.

The selected credential mode is retained by the worker realm and applied to
the entry script, classic `importScripts()` loads, static module dependencies,
and dynamic module imports. Classic entry and dependency loads remain
same-origin. Module dependency loads use CORS and require an admitted HTTP
response to carry a matching `Access-Control-Allow-Origin`; credentialed
cross-origin loads additionally require
`Access-Control-Allow-Credentials: true`. Wildcard origins are rejected with
`include` credentials.

The resource request carries the creator origin, final request base, script
destination, fetch mode, and credential mode to the existing host loader.
Unsuccessful HTTP status, a cross-origin classic redirect, or a missing CORS
admission fails closed. A successful redirect becomes the worker location and
the base for relative classic or module dependencies.

## Failure and lifecycle contract

Worker construction stays asynchronous after synchronous URL and option
conversion. Each worker captures a generation before its thread starts.
Termination, worker-side `close()`, navigation, and engine teardown advance
that generation and clear queued messages. Resource completion and worker
wakeups check the captured generation before publishing cookies, compiling a
script, dispatching an error, or running queued work.

A startup network, status, redirect, CORS, syntax, or module-resolution failure
uses one terminal transition. That transition publishes at most one bounded
error to the owning realm, clears queued messages, marks the worker stopped,
and makes its live execution slot reusable. Later completion or exception
paths cannot publish a second terminal error.

The existing limits remain unchanged: at most 64 live dedicated workers, 256
retained stopped wrappers, 256 messages and 16 MiB in either queue, and 16
bounded error strings per worker.

## Deferred acceptance

Per the implementation-first policy for #701, no build, native suite, browser
comparison, WPT run, package run, cross-platform CI, throughput, or heap/RSS
measurement was executed for this change. Those gates remain acceptance work
under #81 and #227. MessagePort reachability remains owned by #288, and general
nested-document navigation and security remain owned by #267.

The host resource API exposes the final response rather than each intermediate
redirect response. It receives the worker fetch mode, origin, and credentials
and therefore remains responsible for enforcing every redirect hop; the worker
runtime independently validates the returned final URL and response. A future
resource-contract revision would be needed for engine-side, per-hop CORS audit.
