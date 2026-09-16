# Web Crypto provider policy

WebScene uses the pinned Mbed TLS 3.6.4 release for Web Crypto primitives. The
source archive is fixed by SHA-256 in the native runtime CMake graph. Runtime
code has no platform-provider or software fallback, so every supported RID
executes the same provider implementation.

The initial accepted surface is SHA-1 and SHA-256 digest. SHA-1 is available
only because the Web Crypto API requires it for compatibility; callers must not
use it for new collision-resistant designs. AES and HMAC remain unavailable
until their own API, vector, key-usage, cancellation, and packaged-runtime
acceptance layers land.

Mbed TLS is Apache-2.0 licensed. Its license is included in every native runtime
package. The 3.6 branch is upstream's long-term-support line, but this pin does
not claim a validated FIPS module. Provider updates require a reviewed version
and archive hash change, published digest vectors, all provider lifecycle tests,
and the cumulative top runtime contract.

Opaque key bytes live only in `secure_bytes`. Destruction, replacement, realm
shutdown, and explicit clearing call `mbedtls_platform_zeroize`; copy operations
are disabled. The key store checks realm ownership and allowed usages before it
exposes bytes to a provider callback. Digest input is copied before asynchronous
work and uses the same zeroizing storage. Provider contexts are freed on every
success, error, and cancellation path.

Digest work is chunked so a stop request is observed between provider updates.
The JavaScript digest layer must bound and copy input into zeroizing storage,
bound concurrent requests, and settle its promises on the runtime owner thread.
Background provider work must never enter V8.
