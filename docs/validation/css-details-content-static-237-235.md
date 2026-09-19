# Bounded details-content pseudo-element layout

Issues: WebScene #237, parent #235

Base: `8a58690f8a4059affa4b3caa0ba0e4340e4c9c52`

Unchanged Code OSS: `645f29cc3176500b4b5762ba887cf2a7f0ffdf2c`

## Selected inventory gap

The pinned unchanged Code OSS core CSS has ten `::details-content` selector
occurrences in the chat workbench. They style two disclosure components. The
two base selectors author `block-size: 0`, `overflow: hidden`, `opacity: 0`,
and transitions. Their `[open]` selectors author `block-size: auto` and
`opacity: 1`. One base selector also authors `box-sizing: border-box`, inline
start margin and padding, a one-pixel inline-start border, and a border-image
gradient. The remaining six selectors disable transitions for reduced motion.

This slice gives `::details-content` an independent compiled pseudo kind and a
compact record in the existing lazy pseudo allocation. It retains the observed
static zero/auto block-size, overflow, opacity, inline-start margin/padding,
and solid border components. The observed `box-sizing: border-box` value is
recognized, but has no separate static geometry effect because closed content
is suppressed and open content uses automatic block size. Closed `<details>`
elements expose only their first `<summary>` element. Open content participates
in ordinary bounded retained layout with one inline inset and one retained
group bound for border and opacity paint. Inherited `dir` resolves that inset
and border at the logical inline start, including the right edge for RTL. No
anonymous DOM node or visual-tree node is added.

Code OSS assigns `HTMLDetailsElement.open` directly. WebScene previously
exposed that reflection only for dialogs, so this slice adds the generated
details interface mirror and routes the property through the existing bounded
boolean-attribute recascade. Cancelable summary clicks toggle only when the
clicked summary is the first summary child, publish one noncancelable `toggle`
event, and synchronously invalidate the affected details subtree.
Interactive descendants such as buttons and links retain their nearer native
activation and do not also toggle the disclosure; inert descendants still
delegate activation to the first summary.

## Lifecycle and performance

Closing clears retained geometry, text fragments, and scroll extents only for
the disclosure content subtree. Opening reuses normal layout and compiled
`[open]` selector invalidation. Detach and navigation use existing node and
pseudo-state teardown. Static closed, open, and filled states request no idle
frames; paint performs no parsing, selector matching, cascade, polling, or DOM
mutation.

The source contract checks pseudo classification and bounded values. The V8
scene contract checks closed suppression, `open` reflection, inline geometry,
inherited RTL logical geometry, solid border paint, interactive-descendant
activation isolation, inert-descendant first-summary activation, exactly-once
toggle dispatch, and cleanup. The browser candidate mirrors these disclosure
semantics.

## Explicit remainder

This implementation does not claim interpolation between zero and the `auto`
keyword, transition timing, discrete `content-visibility` transitions, or the
`interpolate-size: allow-keywords` host property. Those require a retained
details-content animation track and deadline integration. The Code OSS
border-image linear gradient remains deferred; this slice paints the authored
solid border color underneath it. General details name-group exclusivity,
programmatic-open toggle-event task coalescing, an implicit summary when none
is authored, and non-Code pseudo properties also remain outside this bounded
slice. No Code OSS source changes.
