# Code OSS retained scrollbar part state (#785)

## Current-main boundary

WebScene already projects authored base `::-webkit-scrollbar` width/height and visibility, thumb/track color and radius, content-box transparent borders, proportional overlay geometry, scrolling, and primary-button thumb dragging. This slice keeps that path and fills the remaining declarations reached by unchanged Code OSS 1.137.

The pinned checkout contains 39 `::-webkit-scrollbar-thumb`, 9 track, and 9 corner occurrences across source and generated CSS. Automation dialogs, Issue Reporter, Image Carousel, and Prompt Timeline use thumb `:hover`/`:active`, `min-height`, transparent borders, themed colors, and hidden corners.

## Retained implementation

`::-webkit-scrollbar-thumb:hover` and `:active` receive distinct parse kinds. Matching admits each only while the native thumb owns that state, then folds it into pseudo kind 4 before the existing cascade. Base and state declarations therefore share source order, specificity, layers, rollback, variables, and `!important` handling.

Two interaction bits occupy the existing `dom_node` flag byte. A single runtime pointer identifies the hovered thumb; the existing drag pointer remains the active owner. Pointer transitions recascade only the previous/new scrollers. No pseudo DOM node, document scan, timer, animation frame, or platform widget is introduced.

The lazy textual scrollbar record adds one authored vertical minimum thumb extent, matching the `min-height` declaration used by Code OSS. Scene paint and thumb hit testing consume the same value. Existing transparent-border/content-box inset and absent-corner behavior remain unchanged; the authored Code OSS `display:none` corner rules therefore continue to produce no corner command.

## Authored gates

- Portable compiler protection/restoration retains thumb `:hover` and `:active` selectors.
- Native selector payload contracts retain kinds 11/12 and the common host origin.
- The focused retained-scene contract checks normal, hover, active, 28 px minimum extent, transparent border inset, hidden-corner absence, drag geometry, and scroll event count.
- The performance contract requires pointer-state changes to preserve layout-pass count, publish bounded scene work, avoid sustained frame demand, and keep unrelated scroller cascade work at zero.
- Final product acceptance compares Prompt Timeline, Automation dialog, Issue Reporter, and Image Carousel regions in light/dark Chromium and AppScene captures, then exercises 100 create/scroll/style/remove cycles and a 4,096-scroller mutation fixture with heap/RSS and retained-count return.

These gates are authored but unexecuted under the active implementation-throughput direction. No validation result is claimed by this document.
