# Effective-property cascade validation (2026-09-17)

This stage of issue #235 moves rollback and overlapping inline declarations from
authored property names to the effective longhands they control. It covers the
high-frequency box shorthands, `all`, and CSSOM declaration order without adding
expansion work to the ordinary no-rollback stylesheet cascade.

## Browser contract

`tests/WebPlatformSubset/contracts/css-effective-property-cascade.html` is a
product-neutral 12-subtest contract. Chrome 153.0.8010.48 passes 12/12. Before
the implementation, the native engine passed 1/8 of the original contract;
margin, padding, inset, `all`, and inline ordering checks failed.

The final native engine passes 12/12 with both parser configurations:

- current CSSParser build: 12/12
- legacy parser build: 12/12
- unchanged cascade winner matrix: 17/17

The contract checks effective-longhand rollback for margin, padding, inset,
border width/color, gap, and overflow; `all: revert-layer`; important
`all: unset`; font/line-height winner identity; and inline CSSOM ordering,
removal, serialization, and recascade behavior.

## Scaling and compatibility checks

The native cascade-layer scaling regression retains exact work invariance as
unrelated DOM size grows:

| affected | unrelated | selector candidates | fallback visits | rule checks | applications | cascade candidates |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8 | 32 | 16 | 0 | 32 | 18 | 32 |
| 8 | 1024 | 16 | 0 | 32 | 18 | 32 |
| 128 | 32 | 256 | 0 | 512 | 258 | 512 |
| 128 | 1024 | 256 | 0 | 512 | 258 | 512 |

Adjacent native contracts also pass: `css-all-unset-primitives` (2/2),
`cssom-inline-declaration-validity` (4/4), `cssom-padding-assignment` (5/5),
and `cssom-border-assignment` (5/5). The shared NativeWeb CSS service, shared
styles, and compiler CTests pass 3/3.

## Performance boundary

The common stylesheet cascade path remains a direct declaration replay. Only a
sheet containing `revert` or `revert-layer` constructs effective-longhand
winners and tokenizes recognized shorthands. Inline declarations use authored
order only when CSSOM mutation, `all`, or overlapping shorthand masks require a
full replay.

This is a bounded stage, not a claim of complete CSS Cascade support. Complex
shorthands such as `background`, `flex`, `transition`, and grid shorthands still
need a general expansion model. `all: initial` and `all: inherit`, UA/user
origins, broader inherited-property coverage, and complete CSSOM priority
projection remain follow-up work in issue #235.

Machine-readable results are summarized in
`docs/validation/evidence/css-effective-property-cascade-20260917.json`.
