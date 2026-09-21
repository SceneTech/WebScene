#pragma once

#include <string_view>

namespace webscene_native {

inline constexpr std::string_view indexeddb_compatibility_source = R"JS(
(() => {
  'use strict';
  const nativeStorage = globalThis.__webSceneIndexedDBStorage;
  const scheduleTask = globalThis.__webSceneIndexedDBTask;
  const shouldDeferClose = globalThis.__webSceneIndexedDBShouldDeferClose;
  if (typeof nativeStorage !== 'function' || typeof scheduleTask !== 'function'
      || typeof shouldDeferClose !== 'function') return;
  const openConnections = new Map();
  const loadedDatabases = new Map();
  const writeTails = new Map();
  const clone = value => structuredClone(value);
  const failure = (message, name) => new DOMException(message, name);
  const event = (type, fields = {}) => Object.assign({
    type, bubbles: false, cancelable: false, defaultPrevented: false,
    preventDefault() { if (this.cancelable) this.defaultPrevented = true; }
  }, fields);

  const names = values => {
    const result = Array.from(values).sort();
    Object.defineProperties(result, {
      contains: { value: name => result.includes(String(name)) },
      item: { value: index => result[index] ?? null }
    });
    return result;
  };
  const validKey = key => {
    if (typeof key === 'string') return ['s', key];
    if (typeof key === 'number' && Number.isFinite(key)) {
      return ['n', Object.is(key, -0) ? 0 : key];
    }
    if (key instanceof Date && Number.isFinite(key.getTime())) {
      return ['d', key.getTime()];
    }
    if (Array.isArray(key) && key.length) {
      return ['a', key.map(value => validKey(value))];
    }
    if (key instanceof ArrayBuffer || ArrayBuffer.isView(key)) {
      const bytes = key instanceof ArrayBuffer
        ? new Uint8Array(key) : new Uint8Array(key.buffer, key.byteOffset, key.byteLength);
      return ['b', Array.from(bytes)];
    }
    throw failure('The supplied key is not a valid IndexedDB key', 'DataError');
  };
  const keyToken = key => JSON.stringify(validKey(key));
  const compareTokens = (left, right) => left < right ? -1 : left > right ? 1 : 0;
  const emptyState = version => ({ version, stores: Object.create(null) });
  const normalizeState = value => {
    if (!value || typeof value !== 'object'
        || !Number.isSafeInteger(value.version) || value.version < 0
        || !value.stores || typeof value.stores !== 'object') {
      throw failure('The IndexedDB data file is invalid', 'DataError');
    }
    for (const name of Object.keys(value.stores)) {
      const store = value.stores[name];
      if (!store || !Array.isArray(store.entries)) {
        throw failure('The IndexedDB object store is invalid', 'DataError');
      }
    }
    return value;
  };

  class IDBRequest extends EventTarget {
    constructor(source = null, transaction = null) {
      super();
      this.source = source;
      this.transaction = transaction;
      this.readyState = 'pending';
      this.result = undefined;
      this.error = null;
      this.onsuccess = null;
      this.onerror = null;
    }
    _success(value) {
      if (this.readyState === 'done') return;
      this.result = value;
      this.readyState = 'done';
      this.dispatchEvent(event('success'));
    }
    _failure(error) {
      if (this.readyState === 'done') return null;
      this.error = error instanceof DOMException
        ? error : failure(String(error?.message || error), 'UnknownError');
      this.readyState = 'done';
      const errorEvent = event('error', { cancelable: true });
      this.dispatchEvent(errorEvent);
      return errorEvent;
    }
  }

  class IDBOpenDBRequest extends IDBRequest {
    constructor() {
      super();
      this.onblocked = null;
      this.onupgradeneeded = null;
      this.transaction = null;
    }
  }

  class IDBCursor {
    constructor(request, entries, index) {
      this._request = request;
      this._entries = entries;
      this._index = index;
      this._continued = false;
      this.direction = 'next';
      this.key = clone(entries[index][1]);
      this.primaryKey = clone(entries[index][1]);
      this.value = clone(entries[index][2]);
      this.source = request.source;
    }
    continue(key = undefined) {
      if (this._continued) throw failure('Cursor has already advanced', 'InvalidStateError');
      this._continued = true;
      let next = this._index + 1;
      if (key !== undefined) {
        const token = keyToken(key);
        while (next < this._entries.length
               && compareTokens(this._entries[next][0], token) < 0) next++;
      }
      this._request._cursorStep(this._entries, next);
    }
  }

  class IDBObjectStore {
    constructor(transaction, name) {
      this.transaction = transaction;
      this.name = name;
      this.keyPath = null;
      this.autoIncrement = false;
      this.indexNames = names([]);
    }
    _store() {
      this.transaction._requireActive();
      const store = this.transaction._state.stores[this.name];
      if (!store) throw failure(`Object store '${this.name}' was not found`, 'NotFoundError');
      return store;
    }
    get(key) {
      const token = keyToken(key);
      return this.transaction._enqueue(this, () => {
        const entry = this._store().entries.find(item => item[0] === token);
        return entry ? clone(entry[2]) : undefined;
      });
    }
    put(value, key) { return this._write(value, key, false); }
    add(value, key) { return this._write(value, key, true); }
    _write(value, key, addOnly) {
      if (this.transaction.mode === 'readonly') {
        throw failure('The transaction is read-only', 'ReadOnlyError');
      }
      const token = keyToken(key);
      const clonedKey = clone(key);
      const clonedValue = clone(value);
      return this.transaction._enqueue(this, () => {
        const entries = this._store().entries;
        const index = entries.findIndex(item => item[0] === token);
        if (addOnly && index >= 0) throw failure('The key already exists', 'ConstraintError');
        const entry = [token, clone(clonedKey), clone(clonedValue)];
        if (index < 0) entries.push(entry); else entries[index] = entry;
        this.transaction._mutations.push({ kind: 'put', store: this.name, entry });
        return clone(clonedKey);
      });
    }
    delete(key) {
      if (this.transaction.mode === 'readonly') {
        throw failure('The transaction is read-only', 'ReadOnlyError');
      }
      const token = keyToken(key);
      return this.transaction._enqueue(this, () => {
        const entries = this._store().entries;
        const index = entries.findIndex(item => item[0] === token);
        if (index >= 0) entries.splice(index, 1);
        this.transaction._mutations.push({ kind: 'delete', store: this.name, token });
        return undefined;
      });
    }
    clear() {
      if (this.transaction.mode === 'readonly') {
        throw failure('The transaction is read-only', 'ReadOnlyError');
      }
      return this.transaction._enqueue(this, () => {
        this._store().entries.length = 0;
        this.transaction._mutations.push({ kind: 'clear', store: this.name });
        return undefined;
      });
    }
    count(key = undefined) {
      const token = key === undefined ? null : keyToken(key);
      return this.transaction._enqueue(this, () => token === null
        ? this._store().entries.length
        : Number(this._store().entries.some(item => item[0] === token)));
    }
    getAll() {
      return this.transaction._enqueue(this,
        () => this._store().entries.map(item => clone(item[2])));
    }
    getAllKeys() {
      return this.transaction._enqueue(this,
        () => this._store().entries.map(item => clone(item[1])));
    }
    openCursor() {
      const request = new IDBRequest(this, this.transaction);
      const entries = this._store().entries.slice()
        .sort((left, right) => compareTokens(left[0], right[0]));
      request._cursorStep = (values, index) => {
        this.transaction._queueCursor(request, values, index);
      };
      request._cursorStep(entries, 0);
      return request;
    }
    createIndex() { throw failure('Indexes are not implemented', 'NotSupportedError'); }
    index() { throw failure('Indexes are not implemented', 'NotSupportedError'); }
    deleteIndex() { throw failure('Indexes are not implemented', 'NotSupportedError'); }
  }

)JS"
R"JS(  class IDBTransaction extends EventTarget {
    constructor(database, storeNames, mode, state, revision, versionchange = false) {
      super();
      this.db = database;
      this.mode = mode;
      this.objectStoreNames = names(storeNames);
      this.error = null;
      this.onabort = null;
      this.oncomplete = null;
      this.onerror = null;
      this._state = state;
      this._revision = revision;
      this._versionchange = versionchange;
      this._active = true;
      this._pending = 0;
      this._finishScheduled = false;
      this._commitQueued = false;
      this._mutations = [];
      this._settled = versionchange ? new Promise((resolve, reject) => {
        this._settleResolve = resolve;
        this._settleReject = reject;
      }) : null;
    }
    _requireActive() {
      if (!this._active) throw failure('The transaction is inactive', 'TransactionInactiveError');
    }
    objectStore(name) {
      this._requireActive();
      name = String(name);
      if (!this.objectStoreNames.includes(name)) {
        throw failure(`Object store '${name}' is outside this transaction`, 'NotFoundError');
      }
      return new IDBObjectStore(this, name);
    }
    _enqueue(source, operation) {
      this._requireActive();
      const request = new IDBRequest(source, this);
      this._pending++;
      scheduleTask(() => {
        if (!this._active) return;
        try { request._success(operation()); }
        catch (error) {
          const errorEvent = request._failure(error);
          if (!errorEvent?.defaultPrevented) this._fail(request.error);
        }
        this._pending--;
        this._scheduleFinish();
      });
      return request;
    }
    _queueCursor(request, entries, index) {
      this._requireActive();
      request.readyState = 'pending';
      this._pending++;
      scheduleTask(() => {
        if (!this._active) return;
        request.result = index < entries.length ? new IDBCursor(request, entries, index) : null;
        request.readyState = 'done';
        request.dispatchEvent(event('success'));
        this._pending--;
        this._scheduleFinish();
      });
    }
    _scheduleFinish() {
      if (!this._active || this._pending !== 0 || this._finishScheduled) return;
      this._finishScheduled = true;
      scheduleTask(() => {
        this._finishScheduled = false;
        if (!this._active || this._pending !== 0) return;
        if (this.mode === 'readonly') this._complete();
        else this._commit();
      });
    }
    async _commit() {
      if (this._commitQueued) return;
      this._commitQueued = true;
      const name = this.db.name;
      const previous = writeTails.get(name) ?? Promise.resolve();
      let release;
      const current = new Promise(resolve => { release = resolve; });
      writeTails.set(name, current);
      await previous.catch(() => {});
      try {
        if (this._active) await this._commitNow();
      } finally {
        release();
        if (writeTails.get(name) === current) writeTails.delete(name);
      }
    }
    async _commitNow(attempt = 0) {
      try {
        const revision = await nativeStorage(
          'store', this.db.name, this._revision, this._state);
        this._revision = revision.revision ?? revision;
        this.db._state = this._state;
        this.db._revision = this._revision;
        loadedDatabases.set(this.db.name, {
          state: this._state, revision: this._revision
        });
        this._complete();
      } catch (error) {
        if (error?.name === 'AbortError' && attempt < 4 && !this._versionchange) {
          try {
            const latest = await nativeStorage('load', this.db.name);
            const state = normalizeState(latest.data);
            if (state.version !== this._state.version) throw error;
            for (const mutation of this._mutations) {
              const store = state.stores[mutation.store];
              if (!store) throw error;
              if (mutation.kind === 'clear') store.entries.length = 0;
              else if (mutation.kind === 'delete') {
                const index = store.entries.findIndex(item => item[0] === mutation.token);
                if (index >= 0) store.entries.splice(index, 1);
              } else {
                const index = store.entries.findIndex(item => item[0] === mutation.entry[0]);
                const entry = clone(mutation.entry);
                if (index < 0) store.entries.push(entry); else store.entries[index] = entry;
              }
            }
            this._state = state;
            this._revision = latest.revision;
            return this._commitNow(attempt + 1);
          } catch (reloadError) { error = reloadError; }
        }
        this._fail(error);
      }
    }
    _complete() {
      if (!this._active) return;
      this._active = false;
      this.dispatchEvent(event('complete'));
      this._settleResolve?.();
    }
    _fail(error) {
      if (!this._active) return;
      this.error = error instanceof DOMException
        ? error : failure(String(error?.message || error), 'UnknownError');
      this._active = false;
      this.dispatchEvent(event('error'));
      this.dispatchEvent(event('abort'));
      this._settleReject?.(this.error);
    }
    abort() {
      this._requireActive();
      this._fail(failure('The transaction was aborted', 'AbortError'));
    }
    commit() { this._requireActive(); this._scheduleFinish(); }
  }

  class IDBDatabase extends EventTarget {
    constructor(name, state, revision) {
      super();
      this.name = name;
      this._state = state;
      this._revision = revision;
      this._closed = false;
      this._terminalClosePending = false;
      this._upgradeTransaction = null;
      this.onabort = null;
      this.onerror = null;
      this.onversionchange = null;
    }
    get version() { return this._state.version; }
    get objectStoreNames() { return names(Object.keys(this._state.stores)); }
    createObjectStore(name, options = {}) {
      name = String(name);
      if (!this._upgradeTransaction?._active) {
        throw failure('createObjectStore requires an upgrade transaction', 'InvalidStateError');
      }
      if (options.keyPath != null || options.autoIncrement) {
        throw failure('keyPath and autoIncrement stores are not implemented', 'NotSupportedError');
      }
      if (this._state.stores[name]) throw failure('Object store already exists', 'ConstraintError');
      this._state.stores[name] = { entries: [] };
      this._upgradeTransaction.objectStoreNames = names(Object.keys(this._state.stores));
      return new IDBObjectStore(this._upgradeTransaction, name);
    }
    deleteObjectStore(name) {
      if (!this._upgradeTransaction?._active) {
        throw failure('deleteObjectStore requires an upgrade transaction', 'InvalidStateError');
      }
      name = String(name);
      if (!this._state.stores[name]) throw failure('Object store was not found', 'NotFoundError');
      delete this._state.stores[name];
      this._upgradeTransaction.objectStoreNames = names(Object.keys(this._state.stores));
    }
    transaction(storeNames, mode = 'readonly') {
      if (this._closed) throw failure('The database connection is closed', 'InvalidStateError');
      const selected = typeof storeNames === 'string' ? [storeNames] : Array.from(storeNames);
      if (!selected.length || selected.some(name => !this._state.stores[String(name)])) {
        throw failure('An object store was not found', 'NotFoundError');
      }
      if (mode !== 'readonly' && mode !== 'readwrite') {
        throw new TypeError('Unsupported transaction mode');
      }
      const state = mode === 'readonly' ? this._state : clone(this._state);
      const transaction = new IDBTransaction(
        this, selected.map(String), mode, state, this._revision);
      transaction._scheduleFinish();
      return transaction;
    }
    close() {
      if (this._closed) return;
      if (shouldDeferClose()) {
        if (this._terminalClosePending) return;
        this._terminalClosePending = true;
        scheduleTask(() => {
          this._terminalClosePending = false;
          this.close();
        });
        return;
      }
      this._closed = true;
      openConnections.get(this.name)?.delete(this);
    }
  }

  const waitForConnections = (name, request, oldVersion, newVersion) => {
    const connections = Array.from(openConnections.get(name) || [])
      .filter(connection => !connection._closed);
    if (!connections.length) return Promise.resolve();
    for (const connection of connections) {
      connection.dispatchEvent(event('versionchange', { oldVersion, newVersion }));
    }
    const remaining = () => Array.from(openConnections.get(name) || [])
      .some(connection => !connection._closed);
    if (!remaining()) return Promise.resolve();
    request.dispatchEvent(event('blocked', { oldVersion, newVersion }));
    return new Promise(resolve => {
      const poll = () => remaining() ? setTimeout(poll, 2) : resolve();
      poll();
    });
  };

  class IDBFactory {
    open(name, version = undefined) {
      name = String(name);
      if (!name) throw new TypeError('Database name must not be empty');
      if (version !== undefined
          && (!Number.isSafeInteger(Number(version)) || Number(version) <= 0)) {
        throw new TypeError('Database version must be a positive integer');
      }
      const requested = version === undefined ? undefined : Number(version);
      const request = new IDBOpenDBRequest();
      scheduleTask(async () => {
        try {
          const loaded = await nativeStorage('load', name);
          const existing = loaded.data === null ? emptyState(0) : normalizeState(loaded.data);
          let target = requested ?? (existing.version || 1);
          if (target < existing.version) throw failure('Requested version is older', 'VersionError');
          if (target > existing.version) {
            await waitForConnections(name, request, existing.version, target);
          }
          const state = target > existing.version ? clone(existing) : existing;
          state.version = target;
          const database = new IDBDatabase(name, state, loaded.revision);
          request.result = database;
          if (target > existing.version) {
            const transaction = new IDBTransaction(
              database, Object.keys(state.stores), 'versionchange', state,
              loaded.revision, true);
            database._upgradeTransaction = transaction;
            request.transaction = transaction;
            request.dispatchEvent(event('upgradeneeded', {
              oldVersion: existing.version, newVersion: target
            }));
            database._upgradeTransaction = null;
            transaction._scheduleFinish();
            await transaction._settled;
          }
          loadedDatabases.set(name, { state, revision: database._revision });
          if (!openConnections.has(name)) openConnections.set(name, new Set());
          openConnections.get(name).add(database);
          request.transaction = null;
          request._success(database);
        } catch (error) { request._failure(error); }
      });
      return request;
    }
    deleteDatabase(name) {
      name = String(name);
      const request = new IDBOpenDBRequest();
      scheduleTask(async () => {
        try {
          let oldVersion = 0;
          try {
            const prior = await nativeStorage('load', name);
            oldVersion = prior.data?.version ?? 0;
          } catch (error) {
            if (error?.name !== 'DataError') throw error;
          }
          await waitForConnections(name, request, oldVersion, null);
          await nativeStorage('delete', name);
          loadedDatabases.delete(name);
          request._success(undefined);
        } catch (error) { request._failure(error); }
      });
      return request;
    }
    cmp(first, second) {
      const left = keyToken(first), right = keyToken(second);
      return compareTokens(left, right);
    }
    databases() {
      return Promise.resolve(Array.from(loadedDatabases, ([name, value]) => ({
        name, version: value.state.version
      })));
    }
  }

  Object.defineProperties(globalThis, {
    IDBRequest: { value: IDBRequest, configurable: true },
    IDBOpenDBRequest: { value: IDBOpenDBRequest, configurable: true },
    IDBDatabase: { value: IDBDatabase, configurable: true },
    IDBTransaction: { value: IDBTransaction, configurable: true },
    IDBObjectStore: { value: IDBObjectStore, configurable: true },
    IDBCursor: { value: IDBCursor, configurable: true },
    IDBFactory: { value: IDBFactory, configurable: true },
    indexedDB: { value: new IDBFactory(), configurable: true }
  });
})();
)JS";

} // namespace webscene_native
