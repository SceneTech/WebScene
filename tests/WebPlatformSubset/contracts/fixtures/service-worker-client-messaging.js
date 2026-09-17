self.addEventListener("install", event => event.waitUntil(self.skipWaiting()));
self.addEventListener("activate", event => event.waitUntil(self.clients.claim()));
self.addEventListener("message", event => {
  event.waitUntil((async () => {
    const byId = await self.clients.get(event.source.id);
    const matches = await self.clients.matchAll({ type: "window" });
    const bytes = event.data.bytes;
    event.source.postMessage({
      kind: "service-worker-echo",
      sourceStable: byId?.id === event.source.id,
      listedStable: matches.some(client => client.id === event.source.id),
      clientBrand: byId instanceof WindowClient && event.source instanceof WindowClient,
      client: {
        id: event.source.id,
        type: event.source.type,
        url: event.source.url,
        frameType: event.source.frameType,
        focused: event.source.focused,
        visibilityState: event.source.visibilityState
      },
      bytes
    }, [bytes.buffer]);
    if (event.ports[0]) {
      event.ports[0].postMessage({ kind: "port-echo", value: event.data.value + 1 });
    }
  })());
});
