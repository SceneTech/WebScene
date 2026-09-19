import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { performance as nodePerformance } from 'node:perf_hooks';
import { createContext, runInContext } from 'node:vm';

const header = readFileSync(new URL('../../experiments/WebScene.NativeEngine.Probe/native/webscene_stylesheet_cssom_compatibility.h', import.meta.url), 'utf8');
const scriptDeclaration = header.match(
  /cssCompatibilityScriptParts\{([\s\S]*?)\};\nstatic_assert/)?.[1];
assert.ok(scriptDeclaration, 'native CSS compatibility script parts must be embedded');
const script = Array.from(scriptDeclaration.matchAll(/R"JS\(([\s\S]*?)\)JS"/g), match => match[1]).join('');
assert.ok(script, 'native CSS compatibility script must be reconstructable');
assert.equal(/<\/script/i.test(script), false, 'script must be safe to embed in HTML');

function setup() {
  // This fixture models the pinned engine's sheet identity/connectivity and
  // records native textContent writes. Rendering is verified by the macOS
  // native smoke test; these unit tests verify that CSSOM mutations reach that
  // native setter rather than being retained only in JavaScript rule records.
  class HTMLStyleElement {
    constructor() {
      this.connected = true;
      this.source = '';
      this.writes = [];
      this.nativeSheet = { ownerNode: this };
      this.ownerDocument = { createElement: () => ({ style: {
        cssText: '',
        getPropertyValue(property) {
          return this.cssText.match(new RegExp(`(?:^|;)\\s*${property}\\s*:\\s*([^;]+)`))?.[1].trim() || '';
        },
        setProperty(property, value) { this.cssText = `${property}: ${value};`; },
        removeProperty() { const previous = this.cssText; this.cssText = ''; return previous; }
      } }) };
    }
    get sheet() { return this.connected ? this.nativeSheet : null; }
    get textContent() { return this.source; }
    set textContent(value) { this.source = String(value); this.writes.push(this.source); }
  }
  const realm = createContext({ HTMLStyleElement, DOMException, URL });
  runInContext(script, realm);
  return {
    realm,
    createStyle: beforeAugment => {
      const style = new HTMLStyleElement();
      beforeAugment?.(style);
      realm.__webSceneAugmentStyleSheet(style.nativeSheet);
      return style;
    }
  };
}

test('Code OSS inserts and removes decorations through native stylesheet text', () => {
  const { createStyle } = setup();
  const style = createStyle();
  const sheet = style.sheet;
  const rules = sheet.cssRules;
  assert.equal(sheet, style.nativeSheet, 'retain the actual native sheet');
  assert.equal(sheet.insertRule('.decoration-a {color: red}', 0), 0);
  assert.equal(sheet.insertRule('.decoration-b {color: blue}', 0), 0);
  assert.equal(style.writes.length, 2);
  assert.equal(style.textContent, '.decoration-b { color: blue; }\n.decoration-a { color: red; }');
  assert.equal(rules.length, 2, 'previously obtained rule lists remain live');
  assert.equal(rules[1].selectorText, '.decoration-a');
  assert.equal(rules.item(1), rules[1]);
  assert.equal(rules.item(2), null);
  assert.deepEqual(Array.from(rules, rule => rule.selectorText), ['.decoration-b', '.decoration-a']);
  const removed = rules[0];
  sheet.deleteRule(0);
  assert.equal(style.textContent, '.decoration-a { color: red; }');
  assert.equal(style.writes.length, 3);
  assert.equal(removed.parentStyleSheet, null);
  assert.equal(rules.length, 1);
  assert.throws(() => { rules[0] = {}; }, { name: 'TypeError' });
});

test('external native stylesheet replacements refresh the same live rule list', () => {
  const { createStyle } = setup();
  const style = createStyle();
  style.textContent = '.before {color: red}';
  const rules = style.sheet.cssRules;
  const oldRule = rules[0];
  style.textContent = '.after {color: green}\n.other {width: 10px}';
  assert.equal(rules.length, 2);
  assert.equal(rules[0].selectorText, '.after');
  assert.equal(oldRule.parentStyleSheet, null);
  style.sheet.insertRule('.first {height: 5px}');
  assert.equal(style.textContent, '.first { height: 5px; }\n.after { color: green; }\n.other { width: 10px; }');
});

test('CSS token boundaries retain quoted braces, comments, escapes and grouped rules', () => {
  const { createStyle } = setup();
  const style = createStyle();
  const rule = '[data-value="}"]::before { content: "a;{b}\\\""; background: url("data:image/svg+xml,<svg>{}</svg>") }';
  style.sheet.insertRule(rule);
  style.sheet.insertRule('.escaped\\{name { --tokens: {one: two}; color: red }', 1);
  style.sheet.insertRule('@media screen { .nested {color: blue} }', 2);
  assert.equal(style.sheet.cssRules.length, 3);
  assert.equal(style.sheet.cssRules[0].cssText,
    '[data-value="}"]::before { content: "a;{b}\\\""; background: url("data:image/svg+xml,<svg>{}</svg>"); }');
  assert.equal(style.sheet.cssRules[0].selectorText, '[data-value="}"]::before');
  assert.equal(style.sheet.cssRules[1].selectorText, '.escaped\\{name');
  assert.equal(style.sheet.cssRules[2].selectorText, undefined);
  style.textContent = '/* before {} */ .retained { content: "/*quoted*/" } /* after */';
  assert.equal(style.sheet.cssRules.length, 1);
  assert.equal(style.sheet.cssRules[0].selectorText, '.retained');
});

test('starting-style rules expose grouping identity and publish child mutations', () => {
  const { createStyle, realm } = setup();
  const style = createStyle();
  style.textContent = '@starting-style { .entry { opacity: 0; transform: translateX(-6px); } }';
  const rule = style.sheet.cssRules[0];
  assert.equal(rule instanceof realm.CSSStartingStyleRule, true);
  assert.equal(rule.constructor.name, 'CSSStartingStyleRule');
  assert.equal(rule.cssRules.length, 1);
  assert.equal(rule.cssRules[0].selectorText, '.entry');
  rule.deleteRule(0);
  assert.equal(style.textContent, '@starting-style {}');
  rule.insertRule('.replacement { opacity: .5; }');
  assert.equal(rule.cssRules[0].selectorText, '.replacement');
  assert.equal(style.textContent, '@starting-style {.replacement { opacity: .5; }}');
});

test('invalid indices and malformed rules fail before native mutation', () => {
  const { createStyle } = setup();
  const style = createStyle();
  const sheet = style.sheet;
  assert.throws(() => sheet.insertRule('.a {}', 1), { name: 'IndexSizeError' });
  assert.throws(() => sheet.insertRule('.a {}', -1), { name: 'IndexSizeError' });
  assert.throws(() => sheet.deleteRule(0), { name: 'IndexSizeError' });
  for (const value of ['', '.a {} .b {}', '.a {', '.a {content: "unterminated}', 'color: red;']) {
    assert.throws(() => sheet.insertRule(value), { name: 'SyntaxError' });
  }
  assert.throws(() => sheet.insertRule(), { name: 'TypeError' });
  assert.throws(() => sheet.deleteRule(), { name: 'TypeError' });
  assert.equal(style.writes.length, 0);
});

test('import rules expose live child sheets and publish flattened native rules', () => {
  const { createStyle, realm } = setup();
  const staged = [];
  const style = createStyle(style => {
    style.nativeSheet.__webSceneBaseURL = 'https://example.test/css/main.css';
    style.nativeSheet.__webSceneLoadImport = (href, base) => {
      const url = new URL(href, base).href;
      if (url.endsWith('/theme.css')) return {
        url,
        source: '@import "nested/colors.css" screen; .theme { color: red; }'
      };
      if (url.endsWith('/nested/colors.css')) return {
        url,
        source: '.colors { background: blue; }'
      };
      return null;
    };
    style.nativeSheet.__webSceneStageRules = source => {
      staged.push(source);
      return true;
    };
  });
  style.textContent = '@import url("theme.css") layer(editor) supports(display: grid) screen; .local { width: 1px; }';
  style.writes.length = 0;
  const imported = style.sheet.cssRules[0];
  assert.equal(imported instanceof realm.CSSImportRule, true);
  assert.equal(imported.type, 3);
  assert.equal(imported.href, 'https://example.test/css/theme.css');
  assert.equal(imported.layerName, 'editor');
  assert.equal(imported.supportsText, 'display: grid');
  assert.equal(imported.media.mediaText, 'screen');
  assert.equal(imported.styleSheet.ownerRule, imported);
  assert.equal(imported.styleSheet.cssRules[0].href,
    'https://example.test/css/nested/colors.css');
  assert.equal(imported.styleSheet.cssRules[1].selectorText, '.theme');
  imported.styleSheet.cssRules[1].style.setProperty('color', 'green');
  assert.equal(style.writes.length, 0, 'CSSOM publication must not rewrite owner text');
  assert.match(staged.at(-1), /@layer editor/);
  assert.match(staged.at(-1), /@supports \(display: grid\)/);
  assert.match(staged.at(-1), /@media screen/);
  assert.match(staged.at(-1), /\.colors \{ background: blue; \}/);
  assert.match(staged.at(-1), /\.theme \{ color: green; \}/);
});

test('rule declarations use the native parser and publish native declaration mutations', () => {
  const { createStyle } = setup();
  const style = createStyle();
  style.sheet.insertRule('.a {color: red}');
  const rule = style.sheet.cssRules[0];
  assert.equal(rule.style.getPropertyValue('color'), 'red');
  rule.style.setProperty('color', 'green');
  assert.equal(style.textContent, '.a { color: green; }');
  rule.style.cssText = 'width: 42px;';
  assert.equal(style.textContent, '.a { width: 42px; }');
  assert.equal(rule.style.getPropertyValue('width'), '42px');
  style.textContent = '.replacement {height: 10px}';
  rule.style.setProperty('color', 'blue');
  assert.equal(style.textContent, '.replacement {height: 10px}', 'stale declarations cannot resurrect removed rules');
});

test('native connectivity, complete implementations and repeated bootstrap are preserved', () => {
  const { createStyle, realm } = setup();
  const style = createStyle();
  style.connected = false;
  assert.equal(style.sheet, null);
  style.connected = true;
  style.sheet.insertRule('.a {}');
  const insert = style.sheet.insertRule;
  runInContext(script, realm);
  assert.equal(style.sheet.insertRule, insert);
  assert.equal(style.sheet.cssRules.length, 1);
  const complete = createStyle(style => { style.nativeSheet.insertRule = () => 123; });
  assert.equal(complete.sheet.insertRule(), 123);
  assert.equal(Object.hasOwn(complete.sheet, 'cssRules'), false);
});

test('stylesheet augmentation stays inside each V8 context', () => {
  const first = setup();
  const second = setup();
  assert.notEqual(
    first.realm.__webSceneAugmentStyleSheet,
    second.realm.__webSceneAugmentStyleSheet,
    'contexts must not share JavaScript closures through constructor templates'
  );
  first.createStyle().sheet.insertRule('.first {}');
  second.createStyle().sheet.insertRule('.second {}');
});

test('performance gate: publishes 300 decoration mutations within budget', () => {
  const { createStyle } = setup();
  const style = createStyle();
  const started = nodePerformance.now();
  for (let index = 0; index < 300; index++) {
    style.sheet.insertRule(`.decoration-${index} { color: rgb(${index % 255}, 0, 0) }`, index);
  }
  for (let index = 0; index < 150; index++) style.sheet.deleteRule(0);
  const elapsed = nodePerformance.now() - started;
  assert.equal(style.sheet.cssRules.length, 150);
  assert.equal(style.writes.length, 450);
  assert.ok(elapsed < 2_000, `stylesheet mutation gate took ${elapsed.toFixed(1)} ms`);
});
