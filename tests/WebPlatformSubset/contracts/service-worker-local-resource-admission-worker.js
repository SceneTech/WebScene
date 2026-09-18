const pending = new Map();
const writers = new Map();
let nextRequestId = 0;
const observations = {
  requested: 0,
  admitted: 0,
  denied: 0,
  missing: 0,
  notModified: 0,
  ranged: 0,
  ignored: 0
};

self.addEventListener('install', event => event.waitUntil(self.skipWaiting()));
self.addEventListener('activate', event => event.waitUntil(self.clients.claim()));

const reply = (id, value) => {
  const request = pending.get(id);
  if (!request) {
    observations.ignored++;
    return false;
  }
  pending.delete(id);
  clearTimeout(request.timeout);
  request.resolve(value);
  return true;
};

self.addEventListener('message', event => {
  const message = event.data || {};
  const data = message.data || {};
  if (message.channel === 'did-load-resource') {
    if (data.status !== 200 && data.status !== 206) {
      reply(data.id, data);
      return;
    }
    if (!pending.has(data.id) || writers.has(data.id)) {
      observations.ignored++;
      return;
    }
    const transform = new TransformStream();
    const writer = transform.writable.getWriter();
    writers.set(data.id, writer);
    writer.closed.then(
      () => writers.delete(data.id),
      () => writers.delete(data.id));
    reply(data.id, {...data, stream:transform.readable});
    return;
  }
  if (message.channel === 'did-load-resource-chunk') {
    const writer = writers.get(data.id);
    if (!writer || !(data.data instanceof Uint8Array)) {
      observations.ignored++;
      if (writer) writer.abort(new TypeError('Resource chunks must be bytes')).catch(() => {});
      return;
    }
    writer.write(data.data).catch(() => writers.delete(data.id));
    return;
  }
  if (message.channel === 'did-load-resource-end') {
    const writer = writers.get(data.id);
    if (!writer) {
      observations.ignored++;
      return;
    }
    writers.delete(data.id);
    if (data.error) writer.abort(new Error('Host stream failed')).catch(() => {});
    else writer.close().catch(() => {});
    return;
  }
  if (message.channel === 'report') {
    event.waitUntil((async () => {
      const cache = await caches.open('local-resource-admission-v1');
      const keys = await cache.keys();
      event.source?.postMessage({
        channel:'admission-report',
        observations:{...observations},
        pending:pending.size,
        writers:writers.size,
        cached:keys.map(request => request.url)
      });
    })());
  }
});

const requestHost = async (event, components, cached) => {
  const client = await clients.get(event.clientId);
  if (!client) return {status:404};
  const id = ++nextRequestId;
  const value = new Promise(resolve => {
    const timeout = setTimeout(() => {
      if (!pending.delete(id)) return;
      resolve({status:408});
    }, 500);
    pending.set(id, {resolve, timeout});
  });
  observations.requested++;
  client.postMessage({
    channel:'load-resource',
    id,
    ...components,
    ifNoneMatch:cached?.headers.get('ETag'),
    range:parseRange(event.request.headers.get('range'))
  });
  return value;
};

const parseRange = header => {
  if (!header) return undefined;
  const match = /^bytes=(\d+)-(\d+)?$/.exec(header);
  if (!match) return null;
  return {start:Number(match[1]), end:match[2] === undefined ? undefined : Number(match[2])};
};

const processResource = async (event, components) => {
  const range = parseRange(event.request.headers.get('range'));
  if (range === null) {
    return new Response(null, {
      status:416,
      headers:{'Access-Control-Allow-Origin':'*', 'Content-Range':'*/*'}
    });
  }
  const shouldCache = event.request.method === 'GET' && range === undefined;
  const cache = await caches.open('local-resource-admission-v1');
  const cached = shouldCache ? await cache.match(event.request) : undefined;
  const entry = await requestHost(event, components, cached);
  const common = {
    'Access-Control-Allow-Origin':'*',
    'Cross-Origin-Resource-Policy':'cross-origin'
  };
  if (entry.status === 304 && cached) {
    observations.notModified++;
    const response = cached.clone();
    for (const [name, value] of Object.entries(common)) response.headers.set(name, value);
    return response;
  }
  if (entry.status === 401) {
    observations.denied++;
    return new Response('Unauthorized', {status:401, headers:common});
  }
  if (entry.status !== 200 && entry.status !== 206) {
    observations.missing++;
    return new Response('Not Found', {status:404, headers:common});
  }
  observations.admitted++;
  const headers = {...common, 'Content-Type':entry.mime};
  if (entry.etag) {
    headers.ETag = entry.etag;
    headers['Cache-Control'] = 'no-cache';
  }
  if (entry.status === 206) {
    observations.ranged++;
    headers['Content-Range'] = entry.range;
    headers['Cache-Control'] = 'no-store';
    return new Response(entry.stream, {status:206, headers});
  }
  if (shouldCache && entry.etag) {
    const [responseStream, cacheStream] = entry.stream.tee();
    const response = new Response(responseStream, {status:200, headers});
    void cache.put(event.request, new Response(cacheStream, {status:200, headers}));
    return response;
  }
  return new Response(entry.stream, {status:200, headers});
};

self.addEventListener('fetch', event => {
  const url = new URL(event.request.url);
  if (url.protocol !== 'https:' || !url.hostname.endsWith('.resource.test')) return;
  if (event.request.method !== 'GET' && event.request.method !== 'HEAD') {
    event.respondWith(new Response('Method Not Allowed', {status:405}));
    return;
  }
  const prefix = url.hostname.slice(0, -'.resource.test'.length);
  const scheme = prefix.split('+', 1)[0];
  event.respondWith(processResource(event, {
    scheme,
    authority:prefix.slice(scheme.length + 1),
    path:url.pathname,
    query:url.search.replace(/^\?/, '')
  }));
});
