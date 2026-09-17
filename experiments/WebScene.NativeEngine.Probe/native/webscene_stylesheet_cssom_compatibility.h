#pragma once
#include <string_view>

namespace webscene_native {
// The pinned WebScene exposes a stable native HTMLStyleElement.sheet object,
// but no rule mutation methods. Its textContent setter *does* replace that
// owner's parsed native rules. Bridge dynamic CSSOM operations to a native
// staged rule-set replacement so one task publishes each owner's final rules
// and recascades once.
// This is a bounded adapter, not a complete CSSOM implementation: constructed
// sheets, imported-sheet inspection and nested rule mutation remain unsupported.
// Semantics: https://www.w3.org/TR/cssom-1/
inline constexpr std::string_view cssCompatibilityScript = R"JS(
// SCENETECH_CSS_COMPATIBILITY_V1
(() => {
  'use strict';
  if (typeof globalThis.__webSceneAugmentStyleSheet === 'function') return;
  const sheets = new WeakMap();
  const exception = (message, name) => { throw new DOMException(message, name); };
  const text = value => {
    if (typeof value === 'symbol') throw new TypeError('A CSS rule cannot be a Symbol');
    return String(value);
  };
  // Split only at top-level CSS rule boundaries. Quoted strings, escaped
  // delimiters, comments, URLs, attribute selectors and nested blocks are kept
  // intact. The actual declarations/selectors are interpreted by native CSS.
  const splitRules = source => {
    const rules = [];
    let start = -1, open = -1, braces = 0, parentheses = 0, brackets = 0;
    let quote = '', comment = false;
    for (let index = 0; index < source.length; index++) {
      const character = source[index], next = source[index + 1];
      if (comment) {
        if (character === '*' && next === '/') { comment = false; index++; }
        continue;
      }
      if (quote) {
        if (character === '\\') index++;
        else if (character === quote) quote = '';
        continue;
      }
      if (character === '/' && next === '*') { comment = true; index++; continue; }
      if (start < 0) {
        if (/\s/.test(character)) continue;
        start = index;
      }
      if (character === '\\') { index++; continue; }
      if (character === '"' || character === "'") { quote = character; continue; }
      if (character === '(') parentheses++;
      if (character === ')') parentheses--;
      if (character === '[') brackets++;
      if (character === ']') brackets--;
      if (parentheses < 0 || brackets < 0) exception('Unbalanced CSS delimiters', 'SyntaxError');
      if (parentheses || brackets) continue;
      if (character === '{') { if (braces++ === 0) open = index; }
      if (character === '}' && --braces < 0) exception('Unbalanced CSS block', 'SyntaxError');
      if ((character === '}' && braces === 0) || (character === ';' && braces === 0)) {
        const cssText = source.slice(start, index + 1).trim();
        const selectorText = source[start] === '@' || open < 0 ? undefined : source.slice(start, open).trim();
        if (open < 0 && source[start] !== '@') exception('A CSS style rule requires a block', 'SyntaxError');
        rules.push({ cssText, selectorText, body: open < 0 ? undefined : source.slice(open + 1, index) });
        start = -1; open = -1;
      }
    }
    if (start >= 0 || braces || parentheses || brackets || quote || comment)
      exception('An incomplete CSS rule is unsupported', 'SyntaxError');
    return rules;
  };
  const stateFor = sheet => {
    const state = sheets.get(sheet);
    if (!state) throw new TypeError('Illegal stylesheet receiver');
    synchronize(state);
    return state;
  };
  const publish = state => {
    const source = state.rules.map(rule => rule.cssText).join('\n');
    const publishedSource = state.disabled ? ''
      : state.mediaText.trim() ? `@media ${state.mediaText} {\n${source}\n}`
      : source;
    if (typeof state.sheet.__webSceneStageRules === 'function') {
      // CSSOM mutation does not replace the style element's DOM text nodes.
      // Stage the final serialized rule set in native state; synchronous
      // style/layout reads and the browser-task boundary flush it.
      state.sheet.__webSceneStageRules(publishedSource);
    } else {
      state.owner.textContent = publishedSource;
      state.ownerSource = publishedSource;
    }
    state.source = source;
  };
  const makeRule = (state, parsed) => {
    let cssText = parsed.cssText;
    let parent = state.sheet;
    let style;
    const rule = {};
    Object.defineProperties(rule, {
      cssText: { enumerable: true, get: () => cssText },
      parentStyleSheet: { enumerable: true, get: () => parent },
      parentRule: { enumerable: true, value: null },
      detach: { value: () => { parent = null; } }
    });
    if (parsed.selectorText !== undefined) {
      Object.defineProperties(rule, {
        type: { enumerable: true, value: 1 },
        selectorText: { enumerable: true, value: parsed.selectorText },
        style: { enumerable: true, get() {
          if (style) return style;
          // Reuse WebScene's real CSSStyleDeclaration parser/property methods.
          // The scratch element remains detached; only the owning style element
          // is published when callers mutate this declaration.
          const declaration = state.owner.ownerDocument.createElement('span').style;
          declaration.cssText = parsed.body;
          const commit = () => {
            synchronize(state);
            cssText = parsed.selectorText + ' {' + declaration.cssText + '}';
            if (parent && state.rules.includes(rule)) publish(state);
          };
          style = new Proxy(declaration, {
            get(target, key) {
              const value = Reflect.get(target, key, target);
              if (typeof value !== 'function') return value;
              return (...args) => {
                const result = Reflect.apply(value, target, args);
                if (key === 'setProperty' || key === 'removeProperty') commit();
                return result;
              };
            },
            set(target, key, value) {
              const result = Reflect.set(target, key, value, target);
              if (result) commit();
              return result;
            }
          });
          return style;
        } }
      });
    }
    return rule;
  };
  const synchronize = state => {
    const source = state.owner.textContent || '';
    if (source === state.ownerSource) return;
    const parsed = splitRules(source);
    for (const rule of state.rules) rule.detach();
    state.rules = parsed.map(rule => makeRule(state, rule));
    state.source = source;
    state.ownerSource = source;
  };
  const augment = sheet => {
    if (!sheet || typeof sheet.insertRule === 'function') return sheet;
    const state = {
      sheet, owner: sheet.ownerNode,
      source: undefined, ownerSource: undefined, rules: [], disabled: false,
      mediaText: sheet.ownerNode.getAttribute('media') || ''
    };
    const list = new Proxy({}, {
      get(_target, key) {
        synchronize(state);
        if (key === 'length') return state.rules.length;
        if (key === 'item') return index => { synchronize(state); return state.rules[Number(index) >>> 0] || null; };
        if (key === Symbol.iterator) return function* () {
          for (let index = 0; index < state.rules.length; index++) { synchronize(state); yield state.rules[index]; }
        };
        if (typeof key === 'string' && /^(0|[1-9][0-9]*)$/.test(key)) return state.rules[Number(key)];
      },
      set() { return false; },
      deleteProperty() { return false; },
      defineProperty() { return false; }
    });
    const media = {};
    Object.defineProperties(media, {
      mediaText: {
        enumerable: true,
        get() { return state.mediaText; },
        set(value) {
          state.mediaText = text(value).trim();
          if (state.mediaText) state.owner.setAttribute('media', state.mediaText);
          else state.owner.removeAttribute('media');
          publish(state);
        }
      },
      length: { enumerable: true, get() {
        return state.mediaText ? state.mediaText.split(',').length : 0;
      } },
      item: { value(index) {
        return state.mediaText.split(',').map(value => value.trim())[Number(index)] || null;
      } }
    });
    sheets.set(sheet, state);
    Object.defineProperties(sheet, {
      cssRules: { configurable: true, enumerable: true, get() { stateFor(this); return list; } },
      disabled: {
        configurable: true, enumerable: true,
        get() { return stateFor(this).disabled; },
        set(value) {
          const current = stateFor(this), disabled = Boolean(value);
          if (current.disabled === disabled) return;
          current.disabled = disabled;
          publish(current);
        }
      },
      media: { configurable: true, enumerable: true, get() { stateFor(this); return media; } },
      insertRule: { configurable: true, writable: true, value(rule, index = 0) {
        if (arguments.length === 0) throw new TypeError('A CSS rule is required');
        const current = stateFor(this);
        index = Number(index) >>> 0;
        if (index > current.rules.length) exception('Rule index is out of bounds', 'IndexSizeError');
        const parsed = splitRules(text(rule));
        if (parsed.length !== 1) exception('Exactly one CSS rule is required', 'SyntaxError');
        // Imported sheets cannot be represented by the native owner-text bridge.
        if (/^@(import|namespace)\b/i.test(parsed[0].cssText))
          exception('Imported and namespace rules require native CSSOM support', 'NotSupportedError');
        current.rules.splice(index, 0, makeRule(current, parsed[0]));
        publish(current);
        return index;
      } },
      deleteRule: { configurable: true, writable: true, value(index) {
        if (arguments.length === 0) throw new TypeError('A rule index is required');
        const current = stateFor(this);
        index = Number(index) >>> 0;
        if (index >= current.rules.length) exception('Rule index is out of bounds', 'IndexSizeError');
        current.rules.splice(index, 1)[0].detach();
        publish(current);
      } }
    });
    return sheet;
  };
  Object.defineProperty(globalThis, '__webSceneAugmentStyleSheet', {
    value: augment, configurable: true
  });
})();
)JS";
}
