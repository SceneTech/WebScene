# Bounded form history and autofill source contract

Issue: WebScene #717

Parents: WebScene #257 and #267

Base: `3fcd07afd753b0374d1d61fc7857b45be91efbf8`

## State and lifecycle boundary

- The runtime captures eligible controls only when a session-history or
  cross-document navigation boundary is crossed. It does not observe input
  mutations, poll controls, or schedule frames. Nested history entries retain
  their own snapshots for same-document traversal, cross-document traversal,
  and reload. Top-level replacement uses the existing host-managed profile
  partition when durable storage is configured.
- Restoration runs after blocking and deferred scripts, including synchronous
  custom-element upgrades, and before `DOMContentLoaded`. A bounded pending
  map delivers string restoration state to a form-associated custom element
  which upgrades later in the same document generation. History restoration
  uses mode `restore`; partition restoration uses mode `autocomplete`.
- Native input/textarea value, checkbox/radio checkedness, and select option
  selectedness are restored without dispatching `input`, `change`, keyboard,
  pointer, or submission events. The affected document subtree is recascaded
  once and semantic publication observes the resulting dirty document. Custom
  elements receive their existing `formStateRestoreCallback` reaction.

## Identity and privacy boundary

- The stable document key hashes effective origin plus the navigation URL
  without its fragment. The stable control key hashes tag/type, bounded
  id/name/form-owner material, and a bounded tree path. Profile files therefore
  contain opaque keys and admitted state, while the existing host profile key
  remains the outer application/profile partition.
- Password, file, hidden, image, submit/reset/button, disabled, inert, readonly
  or otherwise barred controls are rejected before value access. So are forms
  or controls with `autocomplete=off`, password/one-time-code tokens, credit
  card and transaction tokens, and conservative payment/security field-name
  markers. File/FormData/custom non-string restoration state is never copied.
  Values are never written to diagnostics.
- Per document the runtime visits at most 32,768 nodes and admits at most 512
  controls, 256 selected option indices per select, 64 KiB per string, and
  256 KiB total state. Nested history retains at most 4 MiB across its existing
  entry limit, evicting older form snapshots first. Durable state retains at
  most 32 document identities and 512 KiB in the existing profile quota.

## Persistence, cancellation, and cleanup

The profile storage schema is additive and reads existing schema-v1 cookie and
local-storage files. Schema v2 appends the bounded form section and keeps the
existing atomic replacement, owner-only permissions, coalescing worker, and
shutdown checkpoint. `WEBSCENE_PROFILE_CLEAR_ALL_SITE_DATA_V1` clears form
state with cookies, local storage, and IndexedDB; narrower cookie/local-storage
flags retain it. Navigation and frame retirement clear pending records, normal
history eviction releases entry snapshots, and runtime teardown releases all
remaining memory before joining profile storage.

## Deferred acceptance

The focused `form-history-autofill-state.html` fixture covers reload restoration,
secret/off exclusions, select state, and event suppression. Restart/privacy
automation, upstream WPT execution, package builds,
performance measurement, and physical-platform qualification remain deferred
under the fast implementation policy. This slice adds no Code OSS source,
credential store, general top-level history provider, input-event replay, or
autofill UI.
