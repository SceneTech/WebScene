const requests = [];

self.addEventListener('install', event => event.waitUntil(self.skipWaiting()));
self.addEventListener('activate', event => event.waitUntil(self.clients.claim()));
self.addEventListener('fetch', event => {
  const url = new URL(event.request.url);
  requests.push({
    path:url.pathname,
    destination:event.request.destination,
    clientId:event.clientId
  });
  if (url.pathname.endsWith('/controlled-parser-fallback.js')) return;
  if (url.pathname.endsWith('/controlled-parser-denied.js')
      || url.pathname.endsWith('/controlled-parser-denied.css')) {
    event.respondWith(Promise.reject(new Error('controlled parser request denied')));
    return;
  }
  if (url.pathname.endsWith('/controlled-parser-script.js')) {
    event.respondWith(new Response(
      "__parserTrace.push('controlled');globalThis.__controlledParserScript=1;",
      {headers:{'content-type':'text/javascript'}}));
  } else if (url.pathname.endsWith('/controlled-parser-style.css')) {
    event.respondWith(new Response('#styled-target{color:rgb(12, 34, 56)}',
      {headers:{'content-type':'text/css'}}));
  } else if (url.pathname.endsWith('/controlled-parser-image.svg')) {
    event.respondWith(new Response(
      '<svg xmlns="http://www.w3.org/2000/svg" width="7" height="5" viewBox="0 0 7 5"><rect width="7" height="5"/></svg>',
      {headers:{'content-type':'image/svg+xml'}}));
  }
});
self.addEventListener('message', event => {
  if (event.data === 'report') {
    event.source.postMessage({requests, sourceId:event.source.id});
  }
});
