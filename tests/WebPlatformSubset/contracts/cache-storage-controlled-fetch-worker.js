let fetches = 0;
let misses = 0;
let emptyMatch = false;

self.addEventListener('install', event => event.waitUntil(self.skipWaiting()));
self.addEventListener('activate', event => event.waitUntil((async () => {
  const cache = await caches.open('cache-broker-v1');
  emptyMatch = await cache.match(
    new Request(`${self.location.origin}/never`)) === undefined;
  await self.clients.claim();
})()));
self.addEventListener('fetch', event => {
  if (!event.request.url.endsWith('/cache-broker-oracle')) return;
  event.respondWith((async () => {
    ++fetches;
    const cache = await caches.open('cache-broker-v1');
    const known = await cache.match(event.request);
    if (known) return known;
    ++misses;
    const response = new Response('broker-ok', {
      status:200,
      headers:{'content-type':'text/plain', 'x-resource-plane':'native'}
    });
    await cache.put(event.request, response.clone());
    return response;
  })());
});
self.addEventListener('message', async event => {
  if (event.data !== 'probe') return;
  const cache = await caches.open('cache-broker-v1');
  event.ports[0].postMessage({
    fetches, misses, emptyMatch,
    cachesTag:Object.prototype.toString.call(caches),
    cacheStorageBrand:caches instanceof CacheStorage,
    cacheTag:Object.prototype.toString.call(cache),
    cacheMethodEnumerable:Object.getOwnPropertyDescriptor(
      CacheStorage.prototype, 'open').enumerable
  });
});
