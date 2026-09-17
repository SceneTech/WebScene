let fetches = 0;
let misses = 0;
let emptyMatch = false;

self.addEventListener('install', event => event.waitUntil(self.skipWaiting()));
self.addEventListener('activate', event => event.waitUntil((async () => {
  const cache = await caches.open('cache-transform-v1');
  emptyMatch = await cache.match(
    new Request(`${self.location.origin}/never`)) === undefined;
  await self.clients.claim();
})()));
self.addEventListener('fetch', event => {
  if (!event.request.url.endsWith('/cache-transform-oracle')) return;
  event.respondWith((async () => {
    ++fetches;
    const cache = await caches.open('cache-transform-v1');
    const known = await cache.match(event.request);
    if (known) return known;
    ++misses;
    const transform = new TransformStream();
    const writer = transform.writable.getWriter();
    const response = new Response(transform.readable, {
      status:200, headers:{'content-type':'text/plain'}
    });
    const stored = cache.put(event.request, response.clone());
    await writer.write(new Uint8Array([98, 114, 111, 107, 101, 114, 45]));
    await writer.write(new Uint8Array([111, 107]));
    await writer.close();
    await writer.closed;
    await stored;
    return response;
  })());
});
self.addEventListener('message', async event => {
  if (event.data !== 'probe') return;
  const cache = await caches.open('cache-transform-v1');
  const transform = new TransformStream();
  const writer = transform.writable.getWriter();
  event.ports[0].postMessage({
    fetches, misses, emptyMatch,
    cachesTag:Object.prototype.toString.call(caches),
    cacheStorageBrand:caches instanceof CacheStorage,
    cacheTag:Object.prototype.toString.call(cache),
    cacheMethodEnumerable:Object.getOwnPropertyDescriptor(
      CacheStorage.prototype, 'open').enumerable,
    transformTag:Object.prototype.toString.call(transform),
    writableTag:Object.prototype.toString.call(transform.writable),
    writerTag:Object.prototype.toString.call(writer),
    transformReadable:Object.getOwnPropertyDescriptor(
      TransformStream.prototype, 'readable').enumerable,
    writerMethodEnumerable:Object.getOwnPropertyDescriptor(
      WritableStreamDefaultWriter.prototype, 'write').enumerable
  });
  writer.releaseLock();
});
