#include "webscene_native_engine.h"

#include <stddef.h>

_Static_assert(sizeof(webscene_file_grant_write_request_v2) == 88,
    "write request ABI changed");
_Static_assert(_Alignof(webscene_file_grant_write_request_v2) == 8,
    "write request alignment changed");
_Static_assert(sizeof(webscene_file_grant_write_completion_v2) == 88,
    "write completion ABI changed");
_Static_assert(offsetof(webscene_file_grant_write_request_v2, data) == 64,
    "write request data offset changed");

int main(void) {
    webscene_file_grant_write_request_v2 request = {0};
    webscene_file_grant_write_completion_v2 completion = {0};
    request.struct_size = (uint32_t)sizeof(request);
    request.version = 2;
    completion.struct_size = (uint32_t)sizeof(completion);
    completion.version = 2;
    return request.version == completion.version ? 0 : 1;
}
