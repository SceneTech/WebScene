#include "webscene_native_engine.h"

_Static_assert(WEBSCENE_FILE_GRANT_REMOVE_INVALID_MODIFICATION_V2 == 4,
    "remove invalid-modification ABI changed");
_Static_assert(sizeof(webscene_file_grant_remove_request_v2) == 56,
    "remove request layout changed");
_Static_assert(_Alignof(webscene_file_grant_remove_request_v2) == 8,
    "remove request alignment changed");
_Static_assert(sizeof(webscene_file_grant_remove_completion_v2) == 24,
    "remove completion layout changed");

int main(void) {
    webscene_file_grant_remove_request_v2 request = {0};
    webscene_file_grant_remove_completion_v2 completion = {0};
    return request.struct_size != completion.struct_size;
}
