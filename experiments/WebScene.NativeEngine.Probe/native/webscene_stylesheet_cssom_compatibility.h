#pragma once
#include <string_view>

namespace webscene_native {
// The pinned WebScene exposes a stable native HTMLStyleElement.sheet object,
// but no rule mutation methods. Its textContent setter *does* replace that
// owner's parsed native rules. Bridge dynamic CSSOM operations to a native
// staged rule-set replacement so one task publishes each owner's final rules
// and recascades once.
// This is a bounded adapter, not a complete CSSOM implementation: imported-
// sheet inspection remains unsupported.
// Semantics: https://www.w3.org/TR/cssom-1/
inline constexpr std::string_view cssCompatibilityScript = R"JS(
// SCENETECH_CSS_COMPATIBILITY_V1
(() => {
  'use strict';
  if (typeof globalThis.__webSceneAugmentStyleSheet === 'function') return;
  const sheets = new WeakMap();
  const adoptionRoots = new WeakMap();
  const styleSheetInstances = new WeakSet();
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
  const supportsRuleInstances = new WeakSet();
  const layerBlockRuleInstances = new WeakSet();
  const layerStatementRuleInstances = new WeakSet();
  const nestedDeclarationsInstances = new WeakSet();
  const mediaListInstances = new WeakSet();
  const ruleListInstances = new WeakSet();
  let makeConstructedStyleSheet;
  const publishedSource = state => {
    const source = state.rules.map(rule => rule.cssText).join('\n');
    return state.disabled ? ''
      : state.mediaText.trim() ? `@media ${state.mediaText} {\n${source}\n}`
      : source;
  };
  const publishAdoptionRoot = rootState => {
    if (typeof rootState.root.__webScenePublishAdoptedStyleSheets !== 'function') return;
    const sources = rootState.list.map(sheet => publishedSource(sheets.get(sheet)));
    if (!rootState.root.__webScenePublishAdoptedStyleSheets(sources))
      exception('The adopted stylesheet publication exceeds native bounds', 'QuotaExceededError');
  };
  const linkAdopter = (sheet, rootState) => {
    let links = adoptionRoots.get(sheet);
    if (!links) adoptionRoots.set(sheet, links = new Set());
    if (rootState.linkedSheets.has(sheet)) return;
    rootState.linkedSheets.add(sheet);
    links.add(typeof WeakRef === 'function' ? new WeakRef(rootState) : rootState);
  };
  const notifyAdopters = sheet => {
    const links = adoptionRoots.get(sheet);
    if (!links) return;
    for (const link of [...links]) {
      const rootState = typeof link?.deref === 'function' ? link.deref() : link;
      if (!rootState) {
        links.delete(link);
      } else if (rootState.list.includes(sheet)) {
        publishAdoptionRoot(rootState);
      }
    }
  };
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
  const CSSStyleSheetInterface = {
    CSSStyleSheet: function CSSStyleSheet(options = undefined) {
      if (!new.target) throw new TypeError('CSSStyleSheet must be constructed');
      return makeConstructedStyleSheet(options);
    }
  }.CSSStyleSheet;
  Object.defineProperty(CSSStyleSheetInterface, Symbol.hasInstance, {
    value: value => styleSheetInstances.has(value)
  });
  const CSSStyleRuleInterface = interfaceConstructor(
    'CSSStyleRule', styleRuleInstances, CSSRuleInterface);
  const CSSGroupingRuleInterface = interfaceConstructor(
    'CSSGroupingRule', groupingRuleInstances, CSSRuleInterface);
  const CSSConditionRuleInterface = interfaceConstructor(
    'CSSConditionRule', conditionRuleInstances, CSSGroupingRuleInterface);
  const CSSMediaRuleInterface = interfaceConstructor(
    'CSSMediaRule', mediaRuleInstances, CSSConditionRuleInterface);
  const CSSSupportsRuleInterface = interfaceConstructor(
    'CSSSupportsRule', supportsRuleInstances, CSSConditionRuleInterface);
  const CSSLayerBlockRuleInterface = interfaceConstructor(
    'CSSLayerBlockRule', layerBlockRuleInstances, CSSGroupingRuleInterface);
  const CSSLayerStatementRuleInterface = interfaceConstructor(
    'CSSLayerStatementRule', layerStatementRuleInstances, CSSRuleInterface);
  const CSSNestedDeclarationsInterface = interfaceConstructor(
    'CSSNestedDeclarations', nestedDeclarationsInstances, CSSRuleInterface);
  const MediaListInterface = interfaceConstructor('MediaList', mediaListInstances);
  const CSSRuleListInterface = interfaceConstructor('CSSRuleList', ruleListInstances);
  const installInterfaces = view => {
    for (const [name, constructor] of [
      ['CSSRule', CSSRuleInterface],
      ['CSSStyleSheet', CSSStyleSheetInterface],
      ['CSSStyleRule', CSSStyleRuleInterface],
      ['CSSGroupingRule', CSSGroupingRuleInterface],
      ['CSSConditionRule', CSSConditionRuleInterface],
      ['CSSMediaRule', CSSMediaRuleInterface],
      ['CSSSupportsRule', CSSSupportsRuleInterface],
      ['CSSLayerBlockRule', CSSLayerBlockRuleInterface],
      ['CSSLayerStatementRule', CSSLayerStatementRuleInterface],
      ['CSSNestedDeclarations', CSSNestedDeclarationsInterface],
      ['MediaList', MediaListInterface],
      ['CSSRuleList', CSSRuleListInterface]
    ]) {
      if (typeof view[name] !== 'function')
        Object.defineProperty(view, name, { value: constructor, configurable: true });
    }
    for (const target of [view.CSSRule, view.CSSRule?.prototype]) {
      if (target && !('SUPPORTS_RULE' in target)) Object.defineProperty(
        target, 'SUPPORTS_RULE', { value: 12, enumerable: true });
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
  // CSS Nesting exposes rules inside a CSSStyleRule. Declarations before the
  // first nested rule remain on CSSStyleRule.style; later declaration runs are
  // represented by CSSNestedDeclarations entries in cssRules.
  const splitStyleContents = source => {
    const rules = [], leading = [];
    let declarations = [], cursor = 0, parentheses = 0, brackets = 0;
    let quote = '', comment = false, sawRule = false;
    const flushDeclarations = () => {
      const body = declarations.join(' ').trim();
      declarations = [];
      if (!body) return;
      if (!sawRule) leading.push(body);
      else rules.push({ nestedDeclarations: true, body, cssText: body });
    };
    for (let index = 0; index <= source.length; index++) {
      const atEnd = index === source.length;
      const character = atEnd ? '' : source[index];
      const next = source[index + 1];
      if (comment) {
        if (character === '*' && next === '/') { comment = false; index++; }
        continue;
      }
      if (quote) {
        if (character === '\\') index++;
        else if (character === quote) quote = '';
        continue;
      }
      if (!atEnd && character === '/' && next === '*') {
        comment = true; index++; continue;
      }
      if (!atEnd && character === '\\') { index++; continue; }
      if (!atEnd && (character === '"' || character === "'")) {
        quote = character; continue;
      }
      if (!atEnd && character === '(') parentheses++;
      else if (!atEnd && character === ')' && parentheses) parentheses--;
      else if (!atEnd && character === '[') brackets++;
      else if (!atEnd && character === ']' && brackets) brackets--;
      if (parentheses || brackets) continue;
      if (!atEnd && character === ';') {
        const statement = source.slice(cursor, index + 1).trim();
        if (statement) declarations.push(statement);
        cursor = index + 1;
        continue;
      }
      if (!atEnd && character === '{') {
        const prelude = source.slice(cursor, index).trim();
        if (!prelude) exception('A nested rule requires a prelude', 'SyntaxError');
        flushDeclarations();
        let depth = 1, innerQuote = '', innerComment = false, close = index + 1;
        for (; close < source.length && depth; close++) {
          const inner = source[close], innerNext = source[close + 1];
          if (innerComment) {
            if (inner === '*' && innerNext === '/') { innerComment = false; close++; }
            continue;
          }
          if (innerQuote) {
            if (inner === '\\') close++;
            else if (inner === innerQuote) innerQuote = '';
            continue;
          }
          if (inner === '/' && innerNext === '*') { innerComment = true; close++; continue; }
          if (inner === '"' || inner === "'") { innerQuote = inner; continue; }
          if (inner === '{') depth++;
          else if (inner === '}') depth--;
        }
        if (depth) exception('An incomplete nested rule is unsupported', 'SyntaxError');
        const parsed = splitRules(source.slice(cursor, close));
        if (parsed.length !== 1) exception('Exactly one nested rule is required', 'SyntaxError');
        if (parsed[0].selectorText !== undefined
            && !parsed[0].selectorText.includes('&')) {
          parsed[0].selectorText = '& ' + parsed[0].selectorText;
          parsed[0].cssText = parsed[0].selectorText + ' {' + parsed[0].body + '}';
        }
        rules.push(parsed[0]);
        sawRule = true;
        cursor = close;
        index = close - 1;
      } else if (atEnd) {
        const tail = source.slice(cursor).trim();
        if (tail) declarations.push(tail);
        flushDeclarations();
      }
    }
    return { leading: leading.join(' '), rules };
  };
  const stateFor = sheet => {
    const state = sheets.get(sheet);
    if (!state) throw new TypeError('Illegal stylesheet receiver');
    synchronize(state);
    return state;
  };
  // Keep the WPT-covered MediaList grammar bounded to comma-separated media
  // queries while respecting strings and nested functional expressions. The
  // native cascade remains the authority for whether each query matches.
  const splitMediaQueries = value => {
    const source = value === null ? '' : text(value);
    if (!source.trim()) return [];
    const queries = [];
    let start = 0, parentheses = 0, quote = '', comment = false;
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
      if (character === '\\') { index++; continue; }
      if (character === '"' || character === "'") { quote = character; continue; }
      if (character === '(') parentheses++;
      if (character === ')' && --parentheses < 0) return ['not all'];
      if (character === ',' && parentheses === 0) {
        queries.push(source.slice(start, index));
        start = index + 1;
      }
    }
    if (parentheses || quote || comment) return ['not all'];
    queries.push(source.slice(start));
    return queries;
  };
  const normalizeMediaQuery = value => {
    let query = value.trim();
    if (!query || /[$]/.test(query)) return 'not all';
    query = query.replace(/\/\*[\s\S]*?\*\//g, ' ')
      .replace(/\s+/g, ' ')
      .replace(/\(\s*/g, '(')
      .replace(/\s*\)/g, ')')
      .replace(/\s*:\s*/g, ': ')
      .trim();
    query = query.replace(/^(?:(not|only)\s+)?([a-z][\w-]*)/i,
      (_, prefix, medium) => (prefix ? prefix.toLowerCase() + ' ' : '')
        + medium.toLowerCase());
    return query.replace(/\b(and|or|not)\b/gi, keyword => keyword.toLowerCase());
  };
  const parseMediaList = value => splitMediaQueries(value).map(normalizeMediaQuery);
  // Cascade layer statement names use <ident> components separated by dots,
  // and a statement may declare a comma-separated list. Keep escaped dots and
  // commas inside the identifier token, then use CSS.escape() to serialize the
  // decoded identifier the same way as the browser CSSOM.
  const splitLayerNames = (source, delimiter) => {
    const values = [];
    let start = 0;
    for (let index = 0; index < source.length; index++) {
      if (source[index] === '\\') {
        let cursor = index + 1;
        if (/[0-9a-f]/i.test(source[cursor] || '')) {
          let digits = 0;
          while (digits < 6 && /[0-9a-f]/i.test(source[cursor] || '')) {
            cursor++; digits++;
          }
          if (/[\t\n\f\r ]/.test(source[cursor] || '')) cursor++;
        } else if (cursor < source.length) {
          cursor++;
        }
        index = cursor - 1;
      } else if (source[index] === delimiter) {
        values.push(source.slice(start, index));
        start = index + 1;
      }
    }
    values.push(source.slice(start));
    return values;
  };
  const cssEscapeToken = String.raw`\\(?:[0-9a-f]{1,6}(?:[\t\n\f\r ]|\r\n)?|[^\n\r\f0-9a-f])`;
  const cssIdentifierStart = `(?:[a-z_\\u0080-\\uffff]|${cssEscapeToken})`;
  const cssIdentifierCharacter = `(?:[a-z0-9_-\\u0080-\\uffff]|${cssEscapeToken})`;
  const cssIdentifierPattern = new RegExp(
    `^(?:--|-?${cssIdentifierStart})${cssIdentifierCharacter}*$`, 'iu');
  const decodeCssIdentifier = value => value.replace(
    /\\([0-9a-f]{1,6})(?:\r\n|[\t\n\f\r ])?|\\([^\n\r\f0-9a-f])/giu,
    (_, hexadecimal, escaped) => {
      if (escaped !== undefined) return escaped;
      const codePoint = Number.parseInt(hexadecimal, 16);
      return codePoint === 0 || codePoint > 0x10ffff
        ? '\ufffd' : String.fromCodePoint(codePoint);
    });
  const parseLayerStatementNames = cssText => {
    const match = /^@layer\b([\s\S]*);$/i.exec(cssText);
    if (!match) return undefined;
    const source = match[1].replace(/\/\*[\s\S]*?\*\//g, '');
    const serialized = [];
    for (const candidate of splitLayerNames(source, ',')) {
      const components = splitLayerNames(candidate.trim(), '.');
      if (!components.length) return null;
      const name = [];
      for (const componentSource of components) {
        const component = componentSource.trim();
        if (!component || !cssIdentifierPattern.test(component)) return null;
        const decoded = decodeCssIdentifier(component);
        name.push(globalThis.CSS?.escape
          ? globalThis.CSS.escape(decoded) : component);
      }
      serialized.push(name.join('.'));
    }
    return serialized.length && serialized.every(Boolean) ? serialized : null;
  };
  const validateLayerStatement = parsed => {
    if (parsed.body !== undefined || !/^@layer\b/i.test(parsed.cssText)) return;
    if (!parseLayerStatementNames(parsed.cssText))
      exception('The layer statement is invalid', 'SyntaxError');
  };
  const declarationNames = declaration => {
    // The native declaration owns parsing and canonical serialization. Walk
    // that serialized form only to provide CSSStyleDeclaration's indexed Web
    // IDL surface, which the native binding does not expose yet.
    const source = declaration.cssText || '';
    const names = [];
    let start = 0, parentheses = 0, quote = '', comment = false;
    for (let index = 0; index <= source.length; index++) {
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
      if (character === '"' || character === "'") { quote = character; continue; }
      if (character === '(') parentheses++;
      else if (character === ')' && parentheses) parentheses--;
      if ((character === ';' || index === source.length) && parentheses === 0) {
        const declarationText = source.slice(start, index).trim();
        const colon = declarationText.indexOf(':');
        if (colon > 0) names.push(declarationText.slice(0, colon).trim());
        start = index + 1;
      }
    }
    return names;
  };
  const splitDeclarationText = source => {
    const declarations = [];
    let start = 0, parentheses = 0, brackets = 0, quote = '', comment = false;
    for (let index = 0; index <= source.length; index++) {
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
      if (character === '"' || character === "'") { quote = character; continue; }
      if (character === '(') parentheses++;
      else if (character === ')' && parentheses) parentheses--;
      else if (character === '[') brackets++;
      else if (character === ']' && brackets) brackets--;
      if ((character === ';' || index === source.length)
          && parentheses === 0 && brackets === 0) {
        declarations.push(source.slice(start, index).trim());
        start = index + 1;
      }
    }
    return declarations;
  };
  const makeConstructedDeclaration = (initial, commit) => {
    const names = [], values = new Map(), priorities = new Map();
    const canonicalName = value => text(value).trim().toLowerCase();
    const assign = (rawName, rawValue, rawPriority = '') => {
      const name = canonicalName(rawName);
      let value = text(rawValue).trim();
      let priority = text(rawPriority).trim().toLowerCase();
      if (!name || (!name.startsWith('--')
          && !/^-?[a-z][a-z0-9-]*$/.test(name))) return;
      const important = /\s*!important\s*$/i.exec(value);
      if (important) {
        value = value.slice(0, important.index).trim();
        priority = 'important';
      }
      if (priority && priority !== 'important') return;
      if (!value) {
        const index = names.indexOf(name);
        if (index >= 0) names.splice(index, 1);
        values.delete(name); priorities.delete(name);
        return;
      }
      if (!values.has(name)) names.push(name);
      values.set(name, value);
      priorities.set(name, priority);
    };
    const replace = source => {
      names.length = 0; values.clear(); priorities.clear();
      for (const candidate of splitDeclarationText(text(source))) {
        let colon = -1, parentheses = 0, brackets = 0, quote = '';
        for (let index = 0; index < candidate.length; index++) {
          const character = candidate[index];
          if (quote) {
            if (character === '\\') index++;
            else if (character === quote) quote = '';
            continue;
          }
          if (character === '"' || character === "'") { quote = character; continue; }
          if (character === '(') parentheses++;
          else if (character === ')' && parentheses) parentheses--;
          else if (character === '[') brackets++;
          else if (character === ']' && brackets) brackets--;
          else if (character === ':' && !parentheses && !brackets) {
            colon = index; break;
          }
        }
        if (colon > 0) assign(candidate.slice(0, colon), candidate.slice(colon + 1));
      }
    };
    const serialize = () => names.map(name => name + ': ' + values.get(name)
      + (priorities.get(name) ? ' !important' : '') + ';').join(' ');
    const target = {};
    Object.defineProperties(target, {
      cssText: {
        enumerable: true,
        get: serialize,
        set(value) { replace(value); commit(); }
      },
      length: { enumerable: true, get: () => names.length },
      item: { value: index => names[Number(index) >>> 0] || '' },
      getPropertyValue: {
        value: name => values.get(canonicalName(name)) || ''
      },
      getPropertyPriority: {
        value: name => priorities.get(canonicalName(name)) || ''
      },
      setProperty: { value(name, value, priority = '') {
        assign(name, value, priority); commit();
      } },
      removeProperty: { value(name) {
        name = canonicalName(name);
        const previous = values.get(name) || '';
        const index = names.indexOf(name);
        if (index >= 0) names.splice(index, 1);
        values.delete(name); priorities.delete(name); commit();
        return previous;
      } }
    });
    replace(initial || '');
    return new Proxy(target, {
      get(target, key, receiver) {
        if (typeof key === 'string' && /^(?:0|[1-9][0-9]*)$/.test(key))
          return names[Number(key)];
        if (typeof key === 'string' && !(key in target)) {
          const name = key === 'cssFloat' ? 'float'
            : key.replace(/[A-Z]/g, character => '-' + character.toLowerCase());
          return values.get(name) || '';
        }
        return Reflect.get(target, key, receiver);
      },
      set(target, key, value, receiver) {
        if (typeof key === 'string' && !(key in target)
            && !/^(?:0|[1-9][0-9]*)$/.test(key)) {
          const name = key === 'cssFloat' ? 'float'
            : key.replace(/[A-Z]/g, character => '-' + character.toLowerCase());
          assign(name, value); commit(); return true;
        }
        return Reflect.set(target, key, value, receiver);
      }
    });
  };
  const makeDeclaration = (state, initial, commit) => {
    if (state.constructed) return makeConstructedDeclaration(initial, commit);
    const declaration = state.document.createElement('span').style;
    declaration.cssText = initial || '';
    return new Proxy(declaration, {
      get(target, key) {
        // WebScene's native declaration exposes item(index), length and named
        // properties, but does not install Web IDL indexed getters yet.
        if (typeof key === 'string' && /^(?:0|[1-9][0-9]*)$/.test(key))
          return declarationNames(target)[Number(key)];
        if (key === 'length') return declarationNames(target).length;
        if (key === 'item') return index => {
          const name = declarationNames(target)[Number(index) >>> 0];
          return name === undefined ? '' : name;
        };
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
  };
  const makeMediaList = (read, write) => {
    const target = {};
    const queries = () => parseMediaList(read());
    const commit = values => write(values.join(', '));
    Object.defineProperties(target, {
      mediaText: {
        enumerable: true,
        get: () => queries().join(', '),
        set: value => commit(parseMediaList(value))
      },
      length: { enumerable: true, get: () => queries().length },
      item: { value(index) { return queries()[Number(index) >>> 0] ?? null; } },
      appendMedium: { value(medium) {
        if (arguments.length === 0) throw new TypeError('A medium is required');
        const parsed = parseMediaList(medium);
        if (parsed.length !== 1 || splitMediaQueries(medium).length !== 1) return;
        const current = queries();
        if (!current.includes(parsed[0])) commit(current.concat(parsed[0]));
      } },
      deleteMedium: { value(medium) {
        if (arguments.length === 0) throw new TypeError('A medium is required');
        const parsed = parseMediaList(medium);
        if (parsed.length !== 1 || splitMediaQueries(medium).length !== 1) return;
        const current = queries();
        const retained = current.filter(query => query !== parsed[0]);
        if (retained.length === current.length)
          exception('The medium was not found', 'NotFoundError');
        commit(retained);
      } },
      toString: { value: () => queries().join(', ') }
    });
    const list = new Proxy(target, {
      get(target, key, receiver) {
        if (typeof key === 'string' && /^(0|[1-9][0-9]*)$/.test(key))
          return queries()[Number(key)];
        return Reflect.get(target, key, receiver);
      },
      set(target, key, value, receiver) {
        if (typeof key === 'string' && /^(0|[1-9][0-9]*)$/.test(key)) return false;
        return Reflect.set(target, key, value, receiver);
      },
      deleteProperty(target, key) {
        return typeof key !== 'string' || !/^(0|[1-9][0-9]*)$/.test(key)
          ? Reflect.deleteProperty(target, key) : false;
      }
    });
    mediaListInstances.add(list);
    return list;
  };
  const publish = state => {
    const source = state.rules.map(rule => rule.cssText).join('\n');
    const nativeSource = state.disabled ? ''
      : state.mediaText.trim() ? `@media ${state.mediaText} {\n${source}\n}`
      : source;
    if (state.constructed) {
      notifyAdopters(state.sheet);
    } else if (typeof state.sheet.__webSceneStageRules === 'function') {
      // CSSOM mutation does not replace the style element's DOM text nodes.
      // Stage the final serialized rule set in native state; synchronous
      // style/layout reads and the browser-task boundary flush it.
      state.sheet.__webSceneStageRules(
        nativeSource, collectLayerNames(state.rules));
    } else {
      state.owner.textContent = nativeSource;
      state.ownerSource = nativeSource;
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
  const collectLayerNames = (rules, inheritedName = '', names = []) => {
    for (const rule of rules) {
      if (layerStatementRuleInstances.has(rule)) {
        for (const name of rule.nameList) {
          const qualified = inheritedName ? `${inheritedName}.${name}` : name;
          if (!names.includes(qualified)) names.push(qualified);
        }
      } else if (layerBlockRuleInstances.has(rule)) {
        const qualified = rule.name
          ? (inheritedName ? `${inheritedName}.${rule.name}` : rule.name)
          : '';
        if (qualified && !names.includes(qualified)) names.push(qualified);
        // Names below an anonymous layer do not join the document's reusable
        // named-layer registry, so only recurse through named layer blocks.
        if (qualified) collectLayerNames(rule.cssRules, qualified, names);
      } else if (groupingRuleInstances.has(rule)) {
        collectLayerNames(rule.cssRules, inheritedName, names);
      } else if (styleRuleInstances.has(rule) && rule.__webSceneNestedRules) {
        collectLayerNames(rule.__webSceneNestedRules, inheritedName, names);
      }
    }
    return names;
  };
  const makeRule = (state, parsed, containingRule = null, nestedStyleContext = false) => {
    let cssText = parsed.cssText;
    let selectorText = parsed.selectorText;
    if (selectorText !== undefined && nestedStyleContext && !selectorText.includes('&')) {
      selectorText = '& ' + selectorText;
      cssText = selectorText + ' {' + (parsed.body || '') + '}';
    }
    let parent = containingRule ? containingRule.parentStyleSheet : state.sheet;
    let style;
    let serializeGroup;
    const mediaMatch = /^@media(?:\s+([^\{]*?))?\s*\{/i.exec(parsed.cssText);
    const supportsMatch = /^@supports(?:\s+([^\{]*?))?\s*\{/i.exec(parsed.cssText);
    const layerBlockMatch = /^@layer(?:\s+([^\{]*?))?\s*\{/i.exec(parsed.cssText);
    const layerStatementNames = parsed.body === undefined
      ? parseLayerStatementNames(parsed.cssText) : undefined;
    const groupingMatch = mediaMatch || supportsMatch || layerBlockMatch;
    const rule = {};
    ruleInstances.add(rule);
    if (mediaMatch) {
      groupingRuleInstances.add(rule);
      conditionRuleInstances.add(rule);
      mediaRuleInstances.add(rule);
      Object.setPrototypeOf(rule, CSSMediaRuleInterface.prototype);
    } else if (supportsMatch) {
      groupingRuleInstances.add(rule);
      conditionRuleInstances.add(rule);
      supportsRuleInstances.add(rule);
      Object.setPrototypeOf(rule, CSSSupportsRuleInterface.prototype);
    } else if (layerBlockMatch) {
      groupingRuleInstances.add(rule);
      layerBlockRuleInstances.add(rule);
      Object.setPrototypeOf(rule, CSSLayerBlockRuleInterface.prototype);
    } else if (layerStatementNames) {
      layerStatementRuleInstances.add(rule);
      Object.setPrototypeOf(rule, CSSLayerStatementRuleInterface.prototype);
      cssText = `@layer ${layerStatementNames.join(', ')};`;
    } else if (parsed.nestedDeclarations) {
      nestedDeclarationsInstances.add(rule);
      Object.setPrototypeOf(rule, CSSNestedDeclarationsInterface.prototype);
    } else if (parsed.selectorText !== undefined) {
      styleRuleInstances.add(rule);
      Object.setPrototypeOf(rule, CSSStyleRuleInterface.prototype);
    }
    Object.defineProperties(rule, {
      cssText: { enumerable: true, get: () => cssText },
      parentStyleSheet: { enumerable: true, get: () => parent },
      parentRule: { enumerable: true, get: () => parent ? containingRule : null }
    });
    if (groupingMatch) {
      let children = splitRules(parsed.body || '').map(child =>
        makeRule(state, child, rule, nestedStyleContext));
      const isMedia = Boolean(mediaMatch);
      const isSupports = Boolean(supportsMatch);
      let preludeText = isMedia
        ? parseMediaList(mediaMatch[1] || '').join(', ')
        : isSupports
          ? (supportsMatch[1] || '').trim()
          : (layerBlockMatch[1] || '').trim();
      const attached = () => parent && (containingRule
        ? containingRule.cssRules && Array.from(containingRule.cssRules).includes(rule)
        : state.rules.includes(rule));
      const serialize = () => {
        const keyword = isMedia ? 'media' : isSupports ? 'supports' : 'layer';
        cssText = `@${keyword}${preludeText ? ' ' + preludeText : ''} {${children.map(child => child.cssText).join('')}}`;
      };
      const commitCondition = value => {
        const normalized = parseMediaList(value).join(', ');
        if (normalized === preludeText) return;
        preludeText = normalized;
        serialize();
        if (parent && containingRule?.__webSceneSerialize)
          containingRule.__webSceneSerialize();
        if (attached()) publish(state);
      };
      serializeGroup = serialize;
      const list = makeList(() => children, () => synchronize(state));
      const descriptors = {
        type: { enumerable: true, value: isMedia ? 4 : isSupports ? 12 : 0 },
        cssRules: { enumerable: true, get: () => list },
        insertRule: { writable: true, value(ruleText, index = 0) {
          if (arguments.length === 0) throw new TypeError('A CSS rule is required');
          synchronize(state);
          index = Number(index) >>> 0;
          if (index > children.length) exception('Rule index is out of bounds', 'IndexSizeError');
          const parsedChildren = splitRules(text(ruleText));
          if (parsedChildren.length !== 1) exception('Exactly one CSS rule is required', 'SyntaxError');
          validateLayerStatement(parsedChildren[0]);
          if (/^@(import|namespace)\b/i.test(parsedChildren[0].cssText))
            exception('Imported and namespace rules are not allowed in grouping rules', 'HierarchyRequestError');
          children.splice(index, 0,
            makeRule(state, parsedChildren[0], rule, nestedStyleContext));
          serialize();
          if (parent && containingRule?.__webSceneSerialize)
            containingRule.__webSceneSerialize();
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
          if (parent && containingRule?.__webSceneSerialize)
            containingRule.__webSceneSerialize();
          if (attached()) publish(state);
        } },
        detach: { value: () => {
          parent = null;
          for (const child of children) child.detach();
        } }
      };
      if (isMedia) {
        const media = makeMediaList(() => preludeText, commitCondition);
        descriptors.conditionText = {
          enumerable: true,
          get: () => preludeText,
          set: value => {
            synchronize(state);
            commitCondition(value);
          }
        };
        descriptors.media = {
          enumerable: true,
          get: () => media,
          set(value) {
            synchronize(state);
            media.mediaText = value;
          }
        };
      } else if (isSupports) {
        descriptors.conditionText = {
          enumerable: true, get: () => preludeText
        };
      } else {
        descriptors.name = { enumerable: true, get: () => preludeText };
      }
      Object.defineProperties(rule, descriptors);
      serialize();
    } else if (layerStatementNames) {
      Object.defineProperties(rule, {
        type: { enumerable: true, value: 0 },
        nameList: {
          enumerable: true,
          get: () => Object.freeze(layerStatementNames.slice())
        },
        detach: { value: () => { parent = null; } }
      });
    } else if (parsed.nestedDeclarations) {
      let declaration;
      const serialize = () => { cssText = declaration.cssText; };
      const attached = () => parent && containingRule
        && Array.from(containingRule.cssRules || []).includes(rule);
      const commit = () => {
        synchronize(state);
        serialize();
        if (containingRule?.__webSceneSerialize)
          containingRule.__webSceneSerialize();
        if (attached()) publish(state);
      };
      Object.defineProperties(rule, {
        type: { enumerable: true, value: 0 },
        style: { enumerable: true, get() {
          if (!declaration)
            declaration = makeDeclaration(state, parsed.body || '', commit);
          return declaration;
        } },
        detach: { value: () => { parent = null; } }
      });
      declaration = makeDeclaration(state, parsed.body || '', commit);
      serialize();
    } else if (parsed.selectorText !== undefined) {
      const contents = splitStyleContents(parsed.body || '');
      let children = contents.rules.map(child => makeRule(state, child, rule, true));
      let declaration;
      const list = makeList(() => children, () => synchronize(state));
      const attached = () => parent && (containingRule
        ? Array.from(containingRule.cssRules || []).includes(rule)
        : state.rules.includes(rule));
      const serialize = () => {
        const declarations = declaration ? declaration.cssText : contents.leading;
        const body = [declarations, ...children.map(child => child.cssText)]
          .filter(Boolean).join(' ');
        cssText = selectorText + ' {' + body + '}';
      };
      const commit = () => {
        synchronize(state);
        serialize();
        if (containingRule?.__webSceneSerialize)
          containingRule.__webSceneSerialize();
        if (attached()) publish(state);
      };
      Object.defineProperties(rule, {
        type: { enumerable: true, value: 1 },
        selectorText: {
          enumerable: true,
          get: () => selectorText,
          set(value) {
            synchronize(state);
            let candidate = text(value).trim();
            if (nestedStyleContext && !candidate.includes('&')) candidate = '& ' + candidate;
            try {
              // Element.matches() and stylesheet parsing share WebScene's
              // native selector parser. CSSOM ignores invalid selectorText
              // assignments instead of surfacing the parser's SyntaxError.
              state.document.createElement('span').matches(
                candidate.replace(/&/g, '*'));
            } catch (error) {
              if (error?.name === 'SyntaxError') return;
              throw error;
            }
            if (candidate === selectorText) return;
            selectorText = candidate;
            commit();
          }
        },
        cssRules: { enumerable: true, get: () => list },
        insertRule: { writable: true, value(ruleText, index = 0) {
          if (arguments.length === 0) throw new TypeError('A CSS rule is required');
          synchronize(state);
          index = Number(index) >>> 0;
          if (index > children.length)
            exception('Rule index is out of bounds', 'IndexSizeError');
          const source = text(ruleText);
          let child;
          try {
            const parsedChildren = splitRules(source);
            if (parsedChildren.length !== 1)
              exception('Exactly one CSS rule is required', 'SyntaxError');
            child = parsedChildren[0];
          } catch (error) {
            if (error?.name !== 'SyntaxError') throw error;
            const probe = state.document.createElement('span').style;
            probe.cssText = source;
            if (!probe.cssText) throw error;
            child = { nestedDeclarations: true, body: source, cssText: source };
          }
          if (/^@(import|namespace)\b/i.test(child.cssText))
            exception('Imported and namespace rules are not allowed in style rules',
              'HierarchyRequestError');
          validateLayerStatement(child);
          children.splice(index, 0, makeRule(state, child, rule, true));
          commit();
          return index;
        } },
        deleteRule: { writable: true, value(index) {
          if (arguments.length === 0) throw new TypeError('A rule index is required');
          synchronize(state);
          index = Number(index) >>> 0;
          if (index >= children.length)
            exception('Rule index is out of bounds', 'IndexSizeError');
          children.splice(index, 1)[0].detach();
          commit();
        } },
        style: { enumerable: true, get() {
          if (style) return style;
          declaration = makeDeclaration(state, contents.leading, commit);
          style = declaration;
          return style;
        } },
        detach: { value: () => {
          parent = null;
          for (const child of children) child.detach();
        } }
      });
      serialize();
      Object.defineProperty(rule, '__webSceneSerialize', {
        value: () => {
          serialize();
          if (containingRule?.__webSceneSerialize)
            containingRule.__webSceneSerialize();
        }
      });
      Object.defineProperty(rule, '__webSceneNestedRules', { value: children });
    } else {
      Object.defineProperty(rule, 'detach', { value: () => { parent = null; } });
    }
    if (groupingMatch) Object.defineProperty(rule, '__webSceneSerialize', {
      value: () => {
        serializeGroup();
        if (containingRule?.__webSceneSerialize) containingRule.__webSceneSerialize();
      }
    });
    return rule;
  };
  const synchronize = state => {
    if (state.constructed) return;
    const source = state.owner.textContent || '';
    if (source === state.ownerSource) return;
    const parsed = splitRules(source);
    for (const rule of state.rules) rule.detach();
    state.rules = parsed.map(rule => makeRule(state, rule));
    state.source = source;
    state.ownerSource = source;
  };
  const replaceConstructedRules = (state, source) => {
    let parsed;
    try {
      parsed = splitRules(source);
    } catch (error) {
      // CSS stylesheet parsing is forgiving. A syntactically unusable
      // replacement produces an empty sheet rather than surfacing SyntaxError.
      if (error?.name !== 'SyntaxError') throw error;
      parsed = [];
    }
    // Constructed-sheet replacement ignores imports and never fetches them.
    parsed = parsed.filter(rule => !/^@import\b/i.test(rule.cssText));
    for (const rule of state.rules) rule.detach();
    state.rules = parsed.map(rule => makeRule(state, rule));
    publish(state);
  };
  const initializeSheet = state => {
    const { sheet } = state;
    const list = makeList(() => state.rules, () => synchronize(state));
    const media = makeMediaList(() => state.mediaText, value => {
      state.mediaText = value;
      if (!state.constructed) {
        if (state.mediaText) state.owner.setAttribute('media', state.mediaText);
        else state.owner.removeAttribute('media');
      }
      publish(state);
    });
    sheets.set(sheet, state);
    styleSheetInstances.add(sheet);
    Object.defineProperties(sheet, {
      cssRules: { configurable: true, enumerable: true, get() { stateFor(this); return list; } },
      rules: { configurable: true, enumerable: true, get() { stateFor(this); return list; } },
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
      replaceSync: {
        configurable: true, writable: true,
        value: function replaceSync(cssText) {
          if (arguments.length === 0)
            throw new TypeError('CSSStyleSheet.replaceSync requires one argument');
          const current = stateFor(this);
          const source = text(cssText);
          // CSSOM replacement is restricted to constructed sheets. Native
          // owner-backed sheets must retain their rules and publication state.
          if (!current.constructed)
            exception('Cannot replace a non-constructed stylesheet', 'NotAllowedError');
          replaceConstructedRules(current, source);
        }
      },
      replace: {
        configurable: true, writable: true,
        value: function replace(cssText) {
          try {
            if (arguments.length === 0)
              throw new TypeError('CSSStyleSheet.replace requires one argument');
            const current = stateFor(this);
            const source = text(cssText);
            if (!current.constructed)
              exception('Cannot replace a non-constructed stylesheet', 'NotAllowedError');
            replaceConstructedRules(current, source);
            return Promise.resolve(this);
          } catch (error) {
            // Promise-returning Web IDL operations report argument conversion
            // and operation errors through a realm-local rejected Promise.
            return Promise.reject(error);
          }
        }
      },
      insertRule: { configurable: true, writable: true, value(rule, index = 0) {
        if (arguments.length === 0) throw new TypeError('A CSS rule is required');
        const current = stateFor(this);
        index = Number(index) >>> 0;
        if (index > current.rules.length) exception('Rule index is out of bounds', 'IndexSizeError');
        const parsed = splitRules(text(rule));
        if (parsed.length !== 1) exception('Exactly one CSS rule is required', 'SyntaxError');
        validateLayerStatement(parsed[0]);
        // Imported sheets cannot be represented by the native owner-text bridge.
        if (/^@import\b/i.test(parsed[0].cssText)) {
          if (current.constructed)
            exception('Constructed stylesheets cannot contain imports', 'SyntaxError');
          exception('Imported rules require native CSSOM support', 'NotSupportedError');
        }
        if (/^@namespace\b/i.test(parsed[0].cssText))
          exception('Namespace rules require native CSSOM support', 'NotSupportedError');
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
  makeConstructedStyleSheet = options => {
    if (options !== undefined && options !== null
        && typeof options !== 'object' && typeof options !== 'function') {
      throw new TypeError('CSSStyleSheet options must be a dictionary');
    }
    const dictionary = options == null ? {} : options;
    const mediaText = dictionary.media === undefined
      ? '' : parseMediaList(dictionary.media).join(', ');
    let baseURL = globalThis.document?.baseURI || globalThis.location?.href || '';
    if (dictionary.baseURL !== undefined) {
      try {
        const candidate = text(dictionary.baseURL);
        // WebScene's general URL shim is intentionally forgiving about an
        // alphabetic port. CSSStyleSheetInit instead requires URL-parser
        // failure to surface as NotAllowedError, matching the pinned WPT.
        const port = /^[a-z][a-z0-9+.-]*:\/\/(?:[^/?#@]*@)?(?:\[[^\]]+\]|[^:/?#]+):([^/?#]*)/i
          .exec(candidate)?.[1];
        if (port !== undefined && port !== ''
            && (!/^[0-9]+$/.test(port) || Number(port) > 65535)) throw new Error();
        baseURL = new URL(candidate, baseURL).href;
      } catch {
        exception('The stylesheet base URL is invalid', 'NotAllowedError');
      }
    }
    const sheet = Object.create(CSSStyleSheetInterface.prototype);
    Object.defineProperties(sheet, {
      ownerNode: { enumerable: true, value: null },
      ownerRule: { enumerable: true, value: null },
      href: { enumerable: true, value: null },
      title: { enumerable: true, value: null },
      type: { enumerable: true, value: 'text/css' }
    });
    return initializeSheet({
      sheet, owner: null, document: globalThis.document,
      source: '', ownerSource: undefined, rules: [],
      disabled: Boolean(dictionary.disabled), mediaText,
      baseURL, constructed: true
    });
  };
  const makeAdoptedStyleSheetList = root => {
    const ownerDocument = root === globalThis.document
      ? root : root.host?.ownerDocument || globalThis.document;
    const backing = [];
    const rootState = {
      root, ownerDocument, list: null, linkedSheets: new WeakSet()
    };
    const validate = sheet => {
      if (!styleSheetInstances.has(sheet)) {
        if (sheet && typeof sheet === 'object'
            && sheet.constructor?.name === 'CSSStyleSheet')
          exception('The stylesheet belongs to another document', 'NotAllowedError');
        throw new TypeError('adoptedStyleSheets entries must be CSSStyleSheet objects');
      }
      const state = sheets.get(sheet);
      if (!state.constructed)
        exception('Only constructed stylesheets may be adopted', 'NotAllowedError');
      if (state.document !== ownerDocument)
        exception('The stylesheet was constructed in another document', 'NotAllowedError');
      return sheet;
    };
    const commit = () => {
      for (const sheet of backing) linkAdopter(sheet, rootState);
      publishAdoptionRoot(rootState);
    };
    const indexKey = key => typeof key === 'string'
      && /^(0|[1-9][0-9]*)$/.test(key);
    let proxy;
    const mutation = (name, prepare) => (...args) => {
      const next = prepare ? prepare(args) : args;
      const result = Array.prototype[name].apply(backing, next);
      commit();
      return name === 'sort' || name === 'reverse' || name === 'copyWithin'
        || name === 'fill' ? proxy : result;
    };
    const mutators = {
      push: mutation('push', args => args.map(validate)),
      unshift: mutation('unshift', args => args.map(validate)),
      pop: mutation('pop'),
      shift: mutation('shift'),
      reverse: mutation('reverse'),
      sort: mutation('sort'),
      copyWithin: mutation('copyWithin'),
      fill: mutation('fill', args => {
        if (args.length) args[0] = validate(args[0]);
        return args;
      }),
      splice: mutation('splice', args => [
        ...args.slice(0, 2), ...args.slice(2).map(validate)
      ])
    };
    proxy = new Proxy(backing, {
      get(target, key, receiver) {
        if (Object.prototype.hasOwnProperty.call(mutators, key)) return mutators[key];
        return Reflect.get(target, key, receiver);
      },
      set(target, key, value) {
        if (key === 'length') {
          const length = Number(value) >>> 0;
          if (length >= backing.length) return true;
          backing.splice(length);
          commit();
          return true;
        }
        if (indexKey(key)) {
          const index = Number(key);
          if (index > backing.length) return true;
          validate(value);
        }
        const changed = Reflect.set(target, key, value, target);
        if (changed) commit();
        return changed;
      },
      defineProperty(target, key, descriptor) {
        if (key === 'length' && 'value' in descriptor) {
          const length = Number(descriptor.value) >>> 0;
          if (length >= backing.length) return true;
          backing.splice(length);
          commit();
          return true;
        }
        if (indexKey(key) && 'value' in descriptor) {
          const index = Number(key);
          if (index > backing.length) return true;
          validate(descriptor.value);
        }
        const changed = Reflect.defineProperty(target, key, descriptor);
        if (changed) commit();
        return changed;
      },
      deleteProperty(target, key) {
        if (indexKey(key)) {
          const index = Number(key);
          if (index === backing.length - 1) {
            backing.pop();
            commit();
          }
          return true;
        }
        const changed = Reflect.deleteProperty(target, key);
        if (changed) commit();
        return changed;
      }
    });
    rootState.list = proxy;
    return { rootState, proxy };
  };
  const rootLists = new WeakMap();
  const adoptionListFor = root => {
    let entry = rootLists.get(root);
    if (!entry) {
      entry = makeAdoptedStyleSheetList(root);
      rootLists.set(root, entry);
    }
    return entry;
  };
  const setAdoptedStyleSheets = function(value) {
    const entry = adoptionListFor(this);
    let replacement;
    try {
      replacement = Array.from(value);
    } catch (error) {
      throw error instanceof TypeError ? error
        : new TypeError('adoptedStyleSheets must be assigned an iterable');
    }
    const validated = replacement.map(sheet => {
      if (!styleSheetInstances.has(sheet)) {
        if (sheet && typeof sheet === 'object'
            && sheet.constructor?.name === 'CSSStyleSheet')
          exception('The stylesheet belongs to another document', 'NotAllowedError');
        throw new TypeError('adoptedStyleSheets entries must be CSSStyleSheet objects');
      }
      const state = sheets.get(sheet);
      if (!state.constructed || state.document !== entry.rootState.ownerDocument)
        exception('The stylesheet cannot be adopted by this root', 'NotAllowedError');
      return sheet;
    });
    entry.proxy.splice(0, entry.proxy.length, ...validated);
  };
  if (globalThis.document) Object.defineProperty(globalThis.document, 'adoptedStyleSheets', {
    configurable: true, enumerable: true,
    get() { return adoptionListFor(this).proxy; },
    set: setAdoptedStyleSheets
  });
  if (globalThis.ShadowRoot?.prototype) Object.defineProperty(
    globalThis.ShadowRoot.prototype, 'adoptedStyleSheets', {
      configurable: true, enumerable: true,
      get() { return adoptionListFor(this).proxy; },
      set: setAdoptedStyleSheets
    });
  const augment = sheet => {
    if (!sheet || typeof sheet.insertRule === 'function') return sheet;
    const document = sheet.ownerNode?.ownerDocument || globalThis.document;
    installInterfaces(document?.defaultView || globalThis);
    return initializeSheet({
      sheet, owner: sheet.ownerNode, document,
      source: undefined, ownerSource: undefined, rules: [], disabled: false,
      mediaText: typeof sheet.ownerNode?.getAttribute === 'function'
        ? sheet.ownerNode.getAttribute('media') || ''
        : '',
      constructed: false
    });
  };
  installInterfaces(globalThis);
  Object.defineProperty(globalThis, '__webSceneAugmentStyleSheet', {
    value: augment, configurable: true
  });
})();
)JS";
}
