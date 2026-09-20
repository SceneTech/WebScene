# Hidden retained details markers

Issues: WebScene #783, #237, and #235

Pinned unchanged Code OSS `645f29cc3176500b4b5762ba887cf2a7f0ffdf2c`
authors two `::-webkit-details-marker { display:none }` rules for Chat
disclosures. WebScene already represents `<summary>` as a list item and retains
one marker geometry/paint path. The selector was previously classified as an
ordinary unsupported pseudo and its declaration never reached that path.

The selector now has a dedicated pseudo kind. Its bounded cascade accepts the
observed `display:none` value and restores the initial marker for
`initial`/`unset`/`revert`/visible display values. A hidden bit lives inside the
existing copy-on-write pseudo allocation only for a matching summary. Marker
text, geometry, intrinsic contribution, and scene paint all consume the same
existing `list_marker_text` decision; no second marker representation exists.

Layout-style equality includes the hidden bit, so a winning-value mutation
invalidates layout once while unrelated recascades remain paint-only. A
non-summary selector match returns before allocating pseudo state. The authored
native contract covers classification, initial visibility, hiding, restoration,
and the non-summary allocation guard. The browser/native reftest compares the
pseudo-hidden result with the established `list-style:none` reference.

No tests, builds, pixels, or performance gates were executed under the current
implementation-throughput direction. Dynamic mutation, 4,096-summary scaling,
teardown, exact Code OSS Chat pixels, and cross-platform package evidence remain
zero until run.
