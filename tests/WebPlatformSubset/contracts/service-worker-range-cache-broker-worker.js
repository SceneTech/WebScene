const observations = [];
const bulkBody = new Uint8Array(1024 * 1024);
bulkBody[0] = 0x57;
bulkBody[bulkBody.length - 1] = 0x53;

self.addEventListener('install', event => event.waitUntil(self.skipWaiting()));
self.addEventListener('activate', event => event.waitUntil(self.clients.claim()));
self.addEventListener('fetch', event => {
  const url = new URL(event.request.url);
  const range = event.request.headers.get('range');
  const validator = event.request.headers.get('if-none-match');
  const token = event.request.headers.get('x-broker-token');
  observations.push({path:url.pathname, method:event.request.method,
    range, validator, token});
  if (url.pathname.endsWith('/range')) {
    event.respondWith(new Response('2345', {status:206, headers:{
      'content-type':'application/octet-stream',
      'content-range':'bytes 2-5/10',
      'etag':'"range-v1"',
      'last-modified':'Wed, 17 Sep 2025 12:00:00 GMT'
    }}));
  } else if (url.pathname.endsWith('/conditional')) {
    event.respondWith(new Response(null, {status:304, headers:{
      'etag':'"cache-v1"'
    }}));
  } else if (url.pathname.endsWith('/unauthorized')) {
    event.respondWith(new Response(null, {status:401}));
  } else if (url.pathname.endsWith('/missing')) {
    event.respondWith(new Response(null, {status:404}));
  } else if (url.pathname.endsWith('/head')) {
    event.respondWith(new Response(null, {status:200, headers:{
      'content-length':'9', 'content-type':'text/plain'
    }}));
  } else if (url.pathname.endsWith('/method')) {
    event.respondWith(new Response(null, {status:405, headers:{allow:'GET, HEAD'}}));
  } else if (url.pathname.endsWith('/bulk')) {
    event.respondWith(new Response(bulkBody, {headers:{
      'content-type':'application/octet-stream'
    }}));
  }
});
self.addEventListener('message', event => {
  if (event.data === 'report') event.source.postMessage({observations});
});
