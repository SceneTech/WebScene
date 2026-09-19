import 'dart:convert';
import 'dart:ffi';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:ffi/ffi.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:webscene_flutter/src/native_bindings.dart';
import 'package:webscene_flutter/src/scene_projector.dart';
import 'package:webscene_flutter/src/svg_mask_resource.dart';

void main() {
  testWidgets(
      'paints groups transforms inherited opacity fill rules strokes and defs use',
      (tester) async {
    const markup = '''
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 4 4"
     preserveAspectRatio="none">
  <defs><rect id="unit" width="1" height="1"/></defs>
  <g fill="#fff" opacity="0.5" transform="translate(1 0)">
    <use href="#unit" y="1"/>
  </g>
  <path fill="#fff" fill-rule="evenodd"
        d="M2 0h2v4H2z M2.5 1h1v2h-1z"/>
  <line x1="0.25" y1="0" x2="0.25" y2="4"
        stroke="#fff" stroke-width="0.5"/>
</svg>''';
    final fixture = _MaskScene(
      'webscene-mask-svg-v1\t0 0 4 4\tno-repeat\t0% 0%\t8px 4px\t$markup',
      width: 8,
      height: 4,
    );
    final projector = WebSceneSceneProjector();
    try {
      expect(projector.apply(fixture.pointer).accepted, isTrue);
      final pixels = await _paint(tester, projector, 8, 4);
      expect(_alpha(pixels, 8, 0, 2), greaterThan(180));
      expect(_alpha(pixels, 8, 2, 1), inInclusiveRange(80, 190));
      expect(_alpha(pixels, 8, 5, 2), lessThan(20));
      expect(_alpha(pixels, 8, 7, 2), greaterThan(180));
      expect(projector.svgMaskParseCount, 1);
      expect(projector.svgMaskCacheHitCount, greaterThanOrEqualTo(1));
    } finally {
      projector.dispose();
      fixture.dispose();
    }
  });

  testWidgets('applies repeat position size viewBox and aspect-ratio alignment',
      (tester) async {
    const markup = '''
<svg xmlns="http://www.w3.org/2000/svg" viewBox="10 20 4 2"
     preserveAspectRatio="xMaxYMax meet">
  <rect x="10" y="20" width="2" height="2" fill="#fff"/>
</svg>''';
    final fixture = _MaskScene(
      'webscene-mask-svg-v1\t10 20 4 2\trepeat-x no-repeat'
      '\t1px bottom 0px\t4px 4px\t$markup',
      width: 9,
      height: 5,
    );
    final projector = WebSceneSceneProjector();
    try {
      projector.apply(fixture.pointer);
      final pixels = await _paint(tester, projector, 9, 5);
      expect(_alpha(pixels, 9, 1, 3), greaterThan(180));
      expect(_alpha(pixels, 9, 3, 3), lessThan(20));
      expect(_alpha(pixels, 9, 5, 3), greaterThan(180));
      expect(_alpha(pixels, 9, 1, 1), lessThan(20));
    } finally {
      projector.dispose();
      fixture.dispose();
    }
  });

  testWidgets('bounds cache work and releases pictures over reset cycles',
      (tester) async {
    final cache = SvgMaskResourceCache();
    const viewBox = ui.Rect.fromLTWH(0, 0, 8, 8);
    const shared = '<svg viewBox="0 0 8 8"><rect width="8" height="8"/></svg>';
    expect(cache.acquire(shared, viewBox), isNotNull);
    expect(cache.acquire(shared, viewBox), isNotNull);
    expect(cache.parseCount, 1);
    expect(cache.cacheHits, 1);

    for (var index = 0; index < 4096; index++) {
      cache.acquire(
        '<svg viewBox="0 0 8 8"><path d="M0 0h8v8H0z" id="i$index"/></svg>',
        viewBox,
      );
    }
    expect(cache.entryCount, lessThanOrEqualTo(256));
    expect(cache.logicalBytes, lessThanOrEqualTo(64 * 1024 * 1024));
    expect(cache.parseMicroseconds, greaterThan(0));

    for (var cycle = 0; cycle < 100; cycle++) {
      cache.clear();
      expect(cache.entryCount, 0);
      expect(cache.logicalBytes, 0);
      expect(cache.acquire(shared, viewBox), isNotNull);
    }
    cache.clear();
    expect(cache.entryCount, 0);
    expect(cache.logicalBytes, 0);
  });

  testWidgets('fails closed for depth cycles external use commands and bytes',
      (tester) async {
    final cache = SvgMaskResourceCache();
    const viewBox = ui.Rect.fromLTWH(0, 0, 1, 1);
    final deep = '<svg>${List.filled(65, '<g>').join()}'
        '<rect width="1" height="1"/>'
        '${List.filled(65, '</g>').join()}</svg>';
    const cycle = '''
<svg><defs><g id="a"><use href="#b"/></g><g id="b"><use href="#a"/></g></defs>
<use href="#a"/></svg>''';
    const external = '<svg><use href="https://invalid.test/icon.svg#shape"/></svg>';
    final commands = StringBuffer('<svg><path d="');
    for (var index = 0; index < 65537; index++) {
      commands.write('M0 0');
    }
    commands.write('"/></svg>');
    final oversized = '<svg><!--${List.filled(1024 * 1024, 'x').join()}-->'
        '<rect width="1" height="1"/></svg>';

    expect(cache.acquire(deep, viewBox), isNull);
    expect(cache.acquire(cycle, viewBox), isNull);
    expect(cache.acquire(external, viewBox), isNull);
    expect(cache.acquire(commands.toString(), viewBox), isNull);
    expect(cache.acquire(oversized, viewBox), isNull);
    expect(cache.failedEntryCount, 5);
    expect(cache.logicalBytes, lessThanOrEqualTo(64 * 1024 * 1024));
    cache.clear();
  });
}

Future<ByteData> _paint(
  WidgetTester tester,
  WebSceneSceneProjector projector,
  int width,
  int height,
) async {
  final recorder = ui.PictureRecorder();
  final canvas = ui.Canvas(recorder);
  projector.paint(canvas, ui.Size(width.toDouble(), height.toDouble()));
  final picture = recorder.endRecording();
  final image = await tester.runAsync(() => picture.toImage(width, height));
  final pixels = await tester.runAsync(
    () => image!.toByteData(format: ui.ImageByteFormat.rawRgba),
  );
  image!.dispose();
  picture.dispose();
  return pixels!;
}

int _alpha(ByteData pixels, int width, int x, int y) =>
    pixels.getUint8((y * width + x) * 4 + 3);

final class _MaskScene {
  _MaskScene(String resource, {required int width, required int height})
      : pointer = calloc<WebSceneSceneView>(),
        _commands = calloc<WebSceneSceneCommand>(4),
        _strings = calloc<WebSceneSceneString>(),
        _bytes = calloc<Uint8>(utf8.encode(resource).length) {
    final encoded = utf8.encode(resource);
    _bytes.asTypedList(encoded.length).setAll(0, encoded);
    _strings.ref
      ..byteOffset = 0
      ..byteLength = encoded.length;
    _commands[0]
      ..kind = 30
      ..flags = 1 << 26
      ..width = width.toDouble()
      ..height = height.toDouble()
      ..rgba = 255;
    _commands[1]
      ..kind = 1
      ..width = width.toDouble()
      ..height = height.toDouble()
      ..rgba = 0x2878dcff;
    _commands[2]
      ..kind = 47
      ..flags = 0
      ..width = width.toDouble()
      ..height = height.toDouble();
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
      ..stringByteCount = encoded.length;
    pointer.ref.header
      ..revision = 1
      ..baseRevision = 0
      ..viewportWidth = width.toDouble()
      ..viewportHeight = height.toDouble()
      ..commandCount = 4
      ..canvasLayerCount = 0
      ..damageRectCount = 0
      ..flags = 3;
  }

  final Pointer<WebSceneSceneView> pointer;
  final Pointer<WebSceneSceneCommand> _commands;
  final Pointer<WebSceneSceneString> _strings;
  final Pointer<Uint8> _bytes;

  void dispose() {
    calloc
      ..free(_bytes)
      ..free(_strings)
      ..free(_commands)
      ..free(pointer);
  }
}
