# Persistent IndexedDB compatibility

WebScene's native V8 runtime can expose a durable IndexedDB subset for packaged
applications such as VS Code OSS. The host opts in with two values:

- `PersistentStorageDirectory`: a private directory owned by the host;
- `PersistentStoragePartitionKey`: a stable application/profile identifier.

`PersistentStorageQuotaBytes` sets the whole-partition quota. Zero selects the
native default of 256 MiB. IndexedDB remains absent when either required value is
empty, so an application never receives an in-memory API presented as durable.
The component manifest must also declare `storage.indexeddb` for SDK preflight.

The partition key separates applications and profiles. The runtime adds the
document origin and database name below it. Loopback origins use a stable
`scheme//loopback` identity so a trusted host can restart on a different ephemeral
port without losing `vscode-web-state-db-global`, `vscode-web-state-db-global-shared`,
or `vscode-web-state-db-empty-window`. Non-loopback origins retain their full origin.

## Commit and recovery model

Each database is one revisioned snapshot produced by V8 structured clone. Disk I/O
runs on a dedicated storage thread; the V8 owner thread only serializes/deserializes
and settles promises. A commit:

1. acquires a per-database interprocess directory lock;
2. reloads and validates the current revision;
3. rejects a stale expected revision;
4. checks the partition quota;
5. writes a temporary file with schema, identity, length, and content hash;
6. flushes the file and atomically replaces the prior revision.

Readers reject truncated, trailing, identity-mismatched, or hash-mismatched files
with `DataError`. The previous snapshot remains intact if a transaction aborts, a
quota check fails, or a stale writer loses a race. Read/write transactions replay
their mutation list against the latest same-version snapshot after a revision
conflict, up to four attempts. Version upgrades do not replay.

Hosts should remove a partition directory only while its engines are stopped.
Applications can remove an individual database with `indexedDB.deleteDatabase()`.

## Supported application surface

The current slice implements the operations used by VS Code OSS browser storage:

- `indexedDB.open`, `deleteDatabase`, `cmp`, and `databases` for databases seen by
  the current realm;
- upgrade, blocked, and versionchange lifecycles;
- readonly, readwrite, and versionchange transactions with commit and abort;
- out-of-line string, finite number, Date, binary, and array keys;
- object store `get`, `put`, `add`, `delete`, `clear`, `count`, `getAll`,
  `getAllKeys`, and forward cursors;
- structured objects, arrays, maps, sets, dates, array buffers, and typed arrays.

Indexes, `IDBKeyRange`, key paths, key generators, cursor update/delete and reverse
cursors are not yet implemented. Object stores requesting `keyPath` or
`autoIncrement`, and all index operations, fail with `NotSupportedError`. Blob/File
prototype restoration is not yet guaranteed across a durable round trip. This is a
bounded compatibility implementation and does not claim full IndexedDB WPT
conformance.

## Gates

`webscene_indexeddb_storage_tests` covers revision isolation, profile/origin
partitioning, rollback preservation, quota, corruption detection, asynchronous I/O,
abandoned temporary-write and stale-lock recovery, real cross-process stale-writer
rejection, and 100 durable 4 KiB commits under ten seconds.
`webscene_native_indexeddb_contract` covers the V8 API, Code OSS ItemTable
shape, upgrade/versionchange, rollback, cursors, restart across loopback port changes,
quota errors, and corruption errors.
The same regression deletes the rejected corrupt database and verifies that a
fresh version-one database can be created in its place.

The manifest names the native lifecycle regressions as evidence for candidate cases
that a single WPT document cannot drive: engine restart, direct file corruption,
process races, and interruption before atomic replacement. The runnable document
itself covers open/upgrade/versionchange, commit/rollback, cursors, request error
cancellation, connection reopen, quota, and competing connections.

The candidate manifest `tests/WebPlatformSubset/webscene-indexeddb-profile.json`
adds a project-owned WPT-style Code OSS transaction contract and records the focused
upstream areas that still require broader algorithms. Run it with a fresh directory:

```bash
dotnet run --project tests/WebPlatformSubset/runner -c Release -- \
  --manifest tests/WebPlatformSubset/webscene-indexeddb-profile.json \
  --selection candidate \
  --native-library /absolute/path/to/libwebscene_native_engine.dylib \
  --native-storage-directory /absolute/path/to/wpt-storage \
  --native-storage-partition webscene-indexeddb-wpt \
  --native-storage-quota-bytes 4194304 \
  --output TestResults/WebPlatformSubset/indexeddb
```

Storage operations do not mutate DOM, style, layout, or retained-scene state. The
ordinary required visual profile remains the rendering regression gate when this
feature is promoted on each release RID.
