let workerId = -1;

onmessage = event => {
  const message = event.data;
  if (!message || !message.vsWorker || message.type !== 0) return;

  const reply = (res, err) => postMessage({
    vsWorker: workerId < 0 ? message.vsWorker : workerId,
    seq: message.req,
    res,
    err,
    type: 1
  });

  if (message.channel === 'default' && message.method === '$initialize') {
    workerId = message.args[0];
    reply(undefined, undefined);
    return;
  }

  if (message.channel === 'default' && message.method === '$computeStringDiff') {
    const [original, modified] = message.args;
    const deadline = performance.now() + 25;
    while (performance.now() < deadline) {
      // Model non-trivial diff work. This must not starve the document realm.
    }
    reply({
      edits: [{ start: 0, end: original.length, text: modified }]
    }, undefined);
    return;
  }

  reply(undefined, {
    $isError: true,
    name: 'Error',
    message: `Missing method ${message.method} on worker thread channel ${message.channel}`
  });
};
