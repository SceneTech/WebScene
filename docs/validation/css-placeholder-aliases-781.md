# Legacy placeholder selector aliases

Issues: WebScene #781, #237, and #235

Pinned unchanged Code OSS `645f29cc3176500b4b5762ba887cf2a7f0ffdf2c`
authors two `::-webkit-input-placeholder` selectors and two
`::-moz-placeholder` selectors in addition to its standard `::placeholder`
rules. WebScene previously compiled those full selectors as ordinary element
selectors and therefore dropped their declarations before pseudo cascade.

Both legacy suffixes now classify as the existing placeholder pseudo kind.
Their origin is compiled once through the normal selector cache and uses the
same dependency invalidation plan as `::placeholder`. Matching declarations
flow through the existing bounded color/opacity cascade and copy-on-write
placeholder record. Paint still occurs only for an eligible empty input or
textarea that already produces placeholder text.

The shared CSS source contract covers classification, origin compilation, and
interned payload identity for both aliases. The native V8 source contract
switches one retained input between standard, WebKit, and Mozilla rules, checks
their distinct premultiplied colors, suppresses paint for a nonempty live
value, restores the standard rule, and tears the document down.

This change adds no generated box, visual-tree node, layout branch, timer,
animation-frame demand, document scan, or persistent state field. The aliases
reuse the standard placeholder hot path. The authored tests were not executed
under the current implementation-throughput direction; browser pixels,
high-count allocation/performance evidence, and exact-package Code OSS
qualification remain zero until run.
