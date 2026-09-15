#include "webscene_precompiled_javascript.h"
#include <algorithm>
#include <cstring>
extern "C" WEBSCENE_API int webscene_register_precompiled_javascript_v1(const webscene_precompiled_javascript_v1*) { return -1; }
extern "C" WEBSCENE_API int webscene_get_precompiled_javascript_stats_v1(webscene_precompiled_javascript_stats_v1* value) {
    if (!value || value->struct_size < sizeof(*value) || value->version != 1) return -1;
    value->registered_scripts = value->registered_bytes = value->cache_hits = value->cache_rejections = 0;
    return 0;
}
extern "C" WEBSCENE_API int webscene_precompile_javascript_v1(const char*, size_t, const char*, uint32_t,
    webscene_precompiled_javascript_sink_v1, void*, char* error, size_t capacity) {
    constexpr auto message = "This WebScene runtime was built without V8";
    if (error && capacity) {
        const auto size = std::min(std::strlen(message), capacity - 1);
        std::memcpy(error, message, size); error[size] = '\0';
    }
    return -1;
}
