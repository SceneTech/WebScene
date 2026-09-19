import 'dart:collection';
import 'dart:convert';
import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:path_drawing/path_drawing.dart';
import 'package:vector_math/vector_math_64.dart';
import 'package:xml/xml.dart';

const int _maxMarkupBytes = 1024 * 1024;
const int _maxElements = 4096;
const int _maxDepth = 64;
const int _maxPathCommands = 65536;
const int _maxUseReferences = 4096;
const int _maxCacheEntries = 256;
const int _maxCacheBytes = 64 * 1024 * 1024;

final class SvgMaskResource {
  const SvgMaskResource({
    required this.picture,
    required this.preserveAspectRatio,
    required this.logicalBytes,
  });

  final ui.Picture picture;
  final String preserveAspectRatio;
  final int logicalBytes;

  void dispose() => picture.dispose();
}

final class SvgMaskResourceCache {
  final LinkedHashMap<String, _CacheEntry> _entries = LinkedHashMap();
  int _logicalBytes = 0;
  int _cacheHits = 0;
  int _parseCount = 0;
  int _parseMicroseconds = 0;

  int get entryCount => _entries.length;
  int get resourceCount =>
      _entries.values.where((entry) => entry.resource != null).length;
  int get failedEntryCount =>
      _entries.values.where((entry) => entry.resource == null).length;
  int get logicalBytes => _logicalBytes;
  int get cacheHits => _cacheHits;
  int get parseCount => _parseCount;
  int get parseMicroseconds => _parseMicroseconds;

  SvgMaskResource? acquire(String markup, ui.Rect viewBox) {
    final key = '${viewBox.left},${viewBox.top},${viewBox.width},'
        '${viewBox.height}\u0000$markup';
    final known = _entries.remove(key);
    if (known != null) {
      _entries[key] = known;
      _cacheHits++;
      return known.resource;
    }

    final stopwatch = Stopwatch()..start();
    SvgMaskResource? resource;
    try {
      resource = _SvgMaskCompiler(markup, viewBox).compile();
    } catch (_) {
      resource = null;
    } finally {
      stopwatch.stop();
      _parseCount++;
      _parseMicroseconds += stopwatch.elapsedMicroseconds;
    }
    final entry = _CacheEntry(
      resource,
      resource?.logicalBytes ?? utf8.encode(key).length,
    );
    _entries[key] = entry;
    _logicalBytes += entry.logicalBytes;
    _trim();
    return identical(_entries[key], entry) ? resource : null;
  }

  void clear() {
    for (final entry in _entries.values) {
      entry.resource?.dispose();
    }
    _entries.clear();
    _logicalBytes = 0;
  }

  void _trim() {
    while (_entries.length > _maxCacheEntries ||
        _logicalBytes > _maxCacheBytes) {
      final key = _entries.keys.first;
      final removed = _entries.remove(key)!;
      _logicalBytes -= removed.logicalBytes;
      removed.resource?.dispose();
    }
  }
}

final class _CacheEntry {
  const _CacheEntry(this.resource, this.logicalBytes);

  final SvgMaskResource? resource;
  final int logicalBytes;
}

final class _SvgMaskCompiler {
  _SvgMaskCompiler(this.markup, this.viewBox);

  final String markup;
  final ui.Rect viewBox;
  final Map<String, XmlElement> _ids = {};
  final List<_CssRule> _rules = [];
  int _elementCount = 0;
  int _pathCommands = 0;
  int _useReferences = 0;
  int _drawOperations = 0;

  SvgMaskResource? compile() {
    final byteLength = utf8.encode(markup).length;
    if (byteLength == 0 || byteLength > _maxMarkupBytes ||
        !viewBox.left.isFinite || !viewBox.top.isFinite ||
        !viewBox.width.isFinite || !viewBox.height.isFinite ||
        viewBox.width <= 0 || viewBox.height <= 0 ||
        RegExp(r'<!DOCTYPE', caseSensitive: false).hasMatch(markup)) {
      return null;
    }
    final document = XmlDocument.parse(markup);
    final root = document.rootElement;
    if (root.name.local.toLowerCase() != 'svg') return null;
    final elements = <XmlElement>[root, ...root.descendantElements];
    if (elements.length > _maxElements) return null;
    _elementCount = elements.length;
    for (final element in elements) {
      final id = element.getAttribute('id');
      if (id != null && id.isNotEmpty) {
        if (_ids.containsKey(id)) return null;
        _ids[id] = element;
      }
      if (element.name.local.toLowerCase() == 'style') {
        if (!_parseStyleSheet(element.innerText)) return null;
      }
    }
    final preserve = _preserveAspectRatio(root.getAttribute('preserveAspectRatio'));
    if (preserve == null) return null;

    final recorder = ui.PictureRecorder();
    final canvas = ui.Canvas(recorder);
    try {
      if (!_renderElement(
        canvas,
        root,
        const _SvgPaintState(),
        0,
        <String>{},
        referenced: false,
      )) {
        recorder.endRecording().dispose();
        return null;
      }
      final picture = recorder.endRecording();
      if (_drawOperations == 0) {
        picture.dispose();
        return null;
      }
      return SvgMaskResource(
        picture: picture,
        preserveAspectRatio: preserve,
        logicalBytes: byteLength + _elementCount * 96 +
            _pathCommands * 32 + _drawOperations * 128,
      );
    } catch (_) {
      recorder.endRecording().dispose();
      return null;
    }
  }

  bool _renderElement(
    ui.Canvas canvas,
    XmlElement element,
    _SvgPaintState inherited,
    int depth,
    Set<String> useStack, {
    required bool referenced,
  }) {
    if (depth > _maxDepth) return false;
    final tag = element.name.local.toLowerCase();
    const containers = {'svg', 'g', 'defs', 'symbol'};
    const shapes = {'path', 'rect', 'circle', 'ellipse', 'polygon', 'polyline', 'line'};
    if (!containers.contains(tag) && !shapes.contains(tag) && tag != 'use' &&
        tag != 'style' && tag != 'title' && tag != 'desc' && tag != 'metadata') {
      return false;
    }
    if (tag == 'style' || tag == 'title' || tag == 'desc' || tag == 'metadata') {
      return true;
    }
    if (tag == 'svg' && depth > 0 ||
        tag == 'symbol' && element.getAttribute('viewBox') != null) {
      return false;
    }
    if ((tag == 'defs' || tag == 'symbol') && !referenced) return true;

    final declarations = _declarationsFor(element);
    if (declarations == null) return false;
    final state = inherited.apply(declarations);
    if (state == null) return false;
    if (state.display == 'none' || state.visibility == 'hidden' ||
        state.visibility == 'collapse') {
      return true;
    }

    canvas.save();
    var opacityLayer = false;
    try {
      final transform = element.getAttribute('transform');
      if (transform != null && !_applyTransform(canvas, transform)) return false;
      if (state.localOpacity < 1) {
        canvas.saveLayer(
          null,
          ui.Paint()..color = ui.Color.fromARGB(
            (state.localOpacity.clamp(0.0, 1.0) * 255).round(),
            255,
            255,
            255,
          ),
        );
        opacityLayer = true;
      }
      if (tag == 'use') {
        if (++_useReferences > _maxUseReferences) return false;
        final href = _href(element);
        if (href == null || !href.startsWith('#') || href.length == 1) return false;
        final id = href.substring(1);
        final target = _ids[id];
        if (target == null || !useStack.add(id)) return false;
        final x = _length(element.getAttribute('x'), viewBox.width, 0);
        final y = _length(element.getAttribute('y'), viewBox.height, 0);
        if (x == null || y == null) return false;
        canvas.translate(x, y);
        final rendered = _renderElement(
          canvas,
          target,
          state.withoutLocalOpacity(),
          depth + 1,
          useStack,
          referenced: true,
        );
        useStack.remove(id);
        return rendered;
      }
      if (shapes.contains(tag)) {
        final path = _shapePath(element, tag);
        if (path == null) return false;
        if (!_drawPath(canvas, path, state, tag == 'line' || tag == 'polyline')) {
          return false;
        }
        return true;
      }
      for (final child in element.childElements) {
        if (!_renderElement(
          canvas,
          child,
          state.withoutLocalOpacity(),
          depth + 1,
          useStack,
          referenced: false,
        )) {
          return false;
        }
      }
      return true;
    } finally {
      if (opacityLayer) canvas.restore();
      canvas.restore();
    }
  }

  ui.Path? _shapePath(XmlElement element, String tag) {
    final path = ui.Path();
    switch (tag) {
      case 'path':
        final data = element.getAttribute('d');
        if (data == null || data.trim().isEmpty) return ui.Path();
        final commands = RegExp(r'[MmZzLlHhVvCcSsQqTtAa]').allMatches(data).length;
        _pathCommands += commands;
        if (_pathCommands > _maxPathCommands) return null;
        path.addPath(parseSvgPathData(data), ui.Offset.zero);
      case 'rect':
        final x = _length(element.getAttribute('x'), viewBox.width, 0);
        final y = _length(element.getAttribute('y'), viewBox.height, 0);
        final width = _length(element.getAttribute('width'), viewBox.width, null);
        final height = _length(element.getAttribute('height'), viewBox.height, null);
        if (x == null || y == null || width == null || height == null ||
            width < 0 || height < 0) return null;
        if (width == 0 || height == 0) return path;
        var rx = _length(element.getAttribute('rx'), viewBox.width, null);
        var ry = _length(element.getAttribute('ry'), viewBox.height, null);
        rx ??= ry ?? 0;
        ry ??= rx;
        if (rx < 0 || ry < 0) return null;
        final rect = ui.Rect.fromLTWH(x, y, width, height);
        if (rx > 0 || ry > 0) {
          path.addRRect(ui.RRect.fromRectXY(
            rect,
            math.min(rx, width / 2),
            math.min(ry, height / 2),
          ));
        } else {
          path.addRect(rect);
        }
      case 'circle':
        final cx = _length(element.getAttribute('cx'), viewBox.width, 0);
        final cy = _length(element.getAttribute('cy'), viewBox.height, 0);
        final r = _length(
          element.getAttribute('r'),
          math.sqrt((viewBox.width * viewBox.width +
                  viewBox.height * viewBox.height) / 2),
          null,
        );
        if (cx == null || cy == null || r == null || r < 0) return null;
        if (r > 0) path.addOval(ui.Rect.fromCircle(center: ui.Offset(cx, cy), radius: r));
      case 'ellipse':
        final cx = _length(element.getAttribute('cx'), viewBox.width, 0);
        final cy = _length(element.getAttribute('cy'), viewBox.height, 0);
        final rx = _length(element.getAttribute('rx'), viewBox.width, null);
        final ry = _length(element.getAttribute('ry'), viewBox.height, null);
        if (cx == null || cy == null || rx == null || ry == null ||
            rx < 0 || ry < 0) return null;
        if (rx > 0 && ry > 0) {
          path.addOval(ui.Rect.fromLTRB(cx - rx, cy - ry, cx + rx, cy + ry));
        }
      case 'polygon':
      case 'polyline':
        final points = _numbers(element.getAttribute('points') ?? '');
        if (points.length.isOdd) return null;
        if (points.length < (tag == 'polygon' ? 6 : 4)) return path;
        path.moveTo(points[0], points[1]);
        for (var index = 2; index < points.length; index += 2) {
          path.lineTo(points[index], points[index + 1]);
        }
        if (tag == 'polygon') path.close();
      case 'line':
        final x1 = _length(element.getAttribute('x1'), viewBox.width, 0);
        final y1 = _length(element.getAttribute('y1'), viewBox.height, 0);
        final x2 = _length(element.getAttribute('x2'), viewBox.width, 0);
        final y2 = _length(element.getAttribute('y2'), viewBox.height, 0);
        if (x1 == null || y1 == null || x2 == null || y2 == null) return null;
        path.moveTo(x1, y1);
        path.lineTo(x2, y2);
    }
    return path;
  }

  bool _drawPath(ui.Canvas canvas, ui.Path path, _SvgPaintState state, bool strokeOnly) {
    if (state.strokeDasharray != null && state.strokeDasharray != 'none') {
      return false;
    }
    if (!strokeOnly && state.fill != 'none') {
      final alpha = _paintAlpha(state.fill, state.color);
      if (alpha == null) return false;
      path.fillType = state.fillRule == 'evenodd'
          ? ui.PathFillType.evenOdd
          : ui.PathFillType.nonZero;
      canvas.drawPath(
        path,
        ui.Paint()
          ..isAntiAlias = true
          ..style = ui.PaintingStyle.fill
          ..color = ui.Color.fromARGB(
            (255 * alpha * state.fillOpacity.clamp(0.0, 1.0)).round(),
            255,
            255,
            255,
          ),
      );
      _drawOperations++;
    }
    if (state.stroke != 'none' && state.strokeWidth > 0) {
      final alpha = _paintAlpha(state.stroke, state.color);
      if (alpha == null) return false;
      canvas.drawPath(
        path,
        ui.Paint()
          ..isAntiAlias = true
          ..style = ui.PaintingStyle.stroke
          ..strokeWidth = state.strokeWidth
          ..strokeCap = switch (state.strokeLinecap) {
            'round' => ui.StrokeCap.round,
            'square' => ui.StrokeCap.square,
            _ => ui.StrokeCap.butt,
          }
          ..strokeJoin = switch (state.strokeLinejoin) {
            'round' => ui.StrokeJoin.round,
            'bevel' => ui.StrokeJoin.bevel,
            _ => ui.StrokeJoin.miter,
          }
          ..strokeMiterLimit = state.strokeMiterlimit
          ..color = ui.Color.fromARGB(
            (255 * alpha * state.strokeOpacity.clamp(0.0, 1.0)).round(),
            255,
            255,
            255,
          ),
      );
      _drawOperations++;
    }
    return true;
  }

  Map<String, String>? _declarationsFor(XmlElement element) {
    final result = <String, String>{};
    for (final name in _SvgPaintState.properties) {
      final value = element.getAttribute(name);
      if (value != null) result[name] = value.trim().toLowerCase();
    }
    final matching = _rules.where((rule) => rule.matches(element)).toList()
      ..sort((left, right) {
        final specificity = left.specificity.compareTo(right.specificity);
        return specificity != 0 ? specificity : left.order.compareTo(right.order);
      });
    for (final rule in matching) {
      result.addAll(rule.declarations);
    }
    final inline = element.getAttribute('style');
    if (inline != null) {
      final declarations = _parseDeclarations(inline);
      if (declarations == null) return null;
      result.addAll(declarations);
    }
    return result;
  }

  bool _parseStyleSheet(String source) {
    if (source.contains('@')) return false;
    final withoutComments = source.replaceAll(RegExp(r'/\*[\s\S]*?\*/'), '');
    var cursor = 0;
    final rules = RegExp(r'([^{}]+)\{([^{}]*)\}').allMatches(withoutComments);
    for (final match in rules) {
      if (withoutComments.substring(cursor, match.start).trim().isNotEmpty) return false;
      cursor = match.end;
      final declarations = _parseDeclarations(match.group(2)!);
      if (declarations == null) return false;
      for (final selector in match.group(1)!.split(',')) {
        final rule = _CssRule.tryParse(
          selector.trim(),
          declarations,
          _rules.length,
        );
        if (rule == null) return false;
        _rules.add(rule);
      }
    }
    return withoutComments.substring(cursor).trim().isEmpty;
  }

  Map<String, String>? _parseDeclarations(String source) {
    final result = <String, String>{};
    for (final declaration in source.split(';')) {
      if (declaration.trim().isEmpty) continue;
      final separator = declaration.indexOf(':');
      if (separator <= 0) return null;
      final name = declaration.substring(0, separator).trim().toLowerCase();
      var value = declaration.substring(separator + 1).trim().toLowerCase();
      value = value.replaceFirst(RegExp(r'\s*!important\s*$'), '');
      if (_SvgPaintState.properties.contains(name)) result[name] = value;
    }
    return result;
  }

  bool _applyTransform(ui.Canvas canvas, String source) {
    final expression = RegExp(r'([a-zA-Z]+)\s*\(([^)]*)\)');
    var cursor = 0;
    for (final match in expression.allMatches(source)) {
      if (source.substring(cursor, match.start).replaceAll(',', '').trim().isNotEmpty) {
        return false;
      }
      cursor = match.end;
      final values = _numbers(match.group(2)!);
      switch (match.group(1)!.toLowerCase()) {
        case 'translate' when values.length == 1 || values.length == 2:
          canvas.translate(values[0], values.length == 2 ? values[1] : 0);
        case 'scale' when values.length == 1 || values.length == 2:
          canvas.scale(values[0], values.length == 2 ? values[1] : values[0]);
        case 'rotate' when values.length == 1 || values.length == 3:
          if (values.length == 3) {
            canvas
              ..translate(values[1], values[2])
              ..rotate(values[0] * math.pi / 180)
              ..translate(-values[1], -values[2]);
          } else {
            canvas.rotate(values[0] * math.pi / 180);
          }
        case 'skewx' when values.length == 1:
          canvas.transform(Matrix4.fromList([
            1, 0, 0, 0,
            math.tan(values[0] * math.pi / 180), 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
          ]).storage);
        case 'skewy' when values.length == 1:
          canvas.transform(Matrix4.fromList([
            1, math.tan(values[0] * math.pi / 180), 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
          ]).storage);
        case 'matrix' when values.length == 6:
          canvas.transform(Matrix4.fromList([
            values[0], values[1], 0, 0,
            values[2], values[3], 0, 0,
            0, 0, 1, 0,
            values[4], values[5], 0, 1,
          ]).storage);
        default:
          return false;
      }
    }
    return source.substring(cursor).replaceAll(',', '').trim().isEmpty;
  }

  String? _href(XmlElement element) {
    final direct = element.getAttribute('href');
    if (direct != null) return direct.trim();
    for (final attribute in element.attributes) {
      if (attribute.name.local == 'href') return attribute.value.trim();
    }
    return null;
  }

  static String? _preserveAspectRatio(String? source) {
    if (source == null || source.trim().isEmpty) return 'xMidYMid meet';
    final tokens = source.trim().split(RegExp(r'\s+'));
    var index = tokens.first.toLowerCase() == 'defer' ? 1 : 0;
    if (index >= tokens.length) return null;
    const alignments = {
      'none',
      'xminymin', 'xmidymin', 'xmaxymin',
      'xminymid', 'xmidymid', 'xmaxymid',
      'xminymax', 'xmidymax', 'xmaxymax',
    };
    if (!alignments.contains(tokens[index].toLowerCase())) return null;
    index++;
    if (index < tokens.length &&
        tokens[index].toLowerCase() != 'meet' &&
        tokens[index].toLowerCase() != 'slice') {
      return null;
    }
    if (++index < tokens.length) return null;
    return source.trim();
  }

  static List<double> _numbers(String source) {
    final values = RegExp(r'[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?')
        .allMatches(source)
        .map((match) => double.parse(match.group(0)!))
        .toList();
    return values.every((value) => value.isFinite) ? values : <double>[];
  }

  static double? _length(String? source, double reference, double? fallback) {
    if (source == null || source.trim().isEmpty) return fallback;
    final value = source.trim().toLowerCase();
    if (value.endsWith('%')) {
      final parsed = double.tryParse(value.substring(0, value.length - 1));
      return parsed == null || !parsed.isFinite ? null : reference * parsed / 100;
    }
    final number = value.endsWith('px')
        ? value.substring(0, value.length - 2)
        : value;
    final parsed = double.tryParse(number);
    return parsed == null || !parsed.isFinite ? null : parsed;
  }

  static double? _paintAlpha(String paint, String color) {
    var value = paint.trim().toLowerCase();
    if (value == 'currentcolor') value = color;
    if (value == 'none' || value == 'transparent') return 0;
    if (value.startsWith('url(') || value.startsWith('var(')) return null;
    if (value.startsWith('rgba(')) {
      final parts = value.substring(5, value.length - 1).split(',');
      if (parts.length != 4) return null;
      final alpha = double.tryParse(parts[3].trim());
      return alpha?.clamp(0.0, 1.0).toDouble();
    }
    if (value.startsWith('#') && (value.length == 5 || value.length == 9)) {
      final digits = value.length == 5
          ? '${value[4]}${value[4]}'
          : value.substring(7, 9);
      final alpha = int.tryParse(digits, radix: 16);
      return alpha == null ? null : alpha / 255;
    }
    return 1;
  }
}

final class _SvgPaintState {
  const _SvgPaintState({
    this.fill = 'black',
    this.stroke = 'none',
    this.color = 'black',
    this.fillOpacity = 1,
    this.strokeOpacity = 1,
    this.localOpacity = 1,
    this.fillRule = 'nonzero',
    this.strokeWidth = 1,
    this.strokeLinecap = 'butt',
    this.strokeLinejoin = 'miter',
    this.strokeMiterlimit = 4,
    this.strokeDasharray,
    this.display = 'inline',
    this.visibility = 'visible',
  });

  static const Set<String> properties = {
    'fill', 'stroke', 'color', 'fill-opacity', 'stroke-opacity', 'opacity',
    'fill-rule', 'stroke-width', 'stroke-linecap', 'stroke-linejoin',
    'stroke-miterlimit', 'stroke-dasharray', 'display', 'visibility',
  };

  final String fill;
  final String stroke;
  final String color;
  final double fillOpacity;
  final double strokeOpacity;
  final double localOpacity;
  final String fillRule;
  final double strokeWidth;
  final String strokeLinecap;
  final String strokeLinejoin;
  final double strokeMiterlimit;
  final String? strokeDasharray;
  final String display;
  final String visibility;

  _SvgPaintState? apply(Map<String, String> declarations) {
    double? number(String name, double fallback) {
      final value = declarations[name];
      if (value == null) return fallback;
      if (value.endsWith('%')) {
        final parsed = double.tryParse(value.substring(0, value.length - 1));
        return parsed == null ? null : parsed / 100;
      }
      final source = value.endsWith('px')
          ? value.substring(0, value.length - 2)
          : value;
      return double.tryParse(source);
    }
    final nextFillOpacity = number('fill-opacity', fillOpacity);
    final nextStrokeOpacity = number('stroke-opacity', strokeOpacity);
    final nextOpacity = number('opacity', 1);
    final nextStrokeWidth = number('stroke-width', strokeWidth);
    final nextMiter = number('stroke-miterlimit', strokeMiterlimit);
    if (nextFillOpacity == null || nextStrokeOpacity == null ||
        nextOpacity == null || nextStrokeWidth == null || nextMiter == null ||
        !nextFillOpacity.isFinite || !nextStrokeOpacity.isFinite ||
        !nextOpacity.isFinite || !nextStrokeWidth.isFinite ||
        !nextMiter.isFinite || nextStrokeWidth < 0 || nextMiter < 0) {
      return null;
    }
    final nextFillRule = declarations['fill-rule'] ?? fillRule;
    final nextCap = declarations['stroke-linecap'] ?? strokeLinecap;
    final nextJoin = declarations['stroke-linejoin'] ?? strokeLinejoin;
    if (nextFillRule != 'nonzero' && nextFillRule != 'evenodd' ||
        nextCap != 'butt' && nextCap != 'round' && nextCap != 'square' ||
        nextJoin != 'miter' && nextJoin != 'round' && nextJoin != 'bevel') {
      return null;
    }
    return _SvgPaintState(
      fill: declarations['fill'] ?? fill,
      stroke: declarations['stroke'] ?? stroke,
      color: declarations['color'] ?? color,
      fillOpacity: nextFillOpacity,
      strokeOpacity: nextStrokeOpacity,
      localOpacity: nextOpacity,
      fillRule: nextFillRule,
      strokeWidth: nextStrokeWidth,
      strokeLinecap: nextCap,
      strokeLinejoin: nextJoin,
      strokeMiterlimit: nextMiter,
      strokeDasharray: declarations['stroke-dasharray'] ?? strokeDasharray,
      display: declarations['display'] ?? display,
      visibility: declarations['visibility'] ?? visibility,
    );
  }

  _SvgPaintState withoutLocalOpacity() => _SvgPaintState(
    fill: fill,
    stroke: stroke,
    color: color,
    fillOpacity: fillOpacity,
    strokeOpacity: strokeOpacity,
    fillRule: fillRule,
    strokeWidth: strokeWidth,
    strokeLinecap: strokeLinecap,
    strokeLinejoin: strokeLinejoin,
    strokeMiterlimit: strokeMiterlimit,
    strokeDasharray: strokeDasharray,
    display: display,
    visibility: visibility,
  );
}

final class _CssRule {
  const _CssRule(
    this.tag,
    this.id,
    this.classes,
    this.declarations,
    this.specificity,
    this.order,
  );

  final String? tag;
  final String? id;
  final Set<String> classes;
  final Map<String, String> declarations;
  final int specificity;
  final int order;

  static _CssRule? tryParse(
    String selector,
    Map<String, String> declarations,
    int order,
  ) {
    if (selector == '*') {
      return _CssRule(null, null, const {}, declarations, 0, order);
    }
    final match = RegExp(
      r'^([a-zA-Z][\w-]*)?(#[\w-]+)?((?:\.[\w-]+)*)$',
    ).firstMatch(selector);
    if (match == null) return null;
    final tag = match.group(1)?.toLowerCase();
    final id = match.group(2)?.substring(1);
    final classSource = match.group(3) ?? '';
    final classes = RegExp(r'\.([\w-]+)')
        .allMatches(classSource)
        .map((item) => item.group(1)!)
        .toSet();
    if (tag == null && id == null && classes.isEmpty) return null;
    return _CssRule(
      tag,
      id,
      classes,
      declarations,
      (id == null ? 0 : 100) + classes.length * 10 + (tag == null ? 0 : 1),
      order,
    );
  }

  bool matches(XmlElement element) {
    if (tag != null && element.name.local.toLowerCase() != tag) return false;
    if (id != null && element.getAttribute('id') != id) return false;
    final elementClasses = (element.getAttribute('class') ?? '')
        .split(RegExp(r'\s+'))
        .where((item) => item.isNotEmpty)
        .toSet();
    return elementClasses.containsAll(classes);
  }
}
