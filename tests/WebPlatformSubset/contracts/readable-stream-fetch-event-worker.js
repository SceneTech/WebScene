self.addEventListener('install', event => event.waitUntil(self.skipWaiting()));
self.addEventListener('activate', event => event.waitUntil(self.clients.claim()));
self.addEventListener('fetch', event => {
  if (!event.request.url.endsWith('/stream-fetch-oracle')) return;
  event.waitUntil(Promise.resolve());
  event.respondWith(new Response(new ReadableStream({
    start(controller) {
      controller.enqueue(new Uint8Array([65, 66]));
      controller.enqueue(new Uint8Array([67]));
      controller.close();
    }
  }), {status:206, headers:{'content-type':'text/plain'}}));
});
self.addEventListener('message', async event => {
  if (event.data !== 'probe') return;
  try {
  const synthetic = new FetchEvent('fetch', {
    request:new Request(`${self.location.origin}/synthetic`),
    clientId:'oracle-client'
  });
  let late = '';
  self.dispatchEvent(synthetic);
  try { synthetic.respondWith(new Response('late')); }
  catch (error) { late = error.name; }
  const response = new Response(new ReadableStream({
    start(controller) {
      controller.enqueue(new Uint8Array([65, 66]));
      controller.enqueue(new Uint8Array([67]));
      controller.close();
    }
  }), {status:206});
  const clone = response.clone();
  const [text, cloneText] = await Promise.all([response.text(), clone.text()]);
  event.ports[0].postMessage({
    readableTag:Object.prototype.toString.call(new ReadableStream()),
    fetchTag:Object.prototype.toString.call(synthetic),
    fetchChain:synthetic instanceof ExtendableEvent,
    readableEnumerable:Object.getOwnPropertyDescriptor(
      ReadableStream.prototype, 'getReader').enumerable,
    fetchEnumerable:Object.getOwnPropertyDescriptor(
      FetchEvent.prototype, 'respondWith').enumerable,
    late, text, cloneText, status:response.status,
    bodyUsed:response.bodyUsed && clone.bodyUsed
  });
  } catch (error) {
    event.ports[0].postMessage({error:String(error && error.stack || error)});
  }
});
