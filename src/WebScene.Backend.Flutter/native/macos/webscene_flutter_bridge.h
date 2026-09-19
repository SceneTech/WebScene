#pragma once

#include <stddef.h>
#include <stdint.h>

#include "webscene_native_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    WEBSCENE_FLUTTER_VALIDATION_MESSAGE_COUNT_V1 = 9U,
    WEBSCENE_FLUTTER_VALIDATION_MESSAGE_MAX_BYTES_V1 = 1024U
};

typedef struct webscene_flutter_validation_message_v1 {
    uint32_t struct_size;
    uint32_t reason;
    const char* message_utf8;
    size_t message_length;
} webscene_flutter_validation_message_v1;

typedef struct webscene_flutter_engine_options_v1 {
    uint32_t struct_size;
    uint32_t validation_message_count;
    const webscene_flutter_validation_message_v1* validation_messages;
} webscene_flutter_engine_options_v1;

webscene_engine* webscene_flutter_engine_create(
    const char* runtime_path,
    const char* cache_directory);

webscene_engine* webscene_flutter_engine_create_v2(
    const char* runtime_path,
    const char* cache_directory,
    const webscene_flutter_engine_options_v1* options);

void webscene_flutter_engine_destroy(webscene_engine* engine);
const char* webscene_flutter_last_error(void);
uint64_t webscene_flutter_resource_request_count(webscene_engine* engine);

#ifdef __cplusplus
}
#endif
