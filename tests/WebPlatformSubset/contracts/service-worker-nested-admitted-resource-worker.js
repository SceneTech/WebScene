const pending = new Map();
const requests = [];
let nextRequestId = 0;

self.addEventListener('install', event => event.waitUntil(self.skipWaiting()));
self.addEventListener('activate', event => event.waitUntil(self.clients.claim()));
self.addEventListener('fetch', event => {
  const url = new URL(event.request.url);
  if (!url.pathname.endsWith('/service-worker-nested-admitted-resource-fallback.js')) {
    return;
  }
  event.respondWith((async () => {
    const client = await self.clients.get(event.clientId);
    if (!client) return new Response(
      "parent.__nestedAdmittedResourceFinished('missing-client');",
      {headers:{'content-type':'text/javascript'}});
    const webviewId = new URL(client.url).searchParams.get('id');
    const allClients = await self.clients.matchAll({
      includeUncontrolled:true, type:'window'
    });
    const outer = allClients.find(candidate => {
      const candidateUrl = new URL(candidate.url);
      const expectedDocument = candidateUrl.pathname
        .endsWith('/service-worker-nested-admitted-resource.html');
      const nativeHarnessDocument = candidateUrl.protocol === 'file:'
        && candidate.frameType === 'top-level';
      return (expectedDocument || nativeHarnessDocument)
        && (candidateUrl.searchParams.get('id') === webviewId
          || nativeHarnessDocument);
    });
    if (!outer) {
      const detail = JSON.stringify({webviewId, clients:allClients.map(candidate => ({
        id:candidate.id, url:candidate.url, frameType:candidate.frameType
      }))});
      return new Response(
        `parent.__nestedAdmittedResourceFinished(${JSON.stringify(`missing-outer:${detail}`)});`,
        {headers:{'content-type':'text/javascript'}});
    }
    const id = ++nextRequestId;
    const response = new Promise((resolve, reject) => pending.set(id, {
      resolve, reject
    }));
    const record = {clientId:event.clientId, frameType:client.frameType,
      clientUrl:client.url, webviewId, outerClientId:outer.id, outerUrl:outer.url};
    requests.push(record);
    outer.postMessage({channel:'load-resource', id,
      cycle:requests.length - 1, clientId:event.clientId});
    return response;
  })());
});

self.addEventListener('message', event => {
  const message = event.data || {};
  if (message.channel === 'report') {
    event.waitUntil((async () => {
      const clients = await self.clients.matchAll({
        includeUncontrolled:true, type:'window'
      });
      event.source.postMessage({channel:'report', requests, pending:pending.size,
        outerClientId:event.source.id, outerUrl:event.source.url,
        clients:clients.map(client => ({id:client.id, url:client.url,
          frameType:client.frameType}))});
    })());
    return;
  }
  const entry = pending.get(message.id);
  if (!entry) return;
  if (message.channel === 'resource-response') {
    pending.delete(message.id);
    entry.resolve(new Response(new ReadableStream({start(controller) {
      for (const chunk of message.chunks) controller.enqueue(chunk);
      controller.close();
    }}), {headers:{'content-type':message.contentType,
      'cache-control':'no-store'}}));
  }
});
