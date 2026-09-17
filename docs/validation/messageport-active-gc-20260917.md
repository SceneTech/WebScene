# Active MessagePort garbage-collection gate

Issue [#288](https://github.com/SceneTech/WebScene/issues/288) requires an active
receiver to survive garbage collection when JavaScript retains only its entangled
sending port. The exact failure assigned a `Promise` resolver directly to
`port.onmessage`; a callback-shaped substitute is not sufficient evidence.

This focused slice adds the direct resolver to the native regression and to the
portable WPT contract. It also adds read-only MessagePort diagnostics for binding
slots, retained and reclaimed bindings, queued messages and bytes, and queue high
water marks. The diagnostics let the stress gate distinguish successful delivery
from a growing native binding or queue leak.

## Results

- The Release native filter completed 1,000 alternating direct-resolver and
  `addEventListener` round trips with two forced collections per cycle.
- Round-trip p95 was 0.823209 ms and maximum was 1.08904 ms.
- Two binding slots were reused; zero bindings, messages, or bytes remained after
  settling. Queue high water was one message and five bytes.
- V8 used heap changed from 507,576 to 507,828 bytes. Peak RSS changed from
  36,962,304 to 37,289,984 bytes.
- The focused native WPT passed one document and all five subtests. Adjacent
  `messageport-gc` and `worker-messageport` filters passed.
- Chrome for Testing 151.0.7922.34 passed the same five modes with eight exposed
  garbage collections before each delivery.

Machine-readable evidence is in
`docs/validation/evidence/messageport-active-gc-20260917.json`.

## Remaining issue acceptance

This slice does not close #288. Cross-realm identity and release still need direct
coverage for dedicated Workers, Service Workers, iframes, navigation, Worker
termination, and engine teardown. Those tests must also prove listener removal and
inactive-port collection without weakening the active-port guarantee.
