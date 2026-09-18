# Nested `:has()` selector-list validation — 2026-09-18

## Scope and provenance

- WebScene selection base: `dd39f118b8eba301f1884740e8b25573f49d687e`;
  final integration base after #418: `eb3cfe38f13dfe1243404ed87af39bf77eaf902a`.
- Unchanged Code OSS: `645f29cc3176500b4b5762ba887cf2a7f0ffdf2c`.
- Browser oracle: Google Chrome `153.0.8010.50` on macOS arm64.
- Standards authority: Selectors Level 4 relational pseudo-class and forgiving
  relative selector-list semantics.

Unchanged Code OSS uses the exact nested shape
`.monaco-list-row:has(.monaco-icon-label:is(.monaco-decoration-itemColor,
.monaco-decoration-badge))`. WebScene's matcher split `:has()` arguments with a raw
comma search even though its parser, specificity, and compiled dependency metadata
already preserve nested functions and quoted attribute values. The focused change
reuses the existing allocation-free depth-aware selector-list traversal in the matcher.
It does not alter the structural routes merged through PR #245 or the nested
`:is()`/`:where()`/`:not()` work merged through PR #356.

## Browser/native oracle

The dedicated product-neutral contract covers the Code OSS selector shape, a quoted
attribute comma, multiple top-level arms, maximum nested specificity, and class and
attribute mutation restoration.

- Chrome: 6/6 assertions pass in 41 ms.
- Native before: the library built from exact PR #356 head
  `8d1d715e450db8a00967f17baf17dacdf5c87694` passes 1/6 and fails the two nested
  comma matches, dependent specificity application, and both restoration checks.
- Native candidate: 6/6 assertions pass in 88 ms.
- Adjacent native contracts pass 5/5 nested-functional, 10/10 routed nested-selector,
  19/19 attribute-invalidation, and 73/73 child-list assertions.
- The Servo selector/parser and dependency-plan executable passes.

Evidence SHA-256:

- before native result: `aafa62ac760de20c93115b79bd4c1771bac18012d024e45d4bc993e4746b0d73`;
- candidate native result: `6410dc97c25e1d895c86ad975a58e76a617e63d640eecf5cefc1c38af5619068`;
- Chrome result: `0e8ced16c247ee370a72f4c3ca172c5006b8ddd5bb45d318b2e4ac43870ae0f4`;
- candidate native library: `38e85d5abcb438377f5ade73f29bc4ffcb8fa1f63ff26714cf2aa13f2e296625`.

## Bounded invalidation, performance, memory, and lifecycle

The certification gate keeps 128 affected descendants constant while growing unrelated
rules and nodes from 32 to 1,024. It performs 1,000 paired host-attribute and nested-marker
class transitions, reading every affected computed style after each transition.

Both sizes produce the same counters: 6,000 plan lookups, 518,000 candidate visits,
zero fallback visits, 37,664,000 compound checks, 1,024,000 rule checks, 258,000 cascade
applications, and 1,024,000 candidate checks. The high compound count is explicitly
bounded by the affected subtree because relational matching searches that subtree; adding
992 unrelated rules and nodes does not change any counter.

The 32/1,024 unrelated cases complete in 1,539.88/1,777.78 ms, below the two-second bound.
V8 heap returns byte-exactly to 941,360/1,546,100 bytes after detach and low-memory
notification. Native node counts do not increase, peak RSS stays below the 64 MiB growth
bound, and the fixture removes its style, root, and global mutation closure before engine
destruction.

## Remaining selector boundary

This slice only fixes commas nested in `:has()` functional arguments and quoted attribute
values. Relative sibling `:has(+ ...)`/`:has(~ ...)`, the remaining selector/state matrix,
shadow-specific selectors, and broader product acceptance remain open under #237/#235.
