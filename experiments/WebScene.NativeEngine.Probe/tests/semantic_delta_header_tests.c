#include "webscene_native_engine.h"

#include <stddef.h>
#include <stdint.h>

_Static_assert(WEBSCENE_SEMANTIC_DELTA_REMOVE_V1 == 1,
    "semantic delta operation ABI changed");
_Static_assert(WEBSCENE_SEMANTIC_DELTA_FOCUS_V1 == 7,
    "semantic delta operation ABI changed");
_Static_assert(WEBSCENE_SEMANTIC_DELTA_STALE_BASE_V1 == 2,
    "semantic delta admission ABI changed");
_Static_assert(WEBSCENE_SEMANTIC_DELTA_MAXIMUM_PENDING_REQUESTS_V1 == 1U,
    "semantic delta request cap changed");
_Static_assert(WEBSCENE_SEMANTIC_DELTA_MAXIMUM_COMPLETED_LEASES_V1 == 1U,
    "semantic delta completion cap changed");
_Static_assert(WEBSCENE_SEMANTIC_DELTA_MAXIMUM_RETAINED_BASE_SNAPSHOTS_V1 == 2U,
    "semantic delta retained-base cap changed");
_Static_assert(WEBSCENE_SEMANTIC_DELTA_MAXIMUM_OPERATIONS_V1 == 8192U,
    "semantic delta operation cap changed");
_Static_assert(WEBSCENE_SEMANTIC_DELTA_MAXIMUM_STRING_BYTES_V1 == 2097152U,
    "semantic delta string cap changed");
_Static_assert(offsetof(webscene_semantic_delta_operation_v1, semantic_id)
        > offsetof(webscene_semantic_delta_operation_v1, kind),
    "semantic delta operation identity moved");
_Static_assert(offsetof(webscene_semantic_delta_view_v1, new_snapshot_generation)
        > offsetof(webscene_semantic_delta_view_v1, base_snapshot_generation),
    "semantic delta generation order changed");
_Static_assert(offsetof(webscene_semantic_delta_view_v1, lease_token)
        > offsetof(webscene_semantic_delta_view_v1, string_bytes),
    "semantic delta lease layout changed");

int main(void)
{
    webscene_semantic_delta_request_v1 request = {0};
    webscene_semantic_delta_view_v1 view = {0};
    request.struct_size = sizeof(request);
    request.version = 1U;
    view.struct_size = sizeof(view);
    view.version = 1U;
    return request.flags == 0U && view.operations == NULL ? 0 : 1;
}
