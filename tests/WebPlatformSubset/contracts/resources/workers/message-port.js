self.importScripts('./message-port-helper.js');

self.onmessage = event => {
  const { payload, port, bytes } = event.data;
  const valid = event.ports.length === 1
    && event.ports[0] === port
    && port instanceof MessagePort
    && port instanceof EventTarget
    && payload.self === payload
    && payload.map.get(7) === 'seven'
    && payload.date.getTime() === 456
    && bytes[0] === 41
    && self.workerHelperValue === 42
    && typeof document === 'undefined'
    && origin === location.origin;
  bytes[0] += 1;
  port.postMessage({ valid, bytes }, [bytes.buffer]);
  port.close();
};
