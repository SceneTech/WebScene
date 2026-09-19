#include "webscene_native_engine.h"

#include <stddef.h>
#include <stdint.h>

_Static_assert(WEBSCENE_SEMANTIC_LIVE_REGION_STATUS_V1 == 1,
    "semantic live-region role ABI changed");
_Static_assert(WEBSCENE_SEMANTIC_LIVE_REGION_LOG_V1 == 3,
    "semantic live-region role ABI changed");
_Static_assert(WEBSCENE_SEMANTIC_LIVE_ASSERTIVE_V1 == 2,
    "semantic live politeness ABI changed");
_Static_assert(WEBSCENE_SEMANTIC_LIVE_MAXIMUM_PENDING_EVENTS_V1 == 256U,
    "semantic live queue cap changed");
_Static_assert(WEBSCENE_SEMANTIC_LIVE_MAXIMUM_EVENTS_PER_LEASE_V1 == 64U,
    "semantic live lease cap changed");
_Static_assert(WEBSCENE_SEMANTIC_LIVE_MAXIMUM_TEXT_BYTES_V1 == 65536U,
    "semantic live text cap changed");
_Static_assert(WEBSCENE_SEMANTIC_LIVE_MAXIMUM_QUEUED_TEXT_BYTES_V1 == 1048576U,
    "semantic live aggregate text cap changed");
_Static_assert(offsetof(webscene_semantic_live_event_v1, semantic_id)
        > offsetof(webscene_semantic_live_event_v1, top_document_generation),
    "semantic live identity fields changed order");
_Static_assert(offsetof(webscene_semantic_live_batch_view_v1, lease_token)
        > offsetof(webscene_semantic_live_batch_view_v1, string_bytes),
    "semantic live lease fields changed order");

int main(void)
{
    webscene_semantic_live_event_v1 event = {0};
    webscene_semantic_live_batch_view_v1 batch = {0};
    event.struct_size = sizeof(event);
    event.version = 1U;
    batch.struct_size = sizeof(batch);
    batch.version = 1U;
    return event.text.length == 0U && batch.lease_token == NULL ? 0 : 1;
}
