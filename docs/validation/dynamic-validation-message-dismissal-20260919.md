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

Bounded wrapping, host-localizable formatting, and optional timed dismissal
remain follow-up work because they require separate text-layout and policy
contracts. Per task direction, no build, test, browser run, pixel check,
package gate, or CI job was run. Only `git diff --check` is used for this
commit; the source contract remains unexecuted acceptance evidence.
