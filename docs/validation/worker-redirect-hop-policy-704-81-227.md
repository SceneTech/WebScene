# Worker redirect-hop policy (#704, #81, #227)

## Admitted-resource contract

The product-neutral resource response can now carry an ordered redirect ledger.
Each entry contains only the source URL, destination URL, redirect status,
credential-forwarding decision, admitted CORS origin, and CORS-credentials
decision. It carries no cookies, authorization values, bodies, or general
response headers.

The public `webscene_resource_response_v4` adds the ledger as an optional tail.
Existing callbacks remain valid because the engine zero-initializes that tail;
providers compiled against the extended declaration check `struct_size` before
writing it. The native adapter accepts at most 20 hops, 8 KiB per URL/origin
field, and 64 KiB for all response headers and redirect metadata combined.
Unknown flags, unsupported redirect statuses, invalid pointers, control bytes,
and oversized metadata fail the resource load.

## Worker enforcement

A Worker response whose final URL differs from its requested URL must provide a
complete ledger. The first source must equal the request URL, every destination
must equal the next source, and the final destination must equal the response's
final URL. Only HTTP(S) URLs and 301, 302, 303, 307, and 308 statuses are
accepted. Repeated destinations, more than 20 hops, malformed URLs, and HTTPS
to HTTP downgrades fail closed.

Classic worker loads continue to require the creator origin at every source and
destination. Module CORS loads validate each cross-origin redirect response's
admitted origin and require its credentials decision for `include`. A wildcard
origin is rejected for `include`. A provider may not report forwarded
credentials for `omit`, or for a cross-origin destination under `same-origin`.
The existing final-response status, origin, and CORS checks still run after the
ledger check.

The redirect ledger is validated after the asynchronous host callback and
before response cookies, source bytes, the final dependency base, compilation,
or an error can be published. Worker generation checks on both sides of the
callback continue to suppress late completion after termination, navigation,
or engine teardown. Terminal failure remains exactly once through #701's
bounded error and slot-retirement path.

All existing limits remain unchanged: 64 live Workers, 256 retained stopped
wrappers, 256 messages and 16 MiB per message direction, and 16 bounded error
strings. Redirect metadata adds fixed count and byte caps without adding a
queue or retained background task.

## Deferred acceptance

No build, native suite, redirect/CORS WPT, failure-injection run, credential
review, lifecycle stress, performance, memory, package, or cross-platform CI
was executed under the implementation-first policy. Those gates remain under
#81 and #227. The provider still owns transport and must truthfully describe
each followed hop; a future callback-style handshake would be required for the
engine to authorize a destination before the provider performs network I/O.
MessagePort reachability #288 and general iframe lifecycle #267 are unchanged.
