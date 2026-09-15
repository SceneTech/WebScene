#pragma once
#include "webscene_native_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build-time cache data is executable input. Only register artifacts produced by
 * the matching, trusted SDK. This API is not an untrusted-bytecode sandbox.
 * Registration copies all borrowed data and is safe before V8 initialization.
 * Sources remain required by V8 and are not stripped by this feature. */
typedef struct webscene_precompiled_javascript_v1 {
    uint32_t struct_size;
    uint32_t version;                  /* 1 */
    uint32_t is_module;                /* 0: classic, 1: ECMAScript module */
    uint32_t cached_data_version_tag;
    uint8_t source_sha256[32];
    uint8_t payload_sha256[32];
    const char* v8_version;
    const char* snapshot_fingerprint;  /* empty for a no-bootstrap build */
    const uint8_t* data;
    size_t data_length;
} webscene_precompiled_javascript_v1;

typedef struct webscene_precompiled_javascript_stats_v1 {
    uint32_t struct_size;
    uint32_t version;
    uint64_t registered_scripts;
    uint64_t registered_bytes;
    uint64_t cache_hits;
    uint64_t cache_rejections;
} webscene_precompiled_javascript_stats_v1;

/* 0 on success; negative on invalid input/allocation/identity conflict.
 * Compatible duplicate registrations are deduplicated by source hash and kind.
 * There are no process-global V8 handles or mutable application objects here. */
WEBSCENE_API int webscene_register_precompiled_javascript_v1(
    const webscene_precompiled_javascript_v1* entry);
WEBSCENE_API int webscene_get_precompiled_javascript_stats_v1(
    webscene_precompiled_javascript_stats_v1* stats);

/* Compiler-side API. Creates a temporary isolate using WebScene's real V8
 * initialization and bootstrap snapshot. It compiles classic scripts eagerly but NEVER runs or
 * instantiates user scripts/modules. Modules retain V8's lazy-function policy. The result is borrowed until sink returns.
 * Returns 0 on success, negative on failure; error is UTF-8 and NUL terminated.
 * Call on an SDK build host matching the target, not in a running UI application. */
typedef int (*webscene_precompiled_javascript_sink_v1)(
    const webscene_precompiled_javascript_v1* entry, void* user_data);
WEBSCENE_API int webscene_precompile_javascript_v1(
    const char* source, size_t source_length, const char* name, uint32_t is_module,
    webscene_precompiled_javascript_sink_v1 sink, void* user_data,
    char* error, size_t error_capacity);

#ifdef __cplusplus
}
#endif
