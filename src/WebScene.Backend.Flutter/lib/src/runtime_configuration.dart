import 'dart:convert';
import 'dart:io';

/// Stable reason identifiers used by WebScene's built-in validation messages.
enum WebSceneValidationMessageReason {
  valueMissing(1),
  typeMismatch(2),
  patternMismatch(3),
  tooLong(4),
  tooShort(5),
  rangeUnderflow(6),
  rangeOverflow(7),
  stepMismatch(8),
  badInput(9);

  const WebSceneValidationMessageReason(this.id);

  final int id;
}

/// Immutable, bounded localized messages for browser-owned validation reasons.
///
/// Omitted reasons use WebScene's English message. Custom validity text does
/// not use this catalog and remains verbatim.
final class WebSceneValidationMessageCatalog {
  const WebSceneValidationMessageCatalog.empty() : messages = const {};

  WebSceneValidationMessageCatalog(
    Map<WebSceneValidationMessageReason, String> messages,
  ) : messages = Map.unmodifiable(messages);

  static const int maximumMessageBytes = 1024;

  final Map<WebSceneValidationMessageReason, String> messages;

  void validate() {
    if (messages.length > WebSceneValidationMessageReason.values.length) {
      throw StateError('The validation message catalog has too many entries.');
    }
    for (final entry in messages.entries) {
      final length = utf8.encode(entry.value).length;
      if (length < 1 || length > maximumMessageBytes) {
        throw StateError(
          'Validation message ${entry.key.name} must contain 1 to '
          '$maximumMessageBytes UTF-8 bytes.',
        );
      }
    }
  }

  @override
  bool operator ==(Object other) {
    if (identical(this, other)) return true;
    if (other is! WebSceneValidationMessageCatalog ||
        messages.length != other.messages.length) {
      return false;
    }
    for (final entry in messages.entries) {
      if (other.messages[entry.key] != entry.value) return false;
    }
    return true;
  }

  @override
  int get hashCode => Object.hashAll(
        WebSceneValidationMessageReason.values.map(
          (reason) => Object.hash(reason, messages[reason]),
        ),
      );
}

/// Native libraries and cache settings used by an [WebSceneView].
final class WebSceneRuntimeConfiguration {
  const WebSceneRuntimeConfiguration({
    required this.runtimeLibraryPath,
    required this.bridgeLibraryPath,
    this.compilationCacheDirectory,
    this.validationMessages = const WebSceneValidationMessageCatalog.empty(),
  });

  /// ABI-v2 WebScene native engine library.
  final String runtimeLibraryPath;

  /// Worker-safe Flutter host bridge library for the current platform.
  final String bridgeLibraryPath;

  /// Persistent V8 compilation cache. A temporary cache is used when omitted.
  final String? compilationCacheDirectory;

  /// Localized browser-owned validation messages copied at engine creation.
  final WebSceneValidationMessageCatalog validationMessages;

  void validate() {
    if (!Platform.isMacOS) {
      throw UnsupportedError(
        'WebScene.Backend.Flutter currently supports macOS only.',
      );
    }
    if (!File(runtimeLibraryPath).existsSync()) {
      throw StateError('WebScene runtime not found at $runtimeLibraryPath');
    }
    if (!File(bridgeLibraryPath).existsSync()) {
      throw StateError('WebScene Flutter bridge not found at $bridgeLibraryPath');
    }
    validationMessages.validate();
  }
}

/// JavaScript queued after the document request.
final class WebSceneScript {
  const WebSceneScript(this.source, {required this.documentName});

  final String source;
  final String documentName;
}
