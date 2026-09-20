# Microphone MediaStreamAudioSourceNode (#776)

## Implemented graph path

`AudioContext.createMediaStreamSource(stream)` and the branded
`MediaStreamAudioSourceNode` now select the first live audio track using stable
track-ID order and bind it to the existing native audio graph. The node has no
inputs and one output. It does not connect itself to the destination, so Code OSS
can feed its analyser/worklet chain without echoing microphone input.

Each graph source owns an independent cursor over the track's existing bounded
capture ring. The realtime renderer uses fixed 8192-frame stereo storage and
linear sample-rate conversion into the graph quantum. It performs no allocation,
locking, JavaScript, logging, or I/O. Disabled, host-muted, stopped, and ended
tracks become silent immediately while their realtime cursor continues. Graph
disconnect and context close release the reader without stopping sibling track
consumers or the shared host capture lease.

The graph publishes cumulative rendered and dropped-source-frame counters for
native diagnostics and regression gates. A lagging reader jumps to the retained
ring window and reports the discontinuity rather than blocking the provider.

## Authored quality and performance gates

- The native audio graph contract injects constant 16 kHz mono PCM into a 48 kHz
  context, checks analyser samples, confirms the unconnected destination stays
  silent, verifies disabled/muted/end behavior, and requires a nonzero overrun
  count after deliberate ring pressure.
- The browser/runtime host-handoff contract creates the source node from the
  negotiated microphone stream and checks branding and port shape.
- The WPT-derived media contract creates a source from a native stream, checks
  its brand/ports, and rejects an empty stream with `InvalidStateError`.
- A 4096-quantum source/write/render benchmark is capped at two seconds, and a
  100-cycle create/bind/close loop exercises deterministic teardown.

These gates are authored but were not executed under the active rapid-integration
directive. Accepted latency, CPU/RSS, physical-device, exact-package, and
cross-platform evidence remains zero. #777 implements the bounded AudioWorklet
PCM processor/message path used by unchanged Code OSS; AppScene #294 and #295
still own Windows and Linux microphone providers.
