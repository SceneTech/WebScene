import 'dart:convert';
import 'dart:ffi';
import 'dart:ui' as ui;

import 'package:crypto/crypto.dart';
import 'package:ffi/ffi.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:webscene_flutter/src/native_bindings.dart';
import 'package:webscene_flutter/src/scene_projector.dart';

const _png =
    'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mOQ3RnyHwAESwIqgesNvAAAAABJRU5ErkJggg==';
const _pngIdentity =
    'df3985a7b0f470cc915f32583f5fc79318ff2e3ebb0510057725f14f0fcc02bd';
const _webp = 'UklGRiIAAABXRUJQVlA4IBYAAAAwAQCdASoBAAEADsD+JaQAA3AAAA==';
const _webpIdentity =
    'f568bc89e7899aa082d6a7c4aa1bac15dfeca5153bf51fee2f6198cf35fe6085';

void main() {
  testWidgets('decodes PNG and WebP masks then requests an immutable checkpoint',
      (tester) async {
    for (final fixture in [
      (_png, _pngIdentity, 'mask.png'),
      (_webp, _webpIdentity, 'mask.webp'),
    ]) {
      final resource = _maskV2([
        [
          'url("${fixture.$3}")',
          'repeat-x',
          '1px 0px',
          '2px 2px',
          'match-source',
          '0 0 1 1',
          _envelope(1, 1, fixture.$2, fixture.$1),
        ],
      ]);
      final scene = _MaskScene(resource);
      final projector = WebSceneSceneProjector();
      var checkpoints = 0;
      projector.onNeedsSceneCheckpoint = () => checkpoints++;
      try {
        expect(projector.apply(scene.pointer).accepted, isTrue);
        await tester.runAsync(projector.waitForPendingRasterMaskLoads);
        expect(projector.cachedRasterMaskCount, 1);
        expect(projector.rasterMaskDecodedBytes, inInclusiveRange(4, 64 * 1024 * 1024));
        expect(checkpoints, 1);

        scene.revision = 2;
        expect(projector.apply(scene.pointer).accepted, isTrue);
        final recorder = ui.PictureRecorder();
        final canvas = ui.Canvas(recorder);
        projector.paint(canvas, const ui.Size(8, 4));
        final picture = recorder.endRecording();
        final image = await tester.runAsync(() => picture.toImage(8, 4));
        final pixels = await tester.runAsync(
          () => image!.toByteData(format: ui.ImageByteFormat.rawRgba),
        );
        expect(pixels!.getUint8((1 * 8 + 1) * 4 + 3), greaterThan(0));
        expect(pixels.getUint8((1 * 8 + 3) * 4 + 3), greaterThan(0));
        image!.dispose();
        picture.dispose();
      } finally {
        projector.dispose();
        scene.dispose();
      }
    }
  });

  testWidgets('fails closed for invalid identity dimensions base64 format and cap',
      (tester) async {
    final text = base64Encode(ascii.encode('not an image'));
    final textIdentity = sha256.convert(ascii.encode('not an image')).toString();
    final oversize = base64Encode(List<int>.filled(2621441, 0));
    expect(oversize.length, lessThan(4 * 1024 * 1024));
    final zeroIdentity = List<String>.filled(64, '0').join();
    final invalid = [
      _envelope(1, 1, zeroIdentity, _png),
      _envelope(2, 1, _pngIdentity, _png),
      _envelope(1, 1, _pngIdentity, '%%%'),
      _envelope(1, 1, textIdentity, text),
      _envelope(1, 1, zeroIdentity, oversize),
    ];
    for (final markup in invalid) {
      final scene = _MaskScene(_maskV2([
        ['url("mask")', 'no-repeat', '0% 0%', '8px 4px',
          'match-source', '0 0 1 1', markup],
      ]));
      final projector = WebSceneSceneProjector();
      try {
        projector.apply(scene.pointer);
        await tester.runAsync(projector.waitForPendingRasterMaskLoads);
        expect(projector.cachedRasterMaskCount, 0);
        expect(projector.failedRasterMaskCount, 1);
      } finally {
        projector.dispose();
        scene.dispose();
      }
    }
  });

  testWidgets('stale decode completion cannot invalidate a reset scene',
      (tester) async {
    final scene = _MaskScene(_maskV2([
      ['url("mask")', 'no-repeat', '0% 0%', '8px 4px',
        'match-source', '0 0 1 1', _envelope(1, 1, _pngIdentity, _png)],
    ]));
    final projector = WebSceneSceneProjector();
    var checkpoints = 0;
    projector.onNeedsSceneCheckpoint = () => checkpoints++;
    try {
      projector.apply(scene.pointer);
      projector.reset();
      await tester.runAsync(projector.waitForPendingRasterMaskLoads);
      expect(checkpoints, 0);
      expect(projector.cachedRasterMaskCount, 1);
    } finally {
      projector.dispose();
      scene.dispose();
    }
  });
}

String _envelope(int width, int height, String identity, String payload) =>
    'webscene-raster-v2\t$width\t$height\t$identity\t$payload';

String _maskV2(List<List<String>> layers) {
  final buffer = StringBuffer('webscene-mask-v2\t${layers.length}\t');
  for (final layer in layers) {
    for (final field in layer) {
      buffer
        ..write(utf8.encode(field).length)
        ..write(':')
        ..write(field);
    }
  }
  return buffer.toString();
}

final class _MaskScene {
  _MaskScene(String resource)
      : pointer = calloc<WebSceneSceneView>(),
        _commands = calloc<WebSceneSceneCommand>(4),
        _strings = calloc<WebSceneSceneString>(),
        _bytes = calloc<Uint8>(utf8.encode(resource).length),
        _byteLength = utf8.encode(resource).length {
    final encoded = utf8.encode(resource);
    _bytes.asTypedList(encoded.length).setAll(0, encoded);
    _strings.ref
      ..byteOffset = 0
      ..byteLength = encoded.length;
    _commands[0]
      ..kind = 30
      ..flags = 1 << 26
      ..width = 8
      ..height = 4
      ..rgba = 255;
    _commands[1]
      ..kind = 1
      ..width = 8
      ..height = 4
      ..rgba = 0xff0000ff;
    _commands[2]
      ..kind = 47
      ..flags = 0
      ..width = 8
      ..height = 4;
    _commands[3].kind = 31;
    pointer.ref
      ..structSize = sizeOf<WebSceneSceneView>()
      ..abiVersion = 2
      ..commands = _commands
      ..canvasLayers = nullptr
      ..canvasCommands = nullptr
      ..strings = _strings
      ..stringBytes = _bytes
      ..damageRects = nullptr
      ..leaseToken = nullptr
      ..canvasCommandCount = 0
      ..stringCount = 1
      ..stringByteCount = _byteLength;
    pointer.ref.header
      ..revision = 1
      ..baseRevision = 0
      ..viewportWidth = 8
      ..viewportHeight = 4
      ..commandCount = 4
      ..canvasLayerCount = 0
      ..damageRectCount = 0
      ..flags = 3;
  }

  final Pointer<WebSceneSceneView> pointer;
  final Pointer<WebSceneSceneCommand> _commands;
  final Pointer<WebSceneSceneString> _strings;
  final Pointer<Uint8> _bytes;
  final int _byteLength;

  set revision(int value) {
    pointer.ref.header
      ..revision = value
      ..baseRevision = 0
      ..flags = 3;
  }

  void dispose() {
    calloc
      ..free(_bytes)
      ..free(_strings)
      ..free(_commands)
      ..free(pointer);
  }
}
