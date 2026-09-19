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
proved that they are deliberately header-only rather than package exports. The
contract now checks them in the public header, keeps exported host services
required in the export manifest, and uses the union only for the broad
fail-closed scan of unreviewed v1/v2 engine and interop symbols.

Only `git diff --check` was run for this fast CI-recovery slice. Current-main
CI remains the authoritative execution gate.
