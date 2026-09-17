# Compiled subject candidate indexing — 2026-09-17

## Cause and structural correction

A production sample of `51fef1f2` during Spotify media crossings still attributes
substantial work to selector matching and candidate sorting. Pseudo-suffix
classification no longer appears among the reported top-of-stack samples.
The sample is attribution only, not an accepted timing measurement.

The shared stylesheet index reparsed serialized selector text and stopped the
subject at its first pseudo-class. Consequently `:not([data-disabled]).unused`
became an unindexed rule, although the compiled outer compound already knows
that `.unused` is mandatory. Escaped punctuation was also treated as a delimiter:
the old engine fails the browser contract for `.escaped\:token`.

Both V8 and V8-free stylesheet owners now pass the already-compiled selector
(or pseudo origin) to the shared indexer. It chooses mandatory outer subject
ID/class/tag/attribute keys, including features after pseudos and decoded escapes.
It never chooses a feature from an ancestor/sibling compound or functional arm.
`:is(.a,.b)`, `:not(.x)` and `:has(.child)` without an outer mandatory key keep
the universal fallback. Full matching, specificity and source ordering are
unchanged. HTML folded names and XML exact names share conservative candidate
buckets; matching still decides case sensitivity.

The existing legacy descendant-attribute dependency scan is unchanged. This
does not implement general disjunctive indexing or extend selector grammar.

This follows the architecture documented in [Blink style calculation](https://chromium.googlesource.com/chromium/src/+/HEAD/third_party/blink/renderer/core/css/style-calculation.md).
The inspected [Blink RuleSet source](https://chromium.googlesource.com/chromium/src/third_party/+/83c0354004cb4a65b4246b8c9b0df5dd8a3604d5/blink/renderer/core/css/rule_set.cc)
extracts features from the rightmost compound to select a bucket before full
matching. Blink also has more specialized paths; this is not a parity claim.

## Work and correctness evidence

The shared CSS service grows unrelated pseudo-prefixed rules from 32 to 1,024:

| Targets | Old candidate visits (32 / 1,024 rules) | Compiled index (32 / 1,024 rules) |
| --- | --- | --- |
| 8 | 264 / 8,200 | 8 / 8 |
| 128 | 4,224 / 131,200 | 128 / 128 |

The new test fails against the control before implementation (exit 170). It
counts collected candidates, not time. The shared service also checks index
keys and indexed/full-scan matching parity for HTML/XML case handling.

The native media gate checks two activation/deactivation transitions at each
target/rule size and independently verifies inherited widths. Actual cascade
candidate checks are **10** at both unrelated sizes for eight targets, **130**
for 128. The extra two are provider rules on the recascade root. It is part of
both the full suite and `css-invalidation-scaling`; its focused filter is
`css-subject-index-scaling`.

The expanded `css-class-index-cascade-mutation.html` passes 4/4 Chrome/native
checks and is promoted to required. It covers 18 selector shapes, escaped names,
functional alternatives, class mutation and stylesheet replacement/removal.
The old engine fails at the escaped-class assertion. Adjacent attribute/nested,
CSSOM, shadow and media contracts bring the focused native run to **59/59**.
Focused native media, attribute, CSSOM, dimension/shadow geometry groups pass.
The cumulative certification gate passes eight new subject-index transitions,
140 route cases, four stable-text cases, 36 child-vector cases and the original
two-size batched invalidation fixture. Timing printed by this suite is not used
as performance acceptance.

Rejected attempts: the first browser fixture gave its default class rule higher
specificity than a tested type/`:where` selector; the default now uses zero
specificity. Initial native runs selected required tests while this contract was
still optional and ran zero documents; these are not passing evidence. The first
SDK compile exposed a missing direct declarations-header include; the corrected
full SDK build passes without relying on transitive includes.

Local evidence: `/tmp/css-subject-index-control-20260917.log`,
`/tmp/css-subject-index-candidate-20260917.log`,
`/tmp/css-subject-index-runtime-scaling-20260917.log`,
`/tmp/css-subject-index-chrome-r2-20260917/results.json`,
`/tmp/css-subject-index-native-control-r3-20260917/results.json`, and
`/tmp/css-subject-index-*-final-20260917/results.json`.

## Remaining acceptance boundary

The work-count improvement is verified; real-site timing is separate. The
development SDK is `/Volumes/SSD/sdks/spotify-subject-index-dev-20260917`, an
unqualified overlay. The open manual demo and the last integrity-qualified SDK
remain unchanged. No CI result or native presentation/Chrome-speed claim is
made by these engine tests.

A serial production A/B/B/A against `51fef1f2` has 24 measured transitions per
variant and identical 2,723-element/15-image states. Control/candidate medians
are **194.801 / 195.292 ms**, means **193.622 / 193.615 ms**. This establishes
**no Spotify timing speedup**; the deterministic candidate-work and escaped-name
correctness improvements are the accepted results. Both variants precede the
next main integration. Complete values/hashes:
[Spotify index A/B/B/A](evidence/spotify-subject-index-abba-20260917.json).

## Main integration

Implementation `9cf764f5` is followed by merge `4e9cfd0f`, integrating main
`ee6efa9f` without conflicts. This retains the other agents' #254 containment /
container-query and #255 accessibility-preference implementations, plus navigation
and evidence-ledger changes. The shared matcher retains both container-condition
evaluation and our immutable pseudo/host classification and compiled origins.

On this merged head, all 79 checks in ten focused native contract documents pass,
including both new ten-check profiles. Six focused native media/attribute/CSSOM/
dimension groups and the shared CSS service pass. The dedicated containment gate
passes 4,096 descendants × 100 cycles (1,244.02 ms; 4,101 peak additional nodes,
zero retained nodes); the preference gate passes 4,096 controls × 100 cycles /
200 states (6,892.5 ms, within its retained-node/heap bounds). These gates ran
serially after compilation. Portable CSS tests pass 364/364 on each of net8.0
and net10.0; the initial no-restore attempt lacked this worktree's assets file
and is excluded. The full production SDK build succeeds. The merged certification
suite also passes all eight subject-index transitions, 140 route cases, four
stable-text cases, 36 vector cases and the two-size batched fixture. See the
[integration summary](evidence/css-subject-index-integration-20260917.json).

These are merged-source gates, not a freshly packaged/source-denied SDK or a
manual Spotify presentation qualification. The recorded Spotify A/B/B/A belongs
to the pre-merge change and must not be attributed to the merged engine.
