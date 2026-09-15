#pragma once
#include <string_view>

namespace webscene_native {
// The pinned runtime does not expose Performance.getEntriesByType. Code OSS
// calls it while constructing its lifecycle service, before rendering any UI.
// Supply real User Timing entries while preserving the native monotonic clock
// and its coordinate system shared with animation frames. This bounded adapter
// does not claim navigation/resource timing or PerformanceObserver support.
// Semantics: https://www.w3.org/TR/user-timing/
inline constexpr std::string_view browserCompatibilityScript = R"JS(
// SCENETECH_BROWSER_COMPATIBILITY_V1
(() => {
  'use strict';
  const performance = globalThis.performance;
  if (!performance || typeof performance.getEntriesByType === 'function') return;
  if (typeof performance.now !== 'function') throw new Error('The native monotonic performance clock is unavailable');
  const now = performance.now.bind(performance);
  const maximumEntries = 2048;
  let entries = [];
  const timingNames = new Set([
    'navigationStart', 'unloadEventStart', 'unloadEventEnd', 'redirectStart', 'redirectEnd',
    'fetchStart', 'domainLookupStart', 'domainLookupEnd', 'connectStart', 'connectEnd',
    'secureConnectionStart', 'requestStart', 'responseStart', 'responseEnd', 'domLoading',
    'domInteractive', 'domContentLoadedEventStart', 'domContentLoadedEventEnd',
    'domComplete', 'loadEventStart', 'loadEventEnd'
  ]);
  const text = value => {
    if (typeof value === 'symbol') throw new TypeError('A performance name cannot be a Symbol');
    return String(value);
  };
  const timestamp = value => {
    const number = Number(value);
    if (!Number.isFinite(number) || number < 0) throw new TypeError('Performance timestamps must be finite and nonnegative');
    return number;
  };
  const detail = options => options.detail === undefined || options.detail === null
    ? null : structuredClone(options.detail);
  const resolveMark = value => {
    if (typeof value !== 'string') return timestamp(value);
    if (value === 'navigationStart') return 0;
    if (timingNames.has(value)) throw new DOMException('Navigation timing is unavailable', 'InvalidAccessError');
    for (let index = entries.length - 1; index >= 0; --index) {
      const entry = entries[index];
      if (entry.entryType === 'mark' && entry.name === value) return entry.startTime;
    }
    throw new DOMException('The performance mark does not exist: ' + value, 'SyntaxError');
  };
  const retain = (name, entryType, startTime, duration, value) => {
    const entry = Object.freeze({
      name, entryType, startTime, duration, detail: value,
      toJSON() { return { name, entryType, startTime, duration, detail: value }; }
    });
    if (entries.length === maximumEntries) entries.shift();
    entries.push(entry);
    return entry;
  };
  const query = predicate => entries.filter(predicate).sort((left, right) => left.startTime - right.startTime);
  const methods = {
    mark(name, options = {}) {
      if (arguments.length === 0) throw new TypeError('A mark name is required');
      name = text(name);
      if (timingNames.has(name)) throw new DOMException('The mark name is reserved for navigation timing', 'SyntaxError');
      options = options == null ? {} : Object(options);
      const startTime = options.startTime === undefined ? now() : timestamp(options.startTime);
      return retain(name, 'mark', startTime, 0, detail(options));
    },
    measure(name, startOrOptions = {}, endMark = undefined) {
      if (arguments.length === 0) throw new TypeError('A measure name is required');
      name = text(name);
      const dictionary = startOrOptions === null || typeof startOrOptions === 'object' || typeof startOrOptions === 'function';
      const options = dictionary ? (startOrOptions || {}) : {};
      const hasStart = options.start !== undefined;
      const hasEnd = options.end !== undefined;
      const hasDuration = options.duration !== undefined;
      const hasDetail = options.detail !== undefined;
      if (hasStart || hasEnd || hasDuration || hasDetail) {
        if (endMark !== undefined || (!hasStart && !hasEnd) || (hasStart && hasEnd && hasDuration))
          throw new TypeError('Invalid performance measure options');
      }
      const start = hasStart ? resolveMark(options.start) : undefined;
      const end = hasEnd ? resolveMark(options.end) : undefined;
      const duration = hasDuration ? timestamp(options.duration) : undefined;
      const endTime = endMark !== undefined ? resolveMark(text(endMark))
        : hasEnd ? end : hasStart && hasDuration ? start + duration : now();
      const startTime = hasStart ? start : hasEnd && hasDuration ? end - duration
        : dictionary ? 0 : resolveMark(text(startOrOptions));
      return retain(name, 'measure', startTime, endTime - startTime, detail(options));
    },
    clearMarks(name = undefined) {
      const match = name === undefined ? undefined : text(name);
      entries = entries.filter(entry => entry.entryType !== 'mark' || (match !== undefined && entry.name !== match));
    },
    clearMeasures(name = undefined) {
      const match = name === undefined ? undefined : text(name);
      entries = entries.filter(entry => entry.entryType !== 'measure' || (match !== undefined && entry.name !== match));
    },
    getEntries() { return query(() => true); },
    getEntriesByType(type) {
      if (arguments.length === 0) throw new TypeError('An entry type is required');
      type = text(type);
      return query(entry => entry.entryType === type);
    },
    getEntriesByName(name, type = undefined) {
      if (arguments.length === 0) throw new TypeError('An entry name is required');
      name = text(name);
      type = type === undefined ? undefined : text(type);
      return query(entry => entry.name === name && (type === undefined || entry.entryType === type));
    }
  };
  for (const [name, value] of Object.entries(methods)) {
    Object.defineProperty(performance, name, { value, writable: true, configurable: true });
  }
})();
)JS";
}
