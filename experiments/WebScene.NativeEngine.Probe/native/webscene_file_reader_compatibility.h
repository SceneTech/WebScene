#pragma once
#include <string_view>

namespace webscene_native {
inline constexpr std::string_view fileReaderCompatibilityScript = R"JS(
// SCENETECH_FILE_READER_COMPATIBILITY_V1
(() => {
  if (typeof globalThis.FileReader === 'function') return;
  if (typeof globalThis.Blob !== 'function' || typeof globalThis.Blob.prototype.arrayBuffer !== 'function')
    throw new Error('Native Blob.arrayBuffer is required for FileReader');
  const schedule = callback => globalThis.setTimeout(callback, 0);
  const failure = (message, name) => new DOMException(message, name);
  const bytesToBinary = bytes => {
    let result = '';
    for (let offset = 0; offset < bytes.length; offset += 4096)
      result += String.fromCharCode(...bytes.subarray(offset, offset + 4096));
    return result;
  };
  const base64 = bytes => {
    const alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
    let result = '';
    for (let i = 0; i < bytes.length; i += 3) {
      const value = (bytes[i] << 16) | ((bytes[i + 1] || 0) << 8) | (bytes[i + 2] || 0);
      result += alphabet[(value >>> 18) & 63] + alphabet[(value >>> 12) & 63]
        + (i + 1 < bytes.length ? alphabet[(value >>> 6) & 63] : '=')
        + (i + 2 < bytes.length ? alphabet[value & 63] : '=');
    }
    return result;
  };
  class NativeBlobFileReader extends EventTarget {
    constructor() {
      super();
      this._state = 0; this._result = null; this._error = null;
      this._operation = 0; this._handlers = new Map();
      this.onloadstart = this.onprogress = this.onload = this.onerror = this.onabort = this.onloadend = null;
    }
    get readyState() { return this._state; }
    get result() { return this._result; }
    get error() { return this._error; }
    // The SDK EventTarget also invokes ontype properties. Own dispatch avoids
    // depending on that extension or delivering FileReader.onload twice.
    addEventListener(type, listener, options = {}) {
      if (typeof listener !== 'function' && typeof listener?.handleEvent !== 'function') return;
      type = String(type);
      const capture = typeof options === 'boolean' ? options : Boolean(options?.capture);
      if (options?.signal?.aborted) return;
      const entries = this._handlers.get(type) || [];
      if (entries.some(entry => entry.listener === listener && entry.capture === capture)) return;
      const entry = { listener, capture, once: Boolean(options?.once), removeAbort: null };
      entries.push(entry); this._handlers.set(type, entries);
      if (options?.signal?.addEventListener) {
        const remove = () => this.removeEventListener(type, listener, capture);
        options.signal.addEventListener('abort', remove, { once: true });
        entry.removeAbort = () => options.signal.removeEventListener('abort', remove);
      }
    }
    removeEventListener(type, listener, options = {}) {
      const capture = typeof options === 'boolean' ? options : Boolean(options?.capture);
      const entries = this._handlers.get(String(type));
      const index = entries?.findIndex(entry => entry.listener === listener && entry.capture === capture) ?? -1;
      if (index >= 0) { entries[index].removeAbort?.(); entries.splice(index, 1); }
    }
    dispatchEvent(event) {
      if (!event || !event.type) throw new TypeError('dispatchEvent requires an Event');
      Object.defineProperties(event, {
        target: { value: this, configurable: true }, currentTarget: { value: this, configurable: true }
      });
      const invoke = listener => {
        try {
          if (typeof listener === 'function') listener.call(this, event);
          else listener?.handleEvent(event);
        } catch (error) { schedule(() => { throw error; }); }
      };
      invoke(this['on' + event.type]);
      for (const entry of [...(this._handlers.get(event.type) || [])]) {
        if (!this._handlers.get(event.type)?.includes(entry)) continue;
        if (entry.once) this.removeEventListener(event.type, entry.listener, entry.capture);
        invoke(entry.listener);
      }
      Object.defineProperty(event, 'currentTarget', { value: null, configurable: true });
      return !event.defaultPrevented;
    }
    _emit(type, loaded, total) {
      const event = new Event(type);
      Object.defineProperties(event, {
        lengthComputable: { value: true }, loaded: { value: loaded }, total: { value: total }
      });
      this.dispatchEvent(event);
    }
    _read(blob, format, encoding) {
      if (!(blob instanceof Blob)) throw new TypeError('FileReader requires a Blob');
      if (this._state === 1) throw failure('A read is already in progress', 'InvalidStateError');
      const operation = ++this._operation;
      this._state = 1; this._result = null; this._error = null;
      const active = () => operation === this._operation && this._state === 1;
      schedule(() => {
        if (!active()) return;
        this._emit('loadstart', 0, blob.size);
        if (!active()) return;
        Promise.resolve().then(() => blob.arrayBuffer()).then(buffer => {
          const bytes = new Uint8Array(buffer);
          let result = buffer;
          if (format === 'text') {
            let decoder;
            const charset = /(?:^|;)\s*charset\s*=\s*["']?([^;"'\s]+)/i.exec(blob.type);
            try { decoder = new TextDecoder(encoding || charset?.[1] || 'utf-8'); }
            catch { decoder = new TextDecoder('utf-8'); }
            result = decoder.decode(bytes);
          } else if (format === 'data') result = 'data:' + (blob.type || 'application/octet-stream') + ';base64,' + base64(bytes);
          else if (format === 'binary') result = bytesToBinary(bytes);
          schedule(() => {
            if (!active()) return;
            this._emit('progress', bytes.byteLength, blob.size);
            if (!active()) return;
            this._result = result; this._state = 2;
            this._emit('load', bytes.byteLength, blob.size);
            if (operation === this._operation && this._state === 2) this._emit('loadend', bytes.byteLength, blob.size);
          });
        }).catch(error => schedule(() => {
          if (!active()) return;
          this._error = error instanceof DOMException ? error : failure(String(error?.message || error), 'NotReadableError');
          this._state = 2; this._result = null;
          this._emit('error', 0, blob.size);
          if (operation === this._operation && this._state === 2) this._emit('loadend', 0, blob.size);
        }));
      });
    }
    readAsArrayBuffer(blob) { this._read(blob, 'array'); }
    readAsText(blob, encoding) { this._read(blob, 'text', encoding); }
    readAsDataURL(blob) { this._read(blob, 'data'); }
    readAsBinaryString(blob) { this._read(blob, 'binary'); }
    abort() {
      this._result = null;
      if (this._state !== 1) return;
      const operation = ++this._operation;
      this._state = 2; this._error = null;
      this._emit('abort', 0, 0);
      if (operation === this._operation && this._state === 2) this._emit('loadend', 0, 0);
    }
  }
  for (const [name, value] of Object.entries({ EMPTY: 0, LOADING: 1, DONE: 2 })) {
    Object.defineProperty(NativeBlobFileReader, name, { value, enumerable: true });
    Object.defineProperty(NativeBlobFileReader.prototype, name, { value, enumerable: true });
  }
  Object.defineProperty(NativeBlobFileReader.prototype, Symbol.toStringTag, { value: 'FileReader' });
  Object.defineProperty(globalThis, 'FileReader', { value: NativeBlobFileReader, configurable: true, writable: true });
})();
)JS";
}
