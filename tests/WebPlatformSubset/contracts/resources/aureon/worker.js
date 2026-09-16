const isWebWorker = typeof self === 'object' && self.constructor
    && self.constructor.name === 'DedicatedWorkerGlobalScope';
const { count } = await import(isWebWorker ? './module.js' : 'module');
self.onmessage = e => {
    e.data[0] += count;
    postMessage(e.data, [e.data.buffer]);
    let workerScopeConstructorsReject = false;
    try { new DedicatedWorkerGlobalScope(); } catch (error) {
        workerScopeConstructorsReject = error instanceof TypeError;
    }
    postMessage({
        detached: e.data.byteLength === 0,
        documentType: typeof document,
        isWebWorker,
        dedicatedIdentity: self instanceof DedicatedWorkerGlobalScope,
        workerIdentity: self instanceof WorkerGlobalScope,
        eventTargetIdentity: self instanceof EventTarget,
        stringTag: Object.prototype.toString.call(self),
        workerScopeConstructorsReject
    });
};
