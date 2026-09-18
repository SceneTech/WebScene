#include "webscene_native_engine.h"

#include <stddef.h>

_Static_assert(sizeof(webscene_file_grant_durable_request_v2) == 88,
    "durable request ABI changed");
_Static_assert(_Alignof(webscene_file_grant_durable_request_v2) == 8,
    "durable request alignment changed");
_Static_assert(sizeof(webscene_file_grant_durable_completion_v2) == 96,
    "durable completion ABI changed");
_Static_assert(offsetof(webscene_file_grant_durable_request_v2, grant_id) == 24,
    "durable grant offset changed");
_Static_assert(offsetof(webscene_file_grant_durable_completion_v2,
    display_name) == 64, "durable display name offset changed");

int main(void) {
    webscene_file_grant_durable_request_v2 request = {0};
    webscene_file_grant_durable_completion_v2 completion = {0};
    request.struct_size = (uint32_t)sizeof(request);
    completion.struct_size = (uint32_t)sizeof(completion);
    return request.struct_size == sizeof(request)
        && completion.struct_size == sizeof(completion) ? 0 : 1;
}
