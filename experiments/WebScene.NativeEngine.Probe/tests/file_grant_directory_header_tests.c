#include "webscene_native_engine.h"

#include <stddef.h>

_Static_assert(sizeof(webscene_file_grant_directory_request_v2) == 64,
    "directory request ABI changed");
_Static_assert(_Alignof(webscene_file_grant_directory_request_v2) == 8,
    "directory request alignment changed");
_Static_assert(sizeof(webscene_file_grant_directory_entry_v2) == 80,
    "directory entry ABI changed");
_Static_assert(sizeof(webscene_file_grant_directory_completion_v2) == 64,
    "directory completion ABI changed");
_Static_assert(offsetof(webscene_file_grant_directory_request_v2, cursor) == 40,
    "directory cursor offset changed");

int main(void) {
    webscene_file_grant_directory_request_v2 request = {0};
    webscene_file_grant_directory_completion_v2 completion = {0};
    request.struct_size = (uint32_t)sizeof(request);
    request.version = 2;
    completion.struct_size = (uint32_t)sizeof(completion);
    completion.version = 2;
    return request.version == completion.version ? 0 : 1;
}
