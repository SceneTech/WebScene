#include "webscene_native_engine.h"

#include <stddef.h>

_Static_assert(sizeof(webscene_file_grant_ancestry_request_v2) == 56,
    "ancestry request ABI changed");
_Static_assert(_Alignof(webscene_file_grant_ancestry_request_v2) == 8,
    "ancestry request alignment changed");
_Static_assert(sizeof(webscene_file_grant_ancestry_component_v2) == 24,
    "ancestry component ABI changed");
_Static_assert(sizeof(webscene_file_grant_ancestry_completion_v2) == 56,
    "ancestry completion ABI changed");
_Static_assert(offsetof(webscene_file_grant_ancestry_request_v2,
    base_directory_grant_id) == 16, "ancestry base token offset changed");
_Static_assert(offsetof(webscene_file_grant_ancestry_completion_v2,
    components) == 24, "ancestry components offset changed");

int main(void) {
    webscene_file_grant_ancestry_request_v2 request = {0};
    webscene_file_grant_ancestry_component_v2 component = {0};
    webscene_file_grant_ancestry_completion_v2 completion = {0};
    request.struct_size = (uint32_t)sizeof(request);
    component.struct_size = (uint32_t)sizeof(component);
    completion.struct_size = (uint32_t)sizeof(completion);
    return request.struct_size == sizeof(request)
        && component.struct_size == sizeof(component)
        && completion.struct_size == sizeof(completion) ? 0 : 1;
}
