#include "webscene_native_engine.h"

#include <stddef.h>

_Static_assert(sizeof(webscene_file_grant_release_request_v2) == 32,
    "grant release request ABI changed");
_Static_assert(_Alignof(webscene_file_grant_release_request_v2) == 8,
    "grant release request alignment changed");
_Static_assert(offsetof(webscene_file_grant_release_request_v2, grant_id) == 8,
    "grant release token offset changed");
_Static_assert(offsetof(webscene_file_grant_release_request_v2, reserved) == 24,
    "grant release reserved offset changed");

int main(void) {
    webscene_file_grant_release_request_v2 request = {0};
    request.struct_size = (uint32_t)sizeof(request);
    request.version = 2;
    return request.struct_size == sizeof(request) && request.version == 2 ? 0 : 1;
}
