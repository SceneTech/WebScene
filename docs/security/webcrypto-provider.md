# Web Crypto provider policy

WebScene uses the pinned Mbed TLS 3.6.4 release for Web Crypto primitives. The
source archive is fixed by SHA-256 in the native runtime CMake graph. Runtime
code has no platform-provider or software fallback, so every supported RID
executes the same provider implementation.

The accepted surface includes SHA-1 and SHA-256 digest plus AES-GCM 128, 192,
and 256-bit secret keys, raw/JWK import and export, and authenticated encrypt
and decrypt. SHA-1 is available only because the Web Crypto API requires it for
compatibility; callers must not use it for new collision-resistant designs.
AES-CBC decrypt is available for 128, 192, and 256-bit imported raw or JWK keys
and performs Web Crypto PKCS#7 validation inside the provider boundary. HMAC
remains unavailable until its focused API and vector layer lands.

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

Digest work is chunked so a realm shutdown stop request is observed between
provider updates. The JavaScript layer bounds each input at 16 MiB, aggregate
pending input at 64 MiB, and concurrency at 32 requests. It copies input into
zeroizing storage during the call and settles promises on the runtime owner
thread. Background provider work never enters V8.

AES-GCM validates key usages and realm ownership before copying key bytes into
zeroizing asynchronous task storage. Authentication failure exposes no
plaintext. Data and additional authenticated data are each bounded at 16 MiB;
aggregate queued cipher storage is bounded at 64 MiB and 32 operations. Realm
shutdown requests cancellation, joins provider work, and zeroizes all retained
keys and task inputs.
