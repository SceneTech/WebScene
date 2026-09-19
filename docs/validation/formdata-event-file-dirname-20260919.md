# FormDataEvent, file, and dirname source contract

## Implemented boundary

This WebScene #257 slice starts from exact `main`
`fc322d58bf4b95b69c8d6133dc72638c2dd24833`, after #608 established form
submission methods and `submit` / `formdata` ordering. It extends the existing
form-backed `FormData` constructor in both the top-level and frame realms.

`FormDataEvent` is now a dedicated `Event` subclass. Its constructor requires a
`FormDataEventInit`-shaped object containing a `FormData`, and its prototype
exposes the initialized object through a readonly `formData` getter. Form-backed
construction synchronously dispatches that interface with `bubbles` true and
`cancelable` false. Listeners receive the same mutable `FormData` returned by
the constructor, preserving #608 cancellation and event order.

File controls append each selected `File` in list order and preserve those file
objects. A file control with no selection contributes an empty `File` with an
empty name, an `application/octet-stream` type, and an empty body. Eligible
text, search, telephone, URL, email, and textarea controls with a nonempty
`dirname` attribute append direction immediately after their value. Explicit
inherited `ltr` / `rtl` direction is honored; `dir=auto` recognizes the first
strong character in the bounded Latin, Greek, Cyrillic, Hebrew, and Arabic
ranges used by this runtime slice.

## Ordering and routing

The new entries are emitted inside the existing #608 tree-order loop. Duplicate
names, selected-file order, external form ownership, submitter projection, and
the event's final mutation point retain their prior ordering. The implementation
only reads control state, file lists, and ancestor `dir` attributes. It adds no
structural ancestor invalidation route and no interactive validation UI.

Form-associated custom-element entries remain outside the #610 slice. The
adjacent `form-associated-custom-elements-20260919.md` source contract now adds
`HTMLElement.attachInternals()`, `ElementInternals`, static `formAssociated`
admission, form ownership and lifecycle, and `setFormValue()` entry integration.

## Authored evidence and validation status

`tests/WebPlatformSubset/contracts/formdata-event-file-dirname.html` records the
constructor and prototype shape, required member errors, event inheritance and
bubbling, mutable same-object delivery, selected and empty file entries,
`dirname` direction, and exact entry order. It derives from the WHATWG HTML
`FormDataEvent` interface and constructing-the-entry-list algorithms.

Per task direction, no build, generated-binding check, native test,
browser/WPT run, benchmark, computer-use check, scene/pixel check, product
check, package gate, or CI job was run. Only `git diff --check` is used for this
commit. The authored contract remains unexecuted source evidence.
