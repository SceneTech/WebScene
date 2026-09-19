# HTML details toggle task lifecycle

Issue: [#758](https://github.com/SceneTech/WebScene/issues/758)

Unchanged Code OSS sets a completed-response disclosure's `open` property,
attaches a `toggle` listener, and closes the disclosure in the next animation
frame. The listener persists state and updates `aria-expanded` plus the chevron.

WebScene now queues a bounded per-element/context details task for property,
attribute, and summary-activation changes. Repeated changes before dispatch keep
the first old state and final new state. The resulting non-bubbling,
non-cancelable event exposes `oldState` and `newState`. Navigation and realm
teardown clear queued records; the queue is regular task work and never requests
an animation frame by itself.

## Gates

- Focused native selector: `details-content-pseudo`
- WPT candidate: `contracts/html-details-toggle-task.html`
- Product evidence: completed-response disclosure setup and next-rAF close in
  `chatListRenderer.ts`
- Capacity: at most 4,096 distinct pending details targets per runtime; repeated
  mutations of one target retain one record

Execution is deferred under the current implementation-throughput directive;
the focused sources are committed for the PR lane.
