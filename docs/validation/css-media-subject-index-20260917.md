# Media-transition subject indexing — 2026-09-17

## Reproduced gap and implementation

Control: `d95831cc`, after the pass-local ancestor filter. Normal cascade used
prepared subject keys, but `recascade_media_query_changes` matched every
changed media rule against every visited element. With only eight affected
elements and 1,024 unrelated elements/rules, activation performed **1,059,866**
rule/compound checks while applying only eight cascades. The new deterministic
16-transition regression fails on the control and passes after the change.

The media pass now builds a local `rule_subject_index` for the changed subset,
using original rule indices and the same prepared subject key selection as
ordinary styling. It skips ancestor-dependency text scanning, keeps universal
fallback, includes pseudo origins/root aliases/focus, and still runs complete
selector/media-scope matching and complete cascade for selected subtrees.
Both activation and deactivation are tested; rules need not be currently active
to be considered for removal. The object ends before authored callbacks.

Index construction is proportional to changed-rule features, and the document
visit is still linear in visited nodes. This removes the keyed rule-by-node
cross-product, **not all media work**. Universal selectors, dense matches,
inheritance, layout, and publication remain. No cache survives mutation and no
resize deferral or old-frame stretching is introduced.

## Work-count and correctness evidence

The native matrix independently varies affected elements (8/128), unrelated
elements (32/1,024), and changed-but-unmatched rules (32/1,024), crossing each
threshold in both directions. Candidate counts are independent of both unrelated
dimensions:

| Affected | Activation rule/compound checks | Deactivation checks | Cascades | Cascade candidates |
| ---: | ---: | ---: | ---: | ---: |
| 8 | 24 | 16 | 8 | 16 |
| 128 | 384 | 256 | 128 | 256 |

At the largest 128-element control, activation used 1,183,106 checks and
deactivation 1,182,978. Candidate style values agree with the control. These are
matching counts, not a claim of constant total work or zero allocation.

- Expanded media contract **4/4 Chrome and native**, including 15 selector
  forms, activation/deactivation, root variable inheritance, generated boxes,
  positional/sibling ordering and subsequent DOM mutation.
- Native contract slice **86/86 in ten documents**; Chrome adjacent slice
  **62/62 in seven documents**, plus the four media checks above.
- Cumulative 140-route invalidation suite, existing text/child-vector/subject
  fixtures, and six adjacent native groups pass.
- Shared CSS service and parsed/cache/precompiled stylesheet gates pass.
- Production runtime/SDK build passes. Installed-consumer Kestrel parity is
  376/376, 298/298 and 311/311 nodes with zero DOM/style/layout differences.

The first native fixture used inline `b` elements and hit width clipping beyond
the viewport; it was corrected to explicit block boxes before the final control
run. Initial WPT attempts omitted required native viewport-target IDs and are
excluded. Chrome's focus diagnostic showed activeElement=true but
matches(':focus')=false and document.hasFocus()=false: the headless target was
inactive. The harness now activates the page with
[Page.bringToFront](https://chromedevtools.github.io/devtools-protocol/tot/Page/#method-bringToFront)
before navigation. The focus test remains in the passing contract; it was not
removed or emulated as a CSS result. Adjacent Chrome contracts pass after this
harness change.

## Deterministic production timing

[Standalone installed-SDK benchmark](../../experiments/WebScene.NativeEngine.Probe/benchmarks/media_candidates/README.md)
generates identical DOM/CSS, has no network/authored timers, checks every target
and noise box, and emits geometry checksums. Each process warms four crossings
then measures 32. Serial A/B/B/A completes after our builds/tests; no compiler
processes were observed in pre/post host snapshots (not continuous monitoring).
All viewport-specific checksums match across all 384 measurements.

| Workload | A1 median dispatch ms | B1 | B2 | A2 |
| --- | ---: | ---: | ---: | ---: |
| 8 affected, 1,024 unrelated nodes/rules | 17.844 | 0.545 | 0.508 | 18.043 |
| 128 affected, 1,024 unrelated nodes/rules | 20.247 | 0.676 | 0.675 | 20.398 |
| 128 affected, 1,024 matching rules, no noise nodes | 22.366 | 22.931 | 22.110 | 22.318 |

Accepted for this bounded, product-neutral workload: sparse matching improves
substantially; the dense case stays in the observed 22–23 ms range. Public-ABI
barrier medians also improve from 19–22 ms to 2.0–2.3 ms in sparse cases. This is
**not** Spotify, native-window presentation, or a Chrome-speed result. Direct
allocation/RSS gates and physical-frame acceptance remain open.

[Raw control/candidate counts, timings, checksums and hashes](evidence/css-media-subject-index-20260917.json).
Candidate development SDK: `/Volumes/SSD/sdks/spotify-media-index-dev-20260917`.
Its engine archive SHA-256 is
`ed8f803cdebf79df189cc93be1a118532ede8cd4a51be56339ea9a76f8c08cbd`.
This overlay is not a manifest-integrity-qualified package. The existing Dawn
dependency targets macOS 26.5; no macOS 26.0 execution claim is made.

CI was not monitored. #257 remains separately owned. Broader CSS plan and
physical native/Chrome resize acceptance are not complete.
