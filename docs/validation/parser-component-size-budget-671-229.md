# Parser component size budget (#671 / #229)

The CSS/selector parser archive is a reusable SDK component. It must remain
free of the runtime HTML parser while CSS and selector coverage grows.

`eng/sdk/verify-parser-components.py` enforces three independent boundaries:

- required CSS and selector ABI symbols must be present;
- HTML ABI symbols and `html5ever::` symbols must be absent;
- the focused archive must be no more than 90% of the full parser archive and
  no more than 18,000,000 bytes.

The ratio detects a collapsed component split. The absolute limit detects
package growth even when the full parser grows at the same rate. Symbol checks
remain the semantic proof that HTML code was not retained.

The Linux observation that triggered this update was 16,511,566 bytes for the
CSS/selector archive and 19,346,060 bytes for the full archive, a ratio of
0.853485. Both byte values and both configured limits are emitted in
`parser-components.json` for later release/package-size comparison.

`eng/sdk/test-verify-parser-components.py` locks the passing boundaries and
independent failures for ratio, bytes, missing CSS ABI, retained HTML ABI, and
retained `html5ever` symbols.
