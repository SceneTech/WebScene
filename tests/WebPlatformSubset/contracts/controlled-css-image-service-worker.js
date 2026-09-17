const counts = {};
const destinations = {};
const image = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 3 2"><rect width="3" height="2"/></svg>';

self.addEventListener('install', event => event.waitUntil(self.skipWaiting()));
self.addEventListener('activate', event => event.waitUntil(self.clients.claim()));
self.addEventListener('fetch', event => {
  const url = new URL(event.request.url);
  counts[url.pathname] = (counts[url.pathname] || 0) + 1;
  destinations[url.pathname] = event.request.destination;
  if (url.pathname.endsWith('/controlled-css-image-fallback.svg')) return;
  if (url.pathname.endsWith('/controlled-css-image-denied.svg')) {
    event.respondWith(Promise.reject(new Error('controller denied CSS image')));
    return;
  }
  if (url.pathname.endsWith('/controlled-css-image.svg')) {
    event.respondWith(new Response(image, {headers:{'content-type':'image/svg+xml'}}));
  }
});
self.addEventListener('message', event => {
  if (event.data === 'report') event.source.postMessage({counts, destinations});
});
