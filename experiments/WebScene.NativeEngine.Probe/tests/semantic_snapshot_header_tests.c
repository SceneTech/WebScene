#include "webscene_native_engine.h"

#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(webscene_semantic_string_v1) == 8,
    "semantic string ABI changed");
_Static_assert(sizeof(webscene_semantic_node_v1) == 96,
    "semantic v1 node stride changed");
_Static_assert(offsetof(webscene_semantic_node_v1, semantic_id) == 8,
    "semantic node identity offset changed");
_Static_assert(offsetof(webscene_semantic_node_v1, role)
        < offsetof(webscene_semantic_node_v1, description),
    "semantic string fields changed order");
_Static_assert(offsetof(webscene_semantic_node_v1, supported_actions)
        > offsetof(webscene_semantic_node_v1, description),
    "semantic node actions must remain additive");
_Static_assert(offsetof(webscene_semantic_typed_value_v2, numeric_value)
        > offsetof(webscene_semantic_typed_value_v2, text_selection_end_utf16),
    "typed semantic numeric data changed order");
_Static_assert(sizeof(webscene_semantic_typed_value_v2) == 64,
    "typed semantic companion stride changed");
_Static_assert(offsetof(webscene_semantic_snapshot_view_v2, typed_values)
        >= sizeof(webscene_semantic_snapshot_view_v1),
    "v2 semantic snapshot changed the v1 prefix");
_Static_assert(WEBSCENE_SEMANTIC_TYPED_TEXT_SELECTION_V2 == (1U << 5U),
    "typed semantic presence flags changed");
_Static_assert(WEBSCENE_SEMANTIC_NONE_INDEX_V1 == UINT32_MAX,
    "semantic absent-index sentinel changed");
_Static_assert(WEBSCENE_SEMANTIC_RELATION_ACTIVE_DESCENDANT_V1 == 5,
    "semantic relationship ABI changed");
_Static_assert(WEBSCENE_SEMANTIC_RELATION_ERROR_MESSAGE_V1 == 6,
    "semantic error-message relationship ABI changed");

int main(void)
{
    webscene_semantic_snapshot_view_v1 view = {0};
    view.struct_size = sizeof(view);
    view.version = 1;
    return view.focused_node_index == 0 ? 0 : 1;
}
