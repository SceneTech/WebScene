# Editor bootstrap literal portability recovery

Issue #653 repairs the NuGet run 35435909467 failure under #227. Windows,
Linux and macOS configuration all stopped when
`extract_bootstraps.py` found a 22,060-byte raw JavaScript literal after
`install_editor_web_platform_globals`. That exceeded the existing 15,000-byte
portability ceiling which protects the MSVC 16,380-byte string-literal limit.

The custom-elements portion of the editor web-platform bootstrap is now split
at complete JavaScript statement boundaries. Each new raw literal remains
below the existing ceiling. The C++ loop and the snapshot extractor concatenate
the literals directly in declaration order without inserting separators, so
the generated JavaScript program, its single realm and shared lexical scope are
preserved. Global installation still occurs after all constructors, helpers
and descriptors have been declared, in the same order as before.

The portability limit remains enabled and unchanged. Only
`git diff --check` was run under the directed fast policy; bootstrap extraction,
package configuration, builds and tests remain for CI.
