#include "webscene_native_engine.h"

#include <stddef.h>
#include <stdint.h>

_Static_assert(WEBSCENE_SEMANTIC_ACTION_FOCUS_V1 == 1,
    "semantic action kind ABI changed");
_Static_assert(WEBSCENE_SEMANTIC_ACTION_SET_SELECTION_V1 == 7,
    "semantic action kind ABI changed");
_Static_assert(WEBSCENE_SEMANTIC_ACTION_SUPPORT_SET_VALUE_V1 == (1U << 5U),
    "semantic action support ABI changed");
_Static_assert(WEBSCENE_SEMANTIC_ACTION_MAXIMUM_PENDING_V1 == 256U,
    "semantic action queue bound changed");
_Static_assert(WEBSCENE_SEMANTIC_ACTION_MAXIMUM_VALUE_BYTES_V1 == 65536U,
    "semantic action value bound changed");
_Static_assert(offsetof(webscene_semantic_node_v1, supported_actions)
        > offsetof(webscene_semantic_node_v1, description),
    "semantic action capability must remain an additive node field");
_Static_assert(offsetof(webscene_semantic_action_request_v1, semantic_id)
        > offsetof(webscene_semantic_action_request_v1, snapshot_generation),
    "semantic request identity fields changed order");
_Static_assert(offsetof(webscene_semantic_action_request_v1, value_byte_count)
        > offsetof(webscene_semantic_action_request_v1, value_utf8),
    "semantic request payload fields changed order");

int main(void)
{
    webscene_semantic_action_request_v1 request = {0};
    request.struct_size = sizeof(request);
    request.version = 1U;
    request.action = WEBSCENE_SEMANTIC_ACTION_FOCUS_V1;
    return request.value_utf8 == NULL && request.flags == 0U ? 0 : 1;
}
