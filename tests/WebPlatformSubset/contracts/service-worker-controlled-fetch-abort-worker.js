let lateResolvers = [];
let observed = { pending: 0, late: 0, success: 0 };

self.addEventListener('install', event => event.waitUntil(self.skipWaiting()));
self.addEventListener('activate', event => event.waitUntil(self.clients.claim()));
self.addEventListener('fetch', event => {
  const path = new URL(event.request.url).pathname;
  if (path.endsWith('/controlled-pending')) {
    ++observed.pending;
    event.respondWith(new Promise(() => {}));
  } else if (path.endsWith('/controlled-late')) {
    ++observed.late;
    event.respondWith(new Promise(resolve => lateResolvers.push(resolve)));
  } else if (path.endsWith('/controlled-success')) {
    ++observed.success;
    event.respondWith(new Response('released', {
      status: 200,
      headers: {'content-type': 'text/plain'}
    }));
  }
});

self.addEventListener('message', event => {
  if (event.data === 'settle-late') {
    const resolvers = lateResolvers;
    lateResolvers = [];
    for (const resolve of resolvers) resolve(new Response('late'));
  } else if (event.data === 'report') {
    event.source.postMessage({ channel: 'abort-report', observed: {...observed} });
  }
});
