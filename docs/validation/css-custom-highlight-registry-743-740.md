# CSS Custom Highlight object model for Code OSS

Issues: WebScene #743, parent #740; depends on #741 / PR #742

Base stack layer: `f483fcd12208a0af4bae168ace5554252c672d2f`

Unchanged Code OSS: `645f29cc3176500b4b5762ba887cf2a7f0ffdf2c`

## Product behavior

Code OSS merges chat-find owners into persistent `Highlight` instances by
calling `clear()`, `add()`, assigning `priority`, and then `CSS.highlights.set()`.
Disposal calls registry `delete()`. Markdown creates a new Highlight from a
range list and replaces one of two named registry entries.

This stack layer adds that complete ordered setlike/maplike object model. A
Highlight retains its Range wrappers strongly, suppresses duplicate ranges,
and supports add/delete/clear/has/size, keys/values/entries, default iteration,
forEach, and signed priority. Each document or nested context owns an isolated
registry with set/get/has/delete/clear/size and insertion-ordered iteration.
Replacement preserves position; deletion followed by insertion appends.

Native bindings are capped at 1,024 live Highlight objects, 4,096 ranges per
Highlight, and 256 names per document root. Names are capped at 128 bytes.
Unregistered Highlight wrappers are weak. Registry entries retain their
Highlight wrapper strongly; Highlight objects retain their Range wrappers until
removed. Navigation clears registries before Highlights and Ranges so no native
pointer can outlive its owner. Same-document/root checks prevent cross-context
registration. Registered mutations dirty one retained scene generation;
unregistered object mutation schedules no work and no API creates DOM or
visual-tree nodes.

## Explicit remainder

The next stack layer implements functional `::highlight()` parsing, color and
background cascade, range/text-fragment intersection, overlap priority, and
retained paint. This layer deliberately publishes no highlight pixels by
itself. Complete arbitrary Range mutation adjustment and non-Code highlight
properties remain outside #740.
