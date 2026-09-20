# Native microphone capture tracks (#775)

## Implemented contract

Secure top-level `navigator.mediaDevices.getUserMedia()` now accepts audio-only
requests using the constraint names reached by unchanged Code OSS. JavaScript
normalizes those values into a bounded version-1 JSON request and hands it to the
host as `WEBSCENE_HOST_REQUEST_MEDIA_OPEN_CAPTURE_V1`. Video and display capture
remain explicit `NotSupportedError` paths.

The host response is limited to 64 KiB and must use
`application/vnd.webscene.media-capture+json`. WebScene validates nonzero decimal
capture/generation identities, 8–192 kHz sample rates, one to eight channels,
and bounded device metadata before creating branded `MediaStream` and
`MediaStreamTrack` objects. Track labels and settings preserve the scoped host
metadata. A successful host open that cannot be bound is immediately followed
by a one-way stop request.

## PCM and lifecycle boundary

`webscene_engine_submit_media_capture_packet_v1` is the steady-state PCM ingress.
It validates size/version metadata, format identity, generation, frame order,
timestamp order, and an 8192-frame packet ceiling. The call performs no V8 work.
Mono is duplicated and multichannel input is reduced to its first left/right
channels in the existing fixed 16,384-frame stereo ring.

`webscene_engine_submit_media_capture_event_v1` accepts mute, unmute, error, and
end signals into a 256-entry bounded queue. JavaScript event delivery is moved
to the runtime worker. Clones share one host capture lease while keeping their
own PCM cursors and stop/enabled state. Stopping the final live clone emits
`WEBSCENE_HOST_REQUEST_MEDIA_STOP_CAPTURE_V1`; navigation and realm teardown do
the same for every remaining capture. Stale generations, released captures,
format drift, packet replays, and queue pressure return explicit status codes.

## Authored quality and performance gates

- `test_media_capture_track_host_handoff` covers canonical Code OSS constraints,
  typed open/stop requests, strict descriptor validation, branded stream/track
  settings, clone ownership, mute/unmute events, stale generation, format drift,
  packet ordering, post-release rejection, malformed response behavior, and a
  4096-by-128-frame ingress benchmark capped at two seconds.
- `media-devices-surface.html` keeps secure branding and rejects unsupported
  video/display and empty capture shapes without requiring a host completion.
- Producer work is allocation-free after host transport handoff: packet ingress
  writes directly to fixed storage, never schedules V8, and has fixed packet,
  capture, event, and ring bounds. The host provider remains responsible for a
  realtime-safe bounded transport before this serialized consumer API.

These gates are authored but were not executed under the active rapid-integration
directive. Evidence remains zero. #776 connects microphone tracks to
`MediaStreamAudioSourceNode`; #777 must supply the Code OSS AudioWorklet PCM path.
AppScene #294 and #295 must add Windows and Linux providers. Physical-device,
installed Code OSS Chat, long-duration memory/CPU, denial, device-loss, and
cross-platform package qualification remain open.
