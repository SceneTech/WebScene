# Validation message timeout contract (#720, #710)

`webscene_engine_options.validation_message_timeout_milliseconds_v1` is an
optional size-versioned tail. Zero preserves event-driven dismissal and is the
default. Enabled values are admitted only from 1,000 through 60,000
milliseconds; engine creation fails closed outside that range. Managed
Avalonia and Uno hosts expose the same policy as
`NativeWebSceneLoadOptions.ValidationMessageTimeout` with a null default.

The timeout owns one coalesced native steady-clock record. It stores the top
document generation, retained message generation, target ID, and deadline.
Re-reporting replaces that record. Correction, focus change, detach, ordinary
dismissal, navigation, and teardown reset it through the existing dismissal
paths. A stale generation or target cannot clear a newer message.

The record is not a JavaScript timer. It participates in the engine worker's
existing next-deadline calculation and becomes task-ready only when due. It
does not request animation frames, add DOM nodes, rerun selectors, or create a
polling task. Expiration clears only the retained visual message. The immediate
semantic alert remains pending for the ordinary exactly-once semantic capture,
so enabling visual timeout does not delay or cancel announcement delivery.

Formatter calls, English fallback, the 1 KiB UTF-8 message cap, and verbatim
custom validity behavior are unchanged. AppScene, Flutter, and vscode-demo
remain downstream consumers if they choose to expose a nonzero policy; their
current zero-initialized options retain disabled behavior.

Per the fast implementation policy, no build, test, browser, or package gate
was run. `git diff --check` is the only executed check.
