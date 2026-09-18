# Durable browser profiles

WebScene is ephemeral unless the host supplies both
`webscene_engine_options.storage_directory` and a stable
`storage_partition_key`. The same identity and root are shared by cookies,
`localStorage`, and IndexedDB. Origins remain separate below the profile, so a
stable application partition does not merge unrelated web origins.

## Lifecycle and failure model

- A profile takes a non-blocking operating-system file lock. A concurrent
  process cannot open or clear that partition; storage falls back to ephemeral
  operation and the standalone clear API returns `BUSY`.
- Persistent cookies and `localStorage` load when the runtime worker starts.
  Mutations update memory synchronously and enqueue a coalesced profile
  checkpoint. File creation, flush, and replacement run on the storage worker.
- Orderly engine destruction checkpoints the latest admitted snapshot before
  returning. There is at most one active write and one coalesced pending
  snapshot. Atomic replace means interruption exposes either the prior or next
  complete schema, never a partial commit.
- The qualification budget for orderly shutdown is 100 ms per close on average
  (5 seconds for 50 open/mutate/close cycles). The regression test also checks
  that file descriptors and temporary files stay bounded across those cycles.
- The binary profile carries magic, schema version, partition identity, record
  bounds, revision, payload length, and checksum. Unknown, truncated, or
  corrupt data is ignored as an empty profile and replaced by the next valid
  mutation. No cookie names, values, tokens, or serialized contents appear in
  diagnostics.

Only cookies with a future `Expires` or positive `Max-Age` are checkpointed.
Session cookies and all `sessionStorage` data remain memory-only. Cookie
domain, host-only, path, creation order, expiry, `Secure`, `HttpOnly`, and
`SameSite` attributes round-trip through the profile. Normal replacement,
deletion, expiry pruning, visibility checks, and bounded cookie limits operate
on the restored jar.

`localStorage` is partitioned by serialized origin and retains insertion order.
Each origin has a 5 MiB limit (or the smaller configured profile quota), with a
synchronous `QuotaExceededError` before mutation. Opaque origins receive
`SecurityError`. The configured quota also bounds the complete profile file.

## Clearing and local-data threat model

Call `webscene_profile_clear_data_v1` only after closing engines for that
partition. Flags independently clear cookies, `localStorage`, or all data in
the partition; all-site-data also removes IndexedDB database files. Traversal
has a fixed entry bound. Partition keys are hashed and are never interpreted
as paths; clearing cannot escape `storage_directory` or delete the root itself.

Profile directories and files use owner-only permissions where the platform
supports POSIX modes. The data is not encrypted by WebScene and is readable by
the same OS account. Hosts should choose an OS-protected application-data
directory and may place it on encrypted storage or integrate platform
credential protection according to their threat model.

IndexedDB keeps its existing per-database transactional files from issue #56.
The browser profile composes with the same host root, partition hashing, atomic
replacement, quota, and worker-lifecycle model without treating cookies or Web
Storage mutations as IndexedDB transactions.
