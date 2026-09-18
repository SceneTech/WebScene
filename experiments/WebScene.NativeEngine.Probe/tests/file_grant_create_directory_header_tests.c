#include "webscene_native_engine.h"

#include <stddef.h>

_Static_assert(WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_TYPE_MISMATCH_V2 == 4,
    "create-directory type mismatch ABI changed");
_Static_assert(sizeof(webscene_file_grant_create_directory_request_v2) == 56,
    "create-directory request layout changed");
_Static_assert(_Alignof(webscene_file_grant_create_directory_request_v2) == 8,
    "create-directory request alignment changed");
_Static_assert(sizeof(webscene_file_grant_create_directory_completion_v2) == 88,
    "create-directory completion layout changed");

int main(void) {
    webscene_file_grant_create_directory_request_v2 request = {0};
    webscene_file_grant_create_directory_completion_v2 completion = {0};
    return request.struct_size != completion.struct_size;
}
