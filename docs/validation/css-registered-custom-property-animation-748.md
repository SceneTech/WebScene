# Registered custom-property animation contract (#748)

WebScene retains document-level `@property` registrations for the `<angle>` and
`<percentage>` grammars used by unchanged Code OSS. Registrations are bounded to
64 per document; names and values are bounded to 128 bytes. A keyframe definition
retains at most eight registered custom-property tracks and 64 stops per track,
inside the existing eight-animation admission budget.

The shared cssparser stream now emits `@property` descriptors as declarations.
The prepared stylesheet and live runtime retain syntax, inheritance, initial
value, owner identity, and typed keyframe stops. Invalid or incomplete rules are
rejected atomically. Non-inheriting registrations materialize their initial value
only on elements whose declarations consume them; computed style supplies the
same initial value without allocating custom-property state on unrelated nodes.
Invalid authored values remain visible through inline CSSOM and compute to the
registered initial value.

The host-clock sampler interpolates registered values only while an admitted
track contributes. It updates the computed custom-property value and retained
background/mask/border-image tokens only when the serialized sample changes.
Paused and settled tracks request no idle frames. Stylesheet removal, navigation,
cache reset, and document teardown release registrations and sampled track state.

Coverage:

- `webscene_css_parser_tests` proves the Rust syntax stream emits all three
  descriptors for `@property`.
- `native_web_css_service` proves preparation, ownership, non-inheriting initial
  values, zero allocation across 256 unrelated nodes, midpoint interpolation,
  dependent gradient-token updates, and removal.
- `test_registered_custom_property_keyframes_update_dependent_paint` covers live
  CSSOM identity, percentage/angle sampling, forwards fill, and computed paint.
- `css-registered-custom-property-animation.html` is the browser/native candidate
  for CSSPropertyRule reflection and a deterministic paused midpoint.

Conic-gradient rasterization, border-image gradient rasterization, arbitrary
registered syntaxes, `CSS.registerProperty()`, and additive composition remain
separate paint and Properties & Values work.
