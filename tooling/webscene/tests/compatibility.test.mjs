import test from 'node:test';
import assert from 'node:assert/strict';
import { checkSource, validateManifest } from '../compatibility.mjs';

const manifest = {
  schemaVersion: '1.0', id: 'dev.webscene.test', displayName: 'Test', version: '1.0.0',
  profileVersion: '1.0', entryPoint: 'main.js', assets: ['main.js'], capabilities: ['dom', 'host.commands']
};

test('manifest and supported source pass', () => {
  assert.deepEqual(validateManifest(manifest), []);
  assert.deepEqual(checkSource('webscene.host.commands.invoke("save")', manifest), []);
});

test('unsupported and undeclared APIs produce stable diagnostics', () => {
  const diagnostics = checkSource('// localStorage\nnew Worker("x.js");\nwebscene.host.files.invoke("open")', manifest, 'app.ts');
  assert.deepEqual(diagnostics.map(item => item.code), ['WEBSCENE1003', 'WEBSCENE2007']);
  assert.equal(diagnostics[0].line, 2);
});

test('IndexedDB requires the durable storage capability', () => {
  const missing = checkSource('indexedDB.open("state")', manifest);
  assert.equal(missing[0].code, 'WEBSCENE1002');
  assert.equal(missing[0].requiredCapability, 'storage.indexeddb');
  const declared = { ...manifest, capabilities: [...manifest.capabilities, 'storage.indexeddb'] };
  assert.deepEqual(checkSource('indexedDB.open("state")', declared), []);
});
