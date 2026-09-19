# Bounded placeholder pseudo-element paint

Issues: WebScene #237, parent #235

Base: `2112a6d7e53844d3c1a461fa9937dc5063fa4bfe`

Unchanged Code OSS: `645f29cc3176500b4b5762ba887cf2a7f0ffdf2c`

## Selected inventory gap

The pinned unchanged Code OSS core CSS contains twelve standard
`::placeholder` selectors across the workbench shell, action widget, mobile
picker, issue reporter, chat, plan review, and agent-feedback controls. Every
one authors `color`; one also authors `opacity: 1`. WebScene already parsed the
pseudo-element and painted live `input`/`textarea` placeholder strings, but the
runtime pseudo classifier dropped those rules before cascade.

This slice assigns `::placeholder` its own compiled pseudo kind and retains only
the two observed paint components: resolved color and bounded opacity. The
state lives in the existing copy-on-write pseudo allocation and is created only
for a matching authored rule. It creates no generated box and does not affect
control geometry. Empty-value paint reads the retained state while nonempty
values continue to suppress the placeholder. Re-cascade replaces the complete
record, so class, custom-property, stylesheet, detach, and navigation changes
cannot retain stale placeholder paint.

The selector uses the ordinary compiled origin and its existing subject,
functional, combinator, and attribute dependency routes. Cascade does not scan
the document, parse during paint, poll, request frames, or add visual-tree
nodes. Scene publication performs one color selection and alpha multiplication
only when an eligible empty text control already paints its placeholder.

## Contracts and remaining inventory

The V8 scene contract checks custom-property color, fractional opacity, class
mutation, live-value suppression, restoration, and cleanup. The shared CSS
contract checks pseudo classification plus bounded color/opacity application.
The browser candidate checks the corresponding computed pseudo style.

The unchanged Code OSS core pseudo-class forms in its authored CSS are already
covered by the compiled matcher; `:visited` remains deliberately privacy-closed.
The remaining core pseudo-element inventory is:

- ten `::details-content` rules. The bounded static disclosure layout is now
  implemented by the follow-up details-content candidate; keyword-size and
  discrete transition behavior remain open;
- two functional `::highlight()` rules, which require named Highlight ranges,
  functional pseudo parsing, range paint, mutation, and teardown;
- two `::-webkit-details-marker` rules and six internal form-control part
  rules (`::-webkit-inner-spin-button`, `::-webkit-outer-spin-button`,
  `::-ms-clear`, and `::-webkit-search-cancel-button`), which require bounded
  native control-part ownership before their declarations can have an
  observable effect.

The two legacy WebKit/Mozilla placeholder aliases duplicate adjacent standard
rules and remain unclaimed. Extension-bundled CSS additionally uses named
highlights and one `::marker`; shadow-only `::part`/`::slotted`, complete marker
styling, first-line/first-letter layout, and cumulative browser/package/
performance evidence remain outside this slice. No Code OSS source changes.

The remaining parser-recognized but runtime-unmaterialized pseudo-elements are
`::first-letter`, `::first-line`, `::marker`, `::file-selector-button`, `::cue`,
`::cue-region`, `::grammar-error`, `::spelling-error`, and `::target-text`.
Functional `::highlight()`, `::part()`, and `::slotted()` also still require
parser and ownership work. The inventory diagnostics now
report the already implemented selection, backdrop, and WebKit scrollbar kinds
as supported instead of grouping them with those remaining gaps.
