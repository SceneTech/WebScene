# Bounded PRIMARY selected-text publication source contract

WebScene issue #657 adds the provider half of automatic Linux PRIMARY
publication. The exact baseline is WebScene
`dabd1435c8e6415163067ad7ad5befb498a77126`; AppScene #275 is the dependent
native consumer. The provider is a product-neutral native-host signal and has
no X11, browser-process, Electron, CEF, WebView or VS Code dependency.

## Provider boundary

A settled nonempty selection in the currently focused, connected, writable
`input` or `textarea` emits the existing `webscene_host_request_v1` with this
exact shape:

- kind `WEBSCENE_HOST_REQUEST_CLIPBOARD_WRITE_V1`;
- request ID and target node ID zero;
- flags `WEBSCENE_HOST_REQUEST_CLIPBOARD_REPLACE_V1 |
  WEBSCENE_HOST_REQUEST_CLIPBOARD_PRIMARY_V1`;
- content type `text/plain`, no URL, and valid UTF-8 selected bytes;
- a maximum payload of 64 KiB, with oversize selections refused rather than
  truncated.

The zero request ID makes the update one-way. It creates no JavaScript API,
Promise, completion binding or user-activation requirement. Ordinary Clipboard
API and shortcut reads/writes retain their nonzero request IDs, existing flags,
16-operation completion cap and CLIPBOARD meaning.

Password, readonly, disabled, inert, disconnected and unfocused controls are
ineligible. A collapsed or empty selection, an invalid UTF-8 slice, a UTF-16
boundary inside a surrogate pair, and an oversize slice emit nothing. These
cases do not clear a previously published or pending PRIMARY value.

## Coalescing and lifecycle

Selection and value mutations mark only the active text control dirty. The
runtime extracts its final selected slice after the current script, event and
microtask work reaches a stable checkpoint. Extraction checks byte offsets and
the 64 KiB cap before allocating the request payload.

The mutex-protected typed-host FIFO recognizes only the exact zero-ID PRIMARY
publication shape. If one is already pending, the runtime replaces that lease
in place with the newest eligible payload. Otherwise it appends one only while
the aggregate 1,024-request queue has capacity. Publication therefore uses at
most one pending slot and cannot evict or consume capacity reserved by an
existing completion-bearing request. Saturation drops the update without
changing clipboard state or exposing selected bytes in diagnostics.

Top-level navigation discards every publication still in WebScene's queue and
clears its dirty observation. A queued child-frame publication carries a
private frame-owner identity, outside the public ABI, so frame navigation or
detach can remove it before retiring that realm. Engine teardown destroys the
same bounded state. Once a host has taken a stable one-way lease, it represents
the prior selection and may remain PRIMARY across document navigation,
matching desktop selection ownership. Collapse, blur and teardown never send
a clearing write.

## Authored contracts and remaining acceptance

`test_primary_selection_publication` covers the exact ABI, UTF-8 text,
same-task and pending-request coalescing, retention on collapse, ineligible
controls, detach, refusal above 64 KiB and admission at the cap. Existing
clipboard contracts continue to cover ordinary Clipboard API flags, request
IDs, completion and the PRIMARY middle-paste read path.

Unchanged Code OSS mirrors short Monaco selections into its writable hidden
textarea, so those exact selections reach this provider without a source
patch. Monaco can elide large or multipage screen-reader textarea content and
does not represent all multicursor model selections there. Exact large,
multipage and multicursor Monaco publication, readonly controls,
contenteditable and document `Selection` remain outside #657.

Only `git diff --check` is run for this fast implementation slice. The authored
native contract, compiler/build, browser-oracle comparison, 10,000-change CPU
and allocation measurements, AppScene #275 integration, physical X11 clients,
packaged unchanged Code OSS, VM and computer-use gates remain unexecuted.
