# Flutter validation localization source contract

Issue: WebScene #722

Parent: WebScene #719

Release epic: WebScene #227

## Creation and ownership

`WebSceneRuntimeConfiguration.validationMessages` is a catalog keyed by the
stable `WebSceneValidationMessageReason` values 1 through 9. Flutter snapshots
and UTF-8 encodes that catalog during engine creation. The size-versioned
`webscene_flutter_engine_options_v1` structure lends those buffers only for the
`webscene_flutter_engine_create_v2` call; the macOS bridge copies every admitted
message into its existing per-engine `ResourceContext`.

The context is inserted only after the engine is created. Destruction removes
it from the bridge registry, retains the unique owner while
`webscene_engine_destroy` joins the native worker, and then releases it exactly
once. The original `webscene_flutter_engine_create` symbol remains available
and creates an engine without a catalog.

## Bounds and fallback

- A catalog contains at most the nine published reason IDs. Every configured
  value is valid UTF-8, nonempty, and at most 1,024 bytes.
- The callback accepts no more than four ABI arguments. Each argument is at
  most 256 bytes, for a bounded aggregate of 1,024 bytes. The source contract
  retains both the per-argument check and the explicit four-times-256 aggregate
  guard. Struct sizes, reason IDs, argument kinds, pointers, and UTF-8 are
  checked before the bridge copies the configured output.
- Missing reasons, invalid callback input, recursion, exceptions, insufficient
  destination capacity, and empty configuration return zero. The engine then
  uses its existing bounded English message. `setCustomValidity()` continues
  to bypass the formatter verbatim.

The callback reads retained strings and copies directly into the engine's
borrowed fixed-capacity destination. It performs no message-time allocation,
Dart callback, frame scheduling, layout, or polling. An atomic recursion guard
fails closed even if a future host invokes the bridge outside the current
single owner-worker contract.

## Deferred acceptance

Flutter builds, package publication, device execution, and cross-platform
bridge implementations remain deferred under the fast implementation policy.
This slice changes no browser behavior, Code OSS source, AppScene surface, or
general localization service.
