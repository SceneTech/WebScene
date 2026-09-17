# Prepared stylesheet classification integration — 2026-09-17

Fix: `86c83845`, following merged WebScene `5c727756` / AppScene `68a9dbd`.
This closes a precompiled-stylesheet regression from the immutable rule
classification optimization; it is not another resize speedup.

## Failure and correction

The normal SDK producer and relocated read-only consumer build gates passed at
`5c727756`, but the additional installed Kestrel parsed/compiled geometry gate
failed: 41 differences at dark/Home 1280×800 and light/Drafting 1440×900; zero at
dark/Text 980×620, where the inspector is hidden. Node counts still matched
376, 311 and 298 respectively. The first visible difference was a generated
inspector-section heading's height, shifting subsequent rows.

Runtime parsing initializes the immutable payload's `pseudo_kind` and
`host_selector`. `webscene-uic::emit_prepared_sheet` emitted the compiled selector
and pseudo origin but omitted both classification fields. Compiled rules
therefore defaulted to ordinary/non-host classification. The fix serializes both
fields without repeating classification per match or changing cascade order.

The new emission regression fails against the old installed compiler. The fixed
compiler suite passes, as do shared-stylesheet comparisons between freshly
parsed, persistent-cache replayed, and generated C++ payloads. Those comparisons
now assert both classification fields and exercise a generated text box, as well
as the existing scene-equivalence checks. The fixture includes `:host` and
`::backdrop`. The emission test covers each parser-supported pseudo kind and a
pseudo-looking attribute value; scrollbar-corner remains rejected by the parser
and is not claimed as newly supported.

The expanded required `cssom-style-recalculation-barrier.html` passes **6/6 in
Chrome and native WebScene**, including generated text-box replacement. The
compiler-only correction does not change the runtime library.

## SDK provenance and gates

Corrected clean producer inputs:

- WebScene `86c83845ab40de1214abb3af5952c90a41d5a453`.
- AppScene `68a9dbd15e6b02e4bec0a76d5190713868b6517a`, retaining the raster renderer
  and codec-enabled Skia lock; no dependency checks were weakened.
- Pinned LLVM 22.1.8 and full Xcode MacOSX26.5.sdk on the SSD.
- Installed SDK: `/Volumes/SSD/sdks/spotify-css-compiled-qualified-20260917`.
- Archive SHA-256: `6bf47256e72b31d7946d05e33f7433bd514576cf3f0459045661507d0bc603bc`.
- Engine archive SHA-256: `72bd32198a9595f23b2f9876f7b6238725a0df68d0b6768582a01a28c315b2e3`,
  unchanged from the merged `5c727756` package.

The normal producer passes all 16 AppScene tests, 2,868-file manifest integrity
and the macOS compiler/runtime profile. Source-denied, relocated, read-only
consumer qualification passes all eight checks. Installed Kestrel smoke and
parsed/compiled parity also pass under the same sandbox: dark/Home 376/376,
dark/Text 298/298, light/Drafting 311/311 nodes, **zero differences** in each.
This reverses the controlled 41/0/41 failure without changing runtime matching.
Reports are in `/Volumes/SSD/builds/spotify-css-compiled-qualification-20260917/`
and the [retained integration evidence](evidence/css-compiled-payload-integration-20260917.json).
This does not qualify the optional GPU presentation suite or macOS 26.0 runtime:
the existing Dawn binary targets macOS 26.5.

The newly opened manual app is
`/Volumes/SSD/builds/spotify-compiled-qualified-demo-20260917/spotify_catalog.app`.
Native-window inspection confirms populated catalog text, album artwork and
circular artist images. The header remains clipped. This is not a measurement
of physical resize cadence. Previous manual probe windows were closed; the real
Spotify app and the user's browser were not used as native-rendering evidence.

## Current breakpoint attribution, not timing acceptance

The existing public-ABI diagnostic consumer was rebuilt against the exact merged
SDK. Its source SHA-256 remains
`17c7a48812807f541728dc083a018d3bbc1360a3aaddd0f4996ac169354aaab7`.
After initial settling it completed twelve alternating 700/800 CSS-pixel
transitions with **2,726 elements and 18 images** each. This differs from the
earlier ABBA workload (2,723 / 15), and unrelated host compilation was active.
Therefore its 224–245 ms dispatch observations are **not** accepted as either a
regression or an improvement over earlier measurements. Sampling also perturbs
timing; this run was for attribution only.

Top leaf symbols include compound matching (268 samples), relation-cache lookup
(122), candidate-sort partitioning (107) and introsort (56), plus allocation and
copying. These are sample counts, not CPU percentages; sleeping threads and
startup/settling are present in the capture. Candidate matching and sorting
remain the next controlled reproduction targets. No additional optimization was
shipped from this profile, and no Chrome or physical-frame advantage is claimed.

Local evidence:

- `/tmp/spotify-merged-breakpoint-profile-20260917.log`
- `/tmp/spotify-merged-breakpoint-sample-20260917.txt`
- `/tmp/css-compiled-classification-control-20260917.log`
- `/tmp/css-compiled-payload-regressions-20260917.log`
- `/tmp/css-compiled-pseudo-box-{chrome,native}-20260917/results.json`
- `/Volumes/SSD/builds/spotify-css-merged-qualification-20260917/kestrel-parity.json`

## Rejected attempts and separate gap

The producer first rejected abbreviated commit IDs, then an inherited SDKROOT
pointing to Command Line Tools. The successful build uses full commit hashes and
an explicitly scoped Xcode SDKROOT, not global host changes. A shared-CSS test
build likewise required the scoped SDKROOT for Rust linking.

The first parity invocation copied read-only SDK inputs and then failed trying
to modify its scratch copy. Use the writable `consumer-sources/Kestrel/tests`
copy under the **same** source-denial sandbox, preserving SDK read-only checks.
The ensuing 41/0/41 geometry mismatch is a separate genuine engine/compiler
integration failure, not dismissed as a permissions problem.

An attempted font-based browser oracle separately found that an empty flex
origin with `font-size:10px;line-height:1` and
`:before {content:'x';font-size:40px;line-height:1}` does not reach the expected
40px height in native WebScene, while Chrome passes. The runtime-only contract
fails, so this is not the compiler-emission omission. The diagnostic is retained
at `/tmp/css-compiled-pseudo-{chrome,native}-20260917/results.json` and coordinated
in [#238](https://github.com/SceneTech/WebScene/issues/238#issuecomment-5715558291).
The required compiler regression uses explicit box dimensions; no font-metric
workaround was added and this separate issue remains open.
