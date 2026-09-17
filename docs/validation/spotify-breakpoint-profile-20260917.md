# Spotify breakpoint recascade — 2026-09-17

The user's 12.28-second recording shows smooth ordinary resizing and a catch-up
pause after shrinking substantially and growing again. It is qualitative
evidence, not a frame-cadence measurement or a Chrome-speed comparison.

## Reproduction and attribution

Control installed engine: WebScene `6da8c56c`, AppScene `3b00e895`, LLVM 22.1.8.
A local diagnostic consumer loads the unchanged Spotify site with the sample's
resource loader/text measurement and drives the public engine resize/frame ABI.
It warms for 1,800 frame opportunities, then changes widths between 1,440 and
400 CSS pixels. This isolates engine work: no native window or GPU presentation
is measured. Initial DOM: 2,724 elements, 15 images.

The first control run dispatches 800→700 in 313 ms and 700→800 in 344 ms;
nearby non-crossing changes are around 25–44 ms. Retained frame-pipeline spans
for the final 1,400→600→1,400 transitions spend about 315 ms each inside
`recascade_media_query_changes`. Native sampling attributes 714 samples to its
subtree style-application path versus 19 to finding affected roots. DOM size
stays at 2,723–2,724, so this is not explained by a wholesale responsive DOM
replacement. It is not a claim that JavaScript/layout have zero cost.

The first attempted native window drag did not change its bounds and its sample
is excluded. A first diagnostic run used unsupported `document.images` and is
also excluded. Production listener/layout phase fields are unavailable; zeros
in the local diagnostic output must not be read as zero work. The bounded trace
ring retains only the end of longer runs.

## Bounded correction

Media recascade lacked the pass-local matching context already used for DOM
mutation recascades. Reuse sibling positions and selector-prefix answers while
finding and recascading affected roots. Drop the context before returning to
`matchMedia` listeners; do not persist it across viewport/DOM changes or defer,
stretch, or throttle any resize.

The native eight-sibling control fails with 288 positional visits. The candidate
uses eight visits; the 128-sibling case uses 128. All four alternating transitions
pass at each size. Styles, inheritance, sibling selectors and positional rules
are checked independently of work counters. The larger fixture initially used
inline boxes; that invalid width oracle was corrected to explicit block boxes.

The expanded WPT-style media contract tests repeated activation/deactivation
with 128 siblings and reordering between transitions. It passes 2/2 Chrome and
native checks; the adjacent live media-list contract passes 1/1. Chrome's first
attempt lacked a viewport testdriver adapter and is excluded; the new adapter
changes the actual Chromium viewport rather than synthesizing media answers.
Native reentrant media-list, attribute invalidation, stylesheet CSSOM and
dimension-variable groups also pass.

## Remaining boundary

The exploratory candidate has mixed timings, including a slower reverse
crossing. Its exact work reduction is verified, but the full breakpoint stall
is not fixed and no timing speedup is accepted from that single pair.

A subsequent serial A/B/B/A run (no concurrent builds or native sampling) uses
two processes per variant and twelve alternating 800↔700 crossings per process,
after one settling resize. All 48 measured transitions retain 2,723 elements and
15 images. Control median dispatch is **302.961 ms**, candidate **235.400 ms**
(22.3% lower); means are 315.167 and 234.250 ms. Narrowing medians are
306.193→234.603 ms; widening 300.582→235.875 ms. These are production-engine
results with certification disabled. The site and background machine activity
are not controlled, so this supports a local engine-work improvement, not an
end-to-end frame budget or Chrome-speed claim. Complete per-transition evidence:
[A/B/B/A report](evidence/spotify-breakpoint-abba-20260917.json).

The remaining dominant path still reapplies the full cascade over affected
subtrees. Broader cascade/cache work stays coordinated through #235/#242.

Chromium similarly distinguishes media-affecting changes, updates affected
active rule sets and invalidates dependent styles; that is architectural
reference, not evidence that our implementation matches its performance:
[Blink StyleEngine](https://chromium.googlesource.com/chromium/src/+/25a5bc7242a972e00204085eca1bc300b9861102/third_party/blink/renderer/core/css/style_engine.cc).

Local evidence: `/tmp/spotify-breakpoint-{baseline-r2,candidate}-20260917.log`,
`/tmp/spotify-breakpoint-engine-r2-sample-20260917.txt`,
`/tmp/css-media-{control,candidate-r3,adjacent-native}-20260917.log`, and
`/tmp/css-media-matching-{native-r2,chrome-final}-20260917/results.json`.
Candidate development SDK: `/Volumes/SSD/sdks/spotify-media-candidate-dev-20260917`.
It overlays the engine only and is not an integrity-qualified release artifact.
The earlier qualified SDK and manual demo are left unchanged.
