# CharacterData editing checkpoints — 2026-09-17

## Scope and implementation

This is the remaining CharacterData method slice of #237/#245, not a new
implementation of the other CSS agents' workstreams. The control is `47e84144`.
Its text setters worked, but `length`, `substringData`, `appendData`, `insertData`,
`deleteData`, and `replaceData` were not exposed. The initial 20-check contract
passed Chrome and failed **0/20** on that control.

The new methods and the existing data/nodeValue/textContent setters share
`commit_character_data`. Only Text empty/nonempty transitions schedule structural
selector invalidation. Stable nonempty edits still dirty layout and update
textarea defaults, but do not recascade selectors. Connected stylesheet text
always runs stylesheet activation, even when it remains nonempty. Comments and
processing instructions remain irrelevant to `:empty` and stylesheet text.

Following the [DOM CharacterData algorithms](https://dom.spec.whatwg.org/#interface-characterdata)
and [Web IDL unsigned-long conversion](https://webidl.spec.whatwg.org/#es-unsigned-long),
offset/count conversions run left-to-right, followed by DOMString conversion,
before reading the current data or parent. Conversion can reparent or modify the
node, and exceptions retain those side effects. Offsets count UTF-16 code units,
including lone surrogates; native storage remains WTF-8. Counts are clamped using
subtraction, avoiding unsigned addition overflow. Method arguments stringify null
as `"null"`, unlike the legacy-null-to-empty `data` setter.

The generated binding catalog exposes the methods on CharacterData with receiver
signatures, correct arity, and non-constructor functions. The legacy lane has the
same callbacks and retains form/select length handling. The generator validates
24 interfaces and 191 members against its pinned IDL corpus.

## Verification

- Final required contract: **23/23 Chrome and native**, including a seeded
  256-edit string-slice oracle for each of Text, Comment and ProcessingInstruction.
- Existing setter/checkpoint contract: **47/47 Chrome and native**. Combined:
  **70/70**. The extra three seeded-oracle checks were added after the initial
  20-check control run; no claim of a 23-check control run.
- Adjacent native contracts: variadic 79, bulk sibling 76, child-list 73,
  media primitives 8, targeted media 4, custom-element lifecycle 9, shadow DOM 10;
  all pass. One pseudo-layout reftest also passes. Together with character data:
  **329 subtests across ten documents**, including that one reftest.
- Stable-text matrix varies 8/128 targets independently of 32/1,024 unrelated
  nodes/rules. Four setter cases and four method cases preserve identity/styles.
  Method cases perform 6,400/102,400 mutations respectively, plus substring/length
  reads. All eight CSS work-counter deltas are **zero** in every case.
- The cumulative route matrix adds append/insert/delete/replaceData transitions
  to the existing setter routes, checking empty/nonempty changes in both
  directions and ordinary/structural unrelated-rule growth. All **172 cases**
  pass; the 32 new cases have identical eight-counter results at both unrelated
  sizes for each fixed affected size/rule shape.
- Five adjacent native groups pass: custom-element checkpoints, stylesheet CSSOM,
  textarea value lifecycle, DOM traversal/cloning, and existing detached-DOM GC.
- Generated bindings check and certification/production builds pass. A separate
  legacy-installation syntax check fails at generated-only `attr_template` and
  `node_template` references in unchanged DOM-core/compiled-template files.
  It did not report errors in the new legacy registration, but this is **not**
  a passing legacy build or runtime qualification. The supported/default generated
  lane is the one used by the passing builds and tests above.

See [machine-readable evidence](evidence/css-character-data-methods-20260917.json)
for paths, counts, hashes, and final route results. Some initial substring-based
contract selections matched no files; those zero-document runs are excluded.
No wall-clock comparison is accepted: compilation and other checks overlapped.
The counters measure CSS work, not allocations, total frame cost or presentation.

## Boundaries and remaining work

This does not implement `splitText`, `normalize`, complete live Range adjustment,
MutationObserver delivery, or processing-instruction pseudo-attribute semantics.
It qualifies the listed text/CSS checkpoints, not complete DOM conformance. Text
conversion/copying and layout costs remain; this is not a zero-allocation or
constant-time text-editing claim. The existing detached-DOM regression passing
does **not** resolve the distinct #258 retained-subtree failure.

No source changes to #257 live form state or other agents' CSSOM/value/layout
implementations. The open native Spotify app and its qualified SDK remain at
`09ccb478`/`47e84144` evidence; no new SDK package or manual app was installed for
this API stage. Physical threshold-resize acceptance, cascade/layout dependencies,
detached-node lifecycle reconciliation, remaining APIs and broader product gates
keep the general CSS plan open. CI was not monitored.
