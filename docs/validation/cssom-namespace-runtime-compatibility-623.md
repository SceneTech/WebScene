# CSS namespace runtime compatibility — WebScene #623

## Failure

Current-main CI run `35429973812` failed the Linux and macOS Runtime browser
compatibility lanes because the generic stylesheet unit still asserted that
`CSSStyleSheet.insertRule('@namespace ...')` throws `NotSupportedError`.

That assertion predates merged namespace rule reflection and namespace-aware
selector matching. The implemented surface is covered by the dedicated
`cssom-namespace-rule-reflection`, `cssom-namespace-selector-matching`,
functional-selector namespace, and namespace-attribute contracts.

## Change

The generic failure test now owns only invalid indices, malformed rules,
missing arguments, and the invariant that rejected mutations do not reach the
native stylesheet setter. The obsolete namespace rejection was removed rather
than replacing it with a weaker unsupported-rule example: the compatibility
layer currently exposes every top-level rule kind it classifies, while
hierarchy restrictions are covered by the dedicated rule contracts.

## Validation truth

Only `git diff --check` ran for this throughput wave. The affected Node runtime
compatibility unit and Linux/macOS CI lanes remain unexecuted.
