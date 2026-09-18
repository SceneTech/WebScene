import 'dart:async';
import 'dart:convert';
import 'dart:ffi';
import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/foundation.dart';
import 'package:flutter/painting.dart';
import 'package:flutter_svg/flutter_svg.dart';
import 'package:path_drawing/path_drawing.dart';
import 'package:vector_math/vector_math_64.dart';

import 'native_bindings.dart';

const int _sceneCheckpoint = 1;
const int _sceneDomReplacement = 2;
const int _sceneComponentReady = 4;
const int _layerReplace = 1;
const int _layerRemove = 2;
const int _canvasEvenOdd = 1 << 16;
const int _domPolygonClipResource = 1 << 31;
const int _domClipEvenOdd = 1 << 30;
const int _domClipRelativePath = 1 << 29;
const int _domClipObjectBoundingBox = 1 << 28;
const int _domPolygonClipIndexMask = _domClipObjectBoundingBox - 1;
const int _domBrightnessFilter = 1 << 31;
const int _domGrayscaleFilter = 1 << 30;
const int _domContrastFilter = 1 << 29;
const int _domBlurFilter = 1 << 28;
const int _domSaturateFilter = 1 << 27;
const int _domLinearMaskGroup = 1 << 26;
const int _domColorFilterMask =
    _domBrightnessFilter | _domGrayscaleFilter | _domContrastFilter |
    _domSaturateFilter;
const int _domEffectFilterMask =
    _domColorFilterMask | _domBlurFilter | _domLinearMaskGroup;

final class SceneApplyResult {
  const SceneApplyResult({
    required this.accepted,
    required this.ready,
    required this.revision,
  });

  final bool accepted;
  final bool ready;
  final int revision;
}

final class WebSceneSceneProjector extends ChangeNotifier {
  final Map<int, _RetainedLayer> _layers = {};
  final Map<String, _SvgPictureEntry> _svgPictures = {};
  final Map<String, _SvgMaskGeometry?> _svgMaskGeometry = {};
  final List<_DomSvgPlacement> _domSvgPlacements = [];
  ui.Picture? _backdrop;
  ui.Picture? _overlay;
  int _revision = 0;
  bool _disposed = false;
  double viewportWidth = 0;
  double viewportHeight = 0;

  int get revision => _revision;

  @visibleForTesting
  int get fullSvgPlacementCount => _domSvgPlacements.length;

  @visibleForTesting
  int get cachedSvgPictureCount =>
      _svgPictures.values.where((entry) => entry.picture != null).length;

  @visibleForTesting
  int get failedSvgPictureCount =>
      _svgPictures.values.where((entry) => entry.error != null).length;

  @visibleForTesting
  Future<void> waitForPendingSvgLoads() => Future.wait(
        _svgPictures.values
            .map((entry) => entry.pending)
            .whereType<Future<void>>(),
      );

  SceneApplyResult apply(Pointer<WebSceneSceneView> scenePointer) {
    if (!isValidScene(scenePointer)) {
      return SceneApplyResult(
        accepted: false,
        ready: false,
        revision:
            scenePointer == nullptr ? 0 : scenePointer.ref.header.revision,
      );
    }

    final scene = scenePointer.ref;
    final header = scene.header;
    final checkpoint = header.flags & _sceneCheckpoint != 0;
    if (checkpoint) {
      reset();
    } else if (header.revision != _revision &&
        header.baseRevision != _revision) {
      return SceneApplyResult(
        accepted: false,
        ready: false,
        revision: header.revision,
      );
    }

    if (header.revision != _revision) {
      if (header.flags & _sceneDomReplacement != 0) {
        _backdrop?.dispose();
        _overlay?.dispose();
        _backdrop = _compileDom(scene, foreground: false);
        _overlay = _compileDom(scene, foreground: true);
      }

      for (var index = 0; index < header.canvasLayerCount; index++) {
        final change = scene.canvasLayers[index];
        if (change.flags & _layerRemove != 0) {
          _layers.remove(change.nodeId)?.dispose();
          continue;
        }
        if (change.flags & _layerReplace == 0 ||
            !_validateLayer(scene, change)) {
          return SceneApplyResult(
            accepted: false,
            ready: false,
            revision: header.revision,
          );
        }
        final replacement = _compileLayer(scene, change);
        _layers.remove(change.nodeId)?.dispose();
        _layers[change.nodeId] = replacement;
      }
      _revision = header.revision;
      viewportWidth = header.viewportWidth;
      viewportHeight = header.viewportHeight;
    }

    return SceneApplyResult(
      accepted: true,
      ready: header.flags & _sceneComponentReady != 0,
      revision: header.revision,
    );
  }

  @visibleForTesting
  static bool isValidScene(Pointer<WebSceneSceneView> scenePointer) {
    if (scenePointer == nullptr) return false;
    final scene = scenePointer.ref;
    return scene.structSize == sizeOf<WebSceneSceneView>() &&
        scene.abiVersion == 2 &&
        (scene.header.commandCount == 0 || scene.commands != nullptr) &&
        (scene.header.canvasLayerCount == 0 || scene.canvasLayers != nullptr) &&
        // Removal-only canvas diffs legitimately contain layer records but no
        // canvas command payload. Validate every optional buffer against its
        // own count rather than coupling commands to the layer count.
        (scene.canvasCommandCount == 0 || scene.canvasCommands != nullptr) &&
        (scene.stringCount == 0 || scene.strings != nullptr) &&
        (scene.stringByteCount == 0 || scene.stringBytes != nullptr);
  }

  void paint(ui.Canvas canvas, ui.Size size) {
    final sourceWidth = viewportWidth <= 0 ? size.width : viewportWidth;
    final sourceHeight = viewportHeight <= 0 ? size.height : viewportHeight;
    final scaleX = sourceWidth <= 0 ? 1.0 : size.width / sourceWidth;
    final scaleY = sourceHeight <= 0 ? 1.0 : size.height / sourceHeight;
    canvas.save();
    canvas.scale(scaleX, scaleY);
    final backdrop = _backdrop;
    if (backdrop != null) canvas.drawPicture(backdrop);

    final ordered = _layers.values.toList()
      ..sort((left, right) => left.zOrder.compareTo(right.zOrder));
    for (final layer in ordered) {
      if (layer.width <= 0 ||
          layer.height <= 0 ||
          layer.bitmapWidth <= 0 ||
          layer.bitmapHeight <= 0) {
        continue;
      }
      final bounds = ui.Rect.fromLTWH(
        layer.x,
        layer.y,
        layer.width,
        layer.height,
      );
      canvas
        ..save()
        ..clipRect(bounds)
        ..saveLayer(bounds, ui.Paint())
        ..translate(layer.x, layer.y)
        ..scale(
          layer.width / layer.bitmapWidth,
          layer.height / layer.bitmapHeight,
        )
        ..drawPicture(layer.picture)
        ..restore()
        ..restore();
    }
    final overlay = _overlay;
    if (overlay != null) canvas.drawPicture(overlay);
    _drawDomSvgPictures(canvas);
    canvas.restore();
  }

  void reset() {
    _backdrop?.dispose();
    _overlay?.dispose();
    _backdrop = null;
    _overlay = null;
    _domSvgPlacements.clear();
    _svgMaskGeometry.clear();
    for (final layer in _layers.values) {
      layer.dispose();
    }
    _layers.clear();
    _revision = 0;
    viewportWidth = 0;
    viewportHeight = 0;
  }

  @override
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    reset();
    for (final entry in _svgPictures.values) {
      entry.picture?.dispose();
    }
    _svgPictures.clear();
    super.dispose();
  }

  bool _validateLayer(WebSceneSceneView scene, WebSceneCanvasLayer layer) =>
      layer.commandOffset <= scene.canvasCommandCount &&
      layer.commandCount <= scene.canvasCommandCount - layer.commandOffset &&
      layer.stringOffset <= scene.stringCount &&
      layer.stringCount <= scene.stringCount - layer.stringOffset;

  ui.Picture _compileDom(WebSceneSceneView scene, {required bool foreground}) {
    if (foreground) _domSvgPlacements.clear();
    final recorder = ui.PictureRecorder();
    final canvas = ui.Canvas(
      recorder,
      ui.Rect.fromLTWH(
        0,
        0,
        scene.header.viewportWidth.clamp(1, double.infinity),
        scene.header.viewportHeight.clamp(1, double.infinity),
      ),
    );
    for (var index = 0; index < scene.header.commandCount; index++) {
      final command = scene.commands[index];
      switch (command.kind) {
        case 30:
          canvas.saveLayer(null, _domGroupPaint(command));
        case 31:
          canvas.restore();
        case 47:
          _drawDomMask(canvas, scene, command);
        case 15:
          canvas
            ..save()
            ..translate(command.x, command.y)
            ..scale(command.width, command.height)
            ..translate(-command.x, -command.y);
        case 16:
          canvas.restore();
        case 19:
          canvas
            ..save()
            ..translate(command.x, command.y)
            ..rotate(command.strokeWidth * 3.141592653589793 / 180)
            ..translate(-command.x, -command.y);
        case 20:
          canvas.restore();
        case 17 when !foreground:
        case 18 when foreground:
          _drawDomShadow(canvas, command);
        case 1 when !foreground:
        case 9 when foreground:
          canvas.drawRect(
            _domRect(command),
            ui.Paint()
              ..isAntiAlias = false
              ..color = _rgba(command.rgba),
          );
        case 2 when !foreground:
        case 14 when foreground:
          canvas.drawLine(
            ui.Offset(command.x, command.y),
            ui.Offset(command.width, command.height),
            ui.Paint()
              ..isAntiAlias = true
              ..style = ui.PaintingStyle.stroke
              ..strokeWidth = (command.flags / 100).clamp(0.1, double.infinity)
              ..color = _rgba(command.rgba),
          );
        case 3 when foreground:
          _drawDomText(canvas, scene, command);
        case 4 when foreground:
        case 5 when foreground:
          _drawDomSvgPath(canvas, scene, command, stroke: command.kind == 5);
        case 6 when foreground:
          _retainDomSvg(scene, command);
        case 7 when !foreground:
        case 10 when foreground:
          canvas.drawRRect(
            _domRRect(command),
            ui.Paint()
              ..isAntiAlias = true
              ..color = _rgba(command.rgba),
          );
        case 8 when !foreground:
        case 11 when foreground:
          canvas.drawRRect(
            _domRRect(command),
            ui.Paint()
              ..isAntiAlias = true
              ..style = ui.PaintingStyle.stroke
              ..strokeWidth = (command.strokeWidth > 0
                      ? command.strokeWidth
                      : (command.flags & 0xffff) / 100)
                  .clamp(0.1, double.infinity)
              ..color = _rgba(command.rgba),
          );
        case 12:
          canvas.save();
          _clipDomShape(canvas, scene, command);
        case 13:
          canvas.restore();
      }
    }
    return recorder.endRecording();
  }

  void _retainDomSvg(
    WebSceneSceneView scene,
    WebSceneSceneCommand command,
  ) {
    final resource = _domString(scene, command.flags);
    final separator = resource.indexOf('\t');
    if (separator <= 0 || separator == resource.length - 1) return;
    final viewBox = _numbers(resource.substring(0, separator));
    if (viewBox.length < 4 ||
        viewBox[2] == 0 ||
        viewBox[3] == 0 ||
        command.width <= 0 ||
        command.height <= 0) {
      return;
    }
    final markup = resource.substring(separator + 1);
    _domSvgPlacements.add(
      _DomSvgPlacement(
        markup: markup,
        viewBox: ui.Rect.fromLTWH(
          viewBox[0],
          viewBox[1],
          viewBox[2],
          viewBox[3],
        ),
        x: command.x,
        y: command.y,
        width: command.width,
        height: command.height,
        rotationDegrees: command.strokeWidth,
      ),
    );
    _ensureSvgPicture(markup);
  }

  void _ensureSvgPicture(String markup) {
    final entry = _svgPictures.putIfAbsent(markup, _SvgPictureEntry.new);
    if (entry.pending != null || entry.picture != null || entry.error != null) {
      return;
    }
    entry.pending = _loadSvgPicture(markup, entry);
  }

  Future<void> _loadSvgPicture(
    String markup,
    _SvgPictureEntry entry,
  ) async {
    try {
      final picture = await vg.loadPicture(
        SvgStringLoader(markup),
        null,
        clipViewbox: false,
      );
      if (_disposed) {
        picture.picture.dispose();
        return;
      }
      entry.picture = picture.picture;
      notifyListeners();
    } catch (error) {
      entry.error = error;
    }
  }

  void _drawDomSvgPictures(ui.Canvas canvas) {
    for (final placement in _domSvgPlacements) {
      final picture = _svgPictures[placement.markup]?.picture;
      if (picture == null) continue;
      canvas.save();
      if (placement.rotationDegrees.abs() >= 0.001) {
        final centerX = placement.x + placement.width / 2;
        final centerY = placement.y + placement.height / 2;
        canvas
          ..translate(centerX, centerY)
          ..rotate(placement.rotationDegrees * 3.141592653589793 / 180)
          ..translate(-centerX, -centerY);
      }
      canvas
        ..translate(placement.x, placement.y)
        ..scale(
          placement.width / placement.viewBox.width,
          placement.height / placement.viewBox.height,
        )
        ..translate(-placement.viewBox.left, -placement.viewBox.top)
        ..drawPicture(picture)
        ..restore();
    }
  }

  void _drawDomText(
    ui.Canvas canvas,
    WebSceneSceneView scene,
    WebSceneSceneCommand command,
  ) {
    final parts = _domString(scene, command.flags).split('\t');
    if (parts.length != 6 && parts.length != 7) return;
    final text = parts.length == 7 ? parts[6] : parts[5];
    final fontSize = double.tryParse(parts[0]);
    if (fontSize == null || fontSize <= 0) return;
    final parsedLineHeight = double.tryParse(parts[1]) ?? 0;
    final lineHeight = parsedLineHeight > 0 ? parsedLineHeight : fontSize * 1.2;
    final weight = (int.tryParse(parts[2]) ?? 400).clamp(1, 1000);
    final family = _firstFontFamily(parts[4]);
    final painter = TextPainter(
      text: TextSpan(
        text: text,
        style: TextStyle(
          color: _rgba(command.rgba),
          fontFamily: family,
          fontSize: fontSize,
          fontWeight: _fontWeight(weight),
          height: lineHeight / fontSize,
        ),
      ),
      textAlign: switch (parts[3]) {
        'center' => TextAlign.center,
        'right' || 'end' => TextAlign.right,
        _ => TextAlign.left,
      },
      textDirection: TextDirection.ltr,
      maxLines: 1,
    )..layout(maxWidth: command.width.clamp(0, double.infinity));
    final top = domTextPaintTop(
      boxTop: command.y,
      boxHeight: command.height,
      paintedLineHeight: painter.height,
      hasExplicitLineHeight: parsedLineHeight > 0,
    );
    painter.paint(canvas, ui.Offset(command.x, top));
    painter.dispose();
  }

  @visibleForTesting
  static double domTextPaintTop({
    required double boxTop,
    required double boxHeight,
    required double paintedLineHeight,
    required bool hasExplicitLineHeight,
  }) =>
      boxTop +
      ((boxHeight - paintedLineHeight) / 2).clamp(0, double.infinity) +
      (hasExplicitLineHeight ? 0 : 3);

  void _drawDomSvgPath(
    ui.Canvas canvas,
    WebSceneSceneView scene,
    WebSceneSceneCommand command, {
    required bool stroke,
  }) {
    final parts = _domString(scene, command.flags).split('\t');
    if (parts.length != 4 || command.width <= 0 || command.height <= 0) return;
    final viewBox = _numbers(parts[0]);
    if (viewBox.length < 4 || viewBox[2] == 0 || viewBox[3] == 0) return;
    try {
      final path = parseSvgPathData(parts[3]);
      canvas.save();
      _applyDomRotation(canvas, command);
      canvas
        ..translate(command.x, command.y)
        ..scale(command.width / viewBox[2], command.height / viewBox[3])
        ..translate(-viewBox[0], -viewBox[1]);
      _applySvgTransform(canvas, parts[2]);
      canvas.drawPath(
        path,
        ui.Paint()
          ..isAntiAlias = true
          ..style = stroke ? ui.PaintingStyle.stroke : ui.PaintingStyle.fill
          ..strokeWidth =
              stroke ? (double.tryParse(parts[1]) ?? 1).clamp(0.1, 1000) : 1
          ..color = _rgba(command.rgba),
      );
      canvas.restore();
    } on FormatException {
      // A malformed optional icon must not reject the containing scene.
    }
  }

  void _drawDomShadow(ui.Canvas canvas, WebSceneSceneCommand command) {
    final paint = ui.Paint()
      ..isAntiAlias = true
      ..color = _rgba(command.rgba);
    if (command.strokeWidth > 0) {
      paint.maskFilter = ui.MaskFilter.blur(
        ui.BlurStyle.normal,
        (command.strokeWidth * 0.5).clamp(0.1, double.infinity),
      );
    }
    canvas.drawRRect(_domRRect(command), paint);
  }

  _RetainedLayer _compileLayer(
      WebSceneSceneView scene, WebSceneCanvasLayer layer) {
    final recorder = ui.PictureRecorder();
    final canvas = ui.Canvas(
      recorder,
      ui.Rect.fromLTWH(
        0,
        0,
        layer.bitmapWidth.clamp(1, 16384).toDouble(),
        layer.bitmapHeight.clamp(1, 16384).toDouble(),
      ),
    );
    _replayCanvas(canvas, scene, layer);
    return _RetainedLayer(
      nodeId: layer.nodeId,
      generation: layer.generation,
      zOrder: layer.zOrder,
      x: layer.x,
      y: layer.y,
      width: layer.width,
      height: layer.height,
      bitmapWidth: layer.bitmapWidth.toDouble(),
      bitmapHeight: layer.bitmapHeight.toDouble(),
      picture: recorder.endRecording(),
    );
  }

  void _replayCanvas(
    ui.Canvas canvas,
    WebSceneSceneView scene,
    WebSceneCanvasLayer layer,
  ) {
    var state = _CanvasState.defaults();
    final states = <_CanvasState>[];
    var path = ui.Path();
    for (var index = 0; index < layer.commandCount; index++) {
      final command = scene.canvasCommands[layer.commandOffset + index];
      final values = command.values;
      switch (command.kind) {
        case 1:
          states.add(state.copy());
          canvas.save();
        case 2:
          if (states.isNotEmpty) {
            state = states.removeLast();
            canvas.restore();
          }
        case 3:
          _replaceTransform(canvas, Matrix4.identity());
        case 4:
          _replaceTransform(canvas, _matrix(command));
        case 5:
          canvas.transform(_matrix(command).storage);
        case 6:
          canvas.translate(values[0], values[1]);
        case 7:
          canvas.scale(values[0], values[1]);
        case 8:
          canvas.rotate(values[0]);
        case 9:
          path = ui.Path();
        case 10:
          path.close();
        case 11:
          path.moveTo(values[0], values[1]);
        case 12:
          path.lineTo(values[0], values[1]);
        case 13:
          path.cubicTo(
            values[0],
            values[1],
            values[2],
            values[3],
            values[4],
            values[5],
          );
        case 14:
          path.quadraticBezierTo(values[0], values[1], values[2], values[3]);
        case 15:
          _appendArc(path, command);
        case 16:
          path.arcToPoint(
            ui.Offset(values[2], values[3]),
            radius: ui.Radius.circular(values[4].abs()),
          );
        case 17:
          path.addRect(_canvasRect(command));
        case 18:
          canvas.clipPath(path, doAntiAlias: true);
        case 19:
          state.lineDash = [
            for (var dash = 1; dash <= values[0].round().clamp(0, 7); dash++)
              values[dash],
          ];
        case 20:
          canvas.drawPath(path, _paint(state, fill: false));
        case 21:
          path.fillType = command.flags & _canvasEvenOdd != 0
              ? ui.PathFillType.evenOdd
              : ui.PathFillType.nonZero;
          canvas.drawPath(path, _paint(state, fill: true));
        case 22:
          canvas.drawRect(_canvasRect(command), _paint(state, fill: true));
        case 23:
          canvas.drawRect(_canvasRect(command), _paint(state, fill: false));
        case 24:
          canvas.drawRect(
            _canvasRect(command),
            ui.Paint()..blendMode = ui.BlendMode.clear,
          );
        case 25:
          _drawCanvasText(canvas, scene, layer, command, state);
        case 26:
          _drawCanvasText(canvas, scene, layer, command, state, stroke: true);
        case 27:
          _drawCanvasLayer(canvas, command, state);
        case 28:
          _drawCanvasSvgPath(canvas, scene, layer, command, state, fill: true);
        case 29:
          _drawCanvasSvgPath(canvas, scene, layer, command, state, fill: false);
        case 40:
          state.fillStyle = _layerString(scene, layer, command.resourceId);
        case 41:
          state.strokeStyle = _layerString(scene, layer, command.resourceId);
        case 42:
          state.lineWidth = values[0];
        case 43:
          state.lineCap = _layerString(scene, layer, command.resourceId);
        case 44:
          state.lineJoin = _layerString(scene, layer, command.resourceId);
        case 45:
          state.miterLimit = values[0];
        case 46:
          state.globalAlpha = values[0].clamp(0, 1);
        case 47:
          state.lineDashOffset = values[0];
        case 48:
          state.font = _layerString(scene, layer, command.resourceId);
        case 49:
          state.textAlign = _layerString(scene, layer, command.resourceId);
        case 50:
          state.textBaseline = _layerString(scene, layer, command.resourceId);
        case 51:
          state.imageSmoothingEnabled = values[0] != 0;
        case 52:
          state.imageSmoothingQuality = _layerString(
            scene,
            layer,
            command.resourceId,
          );
        case 53:
          state.composite = _layerString(scene, layer, command.resourceId);
        case 54:
          state.shadowColor = _layerString(scene, layer, command.resourceId);
        case 55:
          state.shadowBlur = values[0];
        case 56:
          state.shadowOffsetX = values[0];
        case 57:
          state.shadowOffsetY = values[0];
      }
    }
  }

  void _drawCanvasText(
    ui.Canvas canvas,
    WebSceneSceneView scene,
    WebSceneCanvasLayer layer,
    WebSceneCanvasCommand command,
    _CanvasState state, {
    bool stroke = false,
  }) {
    final text = _layerString(scene, layer, command.resourceId);
    if (text.isEmpty) return;
    final font = _parseFont(state.font);
    final painter = TextPainter(
      text: TextSpan(
        text: text,
        style: TextStyle(
          color: _colorWithAlpha(
            _parseColor(stroke ? state.strokeStyle : state.fillStyle),
            state.globalAlpha,
          ),
          fontFamily: font.$2,
          fontSize: font.$1,
        ),
      ),
      textDirection: TextDirection.ltr,
      maxLines: 1,
    )..layout();
    var x = command.values[0];
    if (state.textAlign == 'center') {
      x -= painter.width / 2;
    } else if (state.textAlign == 'right' || state.textAlign == 'end') {
      x -= painter.width;
    }
    var y = command.values[1];
    switch (state.textBaseline) {
      case 'top':
      case 'hanging':
        break;
      case 'middle':
        y -= painter.height / 2;
      case 'bottom':
      case 'ideographic':
        y -= painter.height;
      default:
        y -= painter.computeDistanceToActualBaseline(TextBaseline.alphabetic);
    }
    painter.paint(canvas, ui.Offset(x, y));
    painter.dispose();
  }

  void _drawCanvasLayer(
    ui.Canvas canvas,
    WebSceneCanvasCommand command,
    _CanvasState state,
  ) {
    final source = _layers[command.resourceId];
    final values = command.values;
    if (source == null || values[2] == 0 || values[3] == 0) return;
    final destination = ui.Rect.fromLTWH(
      values[4],
      values[5],
      values[6],
      values[7],
    );
    canvas
      ..save()
      ..clipRect(destination)
      ..translate(values[4], values[5])
      ..scale(values[6] / values[2], values[7] / values[3])
      ..translate(-values[0], -values[1])
      ..drawPicture(source.picture)
      ..restore();
  }

  void _drawCanvasSvgPath(
    ui.Canvas canvas,
    WebSceneSceneView scene,
    WebSceneCanvasLayer layer,
    WebSceneCanvasCommand command,
    _CanvasState state, {
    required bool fill,
  }) {
    final data = _layerString(scene, layer, command.resourceId);
    if (data.isEmpty) return;
    try {
      var path = parseSvgPathData(data);
      if ((command.flags & 0xffff) >= 6) {
        path = path.transform(_matrix(command).storage);
      }
      path.fillType = fill && command.flags & _canvasEvenOdd != 0
          ? ui.PathFillType.evenOdd
          : ui.PathFillType.nonZero;
      canvas.drawPath(path, _paint(state, fill: fill));
    } on FormatException {
      // Treat an unsupported optional SVG path as an empty operation.
    }
  }

  ui.Paint _paint(_CanvasState state, {required bool fill}) {
    final paint = ui.Paint()
      ..isAntiAlias = true
      ..style = fill ? ui.PaintingStyle.fill : ui.PaintingStyle.stroke
      ..strokeWidth = state.lineWidth.clamp(0, double.infinity)
      ..strokeMiterLimit = state.miterLimit.clamp(0, double.infinity)
      ..strokeCap = switch (state.lineCap) {
        'round' => ui.StrokeCap.round,
        'square' => ui.StrokeCap.square,
        _ => ui.StrokeCap.butt,
      }
      ..strokeJoin = switch (state.lineJoin) {
        'round' => ui.StrokeJoin.round,
        'bevel' => ui.StrokeJoin.bevel,
        _ => ui.StrokeJoin.miter,
      }
      ..blendMode = _blendMode(state.composite)
      ..color = _colorWithAlpha(
        _parseColor(fill ? state.fillStyle : state.strokeStyle),
        state.globalAlpha,
      );
    if (state.shadowBlur > 0 && _parseColor(state.shadowColor).a > 0) {
      paint.maskFilter = ui.MaskFilter.blur(
        ui.BlurStyle.normal,
        state.shadowBlur * 0.5,
      );
    }
    return paint;
  }

  static void _replaceTransform(ui.Canvas canvas, Matrix4 desired) {
    final current = Matrix4.fromFloat64List(canvas.getTransform());
    final determinant = current.invert();
    if (determinant != 0) canvas.transform(current.storage);
    canvas.transform(desired.storage);
  }

  static Matrix4 _matrix(WebSceneCanvasCommand command) {
    final value = command.values;
    return Matrix4.fromList([
      value[0],
      value[1],
      0,
      0,
      value[2],
      value[3],
      0,
      0,
      0,
      0,
      1,
      0,
      value[4],
      value[5],
      0,
      1,
    ]);
  }

  static void _appendArc(ui.Path path, WebSceneCanvasCommand command) {
    final value = command.values;
    final radius = value[2].abs();
    if (radius <= 0) return;
    var sweep = value[4] - value[3];
    const tau = 3.141592653589793 * 2;
    if (value[5] == 0) {
      while (sweep < 0) {
        sweep += tau;
      }
      sweep = sweep.clamp(-tau, tau);
    } else {
      while (sweep > 0) {
        sweep -= tau;
      }
      sweep = sweep.clamp(-tau, tau);
    }
    path.addArc(
      ui.Rect.fromCircle(center: ui.Offset(value[0], value[1]), radius: radius),
      value[3],
      sweep,
    );
  }

  static ui.Rect _domRect(WebSceneSceneCommand command) =>
      ui.Rect.fromLTWH(command.x, command.y, command.width, command.height);

  static ui.RRect _domRRect(WebSceneSceneCommand command) {
    var topLeft = command.radiusTopLeft;
    var topRight = command.radiusTopRight;
    var bottomRight = command.radiusBottomRight;
    var bottomLeft = command.radiusBottomLeft;
    if (topLeft <= 0 && topRight <= 0 && bottomRight <= 0 && bottomLeft <= 0) {
      final legacy = (command.flags >> 16) / 100;
      topLeft = topRight = bottomRight = bottomLeft = legacy;
    }
    return ui.RRect.fromRectAndCorners(
      _domRect(command),
      topLeft: ui.Radius.circular(topLeft),
      topRight: ui.Radius.circular(topRight),
      bottomRight: ui.Radius.circular(bottomRight),
      bottomLeft: ui.Radius.circular(bottomLeft),
    );
  }

  static ui.Rect _canvasRect(WebSceneCanvasCommand command) => ui.Rect.fromLTWH(
        command.values[0],
        command.values[1],
        command.values[2],
        command.values[3],
      );

  static void _applyDomRotation(
      ui.Canvas canvas, WebSceneSceneCommand command) {
    if (command.strokeWidth.abs() < 0.001) return;
    final centerX = command.x + command.width / 2;
    final centerY = command.y + command.height / 2;
    canvas
      ..translate(centerX, centerY)
      ..rotate(command.strokeWidth * 3.141592653589793 / 180)
      ..translate(-centerX, -centerY);
  }

  static void _applySvgTransform(ui.Canvas canvas, String transform) {
    final expression = RegExp(r'([a-zA-Z]+)\s*\(([^)]*)\)');
    for (final match in expression.allMatches(transform)) {
      final values = _numbers(match.group(2) ?? '');
      switch (match.group(1)) {
        case 'translate' when values.isNotEmpty:
          canvas.translate(values[0], values.length > 1 ? values[1] : 0);
        case 'scale' when values.isNotEmpty:
          canvas.scale(values[0], values.length > 1 ? values[1] : values[0]);
        case 'rotate' when values.isNotEmpty:
          if (values.length >= 3) {
            canvas
              ..translate(values[1], values[2])
              ..rotate(values[0] * 3.141592653589793 / 180)
              ..translate(-values[1], -values[2]);
          } else {
            canvas.rotate(values[0] * 3.141592653589793 / 180);
          }
        case 'matrix' when values.length >= 6:
          canvas.transform(
            Matrix4.fromList([
              values[0],
              values[1],
              0,
              0,
              values[2],
              values[3],
              0,
              0,
              0,
              0,
              1,
              0,
              values[4],
              values[5],
              0,
              1,
            ]).storage,
          );
      }
    }
  }

  static List<double> _numbers(String value) => RegExp(
        r'[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?',
      )
          .allMatches(value)
          .map((match) => double.parse(match.group(0)!))
          .toList();

  static void _clipDomShape(
    ui.Canvas canvas,
    WebSceneSceneView scene,
    WebSceneSceneCommand command,
  ) {
    if (command.flags & _domPolygonClipResource == 0) {
      canvas.clipRRect(_domRRect(command), doAntiAlias: true);
      return;
    }
    try {
      final path = parseSvgPathData(
        _domString(scene, command.flags & _domPolygonClipIndexMask),
      );
      if ((command.flags & _domClipEvenOdd) != 0) {
        path.fillType = ui.PathFillType.evenOdd;
      }
      final relative = (command.flags & _domClipRelativePath) != 0;
      final objectBoundingBox =
          (command.flags & _domClipObjectBoundingBox) != 0;
      if (objectBoundingBox && (command.width <= 0 || command.height <= 0)) {
        canvas.clipRect(ui.Rect.zero, doAntiAlias: false);
        return;
      }
      if (relative) canvas.translate(command.x, command.y);
      if (objectBoundingBox) canvas.scale(command.width, command.height);
      try {
        canvas.clipPath(path, doAntiAlias: true);
      } finally {
        // Restoring a saved canvas would also discard the clip. Undo only the
        // matrix so the clip remains in device coordinates.
        if (objectBoundingBox) {
          canvas.scale(1 / command.width, 1 / command.height);
        }
        if (relative) canvas.translate(-command.x, -command.y);
      }
    } catch (_) {
      canvas.clipRect(ui.Rect.zero, doAntiAlias: false);
    }
  }

  void _drawDomMask(
    ui.Canvas canvas,
    WebSceneSceneView scene,
    WebSceneSceneCommand command,
  ) {
    final resource = _domString(scene, command.flags);
    if (resource.startsWith('webscene-mask-svg-v1\t')) {
      _drawDomSvgMask(canvas, command, resource);
      return;
    }
    if (!resource.startsWith('webscene-bg-v2\t')) {
      _clearDomMaskBounds(canvas, command);
      return;
    }
    _drawDomLinearMask(canvas, scene, command);
  }

  static void _drawDomLinearMask(
    ui.Canvas canvas,
    WebSceneSceneView scene,
    WebSceneSceneCommand command,
  ) {
    const prefix = 'webscene-bg-v2\t';
    final resource = _domString(scene, command.flags);
    if (!resource.startsWith(prefix)) return;
    final fields = resource.substring(prefix.length).split('\t');
    if (fields.isEmpty) {
      _clearDomMaskBounds(canvas, command);
      return;
    }
    final image = fields[0].trim();
    final match = RegExp(
      r'^linear-gradient\(([\s\S]*)\)$',
      caseSensitive: false,
    ).firstMatch(image);
    if (match == null) {
      _clearDomMaskBounds(canvas, command);
      return;
    }
    final components = _splitCssTopLevel(match.group(1)!, ',');
    if (components.length < 2) {
      _clearDomMaskBounds(canvas, command);
      return;
    }

    var directionX = 0.0;
    var directionY = 1.0;
    var stopStart = 0;
    final direction = components.first.trim().toLowerCase();
    if (direction.startsWith('to ')) {
      directionX = direction.contains('right')
          ? 1
          : direction.contains('left') ? -1 : 0;
      directionY = direction.contains('bottom')
          ? 1
          : direction.contains('top') ? -1 : 0;
      final length = math.sqrt(
        directionX * directionX + directionY * directionY,
      );
      if (length <= 0) {
        _clearDomMaskBounds(canvas, command);
        return;
      }
      directionX /= length;
      directionY /= length;
      stopStart = 1;
    } else {
      final angle = RegExp(
        r'^([-+]?(?:\d+(?:\.\d*)?|\.\d+))(deg|turn)$',
      ).firstMatch(direction);
      if (angle != null) {
        var degrees = double.parse(angle.group(1)!);
        if (angle.group(2) == 'turn') degrees *= 360;
        final radians = degrees * math.pi / 180;
        directionX = math.sin(radians);
        directionY = -math.cos(radians);
        stopStart = 1;
      }
    }

    final repeat = fields.length > 1 ? fields[1].trim().toLowerCase() : 'repeat';
    final position = fields.length > 2 ? fields[2].trim() : '0% 0%';
    final size = fields.length > 3 ? fields[3].trim() : 'auto';
    final resolvedSize = _resolveMaskSize(size, command.width, command.height);
    final tileWidth = resolvedSize.$1;
    final tileHeight = resolvedSize.$2;
    if (tileWidth <= 0 || tileHeight <= 0) {
      _clearDomMaskBounds(canvas, command);
      return;
    }
    final resolvedPosition = _resolveMaskPosition(
      position,
      command.width,
      command.height,
      tileWidth,
      tileHeight,
    );
    var firstX = command.x + resolvedPosition.$1;
    var firstY = command.y + resolvedPosition.$2;
    final repeatX = repeat != 'no-repeat' && repeat != 'repeat-y';
    final repeatY = repeat != 'no-repeat' && repeat != 'repeat-x';
    if (repeatX) {
      while (firstX > command.x) firstX -= tileWidth;
      while (firstX + tileWidth <= command.x) firstX += tileWidth;
    }
    if (repeatY) {
      while (firstY > command.y) firstY -= tileHeight;
      while (firstY + tileHeight <= command.y) firstY += tileHeight;
    }
    final endX = repeatX ? command.x + command.width : firstX + tileWidth;
    final endY = repeatY ? command.y + command.height : firstY + tileHeight;
    final axisLength = math.max(
      1.0,
      directionX.abs() * tileWidth + directionY.abs() * tileHeight,
    );
    final colors = <ui.Color>[];
    final stops = <double>[];
    for (var index = stopStart; index < components.length; index++) {
      final parsed = _parseMaskStop(components[index], axisLength);
      if (parsed == null) continue;
      for (final offset in parsed.$2) {
        colors.add(parsed.$1);
        stops.add(offset);
      }
    }
    if (colors.length < 2) {
      _clearDomMaskBounds(canvas, command);
      return;
    }
    _fillMissingGradientStops(stops);
    final centerX = tileWidth / 2;
    final centerY = tileHeight / 2;
    final halfProjection = (
      directionX.abs() * tileWidth + directionY.abs() * tileHeight
    ) / 2;
    canvas.save();
    try {
      final bounds = ui.Rect.fromLTWH(
        command.x,
        command.y,
        command.width,
        command.height,
      );
      canvas.clipRect(bounds, doAntiAlias: false);
      _clearDomMaskOutsideCoverage(
        canvas,
        bounds,
        repeatX,
        repeatY,
        firstX,
        firstY,
        tileWidth,
        tileHeight,
      );
      for (var y = firstY; y < endY; y += tileHeight) {
        for (var x = firstX; x < endX; x += tileWidth) {
          final start = ui.Offset(
            x + centerX - directionX * halfProjection,
            y + centerY - directionY * halfProjection,
          );
          final end = ui.Offset(
            x + centerX + directionX * halfProjection,
            y + centerY + directionY * halfProjection,
          );
          canvas.drawRect(
            ui.Rect.fromLTWH(x, y, tileWidth, tileHeight),
            ui.Paint()
              ..isAntiAlias = false
              ..blendMode = ui.BlendMode.dstIn
              ..shader = ui.Gradient.linear(start, end, colors, stops),
          );
          if (!repeatX) break;
        }
        if (!repeatY) break;
      }
    } finally {
      canvas.restore();
    }
  }

  void _drawDomSvgMask(
    ui.Canvas canvas,
    WebSceneSceneCommand command,
    String resource,
  ) {
    const prefix = 'webscene-mask-svg-v1\t';
    final fields = resource.substring(prefix.length).split('\t');
    if (fields.length < 5) {
      _clearDomMaskBounds(canvas, command);
      return;
    }
    final viewBoxValues = _numbers(fields[0]);
    if (viewBoxValues.length < 4 ||
        viewBoxValues[2] <= 0 ||
        viewBoxValues[3] <= 0 ||
        command.width <= 0 ||
        command.height <= 0) {
      _clearDomMaskBounds(canvas, command);
      return;
    }
    final markup = fields.sublist(4).join('\t');
    final geometry = _svgMaskGeometry.putIfAbsent(
      markup,
      () => _parseSvgMaskGeometry(markup),
    );
    if (geometry == null) {
      _clearDomMaskBounds(canvas, command);
      return;
    }
    final viewBox = ui.Rect.fromLTWH(
      viewBoxValues[0],
      viewBoxValues[1],
      viewBoxValues[2],
      viewBoxValues[3],
    );
    final repeat = fields[1].trim().toLowerCase();
    final position = fields[2].trim();
    final size = fields[3].trim();
    final resolvedSize = _resolveSvgMaskSize(
      size,
      command.width,
      command.height,
      viewBox.width,
      viewBox.height,
    );
    final tileWidth = resolvedSize.$1;
    final tileHeight = resolvedSize.$2;
    if (tileWidth <= 0 || tileHeight <= 0) {
      _clearDomMaskBounds(canvas, command);
      return;
    }
    final resolvedPosition = _resolveMaskPosition(
      position,
      command.width,
      command.height,
      tileWidth,
      tileHeight,
    );
    var firstX = command.x + resolvedPosition.$1;
    var firstY = command.y + resolvedPosition.$2;
    final repeatX = repeat != 'no-repeat' && repeat != 'repeat-y';
    final repeatY = repeat != 'no-repeat' && repeat != 'repeat-x';
    if (repeatX) {
      while (firstX > command.x) firstX -= tileWidth;
      while (firstX + tileWidth <= command.x) firstX += tileWidth;
    }
    if (repeatY) {
      while (firstY > command.y) firstY -= tileHeight;
      while (firstY + tileHeight <= command.y) firstY += tileHeight;
    }
    final bounds = ui.Rect.fromLTWH(
      command.x,
      command.y,
      command.width,
      command.height,
    );
    canvas.save();
    try {
      canvas.clipRect(bounds, doAntiAlias: false);
      _clearDomMaskOutsideCoverage(
        canvas,
        bounds,
        repeatX,
        repeatY,
        firstX,
        firstY,
        tileWidth,
        tileHeight,
      );
      final endX = repeatX ? bounds.right : firstX + tileWidth;
      final endY = repeatY ? bounds.bottom : firstY + tileHeight;
      canvas.saveLayer(bounds, ui.Paint()..blendMode = ui.BlendMode.dstIn);
      try {
        for (var y = firstY; y < endY; y += tileHeight) {
          for (var x = firstX; x < endX; x += tileWidth) {
            _drawSvgMaskTile(
              canvas,
              geometry,
              viewBox,
              ui.Rect.fromLTWH(x, y, tileWidth, tileHeight),
            );
            if (!repeatX) break;
          }
          if (!repeatY) break;
        }
      } finally {
        canvas.restore();
      }
    } finally {
      canvas.restore();
    }
  }

  static void _drawSvgMaskTile(
    ui.Canvas canvas,
    _SvgMaskGeometry geometry,
    ui.Rect viewBox,
    ui.Rect tile,
  ) {
    final scale = math.min(
      tile.width / viewBox.width,
      tile.height / viewBox.height,
    );
    final renderedWidth = viewBox.width * scale;
    final renderedHeight = viewBox.height * scale;
    final offsetX = tile.left + (tile.width - renderedWidth) / 2;
    final offsetY = tile.top + (tile.height - renderedHeight) / 2;
    canvas.save();
    try {
      canvas.clipRect(tile, doAntiAlias: false);
      canvas
        ..translate(offsetX, offsetY)
        ..scale(scale, scale)
        ..translate(-viewBox.left, -viewBox.top);
      for (final shape in geometry.shapes) {
        canvas.drawPath(
          shape.$1,
          ui.Paint()
            ..isAntiAlias = true
            ..color = ui.Color.fromARGB(
              (shape.$2.clamp(0.0, 1.0) * 255).round(),
              255,
              255,
              255,
            ),
        );
      }
    } finally {
      canvas.restore();
    }
  }

  static _SvgMaskGeometry? _parseSvgMaskGeometry(String markup) {
    if (RegExp(
      r'<(?:defs|g|use|image|text|line|polyline)\b',
      caseSensitive: false,
    ).hasMatch(markup) || RegExp(
      r'<(?:g|path|rect|circle|ellipse|polygon)\b[^>]*\btransform\s*=',
      caseSensitive: false,
    ).hasMatch(markup)) {
      return null;
    }
    final hiddenClasses = <String>{};
    for (final match in RegExp(
      r'\.([_a-zA-Z][_a-zA-Z0-9-]*)\s*\{[^}]*\bfill\s*:\s*none\b[^}]*\}',
      caseSensitive: false,
    ).allMatches(markup)) {
      hiddenClasses.add(match.group(1)!);
    }
    String? attribute(String source, String name) {
      final match = RegExp(
        "${RegExp.escape(name)}\\s*=\\s*([\"'])(.*?)\\1",
        caseSensitive: false,
        dotAll: true,
      ).firstMatch(source);
      return match?.group(2);
    }
    String? declaration(String source, String name) {
      final style = attribute(source, 'style');
      if (style == null) return null;
      return RegExp(
        '(?:^|;)\\s*${RegExp.escape(name)}\\s*:\\s*([^;]+)',
        caseSensitive: false,
      ).firstMatch(style)?.group(1)?.trim();
    }
    String? property(String source, String name) =>
        attribute(source, name) ?? declaration(source, name);
    double number(String? value, [double fallback = 0]) =>
        value == null ? fallback : double.tryParse(value) ?? fallback;
    final root = RegExp(
      r'<svg\b([^>]*)>',
      caseSensitive: false,
      dotAll: true,
    ).firstMatch(markup);
    final rootFill = root == null
        ? null
        : property(root.group(1)!, 'fill')?.trim().toLowerCase();
    bool hidden(String attributes) {
      final fill = property(attributes, 'fill')?.trim().toLowerCase();
      if (fill == 'none' || (fill == null && rootFill == 'none')) {
        return true;
      }
      final classes = (attribute(attributes, 'class') ?? '').split(RegExp(r'\s+'));
      return classes.any(hiddenClasses.contains);
    }
    double alpha(String attributes) {
      final value = property(attributes, 'fill-opacity')
          ?? property(attributes, 'opacity');
      if (value == null) return 1;
      final normalized = value.trim();
      if (normalized.endsWith('%')) {
        return (double.tryParse(
          normalized.substring(0, normalized.length - 1),
        ) ?? 100) / 100;
      }
      return double.tryParse(normalized) ?? 1;
    }

    final shapes = <(ui.Path, double)>[];
    final elements = RegExp(
      r'<(path|rect|circle|ellipse|polygon)\b([^>]*)>',
      caseSensitive: false,
      dotAll: true,
    ).allMatches(markup);
    try {
      for (final element in elements) {
        final tag = element.group(1)!.toLowerCase();
        final attributes = element.group(2)!;
        if (hidden(attributes)) continue;
        if (attribute(attributes, 'transform') != null) return null;
        final stroke = property(attributes, 'stroke')?.trim().toLowerCase();
        if (stroke != null && stroke != 'none') return null;
        final fillRule = property(attributes, 'fill-rule')?.trim().toLowerCase();
        if (fillRule != null && fillRule != 'nonzero') return null;
        final path = ui.Path();
        switch (tag) {
          case 'path':
            final data = attribute(attributes, 'd');
            if (data == null || data.trim().isEmpty) continue;
            path.addPath(parseSvgPathData(data), ui.Offset.zero);
          case 'rect':
            final rect = ui.Rect.fromLTWH(
              number(attribute(attributes, 'x')),
              number(attribute(attributes, 'y')),
              number(attribute(attributes, 'width')),
              number(attribute(attributes, 'height')),
            );
            final rx = number(attribute(attributes, 'rx'));
            final ry = number(attribute(attributes, 'ry'), rx);
            if (rx > 0 || ry > 0) {
              path.addRRect(ui.RRect.fromRectXY(rect, rx, ry));
            } else {
              path.addRect(rect);
            }
          case 'circle':
            final radius = number(attribute(attributes, 'r'));
            path.addOval(ui.Rect.fromCircle(
              center: ui.Offset(
                number(attribute(attributes, 'cx')),
                number(attribute(attributes, 'cy')),
              ),
              radius: radius,
            ));
          case 'ellipse':
            final cx = number(attribute(attributes, 'cx'));
            final cy = number(attribute(attributes, 'cy'));
            final rx = number(attribute(attributes, 'rx'));
            final ry = number(attribute(attributes, 'ry'));
            path.addOval(ui.Rect.fromLTRB(cx - rx, cy - ry, cx + rx, cy + ry));
          case 'polygon':
            final points = _numbers(attribute(attributes, 'points') ?? '');
            if (points.length < 4 || points.length.isOdd) continue;
            path.moveTo(points[0], points[1]);
            for (var index = 2; index < points.length; index += 2) {
              path.lineTo(points[index], points[index + 1]);
            }
            path.close();
        }
        shapes.add((path, alpha(attributes)));
      }
    } catch (_) {
      return null;
    }
    return shapes.isEmpty ? null : _SvgMaskGeometry(shapes);
  }

  static (double, double) _resolveSvgMaskSize(
    String value,
    double width,
    double height,
    double intrinsicWidth,
    double intrinsicHeight,
  ) {
    final tokens = _splitCssTopLevel(value.trim(), ' ')
        .where((token) => token.isNotEmpty)
        .toList();
    final first = tokens.isEmpty ? 'auto' : tokens[0].toLowerCase();
    final second = tokens.length > 1 ? tokens[1].toLowerCase() : 'auto';
    if (first == 'cover' || first == 'contain') {
      final scaleX = width / intrinsicWidth;
      final scaleY = height / intrinsicHeight;
      final scale = first == 'cover'
          ? math.max(scaleX, scaleY)
          : math.min(scaleX, scaleY);
      return (intrinsicWidth * scale, intrinsicHeight * scale);
    }
    double? resolve(String token, double available) {
      if (token == 'auto') return null;
      if (token.endsWith('%')) {
        final percentage = double.tryParse(token.substring(0, token.length - 1));
        return percentage == null ? null : available * percentage / 100;
      }
      if (token.endsWith('px')) {
        return double.tryParse(token.substring(0, token.length - 2));
      }
      return double.tryParse(token);
    }
    final resolvedWidth = resolve(first, width);
    final resolvedHeight = resolve(second, height);
    if (resolvedWidth != null && resolvedHeight != null) {
      return (resolvedWidth, resolvedHeight);
    }
    if (resolvedWidth != null) {
      return (resolvedWidth, intrinsicHeight * resolvedWidth / intrinsicWidth);
    }
    if (resolvedHeight != null) {
      return (intrinsicWidth * resolvedHeight / intrinsicHeight, resolvedHeight);
    }
    return (intrinsicWidth, intrinsicHeight);
  }

  static void _clearDomMaskBounds(
    ui.Canvas canvas,
    WebSceneSceneCommand command,
  ) {
    canvas.save();
    try {
      final bounds = ui.Rect.fromLTWH(
        command.x,
        command.y,
        command.width,
        command.height,
      );
      canvas.clipRect(bounds, doAntiAlias: false);
      canvas.drawRect(bounds, ui.Paint()..blendMode = ui.BlendMode.clear);
    } finally {
      canvas.restore();
    }
  }

  static void _clearDomMaskOutsideCoverage(
    ui.Canvas canvas,
    ui.Rect bounds,
    bool repeatX,
    bool repeatY,
    double firstX,
    double firstY,
    double tileWidth,
    double tileHeight,
  ) {
    final coverageLeft = repeatX
        ? bounds.left
        : firstX.clamp(bounds.left, bounds.right).toDouble();
    final coverageRight = repeatX
        ? bounds.right
        : (firstX + tileWidth).clamp(bounds.left, bounds.right).toDouble();
    final coverageTop = repeatY
        ? bounds.top
        : firstY.clamp(bounds.top, bounds.bottom).toDouble();
    final coverageBottom = repeatY
        ? bounds.bottom
        : (firstY + tileHeight).clamp(bounds.top, bounds.bottom).toDouble();
    final clear = ui.Paint()..blendMode = ui.BlendMode.clear;
    if (coverageRight <= coverageLeft || coverageBottom <= coverageTop) {
      canvas.drawRect(bounds, clear);
      return;
    }
    canvas.drawRect(
      ui.Rect.fromLTRB(bounds.left, bounds.top, bounds.right, coverageTop),
      clear,
    );
    canvas.drawRect(
      ui.Rect.fromLTRB(bounds.left, coverageBottom, bounds.right, bounds.bottom),
      clear,
    );
    canvas.drawRect(
      ui.Rect.fromLTRB(bounds.left, coverageTop, coverageLeft, coverageBottom),
      clear,
    );
    canvas.drawRect(
      ui.Rect.fromLTRB(coverageRight, coverageTop, bounds.right, coverageBottom),
      clear,
    );
  }

  static List<String> _splitCssTopLevel(String value, String separator) {
    final result = <String>[];
    var start = 0;
    var depth = 0;
    String? quote;
    for (var index = 0; index <= value.length; index++) {
      final character = index < value.length ? value[index] : separator;
      if (quote != null) {
        if (character == '\\') index++;
        else if (character == quote) quote = null;
      } else if (character == '"' || character == "'") {
        quote = character;
      } else if (character == '(') {
        depth++;
      } else if (character == ')' && depth > 0) {
        depth--;
      } else if (character == separator && depth == 0) {
        result.add(value.substring(start, index).trim());
        start = index + 1;
      }
    }
    return result;
  }

  static (ui.Color, List<double>)? _parseMaskStop(
    String component,
    double axisLength,
  ) {
    final source = component.trim();
    if (source.isEmpty) return null;
    var colorEnd = source.indexOf(RegExp(r'\s'));
    final open = source.indexOf('(');
    if (open >= 0 && (colorEnd < 0 || open < colorEnd)) {
      var depth = 1;
      colorEnd = open + 1;
      while (colorEnd < source.length && depth > 0) {
        if (source[colorEnd] == '(') depth++;
        else if (source[colorEnd] == ')') depth--;
        colorEnd++;
      }
    }
    if (colorEnd < 0) colorEnd = source.length;
    final color = _parseColor(source.substring(0, colorEnd));
    final positionSource = source.substring(colorEnd).trim();
    final offsets = <double>[];
    if (positionSource.isNotEmpty) {
      for (final token in positionSource.split(RegExp(r'\s+(?![^()]*\))'))) {
        if (token.isEmpty) continue;
        final offset = _parseMaskPositionValue(token, axisLength);
        if (offset != null) offsets.add(offset);
      }
    }
    if (offsets.isEmpty) offsets.add(double.nan);
    return (color, offsets);
  }

  static double? _parseMaskPositionValue(String value, double axisLength) {
    final normalized = value.trim().toLowerCase();
    if (normalized.endsWith('%')) {
      final percentage = double.tryParse(
        normalized.substring(0, normalized.length - 1),
      );
      return percentage == null ? null : percentage / 100;
    }
    if (normalized.endsWith('px')) {
      final pixels = double.tryParse(normalized.substring(0, normalized.length - 2));
      return pixels == null ? null : pixels / axisLength;
    }
    final calc = RegExp(
      r'^calc\(100%\s*([-+])\s*([-+]?(?:\d+(?:\.\d*)?|\.\d+))px\)$',
    ).firstMatch(normalized);
    if (calc != null) {
      final pixels = double.parse(calc.group(2)!);
      return (axisLength + (calc.group(1) == '-' ? -pixels : pixels)) / axisLength;
    }
    return null;
  }

  static void _fillMissingGradientStops(List<double> stops) {
    if (stops.first.isNaN) stops[0] = 0;
    if (stops.last.isNaN) stops[stops.length - 1] = 1;
    var index = 1;
    while (index < stops.length - 1) {
      if (!stops[index].isNaN) {
        stops[index] = stops[index].clamp(stops[index - 1], 1);
        index++;
        continue;
      }
      final runStart = index - 1;
      var runEnd = index + 1;
      while (runEnd < stops.length && stops[runEnd].isNaN) runEnd++;
      final from = stops[runStart];
      final to = runEnd < stops.length ? stops[runEnd] : 1;
      for (var missing = index; missing < runEnd; missing++) {
        stops[missing] = from
            + (to - from) * (missing - runStart) / (runEnd - runStart);
      }
      index = runEnd;
    }
    for (var stop = 0; stop < stops.length; stop++) {
      stops[stop] = stops[stop].clamp(stop == 0 ? 0 : stops[stop - 1], 1);
    }
  }

  static (double, double) _resolveMaskSize(
    String value,
    double width,
    double height,
  ) {
    final tokens = _splitCssTopLevel(value.trim(), ' ')
        .where((token) => token.isNotEmpty)
        .toList();
    final first = tokens.isEmpty ? 'auto' : tokens[0];
    final second = tokens.length > 1 ? tokens[1] : 'auto';
    double resolve(String token, double available) {
      if (token == 'auto') return available;
      final calc = RegExp(
        r'^calc\(\s*([-+]?(?:\d+(?:\.\d*)?|\.\d+))%\s*([-+])\s*([-+]?(?:\d+(?:\.\d*)?|\.\d+))px\s*\)$',
        caseSensitive: false,
      ).firstMatch(token);
      if (calc != null) {
        final percentage = double.parse(calc.group(1)!);
        final pixels = double.parse(calc.group(3)!);
        return available * percentage / 100
            + (calc.group(2) == '-' ? -pixels : pixels);
      }
      if (token.endsWith('%')) {
        return available * (double.tryParse(token.substring(0, token.length - 1)) ?? 100) / 100;
      }
      if (token.endsWith('px')) {
        return double.tryParse(token.substring(0, token.length - 2)) ?? available;
      }
      return available;
    }
    return (resolve(first, width), resolve(second, height));
  }

  static (double, double) _resolveMaskPosition(
    String value,
    double width,
    double height,
    double tileWidth,
    double tileHeight,
  ) {
    final tokens = _splitCssTopLevel(value.trim(), ' ')
        .where((token) => token.isNotEmpty)
        .toList();
    final first = tokens.isEmpty ? '0%' : tokens[0];
    final second = tokens.length > 1 ? tokens[1] : '0%';
    double resolve(String token, double remaining) {
      if (token == 'center') return remaining / 2;
      if (token == 'right' || token == 'bottom') return remaining;
      if (token == 'left' || token == 'top') return 0;
      if (token.endsWith('%')) {
        return remaining * (double.tryParse(token.substring(0, token.length - 1)) ?? 0) / 100;
      }
      if (token.endsWith('px')) {
        return double.tryParse(token.substring(0, token.length - 2)) ?? 0;
      }
      return 0;
    }
    return (resolve(first, width - tileWidth), resolve(second, height - tileHeight));
  }

  static ui.Paint _domGroupPaint(WebSceneSceneCommand command) {
    if (command.flags & _domEffectFilterMask == 0) {
      return ui.Paint()
        ..color = ui.Color.fromARGB(command.rgba & 0xff, 255, 255, 255);
    }
    if (command.flags & _domLinearMaskGroup != 0) return ui.Paint();
    final amount = command.strokeWidth.clamp(0.0, double.infinity).toDouble();
    if (command.flags & _domBlurFilter != 0) {
      return ui.Paint()
        ..imageFilter = ui.ImageFilter.blur(sigmaX: amount, sigmaY: amount);
    }
    final List<double> matrix;
    if (command.flags & _domBrightnessFilter != 0) {
      matrix = <double>[
        amount, 0, 0, 0, 0,
        0, amount, 0, 0, 0,
        0, 0, amount, 0, 0,
        0, 0, 0, 1, 0,
      ];
    } else if (command.flags & _domGrayscaleFilter != 0) {
      matrix = <double>[
        1 - 0.7874 * amount, 0.7152 * amount, 0.0722 * amount, 0, 0,
        0.2126 * amount, 1 - 0.2848 * amount, 0.0722 * amount, 0, 0,
        0.2126 * amount, 0.7152 * amount, 1 - 0.9278 * amount, 0, 0,
        0, 0, 0, 1, 0,
      ];
    } else if (command.flags & _domSaturateFilter != 0) {
      final inverse = 1 - amount;
      matrix = <double>[
        0.2126 + 0.7874 * amount, 0.7152 * inverse, 0.0722 * inverse, 0, 0,
        0.2126 * inverse, 0.7152 + 0.2848 * amount, 0.0722 * inverse, 0, 0,
        0.2126 * inverse, 0.7152 * inverse, 0.0722 + 0.9278 * amount, 0, 0,
        0, 0, 0, 1, 0,
      ];
    } else {
      final intercept = 127.5 * (1 - amount);
      matrix = <double>[
        amount, 0, 0, 0, intercept,
        0, amount, 0, 0, intercept,
        0, 0, amount, 0, intercept,
        0, 0, 0, 1, 0,
      ];
    }
    return ui.Paint()
      ..colorFilter = ui.ColorFilter.matrix(matrix);
  }

  static String _domString(WebSceneSceneView scene, int index) =>
      _stringAt(scene, index);

  static String _layerString(
    WebSceneSceneView scene,
    WebSceneCanvasLayer layer,
    int localIndex,
  ) {
    if (localIndex < 0 || localIndex >= layer.stringCount) return '';
    return _stringAt(scene, layer.stringOffset + localIndex);
  }

  static String _stringAt(WebSceneSceneView scene, int index) {
    if (index < 0 || index >= scene.stringCount) return '';
    final descriptor = scene.strings[index];
    if (descriptor.byteOffset > scene.stringByteCount ||
        descriptor.byteLength > scene.stringByteCount - descriptor.byteOffset) {
      return '';
    }
    return utf8.decode(
      (scene.stringBytes + descriptor.byteOffset).asTypedList(
        descriptor.byteLength,
      ),
      allowMalformed: true,
    );
  }

  static ui.Color _rgba(int rgba) =>
      ui.Color(((rgba & 0xff) << 24) | ((rgba >> 8) & 0x00ffffff));

  static ui.Color _colorWithAlpha(ui.Color color, double alpha) =>
      color.withValues(alpha: color.a * alpha.clamp(0, 1));

  static ui.Color _parseColor(String value) {
    final normalized = value.trim().toLowerCase();
    if (normalized == 'transparent') return const ui.Color(0x00000000);
    if (normalized.startsWith('#')) {
      var hex = normalized.substring(1);
      if (hex.length == 3 || hex.length == 4) {
        hex = hex.split('').map((part) => '$part$part').join();
      }
      if (hex.length == 6) hex = '${hex}ff';
      final rgba = int.tryParse(hex, radix: 16);
      if (rgba != null && hex.length == 8) return _rgba(rgba);
    }
    final function = RegExp(r'rgba?\(([^)]*)\)').firstMatch(normalized);
    if (function != null) {
      final parts = function
          .group(1)!
          .split(RegExp(r'[\s,\/]+'))
          .where((part) => part.isNotEmpty)
          .toList();
      if (parts.length >= 3) {
        int channel(String item) => item.endsWith('%')
            ? (double.parse(item.substring(0, item.length - 1)) * 2.55).round()
            : double.parse(item).round();
        final alpha = parts.length > 3
            ? (parts[3].endsWith('%')
                    ? double.parse(
                          parts[3].substring(0, parts[3].length - 1),
                        ) /
                        100
                    : double.parse(parts[3]))
                .clamp(0, 1)
            : 1.0;
        return ui.Color.fromARGB(
          (alpha * 255).round(),
          channel(parts[0]).clamp(0, 255),
          channel(parts[1]).clamp(0, 255),
          channel(parts[2]).clamp(0, 255),
        );
      }
    }
    return switch (normalized) {
      'white' => const ui.Color(0xffffffff),
      'red' => const ui.Color(0xffff0000),
      'green' => const ui.Color(0xff008000),
      'blue' => const ui.Color(0xff0000ff),
      _ => const ui.Color(0xff000000),
    };
  }

  static (double, String?) _parseFont(String font) {
    final match = RegExp(r'([0-9]+(?:\.[0-9]+)?)px\s+(.+)$').firstMatch(font);
    return (
      double.tryParse(match?.group(1) ?? '') ?? 10,
      match == null ? null : _firstFontFamily(match.group(2)!),
    );
  }

  static String? _firstFontFamily(String families) {
    final family = families
        .split(',')
        .first
        .trim()
        .replaceAll(RegExp(r'''^["']|["']$'''), '');
    return switch (family) {
      '-apple-system' ||
      'BlinkMacSystemFont' ||
      'system-ui' ||
      'sans-serif' =>
        null,
      _ => family,
    };
  }

  static FontWeight _fontWeight(int value) => switch (value) {
        <= 150 => FontWeight.w100,
        <= 250 => FontWeight.w200,
        <= 350 => FontWeight.w300,
        <= 450 => FontWeight.w400,
        <= 550 => FontWeight.w500,
        <= 650 => FontWeight.w600,
        <= 750 => FontWeight.w700,
        <= 850 => FontWeight.w800,
        _ => FontWeight.w900,
      };

  static ui.BlendMode _blendMode(String value) => switch (value) {
        'copy' => ui.BlendMode.src,
        'destination-over' => ui.BlendMode.dstOver,
        'source-in' => ui.BlendMode.srcIn,
        'destination-in' => ui.BlendMode.dstIn,
        'source-out' => ui.BlendMode.srcOut,
        'destination-out' => ui.BlendMode.dstOut,
        'source-atop' => ui.BlendMode.srcATop,
        'destination-atop' => ui.BlendMode.dstATop,
        'xor' => ui.BlendMode.xor,
        'lighter' => ui.BlendMode.plus,
        'multiply' => ui.BlendMode.multiply,
        'screen' => ui.BlendMode.screen,
        'overlay' => ui.BlendMode.overlay,
        'darken' => ui.BlendMode.darken,
        'lighten' => ui.BlendMode.lighten,
        'color-dodge' => ui.BlendMode.colorDodge,
        'color-burn' => ui.BlendMode.colorBurn,
        'hard-light' => ui.BlendMode.hardLight,
        'soft-light' => ui.BlendMode.softLight,
        'difference' => ui.BlendMode.difference,
        'exclusion' => ui.BlendMode.exclusion,
        'hue' => ui.BlendMode.hue,
        'saturation' => ui.BlendMode.saturation,
        'color' => ui.BlendMode.color,
        'luminosity' => ui.BlendMode.luminosity,
        _ => ui.BlendMode.srcOver,
      };
}

final class _SvgMaskGeometry {
  const _SvgMaskGeometry(this.shapes);

  final List<(ui.Path, double)> shapes;
}

final class _RetainedLayer {
  const _RetainedLayer({
    required this.nodeId,
    required this.generation,
    required this.zOrder,
    required this.x,
    required this.y,
    required this.width,
    required this.height,
    required this.bitmapWidth,
    required this.bitmapHeight,
    required this.picture,
  });

  final int nodeId;
  final int generation;
  final int zOrder;
  final double x;
  final double y;
  final double width;
  final double height;
  final double bitmapWidth;
  final double bitmapHeight;
  final ui.Picture picture;

  void dispose() => picture.dispose();
}

final class _DomSvgPlacement {
  const _DomSvgPlacement({
    required this.markup,
    required this.viewBox,
    required this.x,
    required this.y,
    required this.width,
    required this.height,
    required this.rotationDegrees,
  });

  final String markup;
  final ui.Rect viewBox;
  final double x;
  final double y;
  final double width;
  final double height;
  final double rotationDegrees;
}

final class _SvgPictureEntry {
  Future<void>? pending;
  ui.Picture? picture;
  Object? error;
}

final class _CanvasState {
  _CanvasState.defaults()
      : fillStyle = '#000000',
        strokeStyle = '#000000',
        lineCap = 'butt',
        lineJoin = 'miter',
        font = '10px sans-serif',
        textAlign = 'start',
        textBaseline = 'alphabetic',
        imageSmoothingQuality = 'low',
        composite = 'source-over',
        shadowColor = 'rgba(0,0,0,0)',
        lineWidth = 1,
        miterLimit = 10,
        globalAlpha = 1,
        lineDashOffset = 0,
        lineDash = [],
        shadowBlur = 0,
        shadowOffsetX = 0,
        shadowOffsetY = 0,
        imageSmoothingEnabled = true;

  _CanvasState._();

  late String fillStyle;
  late String strokeStyle;
  late String lineCap;
  late String lineJoin;
  late String font;
  late String textAlign;
  late String textBaseline;
  late String imageSmoothingQuality;
  late String composite;
  late String shadowColor;
  late double lineWidth;
  late double miterLimit;
  late double globalAlpha;
  late double lineDashOffset;
  late List<double> lineDash;
  late double shadowBlur;
  late double shadowOffsetX;
  late double shadowOffsetY;
  late bool imageSmoothingEnabled;

  _CanvasState copy() => _CanvasState._()
    ..fillStyle = fillStyle
    ..strokeStyle = strokeStyle
    ..lineCap = lineCap
    ..lineJoin = lineJoin
    ..font = font
    ..textAlign = textAlign
    ..textBaseline = textBaseline
    ..imageSmoothingQuality = imageSmoothingQuality
    ..composite = composite
    ..shadowColor = shadowColor
    ..lineWidth = lineWidth
    ..miterLimit = miterLimit
    ..globalAlpha = globalAlpha
    ..lineDashOffset = lineDashOffset
    ..lineDash = List.of(lineDash)
    ..shadowBlur = shadowBlur
    ..shadowOffsetX = shadowOffsetX
    ..shadowOffsetY = shadowOffsetY
    ..imageSmoothingEnabled = imageSmoothingEnabled;
}
