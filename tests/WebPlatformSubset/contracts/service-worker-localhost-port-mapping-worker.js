const pending = new Map();
let nextId = 0;
let observations = {requested:0, mapped:0, unmapped:0, ignored:0};

self.addEventListener('install', event => event.waitUntil(self.skipWaiting()));
self.addEventListener('activate', event => event.waitUntil(self.clients.claim()));

self.addEventListener('message', event => {
  const message = event.data || {};
  if (message.channel === 'did-load-localhost') {
    const request = pending.get(message.data?.id);
    if (!request) {
      observations.ignored++;
      return;
    }
    pending.delete(message.data.id);
    clearTimeout(request.timeout);
    request.resolve(message.data.location);
    return;
  }
  if (message.channel === 'report') {
    event.source?.postMessage({
      channel:'localhost-mapping-report',
      observations:{...observations},
      pending:pending.size
    });
  }
});

const requestMapping = async (event, origin) => {
  const client = await clients.get(event.clientId);
  if (!client) return undefined;
  const id = ++nextId;
  const location = new Promise(resolve => {
    const timeout = setTimeout(() => {
      if (!pending.delete(id)) return;
      resolve(undefined);
    }, 500);
    pending.set(id, {resolve, timeout});
  });
  observations.requested++;
  client.postMessage({channel:'load-localhost', origin, id});
  return location;
};

self.addEventListener('fetch', event => {
  const url = new URL(event.request.url);
  if (url.origin === self.origin
      || !/^(localhost|127\.0\.0\.1|0\.0\.0\.0):(\d+)$/.test(url.host)) return;
  event.respondWith((async () => {
    const location = await requestMapping(event, url.origin);
    if (!location) {
      observations.unmapped++;
      return fetch(event.request);
    }
    observations.mapped++;
    return new Response(null, {
      status:302,
      headers:{Location:event.request.url.replace(
        new RegExp(`^${url.origin.replace(/[.*+?^$\{\}()|[\]\\]/g, '\\$&')}(/|$)`),
        `${location}$1`)}
    });
  })());
});
