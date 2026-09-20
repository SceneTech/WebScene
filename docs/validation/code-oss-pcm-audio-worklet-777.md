# Code OSS PCM AudioWorklet subset (#777)

## Implemented scope

`AudioContext.audioWorklet.addModule()` accepts the bounded Blob module emitted
by unchanged Code OSS `pcmCaptureWorklet.ts`. The loader caps source at 64 KiB,
recognizes only `PcmCaptureProcessor` registered as `vscode-pcm-capture`, parses
a 128-aligned chunk size from 128 through 65,536 samples, and requires the PCM
buffer, process, transfer, flush, and acknowledgment shapes. Unrelated modules
and node options fail with `NotSupportedError` and are never evaluated.

The branded `AudioWorkletNode` supports exactly one input, one output, and one
channel. Its graph node captures each 128-frame input quantum into the existing
fixed audio ring and returns silence, so the Code OSS `node -> destination`
keepalive connection does not echo the microphone. The device callback performs
no V8, allocation, lock, logging, or I/O. It coalesces one owner-worker wake.

The V8 owner drains at most 32 quanta per pump, converts stereo graph samples to
mono in reusable per-node V8 input/output arrays, and invokes the fixed Code OSS
PCM chunker. Full and flushed partial chunks
use the existing structured-clone/transferable MessagePort implementation;
`vscode-pcm-flush` produces `vscode-pcm-flushed`. Context close shuts both ports,
ends the graph capture, releases processor roots, and retires pending work.

## Authored gates

- The native graph test checks exact 128-frame capture, one coalesced/rearmed
  wake, sample preservation, and zero destination output.
- The WPT-derived media contract loads the bounded Blob module, checks brands,
  ports and channel shape, completes the flush request/ack lifecycle, rejects an
  unrelated module, and closes the context.
- Existing MessagePort gates cover structured-clone ordering, transfer,
  backpressure, teardown, and owner-task delivery used by PCM chunks.
- The upstream #776 4096-quantum/two-second gate and 100-cycle graph teardown
  gate cover the shared fixed source path. Exact package CPU/RSS/latency and
  long-duration chunk delivery remain promotion gates.

These gates were authored but not executed under the active rapid-integration
directive. Evidence remains zero. AppScene #294 and #295 still own Windows and
Linux microphone providers. Physical microphones, permission/denial/device-loss,
exact Code OSS Chat dictation/PTT, VM, package, memory and latency qualification
remain required before the media epic can close.
