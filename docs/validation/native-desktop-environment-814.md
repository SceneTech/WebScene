# Native desktop environment ingress (#814)

`webscene_engine_set_desktop_environment_v1` is the product-neutral host ABI
for desktop appearance, display, power, and session state. The 64-byte v1 value
is copied during the call. Unknown versions, flags, invalid DPI/scale values,
and non-zero reserved data are rejected. Equal snapshots do not wake the
worker.

Appearance updates reuse the existing targeted media-query recascade and live
`matchMedia` delivery. System color values are read from the same worker-local
environment during CSS and scene work. Display, suspend/resume, availability,
lock/unlock, and session-ending changes are reduced to the newest snapshot and
dispatched from the JavaScript worker. No timer, polling thread, document scan,
or idle animation frame is introduced.

## Authored gates

- C and managed ABI size/version/source layout contracts.
- Malformed and future-version rejection.
- 10,000 duplicate notifications produce no additional worker wake.
- Dark/light, forced-colors, prefers-contrast, live `matchMedia`, and system
  color behavior.
- Suspend/resume, display availability/topology/DPI, lock/unlock, and
  session-ending transition ordering and navigation cleanup.
- Cascade/layout/scene counters prove paint-only state avoids layout and
  unrelated selector work.
- Idle CPU/frame/wake, notification latency, and RSS plateau measurements.
- Unchanged Code OSS visual and lifecycle comparisons.

Execution is intentionally deferred under the current fast-integration
direction; this document records the required qualification lane.
