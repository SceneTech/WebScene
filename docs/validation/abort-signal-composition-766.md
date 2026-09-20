# AbortSignal composition for Code OSS requests

Issue #766 closes the missing AbortSignal static surface reached by unchanged
Code OSS browser request code. The main workbench and iframe realms now expose
the same branded EventTarget-based `AbortSignal` and `AbortController` shape,
including `abort`, `timeout`, `any`, `throwIfAborted`, custom/default reasons,
and `onabort` delivery.

`AbortSignal.any` consumes an iterator incrementally, caps fan-in at 4,096,
deduplicates source identity, preserves first-reason order, closes iterators on
conversion failures, and removes every dependent source listener after
settlement. Composite wrappers are weak; collection schedules listener cleanup
at the next runtime task boundary. At most 4,096 live compositions are retained.

`AbortSignal.timeout` schedules through the existing browser timer task source,
settles asynchronously with `TimeoutError`, and shares the runtime-wide 65,536
timer/frame-task capacity. Frame retirement, navigation, and runtime shutdown
release composition roots and callbacks.

Authored coverage consists of
`contracts/abort-signal-composition.html` and
`test_abort_signal_composition_contract`. The contracts cover browser shape,
main/iframe branding, default and custom reasons, `throwIfAborted`, already and
future-aborted ordering, duplicate inputs, iterator closing, invalid input,
asynchronous timeout ordering, event identity/counts, and pending-state runtime
retirement. They were authored but not run under the active implementation
throughput directive.
