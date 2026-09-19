# Web Crypto transient-key zeroization source contract

Issue: WebScene #102
Authoring baseline: `c57d82dfef38c8e18eb691d54d3132851e34025a`

## Provider and exposure decision

WebScene already ships the accepted VS Code-driven Web Crypto surface through
the SHA-256-pinned Mbed TLS 3.6.4 source release on macOS ARM64, Linux x64, and
Windows x64. `SubtleCrypto` is exposed because SHA-1/SHA-256 digest, AES-GCM,
AES-CBC decrypt, and HMAC sign have complete native bindings for their advertised
operations. The bindings retain their 16 MiB per-input, 64 MiB queued-input,
32-operation, realm-ownership, owner-task-queue settlement, and shutdown
cancellation boundaries.

The audit found one lifetime gap inside the complete key-import/export boundary.
A malformed JWK could leave its successfully decoded prefix in an ordinary
`std::vector`, and valid JWK conversion left temporary Base64URL text in an
ordinary `std::string`. These transient copies were outside the provider's
documented deterministic-zeroization invariant.

## Authored source contract

- `base64url_decode` now accumulates every decoded prefix in `secure_bytes`.
  Destruction zeroizes the entire reserved buffer with
  `mbedtls_platform_zeroize`, including all early returns for an illegal
  character or non-canonical tail bits.
- AES and HMAC JWK imports hold the V8-to-UTF-8 `k` conversion only until decode
  completes, then zeroize the backing string before accepting or rejecting it.
- Imports bound the V8 string's UTF-8 length before making that native copy:
  AES admits at most the 43 unpadded Base64URL bytes needed by a 256-bit key,
  while HMAC derives its encoded ceiling from the existing 16 MiB key bound.
- JWK export copies Base64URL text into V8 and zeroizes the native temporary
  before publishing the result object.
- The AES-GCM and HMAC native/WPT-derived contracts contain a malformed `k`
  value with a long valid prefix and an illegal final character. It must reject
  with `DataError`; no incomplete key is admitted to the opaque store.
- Published NIST AES-GCM and RFC 4231 HMAC vectors remain in the same cumulative
  contracts, so the security regression cannot replace the functional provider
  checks.

## Package and update policy

Mbed TLS remains pinned by archive SHA-256 in the native CMake graph and its
Apache-2.0 license remains mandatory in every runtime package. A provider update
must review upstream security advisories and licensing, change the version and
digest together, rebuild every supported RID, and rerun the published-vector,
WPT-derived, shutdown, bounds, package-link, and transient-zeroization reviews.
There is no platform-provider or hand-written primitive fallback.

## Promotion gates

This change was authored under the implementation-first lane and only
`git diff --check` is recorded for the commit. Before release promotion, run the
focused provider and hybrid V8 Web Crypto runners, the WebPlatformSubset
contracts, installed-runtime link/package/license verification, ASan or
equivalent memory review, and unchanged packaged VS Code consumer acceptance on
all three release RIDs. Product acceptance still needs configured MCP secret
storage and browser connection-secret persistence/reopen. Stock Code OSS has no
proprietary VSDA assets, and bundled Copilot HMAC executes in its Node extension
host; those facts must remain explicit rather than simulated as product proof.
