# Native numeric semantic SET_VALUE contract (#762)

Enabled, writable native `input[type=number]` and `input[type=range]` controls now advertise the existing generation-bearing semantic `SET_VALUE` action. ARIA-only value roles remain read-only because WebScene has no authoritative mutation target for application-owned ARIA state.

The worker accepts only values parsed as finite numbers. It applies the existing programmatic numeric-control sanitizer, including range defaults and step normalization, updates retained live form state, recascades value-sensitive selectors, and dispatches ordered `input` then `change` events. Disabled, read-only, retired, cross-generation, oversized, and malformed requests retain the existing rejection boundaries.

The capability is carried by full snapshots and semantic deltas, allowing AppScene's AT-SPI Value adapter to mark `CurrentValue` writable without synthesizing repeated increment/decrement actions. No accessibility query performs DOM work; mutation occurs only after the bounded action queue admits an explicit write.

Regression coverage extends the semantic action fixture with absolute range assignment, step-preserving state, capability publication, event ordering, stale identities, payload budgets, queue caps, and lifecycle retirement. The contracts were authored but not executed in this fast-merge pass; CI is the recorded execution source.
