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

## Follow-up: classify immutable selectors once

A new sample of the production `c7859bec` candidate still identifies selector
matching as a major cost. Pseudo-suffix splitting appears as 232 top-of-stack
samples (193 in its suffix helper, 39 in the splitter itself). This is attribution,
not a timing comparison: the sampler substantially increases dispatch duration.

The shared candidate matcher classified each rule as ordinary/pseudo and tested
the exact `:host` spelling for each candidate on each element. That interpretation
does not change over the lifetime of an interned rule payload. Store the pseudo
kind and exact-host flag at preparation; match pseudo origins with the payload's
already-compiled selector instead of looking their strings up again. CSSOM
replacement still prepares a different payload. No winning declarations or
computed styles are retained across mutations, and no resize is deferred.

The V8-free CSS service checks every supported pseudo suffix, legacy single-colon
before/after, an ordinary selector containing pseudo-looking attribute text, exact
host classification, and payload reuse. A counted compiler verifies 128 repeated
intern requests compile only the full selector and pseudo origin once each.
Chrome and native pass the expanded CSSOM barrier (5 checks) and shadow DOM
(10 checks) contracts; generated-box geometry verifies before→after replacement
and restoration of the original host rule. Native media contracts pass 3/3;
targeted media scaling, reentrant listeners, attribute invalidation, stylesheet
CSSOM, dimensions and shadow geometry groups also pass.
The cumulative certification scaling gate also passes (140 route cases, four
stable-text cases, 36 vector cases and the original batched fixture). Adjacent
attribute/nested-selector WPT contracts pass 29/29. These are local results;
CI was not monitored and the full release SDK gate has not been repeated here.

The first regression attempt used `getComputedStyle(element, pseudo)`. WebScene
currently ignores the second argument, so that assertion failed independently
of this optimization. The geometry oracle replaces the unsupported API oracle;
it does not claim to implement pseudo-element computed-style reflection. This
separate gap is recorded in #238. An initial shadow test run selected only
required tests and ran zero documents; the accepted run uses `--selection all`.

Local tests: `/tmp/css-rule-classification-{cssom-native-r2,shadow-native-r2,chrome-r2}-20260917/results.json`.
Attribution: `/tmp/spotify-breakpoint-current-sample-20260917.txt`.
Development SDK: `/Volumes/SSD/sdks/spotify-rule-classification-dev-20260917`;
this remains an unqualified development overlay, not a new release SDK.

A second serial A/B/B/A comparison uses the previous `c7859bec` optimization as
control, not the original baseline. Each variant has 24 measured crossings after
settling, with all 48 retaining 2,723 elements and 15 images. Median dispatch
falls **226.868→195.708 ms (13.7%)**; means **226.796→192.122 ms**, p95
**233.976→205.888 ms**. Narrowing medians are 226.483→195.107 ms; widening
226.903→195.708 ms. Both candidate runs improve on both control runs. No
compilation or sampling ran concurrently. This still leaves a perceptible pause;
it does not establish a presentation frame budget or a Chrome-speed advantage.
The earlier 303→235 ms result is a separate experiment, not a simultaneous
three-way comparison. Archive/source hashes and all per-transition values are in
[classification A/B/B/A evidence](evidence/spotify-classification-abba-20260917.json).
