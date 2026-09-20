# Native Runtime CI bootstrap repair (#819)

WebScene main at `795551990872aaa4d6af9224648c61af7d4c8276` could not
produce Windows or Linux Runtime packages. The final raw JavaScript literal in
`install_editor_web_platform_globals` had grown to 16,539 UTF-8 bytes, beyond
the 15,000-byte portability ceiling that keeps MSVC below C2026's 16,380-byte
literal limit. The same main commit's general CI also used a stale Windows Dawn
export fixture that omitted the required `websceneDawnQueryD3D12DeviceV1`
bridge.

Wiring the existing portability test into the producer preflight also exposed a
15,203-byte Service Worker bootstrap literal. It is split through the same
ordered concatenation path, making the gate cover every currently known
bootstrap family instead of excluding an existing violation.

The editor bootstrap now splits at a complete statement boundary and continues
to use the existing ordered `source_parts` concatenation. The snapshot extractor
and runtime therefore receive the same JavaScript program without increasing
the compiler-safety limit or changing browser behavior. A focused test checks
every editor-platform part against the limit and compares the ordered join with
the extractor output. The native package workflow executes that test before
starting any RID build.

The Windows symbol fixture now contains the exact D3D12 bridge export already
required by the production verifier. This changes test evidence only; it does
not broaden the accepted export surface.

Authoritative failure evidence:

- WebScene CI run `35505459474`, Linux graphics boundary job.
- WebScene NuGet run `35505459479`, Windows and Linux Runtime package jobs.

The AppScene SDK producer must use the WebScene merge commit containing this
repair. The cancelled producer run `35507539063` still points at the broken
WebScene revision and cannot be used as release evidence.
