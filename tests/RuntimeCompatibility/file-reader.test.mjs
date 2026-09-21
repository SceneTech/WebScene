import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { performance as nodePerformance } from 'node:perf_hooks';
import { createContext, runInContext } from 'node:vm';

const header = readFileSync(new URL('../../experiments/WebScene.NativeEngine.Probe/native/webscene_file_reader_compatibility.h', import.meta.url), 'utf8');
const script = header.match(/fileReaderCompatibilityScript = R"JS\(([\s\S]*?)\)JS";/)?.[1];
assert.ok(script);
assert.equal(/<\/script/i.test(script), false);

function setup(overrides = {}) {
  // Model the SDK's additional ontype behavior: the implementation must own
  // dispatch, or property handlers would run twice through this superclass.
  class SDKEventTarget extends EventTarget {
    dispatchEvent(event) {
      this['on' + event.type]?.(event);
      return super.dispatchEvent(event);
    }
  }
  const realm = createContext({ Blob, Event, EventTarget: SDKEventTarget,
    DOMException, TextDecoder, setTimeout, ...overrides });
  runInContext(script, realm);
  return realm.FileReader;
}
const delay = () => new Promise(resolve => setTimeout(resolve, 10));
function read(FileReader, blob, method = 'readAsArrayBuffer', ...args) {
  const reader = new FileReader();
  return new Promise((resolve, reject) => {
    reader.onload = event => resolve({ reader, result: event.target.result });
    reader.onerror = () => reject(reader.error);
    reader[method](blob, ...args);
  });
}

test('actual Blob bytes are read asynchronously with one ordered event delivery', async () => {
  const FileReader = setup();
  const reader = new FileReader();
  const bytes = Uint8Array.of(0, 255, 128, 13, 10);
  const events = [];
  let propertyCalls = 0, listenerCalls = 0;
  for (const name of ['loadstart', 'progress', 'load', 'loadend']) reader.addEventListener(name, event => {
    assert.equal(event.target, reader);
    assert.equal(event.currentTarget, reader);
    assert.equal(event.total, bytes.length);
    assert.equal(event.lengthComputable, true);
    if (name === 'load' || name === 'loadend') assert.equal(reader.readyState, FileReader.DONE);
    events.push(name);
  });
  const loaded = new Promise(resolve => { reader.onloadend = resolve; });
  reader.onload = () => { propertyCalls++; };
  reader.addEventListener('load', () => { listenerCalls++; }, { once: true });
  reader.readAsArrayBuffer(new Blob([bytes]));
  assert.equal(reader.readyState, FileReader.LOADING);
  assert.equal(reader.result, null);
  assert.deepEqual(events, []);
  await loaded;
  assert.deepEqual(events, ['loadstart', 'progress', 'load', 'loadend']);
  assert.deepEqual(Array.from(new Uint8Array(reader.result)), Array.from(bytes));
  assert.equal(propertyCalls, 1);
  assert.equal(listenerCalls, 1);
  assert.equal(reader.error, null);
});

test('one native file-reading task owns the complete ordered read', async () => {
  const scheduled = [];
  const FileReader = setup({
    __webSceneQueueFileReadingTask(callback) {
      scheduled.push(callback);
      return true;
    },
    setTimeout() { throw new Error('timer fallback must not run'); }
  });
  const reader = new FileReader();
  const events = [];
  for (const name of ['loadstart', 'progress', 'load', 'loadend']) {
    reader.addEventListener(name, () => events.push(name));
  }
  const loaded = new Promise(resolve => { reader.onloadend = resolve; });
  reader.readAsArrayBuffer(new Blob([Uint8Array.of(7, 8, 9)]));
  assert.equal(scheduled.length, 1);
  scheduled.shift()();
  for (let attempt = 0; attempt < 8 && events.at(-1) !== 'loadend'; attempt++) {
    await Promise.resolve();
  }
  await loaded;
  assert.equal(scheduled.length, 0);
  assert.deepEqual(events, ['loadstart', 'progress', 'load', 'loadend']);
  assert.deepEqual(Array.from(new Uint8Array(reader.result)), [7, 8, 9]);
});

test('VS Code onload can immediately read the next socket Blob without stale loadend', async () => {
  const FileReader = setup();
  const reader = new FileReader();
  const values = [], endings = [];
  const finished = new Promise(resolve => { reader.onloadend = () => { endings.push(reader.result); resolve(); }; });
  reader.onload = event => {
    values.push(new Uint8Array(event.target.result)[0]);
    if (values.length === 1) reader.readAsArrayBuffer(new Blob([Uint8Array.of(2)]));
  };
  reader.readAsArrayBuffer(new Blob([Uint8Array.of(1)]));
  await finished;
  assert.deepEqual(values, [1, 2]);
  assert.equal(endings.length, 1);
  assert.equal(new Uint8Array(endings[0])[0], 2);
});

test('concurrent reads fail and abort suppresses delayed native Blob completion', async () => {
  const FileReader = setup();
  let release;
  class DeferredBlob extends Blob { arrayBuffer() { return new Promise(resolve => { release = resolve; }); } }
  const reader = new FileReader();
  const events = [];
  for (const name of ['loadstart', 'load', 'abort', 'loadend', 'error']) reader.addEventListener(name, () => events.push(name));
  reader.readAsArrayBuffer(new DeferredBlob(['bytes']));
  assert.throws(() => reader.readAsText(new Blob(['again'])), { name: 'InvalidStateError' });
  await delay();
  assert.equal(typeof release, 'function');
  reader.abort();
  release(Uint8Array.of(7).buffer);
  await delay();
  assert.deepEqual(events, ['loadstart', 'abort', 'loadend']);
  assert.equal(reader.result, null);
  assert.equal(reader.error, null);
  assert.equal(reader.readyState, FileReader.DONE);
});

test('real read failures emit error and loadend with no successful result', async () => {
  const FileReader = setup();
  class FailedBlob extends Blob { arrayBuffer() { return Promise.reject(new Error('native read failed')); } }
  const reader = new FileReader();
  const events = [];
  for (const name of ['loadstart', 'error', 'load']) reader.addEventListener(name, () => events.push(name));
  const done = new Promise(resolve => { reader.onloadend = resolve; });
  reader.readAsArrayBuffer(new FailedBlob(['x']));
  await done;
  assert.deepEqual(events, ['loadstart', 'error']);
  assert.equal(reader.error.name, 'NotReadableError');
  assert.match(reader.error.message, /native read failed/);
  assert.equal(reader.result, null);
});

test('text, binary strings, and data URLs encode the actual Blob contents', async () => {
  const FileReader = setup();
  const text = 'Zażółć 世界';
  assert.equal((await read(FileReader, new Blob([text]), 'readAsText')).result, text);
  const bytes = Uint8Array.of(0, 255, 127, 3);
  const binary = await read(FileReader, new Blob([bytes]), 'readAsBinaryString');
  assert.deepEqual(Array.from(binary.result, value => value.charCodeAt(0)), Array.from(bytes));
  const data = await read(FileReader, new Blob([bytes], { type: 'application/octet-stream' }), 'readAsDataURL');
  assert.equal(data.result, 'data:application/octet-stream;base64,' + Buffer.from(bytes).toString('base64'));
  assert.equal((await read(FileReader, new Blob([]), 'readAsDataURL')).result, 'data:application/octet-stream;base64,');
  assert.equal((await read(FileReader, new Blob([text]), 'readAsText', 'invalid-encoding')).result, text);
});

test('existing FileReader is preserved and listener removal/abort signals work', async () => {
  class ExistingFileReader {}
  assert.equal(setup({ FileReader: ExistingFileReader }), ExistingFileReader);
  const FileReader = setup();
  const reader = new FileReader();
  let calls = 0;
  const listener = () => calls++;
  reader.addEventListener('load', listener);
  reader.removeEventListener('load', listener);
  const controller = new AbortController();
  reader.addEventListener('load', listener, { signal: controller.signal });
  controller.abort();
  const done = new Promise(resolve => { reader.onloadend = resolve; });
  reader.readAsArrayBuffer(new Blob(['x']));
  await done;
  assert.equal(calls, 0);
  assert.throws(() => reader.readAsArrayBuffer({ arrayBuffer() {} }), { name: 'TypeError' });
});

test('performance gate: reads an 8 MiB socket Blob without byte loss', async () => {
  const FileReader = setup();
  const size = 8 * 1024 * 1024;
  const bytes = new Uint8Array(size);
  for (let index = 0; index < bytes.length; index += 4096) bytes[index] = index >>> 12;
  const started = nodePerformance.now();
  const { result } = await read(FileReader, new Blob([bytes]));
  const elapsed = nodePerformance.now() - started;
  const output = new Uint8Array(result);
  assert.equal(output.byteLength, size);
  assert.equal(output[4096 * 255], 255);
  assert.ok(elapsed < 3_000, `FileReader gate took ${elapsed.toFixed(1)} ms`);
});
