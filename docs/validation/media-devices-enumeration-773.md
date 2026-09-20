# MediaDevices enumeration surface (#773)

## Implemented scope

Secure top-level runtime realms expose a same-object `navigator.mediaDevices`,
branded `MediaDevices`, `MediaDeviceInfo`, and `InputDeviceInfo`, plus the audio
constraint names reached by unchanged Code OSS. `enumerateDevices()` queues the
existing immutable typed host request with kind
`WEBSCENE_HOST_REQUEST_MEDIA_ENUMERATE_DEVICES_V1`, the requesting security
origin, no permission token, and no native device identifier.

The host completes with
`application/vnd.webscene.media-devices+json` and schema version 1. WebScene
accepts at most 256 KiB, 64 audio-input entries, unique non-empty opaque device
IDs, and 4096 code units per ID, group, or label. The host remains responsible
for redacting labels/group identifiers before permission. A marked default
entry is returned first while authored device IDs remain unchanged.

Malformed, oversized, denied, cancelled, stale, insecure, nested-realm, and
queue-pressure cases fail closed. Navigation already retires typed requests and
their promise targets. `getUserMedia()` and `getDisplayMedia()` currently reject
with `NotSupportedError`; they never synthesize a successful stream before the
capture/track children of #770 land.

## Authored gates

- `media-devices-surface.html` covers secure branding, same-object identity,
  exact constraint names, and fail-closed capture methods.
- `test_media_devices_enumeration_host_handoff` covers the typed request,
  security origin, strict response MIME/schema path, default ordering, branded
  immutable device objects, malformed JSON, and bounded failure behavior.

The gates were authored but not executed under the active rapid-integration
directive. This slice does not claim microphone capture, device-change events,
audio graph input, AudioWorklet, or Code OSS Chat acceptance.
