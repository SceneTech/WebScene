self.addEventListener("install", event => event.waitUntil(self.skipWaiting()));
self.addEventListener("activate", event => event.waitUntil(self.clients.claim()));
self.addEventListener("message", event => {
  event.waitUntil((async () => {
    const byId = await self.clients.get(event.source.id);
    const matches = await self.clients.matchAll({ type: "window" });
    const clientsMethod = Object.getOwnPropertyDescriptor(Clients.prototype, "get");
    const clientAttribute = Object.getOwnPropertyDescriptor(Client.prototype, "id");
    const clientMethod = Object.getOwnPropertyDescriptor(Client.prototype, "postMessage");
    const windowClientAttribute = Object.getOwnPropertyDescriptor(
      WindowClient.prototype, "focused");
    const windowClientMethod = Object.getOwnPropertyDescriptor(
      WindowClient.prototype, "focus");
    let incompatibleClientsRejected = false;
    let incompatibleClientRejected = false;
    let incompatibleWindowClientRejected = false;
    let incompatibleClientMethodRejected = false;
    let incompatibleWindowClientMethodRejected = false;
    try { await Clients.prototype.get.call({}, event.source.id); }
    catch (error) { incompatibleClientsRejected = error instanceof TypeError; }
    try { clientAttribute.get.call({}); }
    catch (error) { incompatibleClientRejected = error instanceof TypeError; }
    try { windowClientAttribute.get.call({}); }
    catch (error) { incompatibleWindowClientRejected = error instanceof TypeError; }
    try { clientMethod.value.call({}, null); }
    catch (error) { incompatibleClientMethodRejected = error instanceof TypeError; }
    try { await windowClientMethod.value.call({}); }
    catch (error) { incompatibleWindowClientMethodRejected = error instanceof TypeError; }
    const bytes = event.data.bytes;
    event.source.postMessage({
      kind: "service-worker-echo",
      sourceStable: byId?.id === event.source.id,
      listedStable: matches.some(client => client.id === event.source.id),
      clientBrand: byId instanceof WindowClient && event.source instanceof WindowClient,
      clientsBrand: self.clients instanceof Clients
        && Object.prototype.toString.call(self.clients) === "[object Clients]",
      descriptors: clientsMethod.enumerable && clientsMethod.configurable
        && clientsMethod.writable && clientAttribute.enumerable
        && clientAttribute.configurable && typeof clientAttribute.get === "function"
        && clientMethod.enumerable && clientMethod.configurable && clientMethod.writable
        && windowClientAttribute.enumerable && windowClientAttribute.configurable
        && windowClientMethod.enumerable && windowClientMethod.configurable
        && windowClientMethod.writable,
      incompatibleReceivers: {
        clients: incompatibleClientsRejected,
        client: incompatibleClientRejected,
        windowClient: incompatibleWindowClientRejected,
        clientMethod: incompatibleClientMethodRejected,
        windowClientMethod: incompatibleWindowClientMethodRejected
      },
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
