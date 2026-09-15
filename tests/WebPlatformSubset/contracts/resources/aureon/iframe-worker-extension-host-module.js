postMessage('worker-started');
self.onmessage = event => {
  const port = event.data.port;
  if (!(port instanceof MessagePort)) throw Error('worker port transfer failed');
  port.onmessage = message => port.postMessage(message.data + 1);
  port.start();
};
