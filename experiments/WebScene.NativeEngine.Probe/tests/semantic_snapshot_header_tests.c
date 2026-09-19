#include "webscene_native_engine.h"

#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(webscene_semantic_string_v1) == 8,
    "semantic string ABI changed");
_Static_assert(offsetof(webscene_semantic_node_v1, semantic_id) == 8,
    "semantic node identity offset changed");
_Static_assert(offsetof(webscene_semantic_node_v1, role)
        < offsetof(webscene_semantic_node_v1, description),
    "semantic string fields changed order");
_Static_assert(WEBSCENE_SEMANTIC_NONE_INDEX_V1 == UINT32_MAX,
    "semantic absent-index sentinel changed");
_Static_assert(WEBSCENE_SEMANTIC_RELATION_ACTIVE_DESCENDANT_V1 == 5,
    "semantic relationship ABI changed");

int main(void)
{
    webscene_semantic_snapshot_view_v1 view = {0};
    view.struct_size = sizeof(view);
    view.version = 1;
    return view.focused_node_index == 0 ? 0 : 1;
}
