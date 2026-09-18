#pragma once
#include <string_view>

namespace webscene_native {
// The pinned WebScene exposes a stable native HTMLStyleElement.sheet object,
// but no rule mutation methods. Its textContent setter *does* replace that
// owner's parsed native rules. Bridge dynamic CSSOM operations to a native
// staged rule-set replacement so one task publishes each owner's final rules
// and recascades once.
// This is a bounded adapter, not a complete CSSOM implementation: constructed
// sheets and imported-sheet inspection remain unsupported.
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
  const ruleInstances = new WeakSet();
  const styleRuleInstances = new WeakSet();
  const groupingRuleInstances = new WeakSet();
  const conditionRuleInstances = new WeakSet();
  const mediaRuleInstances = new WeakSet();
  const ruleListInstances = new WeakSet();
  const interfaceConstructor = (name, instances, parent) => {
    const constructor = { [name]: function() {
      throw new TypeError('Illegal constructor');
    } }[name];
    Object.defineProperty(constructor, Symbol.hasInstance, {
      value: value => instances.has(value)
    });
    if (parent) Object.setPrototypeOf(constructor.prototype, parent.prototype);
    if (typeof globalThis[name] !== 'function')
      Object.defineProperty(globalThis, name, { value: constructor, configurable: true });
    return constructor;
  };
  const CSSRuleInterface = interfaceConstructor('CSSRule', ruleInstances);
  const CSSStyleRuleInterface = interfaceConstructor(
    'CSSStyleRule', styleRuleInstances, CSSRuleInterface);
  const CSSGroupingRuleInterface = interfaceConstructor(
    'CSSGroupingRule', groupingRuleInstances, CSSRuleInterface);
  const CSSConditionRuleInterface = interfaceConstructor(
    'CSSConditionRule', conditionRuleInstances, CSSGroupingRuleInterface);
  const CSSMediaRuleInterface = interfaceConstructor(
    'CSSMediaRule', mediaRuleInstances, CSSConditionRuleInterface);
  const CSSRuleListInterface = interfaceConstructor('CSSRuleList', ruleListInstances);
  const installInterfaces = view => {
    for (const [name, constructor] of [
      ['CSSRule', CSSRuleInterface],
      ['CSSStyleRule', CSSStyleRuleInterface],
      ['CSSGroupingRule', CSSGroupingRuleInterface],
      ['CSSConditionRule', CSSConditionRuleInterface],
      ['CSSMediaRule', CSSMediaRuleInterface],
      ['CSSRuleList', CSSRuleListInterface]
    ]) {
      if (typeof view[name] !== 'function')
        Object.defineProperty(view, name, { value: constructor, configurable: true });
    }
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
  const makeList = (rules, synchronizeList) => {
    const target = {};
    const list = new Proxy(target, {
      get(target, key, receiver) {
        synchronizeList();
        if (key === 'length') return rules().length;
        if (key === 'item') return index => {
          synchronizeList();
          return rules()[Number(index) >>> 0] || null;
        };
        if (key === Symbol.iterator) return function* () {
          synchronizeList();
          yield* rules();
        };
        if (typeof key === 'string' && /^(0|[1-9][0-9]*)$/.test(key))
          return rules()[Number(key)];
        return Reflect.get(target, key, receiver);
      },
      set() { return false; },
      deleteProperty() { return false; },
      defineProperty() { return false; }
    });
    ruleListInstances.add(list);
    return list;
  };
  const makeRule = (state, parsed, containingRule = null) => {
    let cssText = parsed.cssText;
    let selectorText = parsed.selectorText;
    let parent = state.sheet;
    let style;
    let serializeGroup;
    const mediaMatch = /^@media(?:\s+([^\{]*?))?\s*\{/i.exec(parsed.cssText);
    const rule = {};
    ruleInstances.add(rule);
    if (mediaMatch) {
      groupingRuleInstances.add(rule);
      conditionRuleInstances.add(rule);
      mediaRuleInstances.add(rule);
    } else if (parsed.selectorText !== undefined) {
      styleRuleInstances.add(rule);
    }
    Object.defineProperties(rule, {
      cssText: { enumerable: true, get: () => cssText },
      parentStyleSheet: { enumerable: true, get: () => parent },
      parentRule: { enumerable: true, get: () => parent ? containingRule : null }
    });
    if (mediaMatch) {
      let children = splitRules(parsed.body || '').map(child => makeRule(state, child, rule));
      let conditionText = (mediaMatch[1] || '').trim();
      const attached = () => parent && (containingRule
        ? containingRule.cssRules && Array.from(containingRule.cssRules).includes(rule)
        : state.rules.includes(rule));
      const serialize = () => {
        cssText = `@media ${conditionText} {${children.map(child => child.cssText).join('')}}`;
      };
      serializeGroup = serialize;
      const list = makeList(() => children, () => synchronize(state));
      Object.defineProperties(rule, {
        type: { enumerable: true, value: 4 },
        conditionText: {
          enumerable: true,
          get: () => conditionText,
          set(value) {
            synchronize(state);
            conditionText = text(value).trim();
            serialize();
            if (attached()) publish(state);
          }
        },
        cssRules: { enumerable: true, get: () => list },
        insertRule: { writable: true, value(ruleText, index = 0) {
          if (arguments.length === 0) throw new TypeError('A CSS rule is required');
          synchronize(state);
          index = Number(index) >>> 0;
          if (index > children.length) exception('Rule index is out of bounds', 'IndexSizeError');
          const parsedChildren = splitRules(text(ruleText));
          if (parsedChildren.length !== 1) exception('Exactly one CSS rule is required', 'SyntaxError');
          if (/^@(import|namespace)\b/i.test(parsedChildren[0].cssText))
            exception('Imported and namespace rules require native CSSOM support', 'NotSupportedError');
          children.splice(index, 0, makeRule(state, parsedChildren[0], rule));
          serialize();
          if (attached()) publish(state);
          return index;
        } },
        deleteRule: { writable: true, value(index) {
          if (arguments.length === 0) throw new TypeError('A rule index is required');
          synchronize(state);
          index = Number(index) >>> 0;
          if (index >= children.length) exception('Rule index is out of bounds', 'IndexSizeError');
          children.splice(index, 1)[0].detach();
          serialize();
          if (attached()) publish(state);
        } },
        detach: { value: () => {
          parent = null;
          for (const child of children) child.detach();
        } }
      });
      serialize();
    } else if (parsed.selectorText !== undefined) {
      Object.defineProperties(rule, {
        type: { enumerable: true, value: 1 },
        selectorText: {
          enumerable: true,
          get: () => selectorText,
          set(value) {
            synchronize(state);
            const candidate = text(value).trim();
            try {
              // Element.matches() and stylesheet parsing share WebScene's
              // native selector parser. CSSOM ignores invalid selectorText
              // assignments instead of surfacing the parser's SyntaxError.
              state.owner.ownerDocument.createElement('span').matches(candidate);
            } catch (error) {
              if (error?.name === 'SyntaxError') return;
              throw error;
            }
            if (candidate === selectorText) return;
            selectorText = candidate;
            cssText = selectorText + ' {'
              + (style ? style.cssText : parsed.body || '') + '}';
            if (parent && (containingRule || state.rules.includes(rule))) {
              if (containingRule && typeof containingRule.__webSceneSerialize === 'function')
                containingRule.__webSceneSerialize();
              publish(state);
            }
          }
        },
        style: { enumerable: true, get() {
          if (style) return style;
          // Reuse WebScene's real CSSStyleDeclaration parser/property methods.
          // The scratch element remains detached; only the owning style element
          // is published when callers mutate this declaration.
          const declaration = state.owner.ownerDocument.createElement('span').style;
          declaration.cssText = parsed.body;
          const commit = () => {
            synchronize(state);
            cssText = selectorText + ' {' + declaration.cssText + '}';
            if (parent && (containingRule || state.rules.includes(rule))) {
              if (containingRule && typeof containingRule.__webSceneSerialize === 'function')
                containingRule.__webSceneSerialize();
              publish(state);
            }
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
        } },
        detach: { value: () => { parent = null; } }
      });
    } else {
      Object.defineProperty(rule, 'detach', { value: () => { parent = null; } });
    }
    if (mediaMatch) Object.defineProperty(rule, '__webSceneSerialize', {
      value: () => {
        serializeGroup();
        if (containingRule?.__webSceneSerialize) containingRule.__webSceneSerialize();
      }
    });
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
    installInterfaces(sheet.ownerNode?.ownerDocument?.defaultView || globalThis);
    const state = {
      sheet, owner: sheet.ownerNode,
      source: undefined, ownerSource: undefined, rules: [], disabled: false,
      mediaText: typeof sheet.ownerNode?.getAttribute === 'function'
        ? sheet.ownerNode.getAttribute('media') || ''
        : ''
    };
    const list = makeList(() => state.rules, () => synchronize(state));
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
