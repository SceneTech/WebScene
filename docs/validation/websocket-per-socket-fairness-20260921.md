# Per-socket WebSocket receive fairness — 21 September 2026

## Product boundary

The unchanged packaged Code OSS remote extension host sends its one-byte `Ready` message about 2.7 ms after process start, but WebScene can dispatch that byte hundreds of milliseconds later. The best retained run records the browser transport connected at `1789952238161`, Node sending `Ready` at `1789952238978`, and the browser receiving it at `1789952239128`. Explorer paints about 50 ms after `Ready`, making incoming socket dispatch the remaining workspace-startup owner.

The native transport previously placed events from every WebSocket in one global FIFO. A burst on a bulk or management connection could therefore delay a latency-sensitive protocol event on an independent socket.

## Focused change

The bounded receive queue now keeps FIFO order within each socket and rotates between sockets with pending events. Global byte and event limits, one empty-to-ready wake, terminal-event admission, and asynchronous JavaScript dispatch remain unchanged.

## Focused gates

- A native two-socket regression queues 128 4 KiB frames on one socket and a one-byte protocol marker on the other. The marker is dispatched as event two.
- Browser WebSocket lifecycle: 100/100 cycles; p95 0.192 ms; at most 19 competing MessagePort events before an echo.
- Product-shaped protocol lifecycle: 20/20 cycles; Ready p95 2.65 ms; Initialized p95 11.91 ms; socket dispatch p95 11 ms.
- FileReader ordering and task-source regression: pass; 64 timer backlog completes in 0.324 ms.
- Settled V8 heap is flat in the focused socket and protocol gates.

The exact installed SDK and unchanged Code OSS package remain the product promotion gate for WebScene #289, #280, and #252.
