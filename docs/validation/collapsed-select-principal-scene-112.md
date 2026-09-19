# Collapsed select principal scene contract

Issue: [#112](https://github.com/SceneTech/WebScene/issues/112)

## Failure boundary

Collapsed single-select controls already retained their replaced-control layout,
selected option state, pointer target, and dedicated selected-label command. The
disclosure arrow was emitted as scene command kind `2`, however, which is the
DOM background plane. A fixed, modal, or positive stacking-context select emits
its opaque principal background on the foreground plane (kind `9` or `10`). The
presenter therefore painted that background after the arrow and hid the native
affordance. This is independent of the consumer and reproduces with an ordinary
fixed `<select>`.

The selected-label command also carried a width for alignment and measurement,
but the native text presenter does not use that width as a clip. A long option
could therefore paint through the disclosure slot or beyond the principal box.

## Implementation

The collapsed-select scene writer now:

- brackets the selected label with one bounded rectangular clip covering only
  the text slot;
- keeps the label on the DOM foreground plane;
- emits both disclosure segments as foreground-line command kind `14`;
- includes foreground lines in retained-scene damage bounds, using endpoint
  geometry just like background-line kind `2`.

The change retains the existing principal node identity, pointer and keyboard
event route, selected-option lookup, and popup controller. It adds two constant
scene commands per painted selected value and performs no tree traversal or
allocation beyond the existing text resource.

## Focused gate

`webscene_native_select_principal_scene` creates product-neutral light and dark
collapsed selects. The light control uses a fixed positive stacking context,
which is the composition case that previously covered the arrow. The contract
requires:

- exact 320 by 26 and 240 by 26 geometry and principal hit ownership;
- the authored light and dark background/foreground colors;
- foreground principal-background command kinds in the elevated case;
- exactly one clip, selected-label command, and clip restore per control;
- exactly two foreground vector lines and no background line for each arrow;
- the dynamically assigned selected option text in the scene resource.

Exact command counts are the bounded retained-scene size gate. Existing
`webscene_native_select_popup_context`, `webscene_native_html_select_add`, and
the `html-dynamic-select-layout.html` component contract continue to own
pointer/keyboard events, dynamic option mutation, geometry, hidden option
boxes, and following-row cadence.

## Validation status

Per the active throughput directive, the focused executable, WPT subset,
package, pixel comparison, and performance lanes were authored but not run in
this worktree. Only repository whitespace/error checking is permitted before
the local commit. The issue remains open until a fresh unchanged Code OSS 1.137
package passes the Settings light/dark pixel lane, pointer and keyboard
selection, reload persistence, and the repeated open/change/close performance
and retention gate.
