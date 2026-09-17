const pending = new Map();
const writers = new Map();
let nextRequestId = 0;
const observations = {
  requested: 0,
  completed: 0,
  aborted: 0,
  timedOut: 0,
  ignored: 0
};

self.addEventListener('install', event => event.waitUntil(self.skipWaiting()));
self.addEventListener('activate', event => event.waitUntil(self.clients.claim()));

function createHostResponse(event) {
  return (async () => {
    const client = await clients.get(event.clientId);
    if (!client) return new Response('missing client', {status: 404});
    const id = ++nextRequestId;
    const pathname = new URL(event.request.url).pathname;
    const path = pathname.slice(pathname.lastIndexOf('/'));
    const result = new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        if (!pending.delete(id)) return;
        ++observations.timedOut;
        reject(new DOMException('Host response timed out', 'TimeoutError'));
      }, 100);
      pending.set(id, {
        resolve(value) {
          clearTimeout(timeout);
          pending.delete(id);
          resolve(value);
        },
        reject(error) {
          clearTimeout(timeout);
          pending.delete(id);
          reject(error);
        }
      });
    });
    ++observations.requested;
    client.postMessage({
      channel: 'load-resource',
      id,
      path
    });
    try {
      return await result;
    } catch (error) {
      return new Response(String(error?.name || error), {status: 408});
    }
  })();
}

self.addEventListener('fetch', event => {
  const path = new URL(event.request.url).pathname;
  if (!path.slice(path.lastIndexOf('/')).startsWith('/host-stream-')) return;
  event.respondWith(createHostResponse(event));
});

self.addEventListener('message', event => {
  const message = event.data || {};
  const data = message.data || {};
  if (message.channel === 'did-load-resource') {
    const request = pending.get(data.id);
    if (!request || writers.has(data.id)) {
      ++observations.ignored;
      return;
    }
    const transform = new TransformStream();
    const writer = transform.writable.getWriter();
    writers.set(data.id, writer);
    writer.closed.then(
      () => writers.delete(data.id),
      () => writers.delete(data.id)
    );
    request.resolve(new Response(transform.readable, {
      status: data.status,
      headers: {'content-type': data.mime}
    }));
    return;
  }
  if (message.channel === 'did-load-resource-chunk') {
    const writer = writers.get(data.id);
    if (!writer) {
      ++observations.ignored;
      return;
    }
    writer.write(data.data).catch(() => writers.delete(data.id));
    return;
  }
  if (message.channel === 'did-load-resource-end') {
    const writer = writers.get(data.id);
    if (!writer) {
      ++observations.ignored;
      return;
    }
    writers.delete(data.id);
    if (data.error) {
      ++observations.aborted;
      writer.abort(new Error('Host stream aborted')).catch(() => {});
    } else {
      ++observations.completed;
      writer.close().catch(() => {});
    }
    return;
  }
  if (message.channel === 'report') {
    event.source?.postMessage({
      channel: 'host-stream-report',
      observations: {...observations},
      pending: pending.size,
      writers: writers.size
    });
  }
});
