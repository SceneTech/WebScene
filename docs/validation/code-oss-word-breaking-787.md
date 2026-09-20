# Code OSS retained word breaking (#787)

Date: 2026-09-20

## Source baseline

- WebScene parent: `dd03421e180546c39aa4c272396b36af628d38c5`
- unchanged Code OSS: `645f29cc3176500b4b5762ba887cf2a7f0ffdf2c`
- tracking: WebScene #787, CSS epic #235

## Reduced defect

The retained inline formatter and fallback text painter wrapped only at whitespace. A token wider than its used content box stayed on one line even when author CSS requested `word-break: break-all`, `word-break: break-word`, `overflow-wrap: anywhere`, `overflow-wrap: break-word`, or the legacy `word-wrap` alias.

The unchanged Code OSS inventory reaches this behavior in Markdown preview, dialogs, notifications, hover and parameter-hint content, quick input, suggestions, REPL/debug surfaces, editor placeholders, comments, and chat/session UI.

## Implementation

- Generated CSS property metadata now gives `word-break` and `overflow-wrap` stable native/managed identities. `word-wrap`, `wordWrap`, and compact CSSOM spellings resolve to `overflow-wrap`.
- Both properties retain authored cascade values in the existing cold textual-style allocation and inherit through empty descendant sentinels. No hot `dom_node` field, timer, animation-frame participant, platform widget, document scan, or application CSS rewrite was added.
- Inline assignment/removal, stylesheet application, custom-property resolution, global keywords, `all: unset`, CSSOM computed values, and managed mutation invalidation include the new properties.
- Both retained inline fragments and fallback text lines use one UTF-8-boundary segmenter. It measures each code point once for the initial fit estimate, verifies the final shaped segment, never splits a multibyte sequence, and preserves source byte ranges used by selection/highlight paint.
- `break-all` may use any code-point boundary. `break-word`/`anywhere` use emergency breaks for over-wide tokens. `normal`, `initial`, `revert`, and the currently bounded Latin `keep-all` path preserve whitespace wrapping.

## Authored gates

- `tests/WebPlatformSubset/contracts/css-word-breaking.html`: browser-referenced geometry, inheritance, CSSOM aliases, mutation/removal, and supplementary-plane text.
- `test_word_break_and_overflow_wrap_layout`: native retained-layout regression for the same behavior.
- `test_word_break_layout_performance_gate`: 4,096 nodes and ten forced style/layout transitions, with at most one layout pass per transition and a five-second wall-time ceiling.
- The WPT-subset profile records the contract as candidate until direct Chromium/native evidence exists.

## Evidence status

No build, test, WPT run, benchmark, visual comparison, memory run, packaged-product run, or CI result was executed for this implementation. Evidence is zero. The gates above are source-authored and must be run during consolidated qualification.

## Remaining qualification

1. Run the focused native correctness and performance filters.
2. Run the candidate contract in the pinned Chromium and native WPT lanes.
3. Compare Markdown preview, dialog, notification, hover, parameter-hint, quick-input, suggestion, REPL, and editor-placeholder pixels and geometry with unchanged Chromium Code OSS.
4. Record long-token, mixed-script, resize, mutation, lifecycle, CPU, allocation, scene-publication, and package evidence.
5. Keep complete Unicode line-breaking, grapheme-cluster breaking, hyphenation, and international `keep-all` behavior outside the claim until separately implemented and qualified.
