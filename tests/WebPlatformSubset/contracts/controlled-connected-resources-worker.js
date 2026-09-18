const destinations = {};

self.addEventListener('install', event => event.waitUntil(self.skipWaiting()));
self.addEventListener('activate', event => event.waitUntil(self.clients.claim()));
self.addEventListener('fetch', event => {
  const url = new URL(event.request.url);
  destinations[url.pathname] = event.request.destination;
  if (url.pathname.endsWith('/controlled-connected-fallback.js')) return;
  if (url.pathname.endsWith('/controlled-connected-denied.js')) {
    event.respondWith(Promise.reject(new Error('controller denied resource')));
    return;
  }
  if (url.pathname.endsWith('/connected-script.js')) {
    event.respondWith(new Response(
      'globalThis.__connectedScript=(globalThis.__connectedScript||0)+1;',
      {headers:{'content-type':'text/javascript'}}));
  } else if (url.pathname.endsWith('/insert-before-script.js')) {
    event.respondWith(new Response(
      'globalThis.__insertBeforeScript=(globalThis.__insertBeforeScript||0)+1;',
      {headers:{'content-type':'text/javascript'}}));
  } else if (url.pathname.endsWith('/connected-style.css')) {
    event.respondWith(new Response('#styled{color:rgb(12, 34, 56)}',
      {headers:{'content-type':'text/css'}}));
  } else if (url.pathname.endsWith('/connected-image.svg')) {
    event.respondWith(new Response(
      '<svg xmlns="http://www.w3.org/2000/svg" width="4" height="3" viewBox="0 0 4 3"><rect width="4" height="3"/></svg>',
      {headers:{'content-type':'image/svg+xml'}}));
  }
});
self.addEventListener('message', event => {
  if (event.data === 'destinations') event.source.postMessage(destinations);
});
