# macOS 12 form conversion and package build (#908)

The `NuGet packages / Build osx-arm64` job compiled for macOS 12 but used
floating `std::from_chars` (available from macOS 26) and floating
`std::to_chars` (available from macOS 13.3). Runs `35585510953`,
`35581500980`, and `35574320848` failed before packaging. The fix keeps
the deployment target at macOS 12: on older Apple targets, form numbers
use the C numeric locale with `strtod_l` and `snprintf_l`; integer
`charconv` and newer-target floating `charconv` remain unchanged. The
accessibility numeric step action now uses the same form formatter.

## Focused gate

`webscene_native_form_charconv_portability_tests` compiles and links with
`-mmacosx-version-min=12.0` and forces the portable path. It checks invalid
and finite numbers, negative zero, representable subnormals, exponent
notation boundaries, time serialization, locale independence, and 10,000
round-trips below 500 ms. The macOS arm64 Release build passed locally:

- Mach-O `LC_BUILD_VERSION` minimum: `12.0`.
- Undefined conversion symbols: `_snprintf_l` and `_strtod_l`; no floating
  `std::from_chars` or `std::to_chars` symbol.
- CTest: 1/1 passed; 10,000 round-trips: 8.6 ms (19.2 ms at `-O0`).
- A separate deterministic 99,969-value finite-double sample compared the
  portable formatter/parser with the host's modern `charconv`: zero text
  differences and zero bitwise parse differences.

The package CI job remains the authoritative test that the complete native
engine compiles and packages with this deployment target. The focused test
does not assert full Code OSS visual acceptance.
