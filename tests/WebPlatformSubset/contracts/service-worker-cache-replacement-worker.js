const cacheName = 'worker-replacement-cache-v1';
const cacheUrl = new URL('service-worker-cache-replacement-entry.txt', self.location).href;
self.addEventListener('install', event => event.waitUntil(self.skipWaiting()));
self.addEventListener('activate', event => event.waitUntil(self.clients.claim()));

const snapshot = async () => {
  const names = await caches.keys();
  const cache = await caches.open(cacheName);
  const response = await cache.match(cacheUrl);
  return {
    names,
    found:!!response,
    status:response?.status,
    statusText:response?.statusText,
    etag:response?.headers.get('etag'),
    marker:response?.headers.get('x-cache-marker'),
    body:response ? await response.text() : null,
    script:self.location.search
  };
};

self.addEventListener('message', event => {
  const message = event.data || {};
  event.waitUntil((async () => {
    if (message.command === 'seed') {
      const cache = await caches.open(cacheName);
      await cache.put(cacheUrl, new Response('cached-across-worker-replacement', {
        status:201,
        statusText:'Cached',
        headers:{ETag:'"replacement-v1"', 'X-Cache-Marker':'seeded'}
      }));
    }
    if (message.command === 'cycles') {
      const durations = [];
      const cache = await caches.open(cacheName);
      for (let cycle = 0; cycle < 100; ++cycle) {
        const started = performance.now();
        const request = `${cacheUrl}?cycle=${cycle}`;
        await cache.put(request, new Response(`cycle-${cycle}`));
        const matched = await cache.match(request);
        if (!matched || await matched.text() !== `cycle-${cycle}`) {
          throw new Error(`cache replacement cycle ${cycle} failed`);
        }
        if (!await cache.delete(request)) {
          throw new Error(`cache replacement delete ${cycle} failed`);
        }
        durations.push(performance.now() - started);
      }
      const ordered = [...durations].sort((left, right) => left - right);
      event.source?.postMessage({
        channel:'cache-replacement-result',
        id:message.id,
        cycles:100,
        p95:ordered[Math.ceil(ordered.length * .95) - 1],
        snapshot:await snapshot()
      });
      return;
    }
    if (message.command === 'delete') {
      const deleted = await caches.delete(cacheName);
      event.source?.postMessage({
        channel:'cache-replacement-result',
        id:message.id, deleted, names:await caches.keys()
      });
      return;
    }
    event.source?.postMessage({
      channel:'cache-replacement-result',
      id:message.id,
      snapshot:await snapshot()
    });
  })());
});
