# IndexedDB read/write transaction serialization

## Consumer failure

The unchanged VS Code OSS 1.137 workbench opens several `readwrite`
transactions against its `vscode-web-state-db-global` database during startup
and state flush. WebScene previously let every transaction commit from the
revision visible when the transaction was created. More than four overlapping
writes could exhaust the compatibility layer's conflict retry limit and reject
a valid transaction with `AbortError`.

The exact AppScene `cd0a02e` / WebScene `98077f23` Release package reproduced
this while saving the encrypted MCP input metadata. The native log reported
`IndexedDB database changed before this transaction committed` before the
reload lifecycle began. The encrypted key survived, while the workspace
`mcpInputs` record did not.

## Contract

WebScene now serializes `readwrite` commits per database inside one runtime.
Each transaction retains its isolated mutation log. When it reaches the head of
the queue, the existing optimistic concurrency path reloads the newest durable
revision, replays the mutations, and commits atomically. A transaction aborted
while queued is skipped. The native revision check remains in place for writes
from separate runtimes or processes.

Serialization is database scoped, so independent databases continue in
parallel. Request delivery and readonly transaction behavior are unchanged.

## Focused gate

`webscene_native_indexeddb_contract` starts twelve overlapping read/write
transactions after the initial database write. This exceeds the former
four-retry ceiling and requires all fourteen records to be visible through the
normal cursor path. The focused Release/V8 test passes in 1.31 seconds.

The final consumer gate is the exact packaged Code OSS MCP store, reload, and
reopen sequence. It remains tracked by WebScene #269 and the Code OSS release
acceptance epic.
