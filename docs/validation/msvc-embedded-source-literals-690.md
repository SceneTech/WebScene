# MSVC embedded source literal portability

Issue #690 repairs the Windows NuGet run 35449781793 failures where MSVC
reported C2026 for hand-authored JavaScript embedded in the stylesheet CSSOM,
cache/frame and File System Access sources. Their largest individual raw
literals were 83,590, 16,956 and 38,859 bytes respectively, above MSVC's
16,380-byte string-literal limit.

Each program is now an ordered array of raw-literal views whose individual
parts stay below a conservative 15,000-byte ceiling. The reusable source
contract checks that ceiling at compile time. The join helper reserves the
exact total size and concatenates each array once at realm installation,
without separators, so V8 receives the same program bytes in one compile and
run operation. The split does not add per-frame work or retained duplicate
payloads.

The stylesheet CSSOM JavaScript contract reconstructs the array in declaration
order before exercising the existing browser-facing behavior. Future edits
must add another part before any literal crosses the shared ceiling. The
compiler-generated-file incident from the same Windows job remains outside
this issue.

Only `git diff --check` was run under the directed fast policy. Windows package
compilation and the existing runtime contracts remain for CI.
