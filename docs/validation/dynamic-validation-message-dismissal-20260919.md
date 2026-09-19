# Dynamic validation-message dismissal source contract

WebScene #710 completes the mutation-driven lifetime boundary for the
interactive validation message added by #708/#709. Programmatic text, numeric,
select, checkedness, reflected constraint, and attribute mutations retire an
active related message. Radio-group and select-option relationships are
included without scanning unrelated document controls.

Existing CSS recascade and inline-style checkpoints validate the retained
target and `ElementInternals` anchor after connected style changes. A valid or
barred target, stale node, or hidden, inert, disabled, or otherwise
nonfocusable anchor clears the state. Clearing is idempotent and uses the
existing scene generation, so a synchronous mutation burst causes at most one
message-state publication. No timer, polling task, animation frame, new DOM
node, selector pass, or presenter ABI is introduced.

The follow-up scene path wraps the bounded message into at most four retained
text commands and ellipsizes the final line at a UTF-8 boundary. The complete
1 KiB-bounded message remains the source of the exactly-once semantic
announcement. Built-in messages now have stable reason identifiers and pass
through a formatter seam before the default English text is selected; custom
validity text remains authored verbatim within the same byte cap.

A public host catalog ABI remains follow-up work. The current engine options
and managed bindings do not own a localization provider, so exposing the seam
would require a new cross-package callback contract. Optional timed dismissal
is also omitted: the existing deadline queue owns JavaScript callbacks and
there is no host opt-in policy, so manufacturing a native deadline would either
change behavior unconditionally or add a second scheduler. Focus, mutation,
navigation, and teardown dismissal continue to provide deterministic lifetime
without idle frame demand.

Per task direction, no build, test, browser run, pixel check, package gate, or
CI job was run. Only `git diff --check` is used for this commit; the source
contract remains unexecuted acceptance evidence.
