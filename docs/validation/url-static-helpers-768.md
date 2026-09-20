# URL static helpers for unchanged Code OSS

Issue #768 covers the `URL.parse()` and `URL.canParse()` calls reached directly
by unchanged Code OSS Browser View, Chat question validation, MCP elicitation,
agent-session inputs, and session-artifact links. WebScene now exposes both
one-argument static methods in main and iframe realms.

The helpers convert each supplied input once, accept absolute supported schemes,
resolve relative references only when a valid absolute base is supplied, reject
invalid special-scheme authorities and invalid bases, and make `URL.parse()`
return `null` for parse failure. Object-URL behavior is unchanged. The enclosing
URL implementation remains explicitly partial rather than claiming complete
WHATWG URL parser conformance.

`contracts/url-static-helpers.html` and
`test_url_static_helpers_contract` cover API shape, main/iframe branding,
absolute and relative inputs, special and non-special schemes, invalid values
and bases, required arguments, null results, and single conversion. These gates
were authored but not executed under the active implementation-throughput
directive.
