import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { performance as nodePerformance } from 'node:perf_hooks';
import { createContext, runInContext } from 'node:vm';

const header = readFileSync(new URL('../../experiments/WebScene.NativeEngine.Probe/native/webscene_performance_timeline_compatibility.h', import.meta.url), 'utf8');
const script = header.match(/browserCompatibilityScript = R"JS\(([\s\S]*?)\)JS";/)?.[1];
assert.ok(script, 'native browser compatibility script must be embedded');
assert.equal(/<\/script/i.test(script), false, 'script must be safe to embed in HTML');

function setup() {
  let clock = 100;
  const now = () => clock;
  const performance = { now, mark() {}, measure() {}, getEntriesByName: () => [] };
  const realm = createContext({ performance, DOMException, structuredClone });
  runInContext(script, realm);
  return { performance, realm, now, advance(value) { clock = value; } };
}
const names = entries => Array.from(entries, entry => entry.name);

test('fresh lifecycle navigation lookup succeeds without invented navigation data', () => {
  const { performance, now } = setup();
  assert.equal(performance.getEntriesByType('navigation').at(0), undefined);
  assert.equal(performance.getEntriesByType('resource').length, 0);
  assert.equal(performance.now, now, 'retain the native animation-frame clock coordinate system');
});

test('marks and measures retain real elapsed times and serialize their entries', () => {
  const { performance, advance } = setup();
  performance.mark('begin');
  advance(142.25);
  performance.mark('end');
  const measured = performance.measure('edit', 'begin', 'end');
  assert.equal(measured.startTime, 100);
  assert.equal(measured.duration, 42.25);
  assert.deepEqual(JSON.parse(JSON.stringify(measured)), { name: 'edit', entryType: 'measure', startTime: 100, duration: 42.25, detail: null });
  assert.deepEqual(names(performance.getEntriesByType('mark')), ['begin', 'end']);
  assert.equal(performance.getEntriesByName('edit', 'measure')[0], measured);
});

test('timeline queries sort by timestamps and repeated names resolve the latest occurrence', () => {
  const { performance } = setup();
  performance.mark('same', { startTime: 80 });
  performance.mark('earlier', { startTime: 10 });
  performance.mark('same', { startTime: 20 });
  assert.deepEqual(names(performance.getEntries()), ['earlier', 'same', 'same']);
  assert.equal(performance.measure('span', 'same').startTime, 20);
  const result = performance.getEntries();
  result.length = 0;
  assert.equal(performance.getEntries().length, 4);
});

test('measure timestamp and duration overloads retain signed measured intervals', () => {
  const { performance } = setup();
  const span = performance.measure('span', { start: 12, duration: 30, detail: { phase: 'layout' } });
  assert.equal(span.startTime, 12);
  assert.equal(span.duration, 30);
  assert.equal(performance.measure('backward', { start: 8, end: 3 }).duration, -5);
  assert.equal(performance.measure('ending', { end: 5, duration: 10 }).startTime, -5);
  assert.equal(performance.measure('from-origin').startTime, 0);
});

test('clearing marks and measures respects type and optional names', () => {
  const { performance } = setup();
  performance.mark('keep');
  performance.mark('remove');
  performance.measure('remove');
  performance.clearMarks('remove');
  assert.deepEqual(names(performance.getEntriesByType('mark')), ['keep']);
  assert.equal(performance.getEntriesByType('measure').length, 1);
  performance.clearMeasures();
  assert.equal(performance.getEntries().length, 1);
  performance.clearMarks();
  assert.equal(performance.getEntries().length, 0);
});

test('metadata is structured-cloned and timestamps reject invalid values', () => {
  const { performance } = setup();
  const metadata = { values: [1] };
  const mark = performance.mark('copied', { detail: metadata });
  metadata.values.push(2);
  assert.deepEqual(mark.detail, { values: [1] });
  assert.throws(() => performance.mark('bad', { startTime: -1 }), { name: 'TypeError' });
  assert.throws(() => performance.mark('bad', { startTime: Infinity }), { name: 'TypeError' });
  assert.throws(() => performance.mark('bad', { detail: () => {} }), { name: 'DataCloneError' });
  assert.throws(() => performance.measure('bad', 'missing'), { name: 'SyntaxError' });
  assert.throws(() => performance.measure('bad', { start: 1, end: 2, duration: 1 }), { name: 'TypeError' });
  assert.throws(() => performance.measure('bad', { duration: 1 }), { name: 'TypeError' });
});

test('timeline storage is bounded and clear releases retained entries', () => {
  const { performance } = setup();
  for (let index = 0; index < 2500; index++) performance.mark(`mark-${index}`);
  assert.equal(performance.getEntries().length, 2048);
  assert.equal(performance.getEntriesByName('mark-0').length, 0);
  assert.equal(performance.getEntriesByName('mark-2499').length, 1);
  performance.clearMarks();
  assert.equal(performance.getEntries().length, 0);
});

test('bootstrap preserves an existing complete implementation and is idempotent', () => {
  const { performance, realm } = setup();
  performance.mark('preserved');
  const methods = { mark: performance.mark, measure: performance.measure, query: performance.getEntriesByType };
  runInContext(script, realm);
  assert.equal(performance.mark, methods.mark);
  assert.equal(performance.measure, methods.measure);
  assert.equal(performance.getEntriesByType, methods.query);
  assert.equal(performance.getEntriesByType('mark').length, 1);
});

test('performance gate: bounded timeline sustains 50,000 marks', () => {
  const { performance } = setup();
  const started = nodePerformance.now();
  for (let index = 0; index < 50_000; index++) performance.mark(`gate-${index}`);
  const entries = performance.getEntriesByType('mark');
  const elapsed = nodePerformance.now() - started;
  assert.equal(entries.length, 2048);
  assert.equal(entries.at(-1).name, 'gate-49999');
  assert.ok(elapsed < 1_500, `timeline gate took ${elapsed.toFixed(1)} ms`);
});
