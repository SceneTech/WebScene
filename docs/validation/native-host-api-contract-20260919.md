# Native host API contract recovery — 2026-09-19

WebScene main `c57d82dfef38c8e18eb691d54d3132851e34025a` failed CI run
`35430278924` in the TypeScript profile and bundler tooling step on Linux and
macOS. The native binary interop contract reported four current typed host APIs
as retired JavaScript invocation symbols:

- `webscene_engine_dispatch_drag_v1`
- `webscene_engine_take_download_request_v1`
- `webscene_engine_take_outbound_drag_request_v1`
- `webscene_engine_complete_outbound_drag_v1`

These APIs exchange drag and download host requests. They do not expose the
retired JSON JavaScript invocation transport. Follow-up run `35430597424`
showed the first contract fix had not reconciled the export surface, and
architecture run `35430888338` established the authoritative rule: every
non-instrumentation `WEBSCENE_API` declaration must appear in the macOS export
manifest. All six drag/download functions are now exported and classified as
independently versioned host services while the broad fail-closed scan for
unreviewed v1/v2 engine and interop symbols remains.

Only `git diff --check` was run for this fast CI-recovery slice. Current-main
CI remains the authoritative execution gate.
