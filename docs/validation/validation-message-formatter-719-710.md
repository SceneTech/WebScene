# Validation message formatter contract (#719, #710)

WebScene engine options now have an optional size-versioned
`validation_message_format_callback_v1` tail. The callback receives one of the
stable `WEBSCENE_VALIDATION_MESSAGE_*_V1` reason IDs and zero or one bounded
author constraint argument. Version 1 allows at most four arguments, 256 total
argument bytes, and 1 KiB of UTF-8 output.

The engine calls the formatter synchronously on its owner worker only when a
built-in `validationMessage` or interactive validation message is requested.
It is not called while producing frames. Empty, oversized, invalid UTF-8, or
failed callback results select the existing English message. A recursive
format request also selects that fallback. The host keeps the callback and
user data alive until `webscene_engine_destroy` returns and must not block,
throw, or reenter the engine.

`setCustomValidity()` text bypasses the formatter and remains verbatim for the
DOM property. Interactive visual and semantic output still shares the existing
1 KiB publication bound. Built-in localized output follows one shared path for
the DOM property, visual message state, and semantic announcement.

Managed Avalonia and Uno hosts can set
`NativeWebSceneLoadOptions.ValidationMessageFormatter`. The callback receives
typed reason and argument values; a null or empty result uses English.

AppScene remains a separate consumer change: its WebScene pin and engine
options mirror must expose the new tail and keep any catalog storage alive
through engine destruction. This WebScene slice does not edit AppScene.
The Flutter bridge likewise remains a focused consumer follow-up: its direct
engine creation site must populate the same callback tail and keep its catalog
context alive until native destruction returns.
