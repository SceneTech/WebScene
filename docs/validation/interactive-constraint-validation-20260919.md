# Interactive constraint-validation source contract

## Implemented boundary

WebScene #708 makes `reportValidity()` interactive without changing
`checkValidity()` or submit-attempt validation. Form reports snapshot invalid
candidates in existing tree/form-owner order, dispatch one cancelable,
non-bubbling `invalid` event to every candidate, and choose the first
uncanceled candidate that remains connected and focusable after dispatch.
Control reports apply the same cancelation rule. A form-associated custom
element retains its validated descendant anchor so direct and form reports
focus and position feedback against the same element.

The selected target receives focus once. The document then owns at most one
validation message: a 1 KiB UTF-8-bounded string plus target and anchor IDs in
the document's existing lazy auxiliary allocation. The scene appends a clipped
overlay through existing retained rectangle, border, clip, and text commands;
it creates no author DOM and needs no presenter ABI. Normal scene publication
recomputes geometry from the current anchor, so layout, scrolling, and viewport
changes reposition feedback without timers, sustained frames, selector
matching, cascade, or visual-tree mutation.

The same bounded message produces one newest-wins assertive semantic live
event through the existing semantic ABI. It is consumed once and respects the
existing live-event count bound. Text content is not written to diagnostics.

## Lifetime and dismissal

Input or change on the target, custom-validity mutation, successful or repeated
interactive validation, focus transfer away from the target/anchor, target or
anchor subtree detachment, navigation, and teardown dismiss the retained
message. Navigation also retires any unconsumed semantic announcement. Stale
native IDs fail closed during scene construction, and a candidate removed by
an earlier `invalid` handler is skipped when focus is selected.

WebScene #710 also retires the message after related programmatic value and
constraint mutations. Existing recascade checkpoints recheck a connected
target and anchor after style/class/hidden/inert/disabled changes, dismissing
when the target becomes valid or barred or either element loses focus
eligibility. The retained-state clear is idempotent, so repeated synchronous
mutations request at most one additional scene publication and never poll.

## Validation status and deferred boundary

The behavior follows the HTML interactive constraint-validation ordering and
cancelation model and reuses WebScene's existing focus, scene, and semantic
contracts. Per task direction, no build, native test, browser/WPT run, pixel
check, package gate, or CI job was run. Only `git diff --check` is used for this
commit, so the authored contract remains unexecuted source evidence.

Rich multi-line bubble layout, host localization beyond the existing validation
messages, optional timed dismissal, and structural selector
invalidation remain outside #708.
