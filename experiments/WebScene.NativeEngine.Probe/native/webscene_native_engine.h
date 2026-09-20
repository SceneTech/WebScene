#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(WEBSCENE_NATIVE_ENGINE_BUILD)
#    define WEBSCENE_API __declspec(dllexport)
#  else
#    define WEBSCENE_API __declspec(dllimport)
#  endif
#else
#  define WEBSCENE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct webscene_engine webscene_engine;
typedef void (*webscene_diagnostic_available_callback)(void* user_data);
enum {
    WEBSCENE_DIAGNOSTIC_EXCEPTIONS = 1U,
    WEBSCENE_DIAGNOSTIC_CONSOLE = 2U,
    WEBSCENE_DIAGNOSTIC_LEGACY_CONSOLE = 4U,
    WEBSCENE_DIAGNOSTIC_RESOURCE_FAILURES = 8U
};

/* Additive ABI 3 diagnostics. Callback is a non-blocking signal only; do not
 * re-enter the engine from it. Passing null unregisters synchronously. */
WEBSCENE_API void webscene_engine_configure_diagnostics(
    webscene_engine* engine, uint32_t flags,
    webscene_diagnostic_available_callback callback, void* user_data);
/* UTF-8 JSON, including trailing NUL. Null/short buffers do not consume. */
WEBSCENE_API size_t webscene_engine_take_diagnostic(
    webscene_engine* engine, char* destination, size_t destination_capacity);
/* Non-consuming terminal status, independent of ordinary script error counters. */
WEBSCENE_API size_t webscene_engine_copy_runtime_failure(
    webscene_engine* engine, char* destination, size_t destination_capacity);
typedef struct webscene_scene_view webscene_scene_view;
typedef struct webscene_interop_result_view_v3 webscene_interop_result_view_v3;
typedef struct webscene_interop_callback_view_v3 webscene_interop_callback_view_v3;

/* Legacy direct-message callback retained for ABI compatibility. */
typedef void (*webscene_inspector_message_callback)(
    void* user_data,
    uint64_t session_id,
    const char* message,
    size_t message_length);

/* Signals that one or more Inspector messages can be pulled off the worker. */
typedef void (*webscene_inspector_message_available_callback_v3)(
    void* user_data,
    uint64_t session_id);

typedef enum webscene_input_kind {
    WEBSCENE_INPUT_POINTER_MOVE = 1,
    WEBSCENE_INPUT_POINTER_DOWN = 2,
    WEBSCENE_INPUT_POINTER_UP = 3,
    WEBSCENE_INPUT_WHEEL = 4,
    // x carries the host compositor's monotonic timestamp in milliseconds.
    WEBSCENE_INPUT_FRAME = 5,
    WEBSCENE_INPUT_RESIZE = 6,
    // x carries a DOM-compatible key code for keyboard events.
    WEBSCENE_INPUT_KEY_DOWN = 7,
    WEBSCENE_INPUT_KEY_UP = 8,
    // x carries one Unicode scalar value. Hosts enqueue one event per scalar.
    WEBSCENE_INPUT_TEXT = 9,
    // A discrete host-surface exit, not a mousemove. Coordinates may still be
    // the last known position inside the surface. Does not cancel capture.
    WEBSCENE_INPUT_POINTER_LEAVE = 10
} webscene_input_kind;

typedef enum webscene_cursor_kind {
    WEBSCENE_CURSOR_DEFAULT = 0,
    WEBSCENE_CURSOR_POINTER = 1,
    WEBSCENE_CURSOR_TEXT = 2,
    WEBSCENE_CURSOR_CROSSHAIR = 3,
    WEBSCENE_CURSOR_WAIT = 4,
    WEBSCENE_CURSOR_MOVE = 5,
    WEBSCENE_CURSOR_NOT_ALLOWED = 6,
    WEBSCENE_CURSOR_HELP = 7,
    WEBSCENE_CURSOR_EW_RESIZE = 8,
    WEBSCENE_CURSOR_NS_RESIZE = 9
} webscene_cursor_kind;

typedef enum webscene_preferred_color_scheme {
    WEBSCENE_PREFERRED_COLOR_SCHEME_LIGHT = 0,
    WEBSCENE_PREFERRED_COLOR_SCHEME_DARK = 1
} webscene_preferred_color_scheme;

typedef enum webscene_accessibility_preference_flags_v1 {
    WEBSCENE_ACCESSIBILITY_PREFERENCE_NONE_V1 = 0,
    WEBSCENE_ACCESSIBILITY_PREFERENCE_FORCED_COLORS_V1 = 1U << 0U,
    WEBSCENE_ACCESSIBILITY_PREFERENCE_REDUCED_MOTION_V1 = 1U << 1U,
    WEBSCENE_ACCESSIBILITY_PREFERENCE_MORE_CONTRAST_V1 = 1U << 2U
} webscene_accessibility_preference_flags_v1;

/*
 * Immutable, host-readable accessibility projection. Strings are UTF-8
 * slices into semantic_snapshot_view.string_bytes. Node and relationship
 * indices use WEBSCENE_SEMANTIC_NONE_INDEX_V1 when absent. A lease is a
 * complete point-in-time value and may outlive its engine.
 */
enum {
    WEBSCENE_SEMANTIC_NONE_INDEX_V1 = UINT32_MAX,
    WEBSCENE_SEMANTIC_SNAPSHOT_TRUNCATED_NODES_V1 = 1U << 0U,
    WEBSCENE_SEMANTIC_SNAPSHOT_TRUNCATED_RELATIONSHIPS_V1 = 1U << 1U,
    WEBSCENE_SEMANTIC_SNAPSHOT_TRUNCATED_STRINGS_V1 = 1U << 2U,
    WEBSCENE_SEMANTIC_SNAPSHOT_TRUNCATED_DOCUMENTS_V1 = 1U << 3U,
    WEBSCENE_SEMANTIC_NODE_DISABLED_V1 = UINT64_C(1) << 0U,
    WEBSCENE_SEMANTIC_NODE_READONLY_V1 = UINT64_C(1) << 1U,
    WEBSCENE_SEMANTIC_NODE_SELECTED_V1 = UINT64_C(1) << 2U,
    WEBSCENE_SEMANTIC_NODE_CHECKED_V1 = UINT64_C(1) << 3U,
    WEBSCENE_SEMANTIC_NODE_MIXED_V1 = UINT64_C(1) << 4U,
    WEBSCENE_SEMANTIC_NODE_EXPANDED_V1 = UINT64_C(1) << 5U,
    WEBSCENE_SEMANTIC_NODE_COLLAPSED_V1 = UINT64_C(1) << 6U,
    WEBSCENE_SEMANTIC_NODE_PRESSED_V1 = UINT64_C(1) << 7U,
    WEBSCENE_SEMANTIC_NODE_HIDDEN_V1 = UINT64_C(1) << 8U,
    WEBSCENE_SEMANTIC_NODE_FOCUSED_V1 = UINT64_C(1) << 9U,
    WEBSCENE_SEMANTIC_NODE_REQUIRED_V1 = UINT64_C(1) << 10U,
    WEBSCENE_SEMANTIC_NODE_INVALID_V1 = UINT64_C(1) << 11U,
    WEBSCENE_SEMANTIC_NODE_BUSY_V1 = UINT64_C(1) << 12U,
    WEBSCENE_SEMANTIC_NODE_MODAL_V1 = UINT64_C(1) << 13U,
    WEBSCENE_SEMANTIC_NODE_MULTISELECTABLE_V1 = UINT64_C(1) << 14U
};

/*
 * Optional typed semantic data carried by v2 companion tables. Presence bits,
 * rather than sentinel numbers, preserve valid zero and negative values. Text
 * offsets use DOM UTF-16 code units so hosts can pass them back through
 * SET_SELECTION without conversion or loss around surrogate pairs.
 */
enum {
    WEBSCENE_SEMANTIC_TYPED_NUMERIC_VALUE_V2 = 1U << 0U,
    WEBSCENE_SEMANTIC_TYPED_NUMERIC_MINIMUM_V2 = 1U << 1U,
    WEBSCENE_SEMANTIC_TYPED_NUMERIC_MAXIMUM_V2 = 1U << 2U,
    WEBSCENE_SEMANTIC_TYPED_NUMERIC_INCREMENT_V2 = 1U << 3U,
    WEBSCENE_SEMANTIC_TYPED_TEXT_CARET_V2 = 1U << 4U,
    WEBSCENE_SEMANTIC_TYPED_TEXT_SELECTION_V2 = 1U << 5U
};

typedef enum webscene_semantic_relationship_kind_v1 {
    WEBSCENE_SEMANTIC_RELATION_LABELLED_BY_V1 = 1,
    WEBSCENE_SEMANTIC_RELATION_DESCRIBED_BY_V1 = 2,
    WEBSCENE_SEMANTIC_RELATION_CONTROLS_V1 = 3,
    WEBSCENE_SEMANTIC_RELATION_OWNS_V1 = 4,
    WEBSCENE_SEMANTIC_RELATION_ACTIVE_DESCENDANT_V1 = 5,
    WEBSCENE_SEMANTIC_RELATION_ERROR_MESSAGE_V1 = 6
} webscene_semantic_relationship_kind_v1;

typedef enum webscene_semantic_action_kind_v1 {
    WEBSCENE_SEMANTIC_ACTION_FOCUS_V1 = 1,
    WEBSCENE_SEMANTIC_ACTION_PRESS_V1 = 2,
    WEBSCENE_SEMANTIC_ACTION_TOGGLE_V1 = 3,
    WEBSCENE_SEMANTIC_ACTION_INCREMENT_V1 = 4,
    WEBSCENE_SEMANTIC_ACTION_DECREMENT_V1 = 5,
    WEBSCENE_SEMANTIC_ACTION_SET_VALUE_V1 = 6,
    WEBSCENE_SEMANTIC_ACTION_SET_SELECTION_V1 = 7
} webscene_semantic_action_kind_v1;

enum {
    WEBSCENE_SEMANTIC_ACTION_SUPPORT_FOCUS_V1 = 1U << 0U,
    WEBSCENE_SEMANTIC_ACTION_SUPPORT_PRESS_V1 = 1U << 1U,
    WEBSCENE_SEMANTIC_ACTION_SUPPORT_TOGGLE_V1 = 1U << 2U,
    WEBSCENE_SEMANTIC_ACTION_SUPPORT_INCREMENT_V1 = 1U << 3U,
    WEBSCENE_SEMANTIC_ACTION_SUPPORT_DECREMENT_V1 = 1U << 4U,
    WEBSCENE_SEMANTIC_ACTION_SUPPORT_SET_VALUE_V1 = 1U << 5U,
    WEBSCENE_SEMANTIC_ACTION_SUPPORT_SET_SELECTION_V1 = 1U << 6U,
    WEBSCENE_SEMANTIC_ACTION_MAXIMUM_PENDING_V1 = 256U,
    WEBSCENE_SEMANTIC_ACTION_MAXIMUM_VALUE_BYTES_V1 = 64U * 1024U,
    WEBSCENE_SEMANTIC_ACTION_MAXIMUM_QUEUED_VALUE_BYTES_V1 = 1024U * 1024U
};

typedef enum webscene_semantic_action_admission_v1 {
    WEBSCENE_SEMANTIC_ACTION_INVALID_V1 = 0,
    WEBSCENE_SEMANTIC_ACTION_QUEUED_V1 = 1,
    WEBSCENE_SEMANTIC_ACTION_STALE_V1 = 2,
    WEBSCENE_SEMANTIC_ACTION_UNSUPPORTED_V1 = 3,
    WEBSCENE_SEMANTIC_ACTION_QUEUE_FULL_V1 = 4,
    WEBSCENE_SEMANTIC_ACTION_PAYLOAD_TOO_LARGE_V1 = 5
} webscene_semantic_action_admission_v1;

typedef enum webscene_semantic_delta_operation_kind_v1 {
    WEBSCENE_SEMANTIC_DELTA_REMOVE_V1 = 1,
    WEBSCENE_SEMANTIC_DELTA_INSERT_V1 = 2,
    WEBSCENE_SEMANTIC_DELTA_REPARENT_V1 = 3,
    WEBSCENE_SEMANTIC_DELTA_UPDATE_V1 = 4,
    WEBSCENE_SEMANTIC_DELTA_RELATIONSHIP_REMOVE_V1 = 5,
    WEBSCENE_SEMANTIC_DELTA_RELATIONSHIP_ADD_V1 = 6,
    WEBSCENE_SEMANTIC_DELTA_FOCUS_V1 = 7
} webscene_semantic_delta_operation_kind_v1;

typedef enum webscene_semantic_delta_admission_v1 {
    WEBSCENE_SEMANTIC_DELTA_INVALID_V1 = 0,
    WEBSCENE_SEMANTIC_DELTA_QUEUED_V1 = 1,
    WEBSCENE_SEMANTIC_DELTA_STALE_BASE_V1 = 2,
    WEBSCENE_SEMANTIC_DELTA_QUEUE_FULL_V1 = 3
} webscene_semantic_delta_admission_v1;

enum {
    WEBSCENE_SEMANTIC_DELTA_FULL_SNAPSHOT_REQUIRED_V1 = 1U << 0U,
    WEBSCENE_SEMANTIC_DELTA_OVERFLOW_V1 = 1U << 1U,
    WEBSCENE_SEMANTIC_DELTA_TRUNCATED_SNAPSHOT_V1 = 1U << 2U,
    WEBSCENE_SEMANTIC_DELTA_MAXIMUM_PENDING_REQUESTS_V1 = 1U,
    WEBSCENE_SEMANTIC_DELTA_MAXIMUM_COMPLETED_LEASES_V1 = 1U,
    WEBSCENE_SEMANTIC_DELTA_MAXIMUM_RETAINED_BASE_SNAPSHOTS_V1 = 2U,
    WEBSCENE_SEMANTIC_DELTA_MAXIMUM_OPERATIONS_V1 = 8U * 1024U,
    WEBSCENE_SEMANTIC_DELTA_MAXIMUM_STRING_BYTES_V1 = 2U * 1024U * 1024U
};

typedef enum webscene_semantic_live_region_role_v1 {
    WEBSCENE_SEMANTIC_LIVE_REGION_GENERIC_V1 = 0,
    WEBSCENE_SEMANTIC_LIVE_REGION_STATUS_V1 = 1,
    WEBSCENE_SEMANTIC_LIVE_REGION_ALERT_V1 = 2,
    WEBSCENE_SEMANTIC_LIVE_REGION_LOG_V1 = 3
} webscene_semantic_live_region_role_v1;

typedef enum webscene_semantic_live_politeness_v1 {
    WEBSCENE_SEMANTIC_LIVE_POLITE_V1 = 1,
    WEBSCENE_SEMANTIC_LIVE_ASSERTIVE_V1 = 2
} webscene_semantic_live_politeness_v1;

enum {
    WEBSCENE_SEMANTIC_LIVE_RELEVANT_ADDITIONS_V1 = 1U << 0U,
    WEBSCENE_SEMANTIC_LIVE_RELEVANT_TEXT_V1 = 1U << 1U,
    WEBSCENE_SEMANTIC_LIVE_RELEVANT_REMOVALS_V1 = 1U << 2U,
    WEBSCENE_SEMANTIC_LIVE_ATOMIC_V1 = 1U << 3U,
    WEBSCENE_SEMANTIC_LIVE_BUSY_COALESCED_V1 = 1U << 4U,
    WEBSCENE_SEMANTIC_LIVE_INITIAL_ALERT_V1 = 1U << 5U,
    WEBSCENE_SEMANTIC_LIVE_TEXT_TRUNCATED_V1 = 1U << 6U,
    WEBSCENE_SEMANTIC_LIVE_BATCH_DROPPED_EVENTS_V1 = 1U << 0U,
    WEBSCENE_SEMANTIC_LIVE_MAXIMUM_PENDING_EVENTS_V1 = 256U,
    WEBSCENE_SEMANTIC_LIVE_MAXIMUM_EVENTS_PER_LEASE_V1 = 64U,
    WEBSCENE_SEMANTIC_LIVE_MAXIMUM_TEXT_BYTES_V1 = 64U * 1024U,
    WEBSCENE_SEMANTIC_LIVE_MAXIMUM_QUEUED_TEXT_BYTES_V1 = 1024U * 1024U
};

typedef struct webscene_semantic_string_v1 {
    uint32_t offset;
    uint32_t length;
} webscene_semantic_string_v1;

typedef struct webscene_semantic_document_v1 {
    uint32_t struct_size;
    uint32_t version;
    uint64_t document_generation;
    uint64_t frame_generation;
    uint32_t frame_owner_dom_node_id;
    uint32_t root_node_index;
    webscene_semantic_string_v1 origin;
} webscene_semantic_document_v1;

typedef struct webscene_semantic_node_v1 {
    uint32_t struct_size;
    uint32_t version;
    uint64_t semantic_id;
    uint64_t states;
    uint32_t dom_node_id;
    uint32_t document_index;
    uint32_t parent_index;
    uint32_t first_child_index;
    uint32_t next_sibling_index;
    float x;
    float y;
    float width;
    float height;
    webscene_semantic_string_v1 role;
    webscene_semantic_string_v1 name;
    webscene_semantic_string_v1 value;
    webscene_semantic_string_v1 description;
    uint32_t supported_actions;
} webscene_semantic_node_v1;

typedef struct webscene_semantic_relationship_v1 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t kind;
    uint32_t source_node_index;
    uint32_t target_node_index;
} webscene_semantic_relationship_v1;

typedef struct webscene_semantic_snapshot_view_v1 {
    uint32_t struct_size;
    uint32_t version;
    uint64_t snapshot_generation;
    uint64_t top_document_generation;
    uint64_t layout_generation;
    uint32_t flags;
    uint32_t focused_node_index;
    const webscene_semantic_document_v1* documents;
    uint32_t document_count;
    const webscene_semantic_node_v1* nodes;
    uint32_t node_count;
    const webscene_semantic_relationship_v1* relationships;
    uint32_t relationship_count;
    const char* string_bytes;
    uint32_t string_byte_count;
    const void* lease_token;
} webscene_semantic_snapshot_view_v1;

/* One fixed-size companion record per v1 node or delta operation. */
typedef struct webscene_semantic_typed_value_v2 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t flags;
    uint32_t text_caret_offset_utf16;
    uint32_t text_selection_start_utf16;
    uint32_t text_selection_end_utf16;
    uint32_t reserved0;
    uint32_t reserved1;
    double numeric_value;
    double numeric_minimum;
    double numeric_maximum;
    double numeric_increment;
} webscene_semantic_typed_value_v2;

/*
 * The v1 base remains byte-for-byte compatible. Its version is 2 and its
 * struct_size is the outer v2 size. typed_value_count always equals node_count.
 */
typedef struct webscene_semantic_snapshot_view_v2 {
    webscene_semantic_snapshot_view_v1 base;
    const webscene_semantic_typed_value_v2* typed_values;
    uint32_t typed_value_count;
    uint32_t reserved;
} webscene_semantic_snapshot_view_v2;

/* Request one worker-owned comparison from the current publication. */
typedef struct webscene_semantic_delta_request_v1 {
    uint32_t struct_size;
    uint32_t version;
    uint64_t base_snapshot_generation;
    uint32_t flags;
} webscene_semantic_delta_request_v1;

/*
 * Identity-based operation. INSERT and UPDATE carry a complete node value.
 * REPARENT carries parent_semantic_id and next_sibling_semantic_id. REMOVE
 * carries semantic_id. Relationship operations carry semantic_id as source,
 * related_semantic_id as target and relationship_kind. FOCUS carries the new
 * focused identity in related_semantic_id, or zero when focus cleared.
 */
typedef struct webscene_semantic_delta_operation_v1 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t kind;
    uint32_t relationship_kind;
    uint64_t semantic_id;
    uint64_t parent_semantic_id;
    uint64_t next_sibling_semantic_id;
    uint64_t related_semantic_id;
    uint64_t document_generation;
    uint64_t frame_generation;
    uint64_t states;
    uint32_t frame_owner_dom_node_id;
    uint32_t dom_node_id;
    uint32_t supported_actions;
    uint32_t reserved;
    float x;
    float y;
    float width;
    float height;
    webscene_semantic_string_v1 role;
    webscene_semantic_string_v1 name;
    webscene_semantic_string_v1 value;
    webscene_semantic_string_v1 description;
} webscene_semantic_delta_operation_v1;

/*
 * Immutable take lease. Any flag requires discarding all operations and
 * acquiring a full snapshot. A lease may outlive its engine.
 */
typedef struct webscene_semantic_delta_view_v1 {
    uint32_t struct_size;
    uint32_t version;
    uint64_t base_snapshot_generation;
    uint64_t new_snapshot_generation;
    uint64_t base_top_document_generation;
    uint64_t new_top_document_generation;
    uint64_t base_layout_generation;
    uint64_t new_layout_generation;
    uint32_t flags;
    const webscene_semantic_delta_operation_v1* operations;
    uint32_t operation_count;
    const char* string_bytes;
    uint32_t string_byte_count;
    const void* lease_token;
} webscene_semantic_delta_view_v1;

/* typed_value_count always equals base.operation_count. */
typedef struct webscene_semantic_delta_view_v2 {
    webscene_semantic_delta_view_v1 base;
    const webscene_semantic_typed_value_v2* typed_values;
    uint32_t typed_value_count;
    uint32_t reserved;
} webscene_semantic_delta_view_v2;

/*
 * One platform-neutral live-region change. text is a UTF-8 slice into its
 * enclosing batch. Sequence and document/frame generations let a native peer
 * preserve order and reject a publication after navigation. The semantic and
 * DOM identities name the live-region root, not a platform accessibility
 * object. Relevant bits describe the coalesced causes represented by text.
 */
typedef struct webscene_semantic_live_event_v1 {
    uint32_t struct_size;
    uint32_t version;
    uint64_t sequence;
    uint64_t top_document_generation;
    uint64_t frame_generation;
    uint64_t semantic_id;
    uint32_t frame_owner_dom_node_id;
    uint32_t dom_node_id;
    uint32_t role;
    uint32_t politeness;
    uint32_t flags;
    webscene_semantic_string_v1 text;
} webscene_semantic_live_event_v1;

/*
 * Immutable take lease. Taking removes at most 64 queued events. A lease may
 * outlive the engine. dropped_event_count is the deterministic loss observed
 * since the preceding successful take, including navigation/low-memory
 * retirement and oldest-first queue eviction.
 */
typedef struct webscene_semantic_live_batch_view_v1 {
    uint32_t struct_size;
    uint32_t version;
    uint64_t batch_generation;
    uint32_t flags;
    uint32_t dropped_event_count;
    const webscene_semantic_live_event_v1* events;
    uint32_t event_count;
    const char* string_bytes;
    uint32_t string_byte_count;
    const void* lease_token;
} webscene_semantic_live_batch_view_v1;

/*
 * Host-to-DOM semantic action. The host copies snapshot_generation and
 * semantic_id from one acquired node. A newer publication may route the same
 * still-live identity; navigation and node retirement make it stale.
 * value_utf8 is copied before return and
 * is accepted only for SET_VALUE. Selection offsets are UTF-16 code units and
 * are accepted only for SET_SELECTION. flags is reserved and must be zero.
 * No platform accessibility object crosses this boundary.
 */
typedef struct webscene_semantic_action_request_v1 {
    uint32_t struct_size;
    uint32_t version;
    uint64_t snapshot_generation;
    uint64_t semantic_id;
    uint32_t action;
    uint32_t flags;
    const char* value_utf8;
    size_t value_byte_count;
    uint32_t selection_start;
    uint32_t selection_end;
} webscene_semantic_action_request_v1;

enum {
    WEBSCENE_INPUT_MODIFIER_SHIFT = 1U << 0U,
    WEBSCENE_INPUT_MODIFIER_CONTROL = 1U << 1U,
    WEBSCENE_INPUT_MODIFIER_ALT = 1U << 2U,
    WEBSCENE_INPUT_MODIFIER_META = 1U << 3U,
    WEBSCENE_INPUT_KEY_REPEAT = 1U << 4U,
    // Pointer flags reserve low bits for DOM `buttons` and bits 8-15 for the
    // changed button. Keep keyboard-compatible modifiers in their own lane.
    WEBSCENE_INPUT_POINTER_MODIFIER_SHIFT = 1U << 16U,
    WEBSCENE_INPUT_POINTER_MODIFIER_CONTROL = 1U << 17U,
    WEBSCENE_INPUT_POINTER_MODIFIER_ALT = 1U << 18U,
    WEBSCENE_INPUT_POINTER_MODIFIER_META = 1U << 19U,
    // Wheel hosts must preserve the device class across the ABI. Precision
    // devices (trackpads and pixel wheels) already provide a frame-dense
    // stream, including platform momentum, and must not be interpolated again.
    // Discrete wheels report coarse ticks and opt into WebScene's bounded
    // target-offset animator. Unclassified legacy input remains immediate.
    WEBSCENE_INPUT_WHEEL_PRECISE = 1U << 20U,
    WEBSCENE_INPUT_WHEEL_NATIVE_MOMENTUM = 1U << 21U,
    WEBSCENE_INPUT_WHEEL_DISCRETE = 1U << 22U
};

typedef struct webscene_input_event {
    uint32_t kind;
    uint32_t flags;
    uint64_t sequence;
    double x;
    double y;
    // For WEBSCENE_INPUT_RESIZE, delta_x carries the positive host device scale
    // factor. Zero retains ABI-v2 compatibility with hosts that predate scale
    // reporting and is interpreted as 1.0.
    double delta_x;
    double delta_y;
} webscene_input_event;

/* Typed host-to-browser drag session input. The engine copies every item
 * before returning; native paths and platform objects are never exposed.
 * ENTER starts or replaces a session and carries its complete item set.
 * OVER, LEAVE, DROP and CANCEL carry no items. DROP is dispatched only after
 * the current target cancelled dragover, matching the browser admission rule.
 *
 * Limits per session: 64 items, 64 MiB file bytes, 1 MiB string payload and
 * metadata, 4 KiB names/relative paths, and 256-byte MIME types. A relative
 * path describes a bounded virtual directory tree; it conveys no filesystem
 * authority. Delivery is asynchronous on the engine worker. */
enum {
    WEBSCENE_DRAG_ENTER_V1 = 1,
    WEBSCENE_DRAG_OVER_V1 = 2,
    WEBSCENE_DRAG_LEAVE_V1 = 3,
    WEBSCENE_DRAG_DROP_V1 = 4,
    WEBSCENE_DRAG_CANCEL_V1 = 5
};
enum {
    WEBSCENE_DRAG_ITEM_STRING_V1 = 1,
    WEBSCENE_DRAG_ITEM_FILE_V1 = 2,
    WEBSCENE_DRAG_ITEM_DIRECTORY_V1 = 3
};
typedef struct webscene_drag_item_v1 {
    uint32_t struct_size, version;
    uint32_t kind, reserved;
    const char* mime_type;
    size_t mime_type_length;
    const char* name;
    size_t name_length;
    const char* relative_path;
    size_t relative_path_length;
    const uint8_t* bytes;
    size_t byte_count;
} webscene_drag_item_v1;
typedef struct webscene_drag_event_v1 {
    uint32_t struct_size, version;
    uint64_t session_id;
    uint64_t sequence;
    uint32_t action, flags;
    double x, y;
    const webscene_drag_item_v1* items;
    size_t item_count;
} webscene_drag_event_v1;
WEBSCENE_API uint8_t webscene_engine_dispatch_drag_v1(
    webscene_engine* engine,
    const webscene_drag_event_v1* event);

typedef struct webscene_scene_header {
    uint64_t revision;
    uint64_t base_revision;
    uint64_t consumed_input_sequence;
    float viewport_width;
    float viewport_height;
    uint32_t command_count;
    uint32_t canvas_layer_count;
    uint32_t damage_rect_count;
    uint32_t flags;
    uint64_t content_hash;
} webscene_scene_header;

// DOM kinds 44/46 are typed linear gradients (background/foreground).
// flags is the number of immediately following kind-45 stop records;
// stroke_width is the CSS angle in degrees. Each stop carries offset in x
// and color in rgba. Stop records are payload, never independent draws.
// DOM kinds 40/41 are dashed rounded strokes; 42/43 are dashed lines
// (background/foreground pairs). stroke_width carries the width in CSS pixels.
// Shadow kinds 17/18: flags bit 0 selects an inverse rounded hole;
// producers must bracket inverse shadows with clip commands 12/13.
// Clip kind 12 uses flags bit 31 to select an SVG path stored in the indexed
// scene string. Bit 30 selects even-odd fill, bit 29 makes path coordinates
// relative to the command box origin, and bit 28 scales objectBoundingBox path
// units by the command box; the remaining bits are its string index.
// A zero flag retains the rounded-rectangle fields used by existing producers
// and presenters.
// Group kind 30 uses flags bit 31 for brightness, bit 30 for grayscale,
// bit 29 for contrast, bit 28 for foreground blur, and bit 27 for saturation;
// bit 26 opens a neutral isolated layer for a following kind-47 alpha mask.
// stroke_width carries
// the bounded non-negative multiplier or CSS blur standard deviation.
// A zero flag retains the opacity-group alpha stored in the low byte of rgba.
// DOM kind 47 applies an indexed webscene-bg-v2 linear gradient,
// webscene-mask-svg-v1 tiled SVG resource, or webscene-mask-v2 bounded ordered
// layer list to the current isolated layer using destination-in before kind 31
// restores it. v2 carries at most 16 length-prefixed linear/radial/SVG/raster
// layers with repeat, position, size, and mode identities. Raster markup uses
// webscene-raster-v2 followed by width, height, SHA-256 content identity, and
// a base64 PNG/WebP payload; its viewBox is `0 0 width height`. The same nested
// markup is valid in webscene-bg-svg-v1 so consumers can share decoded images.
// Consumers union
// add layers on one bounded temporary surface and apply alpha once. Lengths are UTF-8 byte
// counts. webscene-mask-v3 adds a per-layer add/exclude identity; consumers
// paint bottom-to-top and use xor for exclude before applying alpha once.
// webscene-mask-invalid-v1 clears the isolated layer for failed/unsupported masks.
// DOM kind 48 applies one bounded backdrop effect before the element's own
// background and descendants. flags indexes a webscene-backdrop-v1 resource
// containing the authored blur/saturate sequence; rgba is the paint phase,
// the box and corner radii bound output, and stroke_width is the maximum blur
// sigma used to bound sampling and damage. Consumers must sample pixels already
// painted at this exact command position, including retained canvas/GPU output.
// webscene-backdrop-invalid-v1 is an explicit fail-closed no-op.
typedef struct webscene_scene_command {
    uint32_t kind;
    uint32_t flags;
    float x;
    float y;
    float width;
    float height;
    uint32_t rgba;
    uint32_t node_id;
    float radius_top_left;
    float radius_top_right;
    float radius_bottom_right;
    float radius_bottom_left;
    float stroke_width;
} webscene_scene_command;

typedef struct webscene_canvas_layout {
    uint32_t node_id;
    uint32_t flags;
    float x;
    float y;
    float width;
    float height;
    uint32_t bitmap_width;
    uint32_t bitmap_height;
} webscene_canvas_layout;

typedef struct webscene_canvas_layer {
    uint32_t node_id;
    uint32_t flags;
    uint32_t command_offset;
    uint32_t command_count;
    uint32_t string_offset;
    uint32_t string_count;
    uint32_t reserved;
    float x;
    float y;
    float width;
    float height;
    uint32_t bitmap_width;
    uint32_t bitmap_height;
    uint64_t generation;
} webscene_canvas_layer;

typedef union webscene_canvas_command_data {
    double values[8];
    struct {
        double x;
        double y;
    } point;
    struct {
        double x;
        double y;
        double width;
        double height;
    } rect;
    struct {
        double a;
        double b;
        double c;
        double d;
        double e;
        double f;
    } transform;
    struct {
        double x1;
        double y1;
        double x2;
        double y2;
        double x3;
        double y3;
    } curve;
} webscene_canvas_command_data;

enum {
    WEBSCENE_CANVAS_COMMAND_FLAG_EVEN_ODD = 1U << 16U,
    WEBSCENE_CANVAS_COMMAND_FLAG_TEXT_MAX_WIDTH = 1U << 17U
};

/*
 * Fixed-layout native draw operation. resource_id addresses the owning
 * layer's string/resource table when the command kind requires one. Managed
 * renderers traverse this array in-place; there is no packet decoding step.
 */
typedef struct webscene_canvas_command {
    uint32_t kind;
    uint32_t flags;
    uint32_t resource_id;
    uint32_t reserved;
    webscene_canvas_command_data data;
} webscene_canvas_command;

typedef struct webscene_scene_string {
    uint32_t byte_offset;
    uint32_t byte_length;
} webscene_scene_string;

typedef enum webscene_interop_value_kind_v3 {
    WEBSCENE_INTEROP_VALUE_UNDEFINED_V3 = 0,
    WEBSCENE_INTEROP_VALUE_NULL_V3 = 1,
    WEBSCENE_INTEROP_VALUE_BOOLEAN_V3 = 2,
    WEBSCENE_INTEROP_VALUE_NUMBER_V3 = 3,
    WEBSCENE_INTEROP_VALUE_STRING_V3 = 4,
    WEBSCENE_INTEROP_VALUE_ARRAY_V3 = 5,
    WEBSCENE_INTEROP_VALUE_OBJECT_V3 = 6,
    WEBSCENE_INTEROP_VALUE_HANDLE_V3 = 7
} webscene_interop_value_kind_v3;

typedef enum webscene_interop_result_status_v3 {
    WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3 = 0,
    WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V3 = 1,
    WEBSCENE_INTEROP_RESULT_CANCELLED_V3 = 2,
    WEBSCENE_INTEROP_RESULT_INVALID_REQUEST_V3 = 3
} webscene_interop_result_status_v3;

/*
 * One fixed-layout value in an immutable interop result. For strings,
 * offset/length address utf8_bytes. For arrays and objects they address the
 * edge table. Boolean, number and retained-handle payloads use payload.
 */
typedef struct webscene_interop_value_v3 {
    uint32_t kind;
    uint32_t flags;
    uint32_t offset;
    uint32_t length;
    uint64_t payload;
} webscene_interop_value_v3;

/*
 * Array edges use value_index only. Object edges additionally address the
 * UTF-8 property name. Indices and offsets are validated by the managed
 * reader before constructing spans.
 */
typedef struct webscene_interop_edge_v3 {
    uint32_t name_offset;
    uint32_t name_length;
    uint32_t value_index;
    uint32_t reserved;
} webscene_interop_edge_v3;

/*
 * ABI 3 arbitrary-evaluation request. This retains source evaluation for
 * diagnostics and compatibility tests while returning the same leased tagged
 * result used by generated direct invocations.
 */
typedef struct webscene_interop_evaluate_request_v3 {
    uint32_t struct_size;
    uint32_t version;
    const char* source;
    size_t source_length;
    const char* document_name;
    size_t document_name_length;
    uint32_t flags;
    uint32_t reserved;
} webscene_interop_evaluate_request_v3;

typedef enum webscene_interop_operation_v3 {
    WEBSCENE_INTEROP_GET_GLOBAL_V3 = 1,
    WEBSCENE_INTEROP_INVOKE_GLOBAL_V3 = 2,
    WEBSCENE_INTEROP_CONSTRUCT_V3 = 3,
    WEBSCENE_INTEROP_GET_PROPERTY_V3 = 4,
    WEBSCENE_INTEROP_SET_PROPERTY_V3 = 5,
    WEBSCENE_INTEROP_INVOKE_MEMBER_V3 = 6,
    WEBSCENE_INTEROP_RELEASE_HANDLE_V3 = 7,
    WEBSCENE_INTEROP_CREATE_CALLBACK_TARGET_V3 = 8,
    WEBSCENE_INTEROP_CREATE_CALLBACK_FUNCTION_V3 = 9,
    WEBSCENE_INTEROP_CREATE_SYNCHRONOUS_FACTORY_V3 = 10,
    WEBSCENE_INTEROP_INVOKE_FUNCTION_V3 = 11
} webscene_interop_operation_v3;

typedef enum webscene_interop_result_mode_v3 {
    WEBSCENE_INTEROP_RESULT_VALUE_V3 = 0,
    WEBSCENE_INTEROP_RESULT_RETAINED_HANDLE_V3 = 1,
    WEBSCENE_INTEROP_RESULT_VOID_V3 = 2
} webscene_interop_result_mode_v3;

typedef enum webscene_interop_call_flags_v3 {
    WEBSCENE_INTEROP_CALL_AWAIT_PROMISE_V3 = 1
} webscene_interop_call_flags_v3;

/*
 * Generated direct-invocation request. All pointers are copied before
 * webscene_engine_begin_invoke_v3 returns. String offsets in value
 * nodes and object edges address utf8_bytes. arguments_root must identify an
 * array whose items are the call arguments.
 */
typedef struct webscene_interop_invoke_request_v3 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t operation;
    uint32_t flags;
    uint64_t target_handle;
    const char* global_name;
    size_t global_name_length;
    const char* member_name;
    size_t member_name_length;
    const webscene_interop_value_v3* values;
    size_t value_count;
    const webscene_interop_edge_v3* edges;
    size_t edge_count;
    const char* utf8_bytes;
    size_t utf8_byte_count;
    uint32_t arguments_root;
    uint32_t result_mode;
} webscene_interop_invoke_request_v3;

typedef void (*webscene_interop_completed_callback_v3)(
    void* user_data,
    uint64_t operation_id);

/*
 * The result and every table/string pointer remain valid until
 * webscene_interop_result_release_v3 is called with the lease_id copied from
 * this view. The result can outlive the engine that produced it. Callers must
 * retain lease_id separately before release; passing a stale pointer with its
 * old lease_id is a safe no-op even after the pooled address is reused.
 */
struct webscene_interop_result_view_v3 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t status;
    uint32_t flags;
    uint64_t operation_id;
    const webscene_interop_value_v3* values;
    const webscene_interop_edge_v3* edges;
    const char* utf8_bytes;
    const char* error_bytes;
    uint64_t lease_id;
    uint32_t value_count;
    uint32_t edge_count;
    uint32_t utf8_byte_count;
    uint32_t error_byte_count;
    uint32_t root_value_index;
    uint32_t pooled_capacity;
    uint32_t reserved0;
    uint32_t reserved1;
};

typedef enum webscene_interop_callback_return_kind_v3 {
    WEBSCENE_INTEROP_CALLBACK_VOID_V3 = 0,
    WEBSCENE_INTEROP_CALLBACK_PROMISE_V3 = 1,
    WEBSCENE_INTEROP_CALLBACK_SYNCHRONOUS_V3 = 2
} webscene_interop_callback_return_kind_v3;

/*
 * Immutable JavaScript-to-managed callback invocation. The tagged argument
 * arena remains valid until webscene_interop_callback_release_v3 receives the
 * matching lease_id. Taken callback leases may outlive their engine.
 */
struct webscene_interop_callback_view_v3 {
    uint32_t struct_size;
    uint32_t version;
    uint64_t call_id;
    uint64_t target_id;
    uint32_t method_id;
    uint32_t return_kind;
    const webscene_interop_value_v3* values;
    const webscene_interop_edge_v3* edges;
    const char* utf8_bytes;
    uint64_t lease_id;
    uint32_t value_count;
    uint32_t edge_count;
    uint32_t utf8_byte_count;
    uint32_t arguments_root;
    uint32_t pooled_capacity;
    uint32_t reserved0;
};

/*
 * Managed callback completion. All arena pointers are copied before
 * webscene_engine_complete_callback_v3 returns. A successful completion uses
 * root_value_index; a failed completion uses error_bytes.
 */
typedef struct webscene_interop_callback_completion_v3 {
    uint32_t struct_size;
    uint32_t version;
    uint64_t call_id;
    uint32_t succeeded;
    uint32_t reserved;
    const webscene_interop_value_v3* values;
    size_t value_count;
    const webscene_interop_edge_v3* edges;
    size_t edge_count;
    const char* utf8_bytes;
    size_t utf8_byte_count;
    const char* error_bytes;
    size_t error_byte_count;
    uint32_t root_value_index;
    uint32_t reserved1;
} webscene_interop_callback_completion_v3;

typedef struct webscene_interop_pool_metrics_v3 {
    uint32_t struct_size;
    uint32_t version;
    uint64_t outstanding_results;
    uint64_t pooled_bytes;
    uint64_t pool_hits;
    uint64_t pool_misses;
    uint64_t oversize_allocations;
    uint64_t high_water_outstanding_results;
    uint64_t pooled_request_records;
    uint64_t request_pool_hits;
    uint64_t request_pool_misses;
    uint64_t request_oversize_allocations;
    uint64_t active_operation_slots;
    uint64_t available_operation_slots;
    uint64_t operation_slot_high_water;
    uint64_t pooled_result_bytes_4k;
    uint64_t pooled_result_bytes_16k;
    uint64_t pooled_result_bytes_64k;
    uint64_t pooled_result_bytes_256k;
    uint64_t pooled_result_bytes_1m;
    uint64_t taken_result_leases;
    uint64_t operation_result_leases;
    uint64_t queued_callbacks;
    uint64_t taken_callback_leases;
    uint64_t pending_callback_promises;
    uint64_t callback_queue_high_water;
} webscene_interop_pool_metrics_v3;

typedef struct webscene_damage_rect {
    float x;
    float y;
    float width;
    float height;
} webscene_damage_rect;

/*
 * The acquired pointer is the immutable scene. Every pointer below remains
 * valid until webscene_scene_release is called. A renderer can construct spans
 * over the arrays directly without further native calls or copying.
 */
struct webscene_scene_view {
    uint32_t struct_size;
    uint32_t abi_version;
    webscene_scene_header header;
    const webscene_scene_command* commands;
    const webscene_canvas_layer* canvas_layers;
    const webscene_canvas_command* canvas_commands;
    const webscene_scene_string* strings;
    const char* string_bytes;
    const webscene_damage_rect* damage_rects;
    const void* lease_token;
    uint32_t canvas_command_count;
    uint32_t string_count;
    uint32_t string_byte_count;
    uint32_t reserved;
};

/* Separately versioned scene acquisition. No GPU capability is advertised yet. */
#define WEBSCENE_SCENE_VIEW_VERSION_3 3U
#define WEBSCENE_SCENE_CAPABILITY_GPU_IMAGES (UINT64_C(1) << 0)
#define WEBSCENE_SCENE_CAPABILITY_ORDERED_CANVAS (UINT64_C(1) << 1)
/* Consumer must enqueue every native producer dependency before GPU reads. */
#define WEBSCENE_SCENE_CAPABILITY_PRODUCER_GPU_WAITS (UINT64_C(1) << 2)
#define WEBSCENE_SCENE_CAPABILITY_CANVAS_CHECKPOINTS (UINT64_C(1) << 3)
/* UTF-8 versioned raster/state checkpoint resource, interpreted by capable hosts. */
#define WEBSCENE_CANVAS_COMMAND_RASTER_CHECKPOINT 58U
WEBSCENE_API uint8_t webscene_engine_submit_canvas_checkpoint_v3(
    webscene_engine* engine, uint32_t node_id, uint64_t generation,
    uint32_t command_count, const char* payload, size_t payload_length);
/* Draw in the existing command stream at x/y/width/height. rgba carries the
 * scene GPU image index (not a color); node_id retains the canvas node ID. Existing transform/clip/isolation operations apply. */
#define WEBSCENE_SCENE_COMMAND_GPU_IMAGE 256U
/* Requires SCENE_CAPABILITY_ORDERED_CANVAS in the scene capability mask.
 * Ordered v3 placement of a retained Canvas2D layer. node_id selects the layer;
 * its current layout and bitmap dimensions provide placement/scaling. */
#define WEBSCENE_SCENE_COMMAND_CANVAS_LAYER 257U
/* Optional hint on a replacement canvas layer. Against header.base_revision,
 * the previous layer's command and string arrays are unchanged prefixes of
 * this complete replacement payload. Consumers may ignore this hint. */
#define WEBSCENE_CANVAS_LAYER_UNCHANGED_PREFIX 4U
typedef struct webscene_scene_acquire_options_v3 {
    uint32_t struct_size;
    uint32_t scene_version;
    uint64_t consumer_capabilities;
} webscene_scene_acquire_options_v3;
typedef enum webscene_scene_acquire_status {
    WEBSCENE_SCENE_ACQUIRE_SUCCESS = 0,
    WEBSCENE_SCENE_ACQUIRE_EMPTY = 1,
    WEBSCENE_SCENE_ACQUIRE_INVALID_ARGUMENT = 2,
    WEBSCENE_SCENE_ACQUIRE_UNSUPPORTED_VERSION = 3,
    WEBSCENE_SCENE_ACQUIRE_UNSUPPORTED_CAPABILITIES = 4,
    WEBSCENE_SCENE_ACQUIRE_OUT_OF_MEMORY = 5,
    WEBSCENE_SCENE_ACQUIRE_INTERNAL_ERROR = 6,
    WEBSCENE_SCENE_ACQUIRE_BACKPRESSURE = 7
} webscene_scene_acquire_status;
typedef struct webscene_scene_view_v3 {
    uint32_t struct_size;
    uint32_t scene_version;
    uint64_t required_capabilities;
    /* Borrowed for this v3 lease's lifetime. Do not release separately. */
    const webscene_scene_view* cpu_view;
    const void* lease_token;
} webscene_scene_view_v3;
WEBSCENE_API webscene_scene_acquire_status webscene_engine_acquire_latest_scene_v3(
    webscene_engine* engine,const webscene_scene_acquire_options_v3* options,const webscene_scene_view_v3** result);
WEBSCENE_API webscene_scene_acquire_status webscene_engine_acquire_next_scene_v3(
    webscene_engine* engine,const webscene_scene_acquire_options_v3* options,const webscene_scene_view_v3** result);
WEBSCENE_API uint8_t webscene_scene_acknowledge_v3(const webscene_scene_view_v3* scene);
WEBSCENE_API void webscene_scene_release_v3(const webscene_scene_view_v3* scene);

/* Opaque native image leases. A retained lease can outlive its scene/engine.
 * Release ends CPU retention only. Complete a consumer only after its GPU fence.
 * Calls on the same handle must be externally serialized; released handles are invalid.
 */
typedef struct webscene_gpu_image_lease_v3 webscene_gpu_image_lease_v3;
typedef struct webscene_gpu_image_consumer_v3 webscene_gpu_image_consumer_v3;
typedef struct webscene_gpu_image_info_v3 {
    uint32_t struct_size, version;
    uint64_t canvas, allocation, allocation_generation, content_serial;
    uint64_t producer_timeline, producer_value;
    uint32_t width, height;
    /* format: 1 RGBA8 unorm, 2 BGRA8 unorm, 3 RGBA16 float,
     *         4 RGBA8 sRGB, 5 BGRA8 sRGB.
     * alpha: 1 opaque, 2 premultiplied, 3 straight.
     * color_space: 1 sRGB, 2 Display P3. orientation: 1 top-left, 2 bottom-left.
     * Timeline/allocation IDs require native provider resolution, never casts.
     */
    uint32_t format, alpha, color_space, orientation;
} webscene_gpu_image_info_v3;
WEBSCENE_API uint32_t webscene_scene_gpu_image_count_v3(const webscene_scene_view_v3* scene);
WEBSCENE_API webscene_scene_acquire_status webscene_scene_retain_gpu_image_v3(
    const webscene_scene_view_v3* scene,uint32_t index,webscene_gpu_image_lease_v3** result);
WEBSCENE_API webscene_scene_acquire_status webscene_gpu_image_retain_v3(
    const webscene_gpu_image_lease_v3* image,webscene_gpu_image_lease_v3** result);
WEBSCENE_API uint8_t webscene_gpu_image_describe_v3(
    const webscene_gpu_image_lease_v3* image,webscene_gpu_image_info_v3* result);
WEBSCENE_API void webscene_gpu_image_release_v3(webscene_gpu_image_lease_v3* image);
WEBSCENE_API webscene_scene_acquire_status webscene_gpu_image_begin_consumer_v3(
    const webscene_gpu_image_lease_v3* image,webscene_gpu_image_consumer_v3** result);
WEBSCENE_API void webscene_gpu_image_complete_consumer_v3(webscene_gpu_image_consumer_v3* consumer);
/* macOS native presenter hook, not portable image metadata or a JavaScript API.
 * The pointer is borrowed until consumer completion. This lookup does not wait
 * for the producer, begin native access, or authorize early consumer completion.
 * Returns zero for unsupported providers/platforms/builds or invalid arguments.
 */
typedef struct webscene_gpu_iosurface_view_v3 {
    uint32_t struct_size, version;
    void* borrowed_iosurface;
    uint64_t allocation_bytes;
} webscene_gpu_iosurface_view_v3;
WEBSCENE_API uint8_t webscene_gpu_image_get_iosurface_v3(
    const webscene_gpu_image_consumer_v3* consumer,webscene_gpu_iosurface_view_v3* result);

/* Windows host-queue bridge. Keep consumer alive until seal on the drawing
 * thread and poll returning S_OK (zero). Texture is borrowed from import_owner.
 * Destroy only before any draw, or after completed retirement. */
WEBSCENE_API int32_t webscene_gpu_d3d11_import_v3(webscene_gpu_image_consumer_v3* consumer,
    void* borrowed_device,void** import_owner,void** borrowed_texture);
WEBSCENE_API int32_t webscene_gpu_d3d11_supported_v3(void* borrowed_device);
WEBSCENE_API int32_t webscene_gpu_d3d11_seal_v3(void* import_owner);
WEBSCENE_API int32_t webscene_gpu_d3d11_poll_v3(void* import_owner);
WEBSCENE_API void webscene_gpu_d3d11_destroy_v3(void* import_owner);

/* Windows D3D12/DXGI shared-image import envelope. Acquire duplicates the
 * producer's NT texture handle and every attached DXGI fence handle atomically;
 * pointers in the returned views are borrowed from shared_owner. The caller
 * imports them into one same-adapter D3D12/Dawn device before releasing that
 * owner. Imported native objects own their independent references.
 *
 * Releasing shared_owner only closes WebScene's duplicate handles. It neither
 * retires GPU sampling nor completes consumer. Keep consumer alive until the
 * presenting queue's completion primitive proves all image reads finished, then
 * call webscene_gpu_image_complete_consumer_v3. This is the same explicit GPU
 * retirement rule as the IOSurface and D3D11 paths.
 */
enum {
    WEBSCENE_GPU_D3D12_SHARED_NT_HANDLE_V3 = UINT64_C(1) << 0U,
    WEBSCENE_GPU_D3D12_SHARED_TEXTURE2D_V3 = UINT64_C(1) << 1U,
    WEBSCENE_GPU_D3D12_SHARED_SIMULTANEOUS_ACCESS_V3 = UINT64_C(1) << 2U,
    WEBSCENE_GPU_D3D12_SHARED_EXPLICIT_RETIREMENT_V3 = UINT64_C(1) << 3U,
    WEBSCENE_GPU_D3D12_SHARED_PRODUCER_FENCES_V3 = UINT64_C(1) << 4U
};
typedef struct webscene_gpu_d3d12_shared_image_view_v3 {
    uint32_t struct_size, version;
    uint64_t capabilities;
    void* borrowed_texture_handle;
    uint64_t allocation_bytes;
    uint64_t allocation;
    uint64_t allocation_generation;
    uint64_t content_serial;
    uint32_t width, height;
    uint32_t format, alpha, color_space, orientation;
    uint32_t adapter_luid_low;
    int32_t adapter_luid_high;
    uint32_t producer_fence_count;
    uint32_t reserved;
} webscene_gpu_d3d12_shared_image_view_v3;
typedef struct webscene_gpu_dxgi_fence_view_v3 {
    uint32_t struct_size, version;
    void* borrowed_fence_handle;
    uint64_t signaled_value;
} webscene_gpu_dxgi_fence_view_v3;
WEBSCENE_API int32_t webscene_gpu_d3d12_acquire_shared_v3(
    const webscene_gpu_image_consumer_v3* consumer,void** shared_owner,
    webscene_gpu_d3d12_shared_image_view_v3* result);
WEBSCENE_API uint8_t webscene_gpu_d3d12_get_producer_fence_v3(
    const void* shared_owner,uint32_t index,webscene_gpu_dxgi_fence_view_v3* result);
WEBSCENE_API void webscene_gpu_d3d12_release_shared_v3(void* shared_owner);

/* Linux external-memory image export. Acquisition duplicates every plane and
 * producer-wait FD with close-on-exec into shared_owner. FDs returned by the
 * view functions are borrowed from that owner and remain valid until release.
 * Importing APIs must create their own references before shared_owner is
 * released. No call here waits, transitions an image, or completes GPU use.
 *
 * Callers first begin an ordinary image consumer. On success they import the
 * described memory, enqueue every wait in its ordering domain, transition from
 * producer_layout/queue_family to consumer_layout/queue_family, and release
 * shared_owner. They complete the image consumer only after the consuming GPU
 * queue has retired every read. Failed import still requires shared-owner
 * release and consumer completion without submitting a read.
 */
#define WEBSCENE_GPU_LINUX_SHARED_ABI_VERSION 3U
enum {
    WEBSCENE_GPU_LINUX_MEMORY_OPAQUE_FD_V3 = 1U,
    WEBSCENE_GPU_LINUX_MEMORY_DMA_BUF_V3 = 2U,
    WEBSCENE_GPU_LINUX_SYNC_FD_V3 = 1U,
    WEBSCENE_GPU_LINUX_SYNC_VK_SEMAPHORE_OPAQUE_FD_V3 = 2U,
    WEBSCENE_GPU_LINUX_QUEUE_EXCLUSIVE_V3 = 1U,
    WEBSCENE_GPU_LINUX_QUEUE_CONCURRENT_V3 = 2U,
    WEBSCENE_GPU_LINUX_SYNC_TIMELINE_V3 = 1U << 0U
};
enum {
    WEBSCENE_GPU_LINUX_CAP_OPAQUE_FD_V3 = UINT64_C(1) << 0U,
    WEBSCENE_GPU_LINUX_CAP_DMA_BUF_V3 = UINT64_C(1) << 1U,
    WEBSCENE_GPU_LINUX_CAP_SYNC_FD_V3 = UINT64_C(1) << 2U,
    WEBSCENE_GPU_LINUX_CAP_VK_SEMAPHORE_OPAQUE_FD_V3 = UINT64_C(1) << 3U,
    WEBSCENE_GPU_LINUX_CAP_TIMELINE_SEMAPHORE_V3 = UINT64_C(1) << 4U,
    WEBSCENE_GPU_LINUX_CAP_DEVICE_UUID_V3 = UINT64_C(1) << 5U,
    WEBSCENE_GPU_LINUX_CAP_EXPLICIT_LAYOUT_V3 = UINT64_C(1) << 6U,
    WEBSCENE_GPU_LINUX_CAP_QUEUE_FAMILY_OWNERSHIP_V3 = UINT64_C(1) << 7U,
    WEBSCENE_GPU_LINUX_CAP_DEDICATED_ALLOCATION_V3 = UINT64_C(1) << 8U,
    WEBSCENE_GPU_LINUX_CAP_PRODUCER_COMPLETE_V3 = UINT64_C(1) << 9U,
    WEBSCENE_GPU_LINUX_CAP_EXPLICIT_RETIREMENT_V3 = UINT64_C(1) << 10U
};
typedef enum webscene_gpu_linux_shared_status_v3 {
    WEBSCENE_GPU_LINUX_SHARED_SUCCESS_V3 = 0,
    WEBSCENE_GPU_LINUX_SHARED_INVALID_ARGUMENT_V3 = 1,
    WEBSCENE_GPU_LINUX_SHARED_UNSUPPORTED_PROVIDER_V3 = 2,
    WEBSCENE_GPU_LINUX_SHARED_INVALID_DESCRIPTOR_V3 = 3,
    WEBSCENE_GPU_LINUX_SHARED_FD_DUPLICATION_FAILED_V3 = 4,
    WEBSCENE_GPU_LINUX_SHARED_OUT_OF_MEMORY_V3 = 5,
    WEBSCENE_GPU_LINUX_SHARED_INTERNAL_ERROR_V3 = 6
} webscene_gpu_linux_shared_status_v3;
typedef struct webscene_gpu_linux_shared_image_view_v3 {
    uint32_t struct_size, version;
    uint64_t capabilities;
    uint64_t allocation_size;
    uint64_t drm_modifier;
    uint64_t allocation;
    uint64_t allocation_generation;
    uint64_t content_serial;
    uint64_t producer_timeline;
    uint64_t producer_value;
    uint32_t width, height;
    uint32_t format, alpha, color_space, orientation;
    uint32_t memory_handle_type;
    uint32_t queue_sharing;
    uint32_t drm_format;
    uint32_t memory_type_index;
    uint32_t vk_format;
    uint32_t vk_image_type;
    uint32_t vk_tiling;
    uint32_t vk_usage;
    uint32_t vk_create_flags;
    uint32_t vk_sharing_mode;
    int32_t vk_initial_layout;
    uint32_t vk_queue_family_index_count;
    uint32_t sample_count;
    uint32_t mip_level_count;
    uint32_t array_layer_count;
    int32_t producer_layout;
    int32_t consumer_layout;
    uint32_t producer_queue_family;
    uint32_t consumer_queue_family;
    uint32_t vk_queue_family_indices[4];
    uint8_t device_uuid[16];
    uint8_t driver_uuid[16];
    uint32_t plane_count;
    uint32_t producer_wait_count;
} webscene_gpu_linux_shared_image_view_v3;
typedef struct webscene_gpu_linux_plane_view_v3 {
    uint32_t struct_size, version;
    int32_t borrowed_fd;
    uint32_t stride;
    uint64_t offset;
} webscene_gpu_linux_plane_view_v3;
typedef struct webscene_gpu_linux_sync_view_v3 {
    uint32_t struct_size, version;
    uint32_t handle_type;
    uint32_t flags;
    int32_t borrowed_fd;
    uint32_t reserved;
    uint64_t ordering_domain;
    uint64_t signaled_value;
} webscene_gpu_linux_sync_view_v3;
WEBSCENE_API webscene_gpu_linux_shared_status_v3 webscene_gpu_linux_acquire_shared_v3(
    const webscene_gpu_image_consumer_v3* consumer,void** shared_owner,
    webscene_gpu_linux_shared_image_view_v3* result);
WEBSCENE_API uint8_t webscene_gpu_linux_get_plane_v3(
    const void* shared_owner,uint32_t index,webscene_gpu_linux_plane_view_v3* result);
WEBSCENE_API uint8_t webscene_gpu_linux_get_producer_wait_v3(
    const void* shared_owner,uint32_t index,webscene_gpu_linux_sync_view_v3* result);
WEBSCENE_API void webscene_gpu_linux_release_shared_v3(void* shared_owner);

/* Optional native producer synchronization. Borrowed event ownership follows the
 * consumer; callers must encode every dependency before reading an early image.
 * A successful zero count means no attached dependencies, not GPU completion.
 * These hooks never wait or authorize consumer completion. */
typedef struct webscene_gpu_metal_event_view_v3 {
    uint32_t struct_size, version;
    void* borrowed_shared_event;
    uint64_t signaled_value;
} webscene_gpu_metal_event_view_v3;
WEBSCENE_API uint8_t webscene_gpu_image_dependency_count_v3(
    const webscene_gpu_image_consumer_v3* consumer,uint32_t* count);
WEBSCENE_API uint8_t webscene_gpu_image_get_metal_event_v3(
    const webscene_gpu_image_consumer_v3* consumer,uint32_t index,webscene_gpu_metal_event_view_v3* result);


typedef enum webscene_resource_kind {
    WEBSCENE_RESOURCE_DOCUMENT = 0,
    WEBSCENE_RESOURCE_SCRIPT = 1,
    WEBSCENE_RESOURCE_STYLESHEET = 2,
    // Text-backed SVG image resources used by CSS background-image. Binary
    // image formats require a future byte-resource envelope.
    WEBSCENE_RESOURCE_IMAGE = 3,
    WEBSCENE_RESOURCE_DATA = 4
} webscene_resource_kind;

/*
 * Synchronous text-resource callback used by the native DOM runtime. The
 * callback follows the other copy APIs: a null/short destination reports the
 * required response-envelope byte count. The envelope contains status,
 * cacheability/freshness metadata, validators, and UTF-8 content. Returning
 * zero reports a load failure. The URL is already absolute and normalized by
 * WebScene.
 * Envelope (little endian): uint8 status, uint8 cacheable, uint32 tag length,
 * int64 last-modified seconds, int64 fresh-until seconds, UTF-8 tag, content.
 * Status 1 = content, 2 = not modified. Status 3 = failure: tag is a stable
 * error category (http/network/timeout/cancelled/not-found/unsupported/loader),
 * first int64 is HTTP status (0 unknown), second is elapsed microseconds.
 * Failure content must be empty; do not include exception text, credentials or
 * request bodies. Older engines safely treat status 3 as an ordinary failure.
 * Returning zero remains supported, with generic loader diagnostics.
 */
typedef size_t (*webscene_resource_load_callback)(
    void* user_data,
    uint32_t kind,
    const char* url,
    size_t url_length,
    const char* entity_tag,
    size_t entity_tag_length,
    int64_t last_modified_unix_seconds,
    char* destination,
    size_t destination_capacity);

typedef enum webscene_resource_initiator {
    WEBSCENE_RESOURCE_INITIATOR_NAVIGATION = 0,
    WEBSCENE_RESOURCE_INITIATOR_SUBRESOURCE = 1,
    WEBSCENE_RESOURCE_INITIATOR_FETCH = 2
} webscene_resource_initiator;

typedef enum webscene_fetch_mode {
    WEBSCENE_FETCH_MODE_NONE = 0,
    WEBSCENE_FETCH_MODE_SAME_ORIGIN = 1,
    WEBSCENE_FETCH_MODE_CORS = 2,
    WEBSCENE_FETCH_MODE_NO_CORS = 3
} webscene_fetch_mode;

typedef enum webscene_request_destination {
    WEBSCENE_REQUEST_DESTINATION_NONE = 0,
    WEBSCENE_REQUEST_DESTINATION_DOCUMENT = 1,
    WEBSCENE_REQUEST_DESTINATION_SCRIPT = 2,
    WEBSCENE_REQUEST_DESTINATION_STYLE = 3,
    WEBSCENE_REQUEST_DESTINATION_IMAGE = 4,
    WEBSCENE_REQUEST_DESTINATION_FONT = 5
} webscene_request_destination;

typedef struct webscene_resource_request_context {
    uint32_t struct_size;
    uint32_t initiator;
    const char* origin;
    size_t origin_length;
    const char* referrer;
    size_t referrer_length;
    uint32_t mode;
    uint32_t destination;
} webscene_resource_request_context;

/*
 * Versioned resource callback carrying browser request context. Hosts that do
 * not provide it retain the original callback contract above.
 */
typedef size_t (*webscene_resource_load_callback_v2)(
    void* user_data,
    uint32_t kind,
    const char* url,
    size_t url_length,
    const char* entity_tag,
    size_t entity_tag_length,
    int64_t last_modified_unix_seconds,
    const webscene_resource_request_context* request_context,
    char* destination,
    size_t destination_capacity);

/*
 * Extended request metadata used by browser-authored fetch requests. The
 * original v2 callback remains available for binary compatibility; engines
 * call v3 when supplied and otherwise retain the GET-only v2 contract.
 */
typedef struct webscene_resource_request_context_v3 {
    uint32_t struct_size;
    uint32_t initiator;
    const char* origin;
    size_t origin_length;
    const char* referrer;
    size_t referrer_length;
    uint32_t mode;
    uint32_t destination;
    const char* method;
    size_t method_length;
    const char* body;
    size_t body_length;
    const char* content_type;
    size_t content_type_length;
} webscene_resource_request_context_v3;

typedef size_t (*webscene_resource_load_callback_v3)(
    void* user_data,
    uint32_t kind,
    const char* url,
    size_t url_length,
    const char* entity_tag,
    size_t entity_tag_length,
    int64_t last_modified_unix_seconds,
    const webscene_resource_request_context_v3* request_context,
    char* destination,
    size_t destination_capacity);

typedef enum webscene_fetch_credentials {
    WEBSCENE_FETCH_CREDENTIALS_OMIT = 0,
    WEBSCENE_FETCH_CREDENTIALS_SAME_ORIGIN = 1,
    WEBSCENE_FETCH_CREDENTIALS_INCLUDE = 2
} webscene_fetch_credentials;

typedef struct webscene_resource_header_v4 {
    uint32_t struct_size;
    const char* name;
    size_t name_length;
    const char* value;
    size_t value_length;
} webscene_resource_header_v4;

enum {
    WEBSCENE_RESOURCE_REDIRECT_CREDENTIALS_FORWARDED_V4 = 1U << 0U,
    WEBSCENE_RESOURCE_REDIRECT_CORS_CREDENTIALS_V4 = 1U << 1U
};

/* One followed network redirect. Buffers are borrowed for the callback only.
 * CREDENTIALS_FORWARDED describes the request to destination_url.
 * cors_allow_origin and CORS_CREDENTIALS describe the response received from
 * source_url. Response headers and credentials are not copied into the
 * redirect ledger. */
typedef struct webscene_resource_redirect_hop_v4 {
    uint32_t struct_size;
    uint32_t status;
    uint32_t flags;
    const char* source_url;
    size_t source_url_length;
    const char* destination_url;
    size_t destination_url_length;
    const char* cors_allow_origin;
    size_t cors_allow_origin_length;
} webscene_resource_redirect_hop_v4;

/* Response metadata is borrowed only for the callback invocation. The engine
 * copies accepted fields before returning to the host. Header, redirect-hop,
 * and total metadata counts are bounded; Set-Cookie is consumed by the cookie
 * jar and is never exposed through the JavaScript Headers object. A callback
 * must check struct_size before writing the optional redirect_hops tail. */
typedef struct webscene_resource_response_v4 {
    uint32_t struct_size;
    uint32_t status;
    const char* status_text;
    size_t status_text_length;
    const char* final_url;
    size_t final_url_length;
    const webscene_resource_header_v4* headers;
    size_t header_count;
    const webscene_resource_redirect_hop_v4* redirect_hops;
    size_t redirect_hop_count;
} webscene_resource_response_v4;

typedef struct webscene_resource_request_context_v4 {
    uint32_t struct_size;
    uint32_t initiator;
    const char* origin;
    size_t origin_length;
    const char* referrer;
    size_t referrer_length;
    uint32_t mode;
    uint32_t destination;
    const char* method;
    size_t method_length;
    const char* body;
    size_t body_length;
    const char* content_type;
    size_t content_type_length;
    uint32_t credentials;
    const char* cookie;
    size_t cookie_length;
} webscene_resource_request_context_v4;

typedef size_t (*webscene_resource_load_callback_v4)(
    void* user_data,
    uint32_t kind,
    const char* url,
    size_t url_length,
    const char* entity_tag,
    size_t entity_tag_length,
    int64_t last_modified_unix_seconds,
    const webscene_resource_request_context_v4* request_context,
    webscene_resource_response_v4* response,
    char* destination,
    size_t destination_capacity);

/* v5 preserves the bounded author request-header list that accompanies fetch.
 * Header buffers are borrowed only for the callback invocation. Cookie,
 * Origin and Referer remain available through their dedicated fields so hosts
 * can apply transport policy without reconstructing browser-owned metadata. */
typedef struct webscene_resource_request_context_v5 {
    uint32_t struct_size;
    uint32_t initiator;
    const char* origin;
    size_t origin_length;
    const char* referrer;
    size_t referrer_length;
    uint32_t mode;
    uint32_t destination;
    const char* method;
    size_t method_length;
    const char* body;
    size_t body_length;
    const char* content_type;
    size_t content_type_length;
    uint32_t credentials;
    const char* cookie;
    size_t cookie_length;
    const webscene_resource_header_v4* headers;
    size_t header_count;
} webscene_resource_request_context_v5;

typedef size_t (*webscene_resource_load_callback_v5)(
    void* user_data,
    uint32_t kind,
    const char* url,
    size_t url_length,
    const char* entity_tag,
    size_t entity_tag_length,
    int64_t last_modified_unix_seconds,
    const webscene_resource_request_context_v5* request_context,
    webscene_resource_response_v4* response,
    char* destination,
    size_t destination_capacity);

/* Stable, product-neutral identifiers for native constraint-validation
 * messages. Hosts may translate these identifiers without inspecting author
 * content or browser-owned English text. */
enum {
    WEBSCENE_VALIDATION_MESSAGE_VALUE_MISSING_V1 = 1U,
    WEBSCENE_VALIDATION_MESSAGE_TYPE_MISMATCH_V1 = 2U,
    WEBSCENE_VALIDATION_MESSAGE_PATTERN_MISMATCH_V1 = 3U,
    WEBSCENE_VALIDATION_MESSAGE_TOO_LONG_V1 = 4U,
    WEBSCENE_VALIDATION_MESSAGE_TOO_SHORT_V1 = 5U,
    WEBSCENE_VALIDATION_MESSAGE_RANGE_UNDERFLOW_V1 = 6U,
    WEBSCENE_VALIDATION_MESSAGE_RANGE_OVERFLOW_V1 = 7U,
    WEBSCENE_VALIDATION_MESSAGE_STEP_MISMATCH_V1 = 8U,
    WEBSCENE_VALIDATION_MESSAGE_BAD_INPUT_V1 = 9U
};

enum {
    WEBSCENE_VALIDATION_ARGUMENT_CONTROL_TYPE_V1 = 1U,
    WEBSCENE_VALIDATION_ARGUMENT_PATTERN_V1 = 2U,
    WEBSCENE_VALIDATION_ARGUMENT_MIN_LENGTH_V1 = 3U,
    WEBSCENE_VALIDATION_ARGUMENT_MAX_LENGTH_V1 = 4U,
    WEBSCENE_VALIDATION_ARGUMENT_MINIMUM_V1 = 5U,
    WEBSCENE_VALIDATION_ARGUMENT_MAXIMUM_V1 = 6U,
    WEBSCENE_VALIDATION_ARGUMENT_STEP_V1 = 7U,
    WEBSCENE_VALIDATION_MESSAGE_MAX_ARGUMENTS_V1 = 4U,
    WEBSCENE_VALIDATION_MESSAGE_MAX_ARGUMENT_BYTES_V1 = 256U,
    WEBSCENE_VALIDATION_MESSAGE_MAX_OUTPUT_BYTES_V1 = 1024U
};

typedef struct webscene_validation_message_argument_v1 {
    uint32_t struct_size;
    uint32_t kind;
    const char* value_utf8;
    size_t value_length;
} webscene_validation_message_argument_v1;

/* Formats one built-in validation message. The engine invokes this callback
 * synchronously on its owner worker, at most once per requested message, and
 * never for custom validity text or from frame production. Arguments and the
 * destination are borrowed for the call. Return the bytes written (1..1024),
 * or zero to select the bounded English fallback. Oversized or invalid UTF-8
 * output also selects that fallback. The callback must not block, throw, or
 * reenter the engine. Its function and user data must remain valid until
 * webscene_engine_destroy returns. */
typedef size_t (*webscene_validation_message_format_callback_v1)(
    void* user_data,
    uint32_t reason,
    const webscene_validation_message_argument_v1* arguments,
    size_t argument_count,
    char* destination,
    size_t destination_capacity);

enum {
    WEBSCENE_VALIDATION_MESSAGE_TIMEOUT_DISABLED_V1 = 0U,
    WEBSCENE_VALIDATION_MESSAGE_TIMEOUT_MINIMUM_MS_V1 = 1000U,
    WEBSCENE_VALIDATION_MESSAGE_TIMEOUT_MAXIMUM_MS_V1 = 60000U
};

/*
 * Asynchronous notification emitted after an immutable scene has been
 * published. Consumers use this edge to schedule a compositor paint; they
 * still acquire and acknowledge the scene through the normal scene API.
 * The callback runs on the engine worker and must not block.
 */
typedef void (*webscene_scene_published_callback)(
    void* user_data,
    uint64_t revision,
    uint64_t consumed_input_sequence,
    float viewport_width,
    float viewport_height);

typedef struct webscene_text_metrics {
    uint32_t struct_size;
    float advance_width;
    float ascent;
    float descent;
    float leading;
    float actual_bounding_box_left;
    float actual_bounding_box_right;
    float actual_bounding_box_ascent;
    float actual_bounding_box_descent;
} webscene_text_metrics;

/*
 * Synchronous host text shaper used by native layout. Layout and paint must
 * consume the same glyph advances; otherwise kerning, combining marks, font
 * weight, and fallback faces can make inline boxes clip or drift. The callback
 * runs on the engine worker and must not block or call back into the engine.
 */
typedef uint8_t (*webscene_text_measure_callback)(
    void* user_data,
    const char* text,
    size_t text_length,
    const char* font_family,
    size_t font_family_length,
    float font_size,
    int32_t font_weight,
    float letter_spacing,
    float word_spacing,
    webscene_text_metrics* metrics);

/*
 * Edge notification emitted after a managed host request is queued. The
 * callback runs on the engine worker and must only signal non-blocking host
 * work; requests are still consumed through webscene_engine_take_host_request.
 */
typedef void (*webscene_host_request_available_callback)(void* user_data);

/*
 * Edge notification emitted when JavaScript queues an interop callback for
 * its managed host. The callback runs on the engine worker and must only
 * signal non-blocking host work; managed callbacks are drained after the
 * current engine call returns.
 */
typedef void (*webscene_interop_callback_available_callback)(void* user_data);

/*
 * Edge notification emitted when the engine's host animation-frame or
 * frame-aligned pointer-input demand transitions from idle to active. The
 * callback runs on the engine worker and must only wake a compositor; demand
 * is still queried through webscene_engine_requires_animation_frame and
 * released by a compositor observation/frame input.
 */
typedef void (*webscene_animation_frame_requested_callback)(void* user_data);

/* Observes stylesheet consumption, including native cache hits and inline CSS.
 * Runs synchronously on the runtime worker before styling/layout. Buffers are
 * borrowed for the callback only. Hosts may register fonts but must not reenter
 * this engine. The callback must not throw across the ABI. */
typedef void (*webscene_stylesheet_consumed_callback)(
    void* user_data, const char* address, size_t address_length,
    const char* css, size_t css_length);

/* Optional host admission for the main document, evaluated on the runtime
 * worker before scripts with the final resolved URL (initially about:blank).
 * IOSURFACE certifies both a secure context and a GPU-capable scene consumer.
 * Return DISABLED for untrusted/non-secure documents. No ABI reentry or throws.
 * Current implementation supports IOSURFACE only on graphics-enabled macOS. */
enum { WEBSCENE_WEBGPU_DISABLED = 0, WEBSCENE_WEBGPU_IOSURFACE = 1, WEBSCENE_WEBGPU_DXGI = 2 };
typedef uint32_t (*webscene_webgpu_policy_callback)(void* user_data,
    const char* document_url, size_t document_url_length);

typedef struct webscene_engine_options {
    uint32_t struct_size;
    uint32_t simulated_chart_command_count;
    const char* compilation_cache_directory;
    size_t compilation_cache_directory_length;
    webscene_resource_load_callback resource_load_callback;
    void* resource_load_user_data;
    webscene_scene_published_callback scene_published_callback;
    void* scene_published_user_data;
    webscene_text_measure_callback text_measure_callback;
    void* text_measure_user_data;
    webscene_host_request_available_callback host_request_available_callback;
    void* host_request_available_user_data;
    webscene_interop_callback_available_callback interop_callback_available_callback;
    void* interop_callback_available_user_data;
    webscene_animation_frame_requested_callback animation_frame_requested_callback;
    void* animation_frame_requested_user_data;
    webscene_resource_load_callback_v2 resource_load_callback_v2;
    void* resource_load_v2_user_data;
    webscene_resource_load_callback_v3 resource_load_callback_v3;
    void* resource_load_v3_user_data;
    webscene_stylesheet_consumed_callback stylesheet_consumed_callback;
    void* stylesheet_consumed_user_data;
    webscene_webgpu_policy_callback webgpu_policy_callback;
    void* webgpu_policy_user_data;
    webscene_resource_load_callback_v4 resource_load_callback_v4;
    void* resource_load_v4_user_data;
    /*
     * Durable browser storage is disabled unless both strings are supplied.
     * storage_partition_key is a stable host-owned application/profile id;
     * the runtime still partitions its files by the document's effective
     * origin below that key. Hosts may therefore keep a random loopback port
     * out of the profile identity without merging unrelated applications.
     */
    const char* storage_directory;
    size_t storage_directory_length;
    const char* storage_partition_key;
    size_t storage_partition_key_length;
    uint64_t storage_quota_bytes;
    webscene_resource_load_callback_v5 resource_load_callback_v5;
    void* resource_load_v5_user_data;
    webscene_validation_message_format_callback_v1
        validation_message_format_callback_v1;
    void* validation_message_format_user_data_v1;
    /* Optional engine-native visual validation-message timeout. Zero keeps the
     * event-driven default. Nonzero values must be within the published v1
     * bounds and never delay or repeat the semantic announcement. */
    uint32_t validation_message_timeout_milliseconds_v1;
} webscene_engine_options;

/*
 * Profile data is local application data, protected by owner-only filesystem
 * permissions but not encrypted by WebScene. Hosts should select an OS-backed
 * protected location and may layer platform credential/encryption facilities.
 * Clear only while no engine has the partition open; BUSY is returned instead
 * of racing a live writer. The partition key is hashed below storage_directory,
 * so it is never interpreted as a path and cannot broaden the deletion scope.
 */
enum {
    WEBSCENE_PROFILE_CLEAR_COOKIES_V1 = 1U << 0U,
    WEBSCENE_PROFILE_CLEAR_LOCAL_STORAGE_V1 = 1U << 1U,
    /* Also clears bounded non-secret form restoration state. */
    WEBSCENE_PROFILE_CLEAR_ALL_SITE_DATA_V1 = 1U << 2U
};
enum {
    WEBSCENE_PROFILE_STATUS_OK_V1 = 0U,
    WEBSCENE_PROFILE_STATUS_INVALID_ARGUMENT_V1 = 1U,
    WEBSCENE_PROFILE_STATUS_BUSY_V1 = 2U,
    WEBSCENE_PROFILE_STATUS_IO_ERROR_V1 = 3U,
    WEBSCENE_PROFILE_STATUS_CORRUPT_V1 = 4U,
    WEBSCENE_PROFILE_STATUS_QUOTA_EXCEEDED_V1 = 5U
};
WEBSCENE_API uint32_t webscene_profile_clear_data_v1(
    const char* storage_directory,
    size_t storage_directory_length,
    const char* storage_partition_key,
    size_t storage_partition_key_length,
    uint64_t storage_quota_bytes,
    uint32_t flags);

enum {
    WEBSCENE_DOCUMENT_SCRIPT_ALL_FRAMES = 1U << 0U
};

/*
 * Fixed-layout document-start program descriptor. The engine copies every
 * source and name before webscene_engine_load_url_with_options returns; callers
 * retain no buffers for queued navigation work.
 */
typedef struct webscene_document_script {
    uint32_t struct_size;
    uint32_t flags;
    const char* source;
    size_t source_length;
    const char* name;
    size_t name_length;
} webscene_document_script;

typedef struct webscene_navigation_options {
    uint32_t struct_size;
    uint32_t document_script_count;
    const webscene_document_script* document_scripts;
} webscene_navigation_options;

typedef struct webscene_engine_metrics {
    uint64_t enqueued_inputs;
    uint64_t dropped_inputs;
    uint64_t consumed_inputs;
    uint64_t published_scenes;
    uint64_t acquired_scenes;
    uint64_t executed_scripts;
    uint64_t script_errors;
    uint64_t dom_nodes;
    uint64_t layout_passes;
    uint64_t iframe_nodes;
    uint64_t iframe_html_bytes;
    uint64_t frame_scripts_executed;
    uint64_t frame_script_errors;
    uint64_t canvas_nodes;
    uint64_t component_ready;
    uint64_t compilation_requests;
    uint64_t compilation_memory_hits;
    uint64_t compilation_persistent_hits;
    uint64_t compilation_persistent_misses;
    uint64_t compilation_cache_rejections;
    uint64_t compilation_cache_bytes_read;
    uint64_t compilation_cache_bytes_written;
    uint64_t compilation_time_nanoseconds;
    uint64_t input_events_dispatched;
    uint64_t input_callbacks_invoked;
    uint64_t busiest_canvas_width_milli;
    uint64_t busiest_canvas_height_milli;
    uint64_t coalesced_resize_inputs;
    uint64_t applied_resize_inputs;
    uint64_t last_resize_dispatch_nanoseconds;
    uint64_t last_scene_publication_nanoseconds;
    uint64_t last_resize_outer_listeners_nanoseconds;
    uint64_t last_resize_frame_listeners_nanoseconds;
    uint64_t last_resize_layout_nanoseconds;
    uint64_t last_resize_observers_nanoseconds;
    uint64_t coalesced_pointer_move_inputs;
    uint64_t coalesced_wheel_inputs;
    uint64_t applied_pointer_move_inputs;
    uint64_t applied_wheel_inputs;
    uint64_t applied_animation_frames;
    uint64_t coalesced_animation_frames;
    uint64_t last_animation_advance_nanoseconds;
    uint64_t last_layout_nanoseconds;
    uint64_t last_scene_build_nanoseconds;
    uint64_t maximum_scene_publication_nanoseconds;
} webscene_engine_metrics;

typedef struct webscene_input_dispatch_metrics {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t last_dispatch_nanoseconds;
    uint64_t maximum_dispatch_nanoseconds;
    uint64_t last_dispatch_sequence;
    uint64_t dispatched_inputs;
    uint64_t total_dispatch_nanoseconds;
} webscene_input_dispatch_metrics;

typedef struct webscene_animation_frame_metrics {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t dispatched_frames;
    uint64_t total_dispatch_nanoseconds;
    uint64_t last_dispatch_nanoseconds;
    uint64_t maximum_dispatch_nanoseconds;
    uint64_t last_timestamp_microseconds;
} webscene_animation_frame_metrics;

typedef struct webscene_scene_flow_metrics {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t publication_attempts;
    uint64_t blocked_publications;
    uint64_t acknowledged_scenes;
    uint64_t total_acknowledgement_nanoseconds;
    uint64_t last_acknowledgement_nanoseconds;
    uint64_t maximum_acknowledgement_nanoseconds;
    uint64_t acknowledged_revision;
} webscene_scene_flow_metrics;

typedef struct webscene_resize_frame_metrics {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t submitted_pairs;
    uint64_t applied_pairs;
    uint64_t published_pairs;
    uint64_t total_queue_nanoseconds;
    uint64_t last_queue_nanoseconds;
    uint64_t maximum_queue_nanoseconds;
    uint64_t total_dispatch_nanoseconds;
    uint64_t last_dispatch_nanoseconds;
    uint64_t maximum_dispatch_nanoseconds;
    uint64_t animation_frame_callbacks;
    uint64_t total_animation_frame_batch_nanoseconds;
    uint64_t last_animation_frame_batch_nanoseconds;
    uint64_t maximum_animation_frame_batch_nanoseconds;
    uint64_t total_to_publication_nanoseconds;
    uint64_t last_to_publication_nanoseconds;
    uint64_t maximum_to_publication_nanoseconds;
} webscene_resize_frame_metrics;

typedef struct webscene_resource_cache_metrics {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t requests;
    uint64_t hits;
    uint64_t misses;
    uint64_t rejections;
    uint64_t bytes_read;
    uint64_t bytes_written;
} webscene_resource_cache_metrics;

/*
 * Monotonic work counters used to compare equivalent benchmark intervals.
 * JavaScript task counters are copied from the worker-owned runtime state at
 * existing metric update boundaries; gauges and retained sizes live in the
 * separate memory and interop-pool structures.
 */
typedef struct webscene_runtime_work_metrics {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t timers_scheduled;
    uint64_t timers_fired;
    uint64_t timers_cancelled;
    uint64_t late_timers;
    uint64_t total_timer_lateness_nanoseconds;
    uint64_t animation_frames_requested;
    uint64_t animation_frames_invoked;
    uint64_t animation_frames_cancelled;
    uint64_t microtask_checkpoints;
    uint64_t worker_waits;
    uint64_t worker_signalled_wakes;
    uint64_t worker_timeout_wakes;
    uint64_t scene_builds;
    uint64_t no_damage_scene_builds;
    uint64_t full_checkpoint_scene_builds;
    uint64_t arbitrary_evaluation_calls;
    uint64_t generated_invoke_calls;
    uint64_t generated_callback_calls;
    uint64_t arbitrary_evaluation_source_bytes;
    uint64_t generated_request_bytes;
} webscene_runtime_work_metrics;

typedef struct webscene_process_cache_metrics {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t compilation_memory_hits;
    uint64_t compilation_leaders;
    uint64_t compilation_waiters;
    uint64_t compilation_shared_bytes;
    uint64_t resource_memory_hits;
    uint64_t resource_load_leaders;
    uint64_t resource_load_waiters;
    uint64_t resource_shared_bytes;
    /* ABI v2 tail; immutable external script-source sharing. */
    uint64_t script_source_memory_hits;
    uint64_t script_source_shared_bytes;
    /* ABI v3 tail; stable shared-isolate pool ownership diagnostics. */
    uint64_t shared_isolate_slot;
    uint64_t shared_isolate_active_contexts;
    uint64_t shared_isolate_peak_contexts;
} webscene_process_cache_metrics;

/*
 * Last worker-thread snapshot of memory retained by this engine plus the
 * process-wide immutable caches shared by all engines. Process cache byte
 * counts must be counted once per process, not once per engine.
 */
typedef struct webscene_engine_memory_metrics {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t v8_total_heap_bytes;
    uint64_t v8_used_heap_bytes;
    uint64_t v8_executable_heap_bytes;
    uint64_t v8_physical_heap_bytes;
    uint64_t v8_external_bytes;
    uint64_t v8_malloced_bytes;
    uint64_t v8_peak_malloced_bytes;
    uint64_t latest_scene_bytes;
    uint64_t process_compilation_cache_bytes;
    uint64_t process_resource_cache_bytes;
    /* ABI v2 tail; callers using the original prefix remain supported. */
    uint64_t v8_code_and_metadata_bytes;
    uint64_t v8_bytecode_and_metadata_bytes;
    uint64_t v8_external_script_source_bytes;
    /* Optional retained-native-allocation attribution tail. */
    uint64_t native_dom_node_count;
    uint64_t native_dom_node_size_bytes;
    uint64_t native_dom_inline_bytes;
    uint64_t native_dom_pseudo_storage_bytes;
    uint64_t native_dom_canvas_node_count;
    uint64_t native_dom_canvas_storage_bytes;
    uint64_t native_dom_animation_count;
    uint64_t native_dom_animation_storage_bytes;
    uint64_t native_dom_custom_property_node_count;
    uint64_t native_dom_custom_property_entry_count;
    uint64_t native_dom_custom_property_storage_bytes;
    uint64_t native_dom_background_image_count;
    uint64_t native_dom_background_image_storage_bytes;
    uint64_t native_dom_grid_count;
    uint64_t native_dom_grid_storage_bytes;
    uint64_t native_dom_authored_style_node_count;
    uint64_t native_dom_authored_style_entry_count;
    uint64_t native_dom_authored_style_storage_bytes;
    uint64_t native_css_rule_count;
    uint64_t native_css_rule_storage_bytes;
    uint64_t native_css_index_storage_bytes;
    uint64_t process_shared_css_rule_count;
    uint64_t process_shared_css_rule_storage_bytes;
    uint64_t low_memory_notifications;
    uint64_t native_dom_attribute_node_count;
    uint64_t native_dom_attribute_entry_count;
    uint64_t native_dom_attribute_storage_bytes;
    /* Additive metrics tail; native registries, caches and mapped storage. */
    uint64_t native_wrapper_handle_count;
    uint64_t native_wrapper_storage_bytes;
    uint64_t native_text_measurement_cache_entry_count;
    uint64_t native_text_measurement_cache_storage_bytes;
    uint64_t process_compilation_mapped_cache_bytes;
    uint64_t process_resource_mapped_cache_bytes;
    uint64_t native_dom_textual_style_count;
    uint64_t native_dom_textual_style_storage_bytes;
    uint64_t native_dom_node_pool_reserved_bytes;
    uint64_t native_dom_node_pool_peak_bytes;
    uint64_t native_dom_table_layout_count;
    uint64_t native_dom_table_layout_storage_bytes;
    uint64_t native_dom_form_control_count;
    uint64_t native_dom_form_control_storage_bytes;
    uint64_t hidden_low_memory_notifications;
    uint64_t native_event_listener_count;
    uint64_t native_event_listener_storage_bytes;
    /* ABI additive tail; aggregate V8 heap-space attribution. */
    uint64_t v8_young_space_used_bytes;
    uint64_t v8_young_space_physical_bytes;
    uint64_t v8_old_space_used_bytes;
    uint64_t v8_old_space_physical_bytes;
    uint64_t v8_code_space_used_bytes;
    uint64_t v8_code_space_physical_bytes;
    uint64_t v8_map_space_used_bytes;
    uint64_t v8_map_space_physical_bytes;
    uint64_t v8_large_object_space_used_bytes;
    uint64_t v8_large_object_space_physical_bytes;
    uint64_t v8_read_only_space_used_bytes;
    uint64_t v8_read_only_space_physical_bytes;
    uint64_t v8_shared_space_used_bytes;
    uint64_t v8_shared_space_physical_bytes;
    uint64_t v8_trusted_space_used_bytes;
    uint64_t v8_trusted_space_physical_bytes;
    /* ABI additive tail; bounded immutable scene pipeline attribution. */
    uint64_t pending_scene_count;
    uint64_t pending_scene_bytes;
} webscene_engine_memory_metrics;

/*
 * Pays the process-wide native runtime initialization cost without creating a
 * document, isolate, or chart. This does not read or mutate compilation caches.
 */
#define WEBSCENE_ENGINE_BUILD_FEATURE_CERTIFICATION (1U << 0U)
#define WEBSCENE_ENGINE_BUILD_FEATURE_V8_INSPECTOR (1U << 1U)

WEBSCENE_API uint32_t webscene_engine_get_abi_version(void);
/*
 * Reports compile-time features of the loaded native binary. Certification
 * telemetry/profiling and V8 Inspector hooks/state are absent unless their
 * respective bits are present.
 */
WEBSCENE_API uint32_t webscene_engine_get_build_features(void);
#if defined(WEBSCENE_NATIVE_ENGINE_MEDIA_REFRESH_BENCHMARK_COUNTERS)
WEBSCENE_API void webscene_media_refresh_benchmark_reset_counters(void);
WEBSCENE_API uint64_t webscene_media_refresh_benchmark_index_rule_calls(void);
WEBSCENE_API uint64_t webscene_media_refresh_benchmark_root_variable_refreshes(void);
WEBSCENE_API uint64_t webscene_media_refresh_benchmark_class_lookups(void);
WEBSCENE_API uint64_t webscene_media_refresh_benchmark_owned_class_lookup_keys(void);
WEBSCENE_API uint64_t webscene_media_refresh_benchmark_owned_class_lookup_bytes(void);
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_BENCHMARK_COUNTERS)
WEBSCENE_API void webscene_selector_sibling_benchmark_reset_counters(void);
WEBSCENE_API uint64_t webscene_selector_sibling_benchmark_positional_matches(void);
WEBSCENE_API uint64_t webscene_selector_sibling_benchmark_sibling_scans(void);
WEBSCENE_API uint64_t webscene_selector_sibling_benchmark_vector_materializations(void);
WEBSCENE_API uint64_t webscene_selector_sibling_benchmark_pointer_copies(void);
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_CANVAS_PAINT_STATE_BENCHMARK_COUNTERS)
WEBSCENE_API void webscene_canvas_paint_state_benchmark_reset_counters(void);
WEBSCENE_API uint64_t webscene_canvas_paint_state_benchmark_string_property_probes(void);
WEBSCENE_API uint64_t webscene_canvas_paint_state_benchmark_utf8_conversions(void);
WEBSCENE_API uint64_t webscene_canvas_paint_state_benchmark_stack_comparisons(void);
WEBSCENE_API uint64_t webscene_canvas_paint_state_benchmark_cached_value_hits(void);
#endif
WEBSCENE_API uint8_t webscene_engine_prewarm(void);
WEBSCENE_API webscene_engine* webscene_engine_create(uint32_t simulated_chart_command_count);
WEBSCENE_API webscene_engine* webscene_engine_create_with_options(const webscene_engine_options* options);
WEBSCENE_API void webscene_engine_destroy(webscene_engine* engine);

// One optional host observer, independent of the creation-time callbacks.
// Called on a producer thread; it must only schedule work and must not reenter
// this engine. Passing null unregisters and waits for any active call to finish.
// The caller must serialize registration with destruction of the engine.
typedef void (*webscene_work_available_callback_v1)(void* user_data);
WEBSCENE_API void webscene_engine_set_work_available_callback_v1(
    webscene_engine* engine,
    webscene_work_available_callback_v1 callback,
    void* user_data);

WEBSCENE_API uint8_t webscene_engine_set_resource_root(
    webscene_engine* engine,
    const char* resource_root,
    size_t resource_root_length);
// Loads a named, build-time compiled package on the engine worker. Strings and
// viewport are copied before return. No application C++ objects cross this ABI.
WEBSCENE_API uint8_t webscene_engine_load_compiled_document_v1(
    webscene_engine* engine, const char* name, size_t name_length,
    const char* base_url, size_t base_url_length, const webscene_input_event* viewport);

WEBSCENE_API uint8_t webscene_engine_load_url(
    webscene_engine* engine,
    const char* url,
    size_t url_length);
/* Set the initial viewport on the worker immediately before document scripts.
 * This avoids publishing a canvas initialized against default dimensions. */
WEBSCENE_API uint8_t webscene_engine_load_url_with_viewport(
    webscene_engine* engine, const char* url, size_t url_length,
    const webscene_input_event* viewport);
WEBSCENE_API uint8_t webscene_engine_load_url_with_options(
    webscene_engine* engine,
    const char* url,
    size_t url_length,
    const webscene_navigation_options* options);
WEBSCENE_API uint8_t webscene_engine_enqueue(webscene_engine* engine, const webscene_input_event* event);
/*
 * Atomically submits a viewport update and its corresponding host rendering
 * opportunity. The worker applies resize listeners/observers before releasing
 * requestAnimationFrame callbacks, without racing two independently awakened
 * enqueue calls. Both records contribute to the ordinary input metrics.
 */
WEBSCENE_API uint8_t webscene_engine_enqueue_resize_frame(
    webscene_engine* engine,
    const webscene_input_event* resize_event,
    const webscene_input_event* frame_event);
/*
 * Requests a V8 low-memory collection on this engine's worker thread. This is
 * intended for hidden/idle components or host memory-pressure handling; it
 * queues work and does not block the caller on garbage collection.
 */
WEBSCENE_API uint8_t webscene_engine_request_low_memory(webscene_engine* engine);
/*
 * Declares whether the host is actively presenting this engine. A transition
 * to hidden schedules one debounced low-memory collection on the engine
 * worker; returning visible before the deadline cancels it.
 */
WEBSCENE_API uint8_t webscene_engine_set_visible(webscene_engine* engine, uint8_t visible);
/* Publishes native key-window focus to document.hasFocus() and dispatches
 * focus/blur on the selected browsing context. Repeated values are coalesced. */
WEBSCENE_API uint8_t webscene_engine_set_window_focused_v1(
    webscene_engine* engine, uint8_t focused);
/* Synchronizes fullscreen changes initiated by native window controls. Script
 * initiated transitions use the typed request/completion path. */
WEBSCENE_API uint8_t webscene_engine_set_window_fullscreen_v1(
    webscene_engine* engine, uint8_t fullscreen);
/* Synchronously asks the active top-level realm whether a native window close
 * may proceed. A veto leaves the document active. An allow decision dispatches
 * pagehide once before returning and is coalesced until the next navigation. */
enum {
    WEBSCENE_WINDOW_CLOSE_ERROR_V1 = 0,
    WEBSCENE_WINDOW_CLOSE_ALLOW_V1 = 1,
    WEBSCENE_WINDOW_CLOSE_VETO_V1 = 2
};
WEBSCENE_API uint32_t webscene_engine_request_window_close_v1(
    webscene_engine* engine);
/*
 * Updates the host's effective color preference. The worker re-evaluates CSS
 * media rules and subsequent Window.matchMedia snapshots against this value.
 */
WEBSCENE_API uint8_t webscene_engine_set_preferred_color_scheme(
    webscene_engine* engine,
    uint32_t preferred_color_scheme);
/* Publishes accessibility preferences as one coalesced host snapshot. */
WEBSCENE_API uint8_t webscene_engine_set_accessibility_preferences_v1(
    webscene_engine* engine,
    uint32_t preference_flags);
/*
 * Requests an updated semantic publication and acquires the latest completed
 * immutable snapshot. The first call may return null while the engine worker
 * builds the initial value; publication signals the ordinary work-available
 * callback. Limits are 16,384 nodes, 256 documents, 65,536 relationships and
 * 4 MiB of UTF-8. Truncation is explicit in snapshot flags.
 */
WEBSCENE_API const webscene_semantic_snapshot_view_v1*
webscene_engine_acquire_semantic_snapshot_v1(webscene_engine* engine);
WEBSCENE_API void webscene_semantic_snapshot_release_v1(
    const webscene_semantic_snapshot_view_v1* snapshot);
/* v2 adds a fixed-size typed companion table without changing v1 array stride. */
WEBSCENE_API const webscene_semantic_snapshot_view_v2*
webscene_engine_acquire_semantic_snapshot_v2(webscene_engine* engine);
WEBSCENE_API void webscene_semantic_snapshot_release_v2(
    const webscene_semantic_snapshot_view_v2* snapshot);
/* Admission pins an exact retained base until the worker finishes. */
WEBSCENE_API uint32_t webscene_engine_request_semantic_delta_v1(
    webscene_engine* engine,
    const webscene_semantic_delta_request_v1* request);
WEBSCENE_API const webscene_semantic_delta_view_v1*
webscene_engine_take_semantic_delta_v1(webscene_engine* engine);
WEBSCENE_API void webscene_semantic_delta_release_v1(
    const webscene_semantic_delta_view_v1* delta);
WEBSCENE_API const webscene_semantic_delta_view_v2*
webscene_engine_take_semantic_delta_v2(webscene_engine* engine);
WEBSCENE_API void webscene_semantic_delta_release_v2(
    const webscene_semantic_delta_view_v2* delta);
/* Queues bounded worker-thread routing; QUEUED reports admission, not DOM
 * completion. A later snapshot is the observable action result. */
WEBSCENE_API uint32_t webscene_engine_request_semantic_action_v1(
    webscene_engine* engine,
    const webscene_semantic_action_request_v1* request);
/* Takes the next bounded immutable live-region batch, or null when empty. */
WEBSCENE_API const webscene_semantic_live_batch_view_v1*
webscene_engine_take_semantic_live_events_v1(webscene_engine* engine);
WEBSCENE_API void webscene_semantic_live_batch_release_v1(
    const webscene_semantic_live_batch_view_v1* batch);
/* Returns the CSS cursor resolved at the latest hit-tested pointer position. */
WEBSCENE_API uint32_t webscene_engine_get_cursor(const webscene_engine* engine);
/*
 * Advances the document clock at a host input boundary without declaring a
 * rendering opportunity. This keeps idle transitions current while allowing
 * continuous pointer input to remain paced by real compositor boundaries.
 */
WEBSCENE_API void webscene_engine_observe_host_timeline(
    webscene_engine* engine,
    double timestamp_ms);
/*
 * Observes an inexpensive host compositor boundary without releasing V8 RAF
 * callbacks or requesting a scene. This keeps the CSS document timeline
 * current while rendering is idle, so a transition started by later input is
 * anchored to the display clock instead of the last demanded frame.
 */
WEBSCENE_API void webscene_engine_observe_compositor_frame(
    webscene_engine* engine,
    double timestamp_ms);
/*
 * Returns a demand bitmask for the next compositor frame: bit 0 is a pending
 * JavaScript RAF, bit 1 is a native CSS animation, and bit 2 is a focused
 * caret. Zero means a host frame would be empty.
 */
WEBSCENE_API uint8_t webscene_engine_requires_animation_frame(
    const webscene_engine* engine);
WEBSCENE_API uint8_t webscene_engine_execute_script(
    webscene_engine* engine,
    const char* source,
    size_t source_length,
    const char* document_name,
    size_t document_name_length);
/* Raw V8 Inspector/CDP sessions are available for dedicated isolates. */
WEBSCENE_API uint64_t webscene_engine_inspector_connect(
    webscene_engine* engine,
    webscene_inspector_message_callback message_callback,
    void* user_data,
    uint8_t wait_for_debugger);
/*
 * Preferred non-reentrant session contract. The callback only signals
 * availability; use webscene_engine_inspector_take_message to copy messages.
 */
WEBSCENE_API uint64_t webscene_engine_inspector_connect_v3(
    webscene_engine* engine,
    webscene_inspector_message_available_callback_v3 message_available_callback,
    void* user_data,
    uint8_t wait_for_debugger);
/*
 * Returns zero when no message is queued, SIZE_MAX when the bounded output
 * queue overflowed, or the required message size. A null/short destination
 * leaves the front message queued; a sufficiently large destination pops it.
 */
WEBSCENE_API size_t webscene_engine_inspector_take_message(
    webscene_engine* engine,
    uint64_t session_id,
    char* destination,
    size_t destination_capacity);
WEBSCENE_API uint8_t webscene_engine_inspector_dispatch(
    webscene_engine* engine,
    uint64_t session_id,
    const char* message,
    size_t message_length);
WEBSCENE_API uint8_t webscene_engine_inspector_disconnect(
    webscene_engine* engine,
    uint64_t session_id);
WEBSCENE_API uint8_t webscene_engine_inspector_is_available(
    const webscene_engine* engine);
/* Diagnostics: nonzero only after the first Inspector connection attempt. */
WEBSCENE_API uint8_t webscene_engine_inspector_state_created(
    const webscene_engine* engine);
WEBSCENE_API uint64_t webscene_engine_begin_evaluate_v3(
    webscene_engine* engine,
    const webscene_interop_evaluate_request_v3* request,
    webscene_interop_completed_callback_v3 completed,
    void* user_data);
WEBSCENE_API uint64_t webscene_engine_begin_invoke_v3(
    webscene_engine* engine,
    const webscene_interop_invoke_request_v3* request,
    webscene_interop_completed_callback_v3 completed,
    void* user_data);
WEBSCENE_API const webscene_interop_result_view_v3*
webscene_engine_take_invoke_result_v3(
    webscene_engine* engine,
    uint64_t operation_id);
WEBSCENE_API uint8_t webscene_engine_cancel_invoke_v3(
    webscene_engine* engine,
    uint64_t operation_id);
WEBSCENE_API void webscene_interop_result_release_v3(
    const webscene_interop_result_view_v3* result,
    uint64_t lease_id);
WEBSCENE_API const webscene_interop_callback_view_v3*
webscene_engine_take_callback_v3(webscene_engine* engine);
WEBSCENE_API uint8_t webscene_engine_complete_callback_v3(
    webscene_engine* engine,
    const webscene_interop_callback_completion_v3* completion);
WEBSCENE_API uint8_t webscene_engine_cancel_callback_v3(
    webscene_engine* engine,
    uint64_t call_id);
WEBSCENE_API void webscene_interop_callback_release_v3(
    const webscene_interop_callback_view_v3* callback,
    uint64_t lease_id);
WEBSCENE_API uint8_t webscene_engine_get_interop_pool_metrics_v3(
    const webscene_engine* engine,
    webscene_interop_pool_metrics_v3* metrics);
/*
 * Removes one actual managed-datafeed request from the native V8 bridge. The
 * payload is UTF-8 JSON. A too-small/null destination reports the required
 * size without consuming the request; a successful full copy consumes it.
 */
/* Native file service v1. Explicit opt-in permits script-triggered native
 * dialogs. Requests own immutable UTF-8 metadata and bytes until release.
 * Hosts must return only user-selected bytes, never script-supplied paths.
 * kind: 1 Open, 2 Save; status: 0 completed, 1 cancelled, 2 failed.
 * Completion copies its inputs before returning and is delivered on the JS
 * worker. Maximum 64 MiB per operation, 64 files, 16 pending requests.
 * The existing host_request_available callback also signals this queue. */
typedef struct webscene_file_data_v1 {
    const char* name;
    const char* mime_type;
    const uint8_t* bytes;
    size_t byte_count;
} webscene_file_data_v1;
typedef struct webscene_file_request_v1 {
    uint32_t struct_size, version;
    uint64_t request_id;
    uint32_t kind, multiple;
    const char* accept;
    webscene_file_data_v1 file;
} webscene_file_request_v1;
enum {
    WEBSCENE_NATIVE_MEDIA_LOCAL_FILES = 1U << 0U,
    WEBSCENE_NATIVE_MEDIA_NETWORK = 1U << 1U
};
/* Opt-in for trusted native applications. Set before navigation. File/HTTP(S)
 * video URLs use native incremental I/O and audio playback, without a whole-file
 * Blob or encoded-size limit. Zero preserves host-admitted byte loading.
 * This path does not expose streamed audio through Web Audio/capture APIs. */
WEBSCENE_API uint8_t webscene_engine_set_native_media_policy_v1(webscene_engine* engine, uint32_t flags);

WEBSCENE_API uint8_t webscene_engine_enable_file_service_v1(webscene_engine* engine, uint8_t enabled);
WEBSCENE_API const webscene_file_request_v1* webscene_engine_take_file_request_v1(webscene_engine* engine);
WEBSCENE_API void webscene_file_request_release_v1(const webscene_file_request_v1* request);
WEBSCENE_API uint8_t webscene_engine_complete_file_request_v1(webscene_engine* engine,
    uint64_t request_id, uint32_t status, const webscene_file_data_v1* files,
    size_t file_count, const char* error_message);

/* Browser download transfer v1. A request is admitted only from a recent
 * native user activation and, for framed documents, through every sandboxed
 * owner carrying allow-downloads. The immutable lease is the stream handoff:
 * byte-backed downloads may be copied incrementally until release. URL and
 * canvas sources remain explicit so a host can deny them or resolve them with
 * its own destination policy. No path, bookmark, grant, or destination is
 * selected by WebScene. Navigation generations let a host reject a lease that
 * outlives its source document.
 *
 * Limits: 16 queued requests, 64 MiB per and across queued byte payloads,
 * 4 KiB UTF-8 names and origins, 256-byte MIME types, and 8 KiB source URLs.
 * UINT64_MAX denotes an unknown total size. Releasing a request also
 * represents host cancellation. */
enum {
    WEBSCENE_DOWNLOAD_SOURCE_BYTES_V1 = 1,
    WEBSCENE_DOWNLOAD_SOURCE_URL_V1 = 2,
    WEBSCENE_DOWNLOAD_SOURCE_CANVAS_V1 = 3
};
typedef struct webscene_download_request_v1 {
    uint32_t struct_size, version;
    uint64_t request_id;
    uint64_t document_generation;
    uint64_t frame_generation;
    uint64_t target_node_id;
    uint64_t frame_owner_node_id;
    uint32_t source_kind, reserved;
    const char* source_origin;
    const char* suggested_name;
    const char* mime_type;
    uint64_t total_size;
    const uint8_t* bytes;
    size_t byte_count;
    const char* source_url;
    uint64_t canvas_node_id;
} webscene_download_request_v1;
WEBSCENE_API const webscene_download_request_v1*
webscene_engine_take_download_request_v1(webscene_engine* engine);
WEBSCENE_API void webscene_download_request_release_v1(
    const webscene_download_request_v1* request);

/* Immutable desktop-drag handoff. WebScene publishes only browser-admitted
 * metadata; native objects, paths, destination policy, and grant resolution
 * remain host-owned. A lease remains valid until release, including after
 * engine teardown. Completion is copied and delivered on the script worker.
 * Limits: 8 pending requests, 32 items, 1 MiB aggregate UTF-8 metadata,
 * 8 KiB per URI, 256-byte MIME types, and 1 KiB opaque grant references. */
enum {
    WEBSCENE_OUTBOUND_DRAG_OPERATION_COPY_V1 = 1U << 0U,
    WEBSCENE_OUTBOUND_DRAG_OPERATION_LINK_V1 = 1U << 1U,
    WEBSCENE_OUTBOUND_DRAG_OPERATION_MOVE_V1 = 1U << 2U,
    WEBSCENE_OUTBOUND_DRAG_ITEM_TEXT_V1 = 1,
    WEBSCENE_OUTBOUND_DRAG_ITEM_URI_V1 = 2,
    WEBSCENE_OUTBOUND_DRAG_ITEM_IMAGE_V1 = 3,
    WEBSCENE_OUTBOUND_DRAG_ITEM_FILE_GRANT_V1 = 4,
    WEBSCENE_OUTBOUND_DRAG_COMPLETED_V1 = 1,
    WEBSCENE_OUTBOUND_DRAG_CANCELLED_V1 = 2,
    WEBSCENE_OUTBOUND_DRAG_FAILED_V1 = 3
};
typedef struct webscene_outbound_drag_item_v1 {
    uint32_t struct_size, version;
    uint32_t kind, reserved;
    const char* mime_type;
    size_t mime_type_length;
    const char* value;
    size_t value_length;
    const char* name;
    size_t name_length;
    const char* grant_reference;
    size_t grant_reference_length;
    uint64_t byte_size;
} webscene_outbound_drag_item_v1;
typedef struct webscene_outbound_drag_request_v1 {
    uint32_t struct_size, version;
    uint64_t request_id;
    uint64_t document_generation;
    uint64_t node_generation;
    uint64_t frame_generation;
    uint64_t source_node_id;
    uint64_t frame_owner_node_id;
    uint32_t allowed_operations;
    uint32_t item_count;
    const webscene_outbound_drag_item_v1* items;
    float pointer_x, pointer_y;
    uint64_t drag_image_node_id;
    float drag_image_hotspot_x, drag_image_hotspot_y;
    float drag_image_width, drag_image_height;
    const char* source_origin;
    size_t source_origin_length;
} webscene_outbound_drag_request_v1;
typedef struct webscene_outbound_drag_completion_v1 {
    uint32_t struct_size, version;
    uint64_t request_id;
    uint64_t document_generation;
    uint32_t status;
    uint32_t selected_operation;
    double x, y;
    uint32_t modifiers;
    uint32_t reserved;
} webscene_outbound_drag_completion_v1;
WEBSCENE_API const webscene_outbound_drag_request_v1*
webscene_engine_take_outbound_drag_request_v1(webscene_engine* engine);
WEBSCENE_API void webscene_outbound_drag_request_release_v1(
    const webscene_outbound_drag_request_v1* request);
WEBSCENE_API uint8_t webscene_engine_complete_outbound_drag_v1(
    webscene_engine* engine,
    const webscene_outbound_drag_completion_v1* completion);

/* Native file panel transport v2. This queue is independent of the v1
 * byte-copy service. Requests contain presentation metadata and an optional
 * opaque initial-location token. Successful results contain only bounded
 * display metadata and opaque grant identifiers; filesystem paths, bookmark
 * bytes and platform objects remain owned by the native host.
 *
 * Request and nested views remain immutable until release. A completion is
 * copied before the call returns. Limits: 16 pending requests, 64 selected
 * entries, 32 filters, 64 total MIME/extension strings, 64 KiB request
 * metadata, 4 KiB display names/messages, and 1 KiB opaque tokens/grant IDs. */
enum {
    WEBSCENE_FILE_PANEL_OPEN_FILE_V2 = 1,
    WEBSCENE_FILE_PANEL_OPEN_DIRECTORY_V2 = 2,
    WEBSCENE_FILE_PANEL_SAVE_FILE_V2 = 3
};
enum {
    WEBSCENE_FILE_PANEL_ALLOW_MULTIPLE_V2 = 1U << 0U,
    WEBSCENE_FILE_PANEL_SHOW_HIDDEN_V2 = 1U << 1U,
    WEBSCENE_FILE_PANEL_CAN_CREATE_DIRECTORIES_V2 = 1U << 2U,
    WEBSCENE_FILE_PANEL_CONFIRM_OVERWRITE_V2 = 1U << 3U,
    WEBSCENE_FILE_PANEL_EXCLUDE_ACCEPT_ALL_V2 = 1U << 4U,
    WEBSCENE_FILE_PANEL_REQUEST_WRITE_V2 = 1U << 5U
};
enum {
    WEBSCENE_FILE_PANEL_SUCCESS_V2 = 0,
    WEBSCENE_FILE_PANEL_CANCELLED_V2 = 1,
    WEBSCENE_FILE_PANEL_DENIED_V2 = 2,
    WEBSCENE_FILE_PANEL_ERROR_V2 = 3
};
enum {
    WEBSCENE_FILE_PANEL_ENTRY_FILE_V2 = 1,
    WEBSCENE_FILE_PANEL_ENTRY_DIRECTORY_V2 = 2
};
enum {
    WEBSCENE_FILE_PANEL_GRANT_READ_V2 = 1U << 0U,
    WEBSCENE_FILE_PANEL_GRANT_WRITE_V2 = 1U << 1U,
    WEBSCENE_FILE_PANEL_GRANT_ENUMERATE_V2 = 1U << 2U,
    WEBSCENE_FILE_PANEL_GRANT_CREATE_V2 = 1U << 3U,
    WEBSCENE_FILE_PANEL_GRANT_DELETE_V2 = 1U << 4U
};
typedef struct webscene_file_panel_string_v2 {
    const char* data;
    size_t byte_count;
} webscene_file_panel_string_v2;
typedef struct webscene_file_panel_token_v2 {
    const uint8_t* data;
    size_t byte_count;
} webscene_file_panel_token_v2;
typedef struct webscene_file_panel_filter_v2 {
    uint32_t struct_size, version;
    webscene_file_panel_string_v2 description;
    const webscene_file_panel_string_v2* mime_types;
    size_t mime_type_count;
    const webscene_file_panel_string_v2* extensions;
    size_t extension_count;
} webscene_file_panel_filter_v2;
typedef struct webscene_file_panel_request_v2 {
    uint32_t struct_size, version;
    uint64_t request_id;
    uint32_t kind, flags;
    uint32_t maximum_selection_count, reserved;
    webscene_file_panel_string_v2 title;
    webscene_file_panel_string_v2 prompt;
    webscene_file_panel_string_v2 suggested_name;
    webscene_file_panel_token_v2 initial_location_token;
    const webscene_file_panel_filter_v2* filters;
    size_t filter_count;
} webscene_file_panel_request_v2;
typedef struct webscene_file_panel_entry_v2 {
    uint32_t struct_size, version;
    uint32_t kind, capabilities;
    webscene_file_panel_string_v2 display_name;
    webscene_file_panel_token_v2 grant_id;
} webscene_file_panel_entry_v2;
typedef struct webscene_file_panel_completion_v2 {
    uint32_t struct_size, version;
    uint64_t request_id;
    uint32_t status, reserved;
    const webscene_file_panel_entry_v2* entries;
    size_t entry_count;
    webscene_file_panel_string_v2 error_code;
    webscene_file_panel_string_v2 error_message;
} webscene_file_panel_completion_v2;
WEBSCENE_API const webscene_file_panel_request_v2*
webscene_engine_take_file_panel_request_v2(webscene_engine* engine);
WEBSCENE_API void webscene_file_panel_request_release_v2(
    const webscene_file_panel_request_v2* request);
WEBSCENE_API uint8_t webscene_engine_complete_file_panel_request_v2(
    webscene_engine* engine,
    const webscene_file_panel_completion_v2* completion);

/* File-system handle identity remains owned by the native grant authority.
 * Request storage is immutable until release and contains only the two opaque
 * grant tokens. A denied or unavailable comparison completes with admitted=0
 * and same_entry=0 so callers cannot distinguish authority failure details. */
typedef struct webscene_file_grant_same_entry_request_v2 {
    uint32_t struct_size, version;
    uint64_t request_id;
    webscene_file_panel_token_v2 first_grant_id;
    webscene_file_panel_token_v2 second_grant_id;
} webscene_file_grant_same_entry_request_v2;
typedef struct webscene_file_grant_same_entry_completion_v2 {
    uint32_t struct_size, version;
    uint64_t request_id;
    uint8_t admitted, same_entry;
    uint8_t reserved[6];
} webscene_file_grant_same_entry_completion_v2;
WEBSCENE_API const webscene_file_grant_same_entry_request_v2*
webscene_engine_take_file_grant_same_entry_request_v2(webscene_engine* engine);
WEBSCENE_API void webscene_file_grant_same_entry_request_release_v2(
    const webscene_file_grant_same_entry_request_v2* request);
WEBSCENE_API uint8_t webscene_engine_complete_file_grant_same_entry_request_v2(
    webscene_engine* engine,
    const webscene_file_grant_same_entry_completion_v2* completion);

/* Resolve one live opaque grant relative to a live directory grant without
 * exposing native paths. SUCCESS returns zero or more ordered UTF-8 display
 * components; the same directory is SUCCESS with zero components.
 * NOT_DESCENDANT is a successful browser result and also carries no
 * components. Request storage is immutable until release and completion
 * strings are copied before return. Limits match the native ancestry
 * authority: 16 pending requests, 64 components, 1 MiB total UTF-8 names,
 * and 1 KiB per opaque grant token. */
enum {
    WEBSCENE_FILE_GRANT_ANCESTRY_SUCCESS_V2 = 0,
    WEBSCENE_FILE_GRANT_ANCESTRY_NOT_DESCENDANT_V2 = 1,
    WEBSCENE_FILE_GRANT_ANCESTRY_CANCELLED_V2 = 2,
    WEBSCENE_FILE_GRANT_ANCESTRY_DENIED_V2 = 3,
    WEBSCENE_FILE_GRANT_ANCESTRY_NOT_FOUND_V2 = 4,
    WEBSCENE_FILE_GRANT_ANCESTRY_CHANGED_V2 = 5,
    WEBSCENE_FILE_GRANT_ANCESTRY_IO_ERROR_V2 = 6,
    WEBSCENE_FILE_GRANT_ANCESTRY_LIMIT_V2 = 7,
    WEBSCENE_FILE_GRANT_ANCESTRY_MAXIMUM_COMPONENTS_V2 = 64,
    WEBSCENE_FILE_GRANT_ANCESTRY_MAXIMUM_NAME_BYTES_V2 = 1024 * 1024,
    WEBSCENE_FILE_GRANT_ANCESTRY_MAXIMUM_PENDING_OPERATIONS_V2 = 16
};
typedef struct webscene_file_grant_ancestry_request_v2 {
    uint32_t struct_size, version;
    uint64_t request_id;
    webscene_file_panel_token_v2 base_directory_grant_id;
    webscene_file_panel_token_v2 possible_descendant_grant_id;
    uint64_t reserved;
} webscene_file_grant_ancestry_request_v2;
typedef struct webscene_file_grant_ancestry_component_v2 {
    uint32_t struct_size, version;
    webscene_file_panel_string_v2 display_name;
} webscene_file_grant_ancestry_component_v2;
typedef struct webscene_file_grant_ancestry_completion_v2 {
    uint32_t struct_size, version;
    uint64_t request_id;
    uint32_t status, reserved;
    const webscene_file_grant_ancestry_component_v2* components;
    size_t component_count;
    webscene_file_panel_string_v2 error_code;
} webscene_file_grant_ancestry_completion_v2;
WEBSCENE_API const webscene_file_grant_ancestry_request_v2*
webscene_engine_take_file_grant_ancestry_request_v2(webscene_engine* engine);
WEBSCENE_API void webscene_file_grant_ancestry_request_release_v2(
    const webscene_file_grant_ancestry_request_v2* request);
WEBSCENE_API uint8_t webscene_engine_complete_file_grant_ancestry_request_v2(
    webscene_engine* engine,
    const webscene_file_grant_ancestry_completion_v2* completion);

/* Persist and restore opaque grants without exposing live tokens or native
 * location material to storage. Every request is bound to an exact host-owned
 * storage partition plus serialized origin. EXPORT returns a fixed 32-byte
 * locator. RESTORE returns a fresh live grant. REVOKE deletes the durable
 * record. Request storage is immutable until release and completion storage is
 * copied before return. */
enum {
    WEBSCENE_FILE_GRANT_DURABLE_EXPORT_V2 = 1,
    WEBSCENE_FILE_GRANT_DURABLE_RESTORE_V2 = 2,
    WEBSCENE_FILE_GRANT_DURABLE_REVOKE_V2 = 3
};
enum {
    WEBSCENE_FILE_GRANT_DURABLE_SUCCESS_V2 = 0,
    WEBSCENE_FILE_GRANT_DURABLE_CANCELLED_V2 = 1,
    WEBSCENE_FILE_GRANT_DURABLE_DENIED_V2 = 2,
    WEBSCENE_FILE_GRANT_DURABLE_NOT_FOUND_V2 = 3,
    WEBSCENE_FILE_GRANT_DURABLE_CHANGED_V2 = 4,
    WEBSCENE_FILE_GRANT_DURABLE_STALE_V2 = 5,
    WEBSCENE_FILE_GRANT_DURABLE_TAMPERED_V2 = 6,
    WEBSCENE_FILE_GRANT_DURABLE_IO_ERROR_V2 = 7,
    WEBSCENE_FILE_GRANT_DURABLE_LIMIT_V2 = 8,
    WEBSCENE_FILE_GRANT_DURABLE_LOCATOR_BYTES_V2 = 32,
    WEBSCENE_FILE_GRANT_DURABLE_MAXIMUM_PARTITION_BYTES_V2 = 1024,
    WEBSCENE_FILE_GRANT_DURABLE_MAXIMUM_ORIGIN_BYTES_V2 = 4096,
    WEBSCENE_FILE_GRANT_DURABLE_MAXIMUM_RECORDS_V2 = 1024,
    WEBSCENE_FILE_GRANT_DURABLE_MAXIMUM_PENDING_OPERATIONS_V2 = 16
};
typedef struct webscene_file_grant_durable_request_v2 {
    uint32_t struct_size, version;
    uint64_t request_id;
    uint32_t action, reserved;
    webscene_file_panel_token_v2 grant_id;
    webscene_file_panel_token_v2 locator;
    webscene_file_panel_string_v2 storage_partition;
    webscene_file_panel_string_v2 serialized_origin;
} webscene_file_grant_durable_request_v2;
typedef struct webscene_file_grant_durable_completion_v2 {
    uint32_t struct_size, version;
    uint64_t request_id;
    uint32_t status, action;
    webscene_file_panel_token_v2 locator;
    webscene_file_panel_token_v2 grant_id;
    uint32_t kind, capabilities;
    webscene_file_panel_string_v2 display_name;
    webscene_file_panel_string_v2 error_code;
} webscene_file_grant_durable_completion_v2;
WEBSCENE_API const webscene_file_grant_durable_request_v2*
webscene_engine_take_file_grant_durable_request_v2(webscene_engine* engine);
WEBSCENE_API void webscene_file_grant_durable_request_release_v2(
    const webscene_file_grant_durable_request_v2* request);
WEBSCENE_API uint8_t webscene_engine_complete_file_grant_durable_request_v2(
    webscene_engine* engine,
    const webscene_file_grant_durable_completion_v2* completion);

/* Bounded reads over an opaque native file grant. Each request asks for one
 * offset range and owns immutable token storage until release. Successful
 * completions include current metadata so the browser runtime can reject a
 * File whose backing entry changed between chunks. Completion bytes are copied
 * before this call returns. Limits: 64 pending reads, 1 MiB per completion,
 * 1 KiB opaque grant IDs. */
enum {
    WEBSCENE_FILE_GRANT_READ_SUCCESS_V2 = 0,
    WEBSCENE_FILE_GRANT_READ_CANCELLED_V2 = 1,
    WEBSCENE_FILE_GRANT_READ_DENIED_V2 = 2,
    WEBSCENE_FILE_GRANT_READ_NOT_FOUND_V2 = 3,
    WEBSCENE_FILE_GRANT_READ_IO_ERROR_V2 = 4,
    WEBSCENE_FILE_GRANT_READ_MAXIMUM_BYTES_V2 = 1024 * 1024
};
typedef struct webscene_file_grant_metadata_v2 {
    uint32_t struct_size, version;
    uint64_t byte_count;
    int64_t modification_time_ns;
    uint32_t kind, reserved;
} webscene_file_grant_metadata_v2;
typedef struct webscene_file_grant_read_request_v2 {
    uint32_t struct_size, version;
    uint64_t request_id;
    webscene_file_panel_token_v2 grant_id;
    uint64_t offset;
    uint32_t maximum_bytes, reserved;
} webscene_file_grant_read_request_v2;
typedef struct webscene_file_grant_read_completion_v2 {
    uint32_t struct_size, version;
    uint64_t request_id;
    uint32_t status, reserved;
    webscene_file_grant_metadata_v2 metadata;
    uint64_t offset;
    const uint8_t* data;
    size_t byte_count;
    uint8_t eof;
    uint8_t reserved_bytes[7];
} webscene_file_grant_read_completion_v2;
WEBSCENE_API const webscene_file_grant_read_request_v2*
webscene_engine_take_file_grant_read_request_v2(webscene_engine* engine);
WEBSCENE_API void webscene_file_grant_read_request_release_v2(
    const webscene_file_grant_read_request_v2* request);
WEBSCENE_API uint8_t webscene_engine_complete_file_grant_read_request_v2(
    webscene_engine* engine,
    const webscene_file_grant_read_completion_v2* completion);

/* Atomic writes over an opaque native file grant. BEGIN returns a private
 * transaction token. CHUNK copies one nonempty offset range. COMMIT publishes
 * exactly final_byte_count bytes atomically; ABORT discards the transaction.
 * Request storage remains immutable until release and completion storage is
 * copied before return. Limits: 64 pending operations, 16 concurrent
 * transactions in the native authority, 1 MiB per chunk, 1 GiB per
 * transaction, and 1 KiB opaque grant/transaction tokens. */
enum {
    WEBSCENE_FILE_GRANT_WRITE_BEGIN_V2 = 1,
    WEBSCENE_FILE_GRANT_WRITE_CHUNK_V2 = 2,
    WEBSCENE_FILE_GRANT_WRITE_COMMIT_V2 = 3,
    WEBSCENE_FILE_GRANT_WRITE_ABORT_V2 = 4
};
enum {
    WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2 = 0,
    WEBSCENE_FILE_GRANT_WRITE_CANCELLED_V2 = 1,
    WEBSCENE_FILE_GRANT_WRITE_DENIED_V2 = 2,
    WEBSCENE_FILE_GRANT_WRITE_NOT_FOUND_V2 = 3,
    WEBSCENE_FILE_GRANT_WRITE_CHANGED_V2 = 4,
    WEBSCENE_FILE_GRANT_WRITE_IO_ERROR_V2 = 5,
    WEBSCENE_FILE_GRANT_WRITE_LIMIT_V2 = 6,
    WEBSCENE_FILE_GRANT_WRITE_MAXIMUM_CHUNK_BYTES_V2 = 1024 * 1024,
    WEBSCENE_FILE_GRANT_WRITE_MAXIMUM_TRANSACTION_BYTES_V2 = 1024 * 1024 * 1024,
    WEBSCENE_FILE_GRANT_WRITE_MAXIMUM_PENDING_OPERATIONS_V2 = 64
};
typedef struct webscene_file_grant_write_request_v2 {
    uint32_t struct_size, version;
    uint64_t request_id;
    uint32_t action, reserved;
    webscene_file_panel_token_v2 grant_id;
    webscene_file_panel_token_v2 transaction_id;
    uint64_t offset;
    const uint8_t* data;
    size_t byte_count;
    uint64_t final_byte_count;
} webscene_file_grant_write_request_v2;
typedef struct webscene_file_grant_write_completion_v2 {
    uint32_t struct_size, version;
    uint64_t request_id;
    uint32_t status, action;
    webscene_file_panel_token_v2 transaction_id;
    webscene_file_grant_metadata_v2 metadata;
    uint64_t offset;
    size_t byte_count;
} webscene_file_grant_write_completion_v2;
WEBSCENE_API const webscene_file_grant_write_request_v2*
webscene_engine_take_file_grant_write_request_v2(webscene_engine* engine);
WEBSCENE_API void webscene_file_grant_write_request_release_v2(
    const webscene_file_grant_write_request_v2* request);
WEBSCENE_API uint8_t webscene_engine_complete_file_grant_write_request_v2(
    webscene_engine* engine,
    const webscene_file_grant_write_completion_v2* completion);

/* Bounded deterministic directory pages over an opaque directory grant.
 * ENUMERATE starts with an empty cursor and continues with the returned opaque
 * cursor. RELEASE_CURSOR explicitly discards an unfinished snapshot. Requests
 * and completions are copied at the ABI boundary. Names and child grants are
 * relative display metadata and opaque authority only; paths never cross this
 * interface. Limits: 64 pending operations, 32 entries/page, 10,000 entries
 * and 1 MiB of UTF-8 names per native snapshot, 1 KiB grant/cursor tokens. */
enum {
    WEBSCENE_FILE_GRANT_DIRECTORY_ENUMERATE_V2 = 1,
    WEBSCENE_FILE_GRANT_DIRECTORY_RELEASE_CURSOR_V2 = 2
};
enum {
    WEBSCENE_FILE_GRANT_DIRECTORY_SUCCESS_V2 = 0,
    WEBSCENE_FILE_GRANT_DIRECTORY_CANCELLED_V2 = 1,
    WEBSCENE_FILE_GRANT_DIRECTORY_DENIED_V2 = 2,
    WEBSCENE_FILE_GRANT_DIRECTORY_NOT_FOUND_V2 = 3,
    WEBSCENE_FILE_GRANT_DIRECTORY_CHANGED_V2 = 4,
    WEBSCENE_FILE_GRANT_DIRECTORY_IO_ERROR_V2 = 5,
    WEBSCENE_FILE_GRANT_DIRECTORY_LIMIT_V2 = 6,
    WEBSCENE_FILE_GRANT_DIRECTORY_MAXIMUM_PAGE_ENTRIES_V2 = 32,
    WEBSCENE_FILE_GRANT_DIRECTORY_MAXIMUM_SNAPSHOT_ENTRIES_V2 = 10000,
    WEBSCENE_FILE_GRANT_DIRECTORY_MAXIMUM_NAME_BYTES_V2 = 1024 * 1024,
    WEBSCENE_FILE_GRANT_DIRECTORY_MAXIMUM_PENDING_OPERATIONS_V2 = 64
};
typedef struct webscene_file_grant_directory_request_v2 {
    uint32_t struct_size, version;
    uint64_t request_id;
    uint32_t action, reserved;
    webscene_file_panel_token_v2 directory_grant_id;
    webscene_file_panel_token_v2 cursor;
    uint32_t maximum_entries, reserved_entries;
} webscene_file_grant_directory_request_v2;
typedef struct webscene_file_grant_directory_entry_v2 {
    uint32_t struct_size, version;
    webscene_file_grant_metadata_v2 metadata;
    uint32_t capabilities, reserved;
    webscene_file_panel_string_v2 display_name;
    webscene_file_panel_token_v2 grant_id;
} webscene_file_grant_directory_entry_v2;
typedef struct webscene_file_grant_directory_completion_v2 {
    uint32_t struct_size, version;
    uint64_t request_id;
    uint32_t status, action;
    const webscene_file_grant_directory_entry_v2* entries;
    size_t entry_count;
    webscene_file_panel_token_v2 next_cursor;
    uint64_t skipped_symlinks;
} webscene_file_grant_directory_completion_v2;
WEBSCENE_API const webscene_file_grant_directory_request_v2*
webscene_engine_take_file_grant_directory_request_v2(webscene_engine* engine);
WEBSCENE_API void webscene_file_grant_directory_request_release_v2(
    const webscene_file_grant_directory_request_v2* request);
WEBSCENE_API uint8_t webscene_engine_complete_file_grant_directory_request_v2(
    webscene_engine* engine,
    const webscene_file_grant_directory_completion_v2* completion);

/* Atomically gets or creates one direct file child beneath an opaque directory
 * grant. A successful completion carries metadata and a newly derived opaque
 * file grant; paths, bookmarks, descriptors, and native objects stay native. */
enum {
    WEBSCENE_FILE_GRANT_CREATE_FILE_SUCCESS_V2 = 0,
    WEBSCENE_FILE_GRANT_CREATE_FILE_CANCELLED_V2 = 1,
    WEBSCENE_FILE_GRANT_CREATE_FILE_DENIED_V2 = 2,
    WEBSCENE_FILE_GRANT_CREATE_FILE_NOT_FOUND_V2 = 3,
    WEBSCENE_FILE_GRANT_CREATE_FILE_TYPE_MISMATCH_V2 = 4,
    WEBSCENE_FILE_GRANT_CREATE_FILE_CHANGED_V2 = 5,
    WEBSCENE_FILE_GRANT_CREATE_FILE_IO_ERROR_V2 = 6,
    WEBSCENE_FILE_GRANT_CREATE_FILE_LIMIT_V2 = 7,
    WEBSCENE_FILE_GRANT_CREATE_FILE_MAXIMUM_NAME_BYTES_V2 = 1024 * 1024,
    WEBSCENE_FILE_GRANT_CREATE_FILE_MAXIMUM_PENDING_OPERATIONS_V2 = 64
};
typedef struct webscene_file_grant_create_file_request_v2 {
    uint32_t struct_size, version;
    uint64_t request_id;
    webscene_file_panel_token_v2 directory_grant_id;
    webscene_file_panel_string_v2 display_name;
    uint64_t reserved;
} webscene_file_grant_create_file_request_v2;
typedef struct webscene_file_grant_create_file_completion_v2 {
    uint32_t struct_size, version;
    uint64_t request_id;
    uint32_t status, capabilities;
    webscene_file_grant_metadata_v2 metadata;
    webscene_file_panel_string_v2 display_name;
    webscene_file_panel_token_v2 grant_id;
} webscene_file_grant_create_file_completion_v2;
WEBSCENE_API const webscene_file_grant_create_file_request_v2*
webscene_engine_take_file_grant_create_file_request_v2(webscene_engine* engine);
WEBSCENE_API void webscene_file_grant_create_file_request_release_v2(
    const webscene_file_grant_create_file_request_v2* request);
WEBSCENE_API uint8_t webscene_engine_complete_file_grant_create_file_request_v2(
    webscene_engine* engine,
    const webscene_file_grant_create_file_completion_v2* completion);

/* Atomically gets or creates one direct directory child beneath an opaque directory
 * grant. A successful completion carries metadata and a newly derived opaque
 * directory grant; paths, bookmarks, descriptors, and native objects stay native. */
enum {
    WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_SUCCESS_V2 = 0,
    WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_CANCELLED_V2 = 1,
    WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_DENIED_V2 = 2,
    WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_NOT_FOUND_V2 = 3,
    WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_TYPE_MISMATCH_V2 = 4,
    WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_CHANGED_V2 = 5,
    WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_IO_ERROR_V2 = 6,
    WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_LIMIT_V2 = 7,
    WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_MAXIMUM_NAME_BYTES_V2 = 1024 * 1024,
    WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_MAXIMUM_PENDING_OPERATIONS_V2 = 64
};
typedef struct webscene_file_grant_create_directory_request_v2 {
    uint32_t struct_size, version;
    uint64_t request_id;
    webscene_file_panel_token_v2 directory_grant_id;
    webscene_file_panel_string_v2 display_name;
    uint64_t reserved;
} webscene_file_grant_create_directory_request_v2;
typedef struct webscene_file_grant_create_directory_completion_v2 {
    uint32_t struct_size, version;
    uint64_t request_id;
    uint32_t status, capabilities;
    webscene_file_grant_metadata_v2 metadata;
    webscene_file_panel_string_v2 display_name;
    webscene_file_panel_token_v2 grant_id;
} webscene_file_grant_create_directory_completion_v2;
WEBSCENE_API const webscene_file_grant_create_directory_request_v2*
webscene_engine_take_file_grant_create_directory_request_v2(webscene_engine* engine);
WEBSCENE_API void webscene_file_grant_create_directory_request_release_v2(
    const webscene_file_grant_create_directory_request_v2* request);
WEBSCENE_API uint8_t webscene_engine_complete_file_grant_create_directory_request_v2(
    webscene_engine* engine,
    const webscene_file_grant_create_directory_completion_v2* completion);

/* Removes one direct child beneath an opaque directory grant. Recursive
 * removal is bounded and native-owned; no path, bookmark, descriptor, deleted
 * metadata, or platform object crosses this ABI. */
enum {
    WEBSCENE_FILE_GRANT_REMOVE_SUCCESS_V2 = 0,
    WEBSCENE_FILE_GRANT_REMOVE_CANCELLED_V2 = 1,
    WEBSCENE_FILE_GRANT_REMOVE_DENIED_V2 = 2,
    WEBSCENE_FILE_GRANT_REMOVE_NOT_FOUND_V2 = 3,
    WEBSCENE_FILE_GRANT_REMOVE_INVALID_MODIFICATION_V2 = 4,
    WEBSCENE_FILE_GRANT_REMOVE_CHANGED_V2 = 5,
    WEBSCENE_FILE_GRANT_REMOVE_IO_ERROR_V2 = 6,
    WEBSCENE_FILE_GRANT_REMOVE_LIMIT_V2 = 7,
    WEBSCENE_FILE_GRANT_REMOVE_MAXIMUM_NAME_BYTES_V2 = 1024 * 1024,
    WEBSCENE_FILE_GRANT_REMOVE_MAXIMUM_PENDING_OPERATIONS_V2 = 64
};
typedef struct webscene_file_grant_remove_request_v2 {
    uint32_t struct_size, version;
    uint64_t request_id;
    webscene_file_panel_token_v2 directory_grant_id;
    webscene_file_panel_string_v2 display_name;
    uint8_t recursive;
    uint8_t reserved_bytes[7];
} webscene_file_grant_remove_request_v2;
typedef struct webscene_file_grant_remove_completion_v2 {
    uint32_t struct_size, version;
    uint64_t request_id;
    uint32_t status, reserved;
} webscene_file_grant_remove_completion_v2;
WEBSCENE_API const webscene_file_grant_remove_request_v2*
webscene_engine_take_file_grant_remove_request_v2(webscene_engine* engine);
WEBSCENE_API void webscene_file_grant_remove_request_release_v2(
    const webscene_file_grant_remove_request_v2* request);
WEBSCENE_API uint8_t webscene_engine_complete_file_grant_remove_request_v2(
    webscene_engine* engine,
    const webscene_file_grant_remove_completion_v2* completion);

/* One-way release of a live opaque file grant after the final browser-side
 * wrapper or in-flight structured-clone packet relinquishes ownership. The
 * engine queues each token at most once per broker lifetime. Request memory is
 * immutable until release; opaque tokens remain capped at 1 KiB. */
enum {
    WEBSCENE_FILE_GRANT_RELEASE_MAXIMUM_QUEUED_V2 = 16384
};
typedef struct webscene_file_grant_release_request_v2 {
    uint32_t struct_size, version;
    webscene_file_panel_token_v2 grant_id;
    uint32_t reserved, reserved2;
} webscene_file_grant_release_request_v2;
WEBSCENE_API const webscene_file_grant_release_request_v2*
webscene_engine_take_file_grant_release_request_v2(webscene_engine* engine);
WEBSCENE_API void webscene_file_grant_release_request_release_v2(
    const webscene_file_grant_release_request_v2* request);

/* Typed native desktop request ABI. Request memory is immutable and remains
 * valid until release. Byte payloads are capped at 16 MiB, with one-way
 * selected-text publication capped at 64 KiB. Strings are UTF-8, and at most
 * 16 completion-bearing operations may be pending per document. */
enum {
    WEBSCENE_HOST_REQUEST_OPEN_EXTERNAL_URL_V1 = 1,
    WEBSCENE_HOST_REQUEST_CLIPBOARD_READ_V1 = 2,
    WEBSCENE_HOST_REQUEST_CLIPBOARD_WRITE_V1 = 3,
    WEBSCENE_HOST_REQUEST_WINDOW_FOCUS_V1 = 4,
    WEBSCENE_HOST_REQUEST_WINDOW_CLOSE_V1 = 5,
    WEBSCENE_HOST_REQUEST_WINDOW_RELOAD_V1 = 6,
    WEBSCENE_HOST_REQUEST_FULLSCREEN_ENTER_V1 = 7,
    WEBSCENE_HOST_REQUEST_FULLSCREEN_EXIT_V1 = 8,
    WEBSCENE_HOST_REQUEST_WINDOW_NAVIGATE_V1 = 9,
    /* Enumerates privacy-scoped media devices for the requesting security
     * origin. The host completes with
     * application/vnd.webscene.media-devices+json and the bounded v1 schema
     * documented by the media runtime. This request never grants capture. */
    WEBSCENE_HOST_REQUEST_MEDIA_ENUMERATE_DEVICES_V1 = 10
};
enum {
    WEBSCENE_HOST_REQUEST_CLIPBOARD_REPLACE_V1 = 1U << 0U,
    /* Selects the platform's primary/selection clipboard. It is valid either
     * alone for a completion-bearing text/plain READ request or together with
     * REPLACE for the engine's zero-ID, one-way selected-text WRITE. Ordinary
     * Clipboard API and shortcut requests leave this clear and continue to
     * address the normal clipboard. */
    WEBSCENE_HOST_REQUEST_CLIPBOARD_PRIMARY_V1 = 1U << 1U,
    WEBSCENE_HOST_REQUEST_NAVIGATION_REPLACE_V1 = 1U << 0U,
    WEBSCENE_HOST_REQUEST_EXTERNAL_NEW_CONTEXT_V1 = 1U << 0U,
    WEBSCENE_HOST_REQUEST_EXTERNAL_BACKGROUND_V1 = 1U << 1U
};
typedef struct webscene_host_request_v1 {
    uint32_t struct_size, version;
    uint64_t request_id;
    uint32_t kind, flags;
    uint64_t target_node_id;
    const char* content_type;
    const uint8_t* bytes;
    size_t byte_count;
    const char* url;
} webscene_host_request_v1;
WEBSCENE_API const webscene_host_request_v1*
webscene_engine_take_typed_host_request_v1(webscene_engine* engine);
WEBSCENE_API void webscene_host_request_release_v1(
    const webscene_host_request_v1* request);

/* JSON compatibility queue retained for older host integrations and unrelated
 * application-defined messages. New desktop capabilities use the typed ABI. */
WEBSCENE_API size_t webscene_engine_take_host_request(
    webscene_engine* engine,
    char* destination,
    size_t destination_capacity);
/* Consumes the oldest JSON compatibility request without allocating its
 * payload. Hosts use this after rejecting an oversized item so one malformed
 * request cannot permanently block the FIFO. */
WEBSCENE_API uint8_t webscene_engine_discard_host_request_v1(
    webscene_engine* engine);
/* Completes a request carrying a numeric requestId from take_host_request.
 * status: 0 completed, 1 cancelled, 2 denied/failed. Inputs are copied before
 * return. Clipboard data is limited to 16 MiB, content_type to 256 bytes and
 * error_message to 4096 bytes. Completion is delivered on the engine worker;
 * stale request IDs are safely ignored there. */
WEBSCENE_API uint8_t webscene_engine_complete_host_request_v1(
    webscene_engine* engine,
    uint64_t request_id,
    uint32_t status,
    const char* content_type,
    const uint8_t* bytes,
    size_t byte_count,
    const char* error_message);
/*
 * Removes one V8 console entry. The UTF-8 payload is `<level>\n<message>`;
 * querying with a null/short destination reports the required byte count
 * without consuming the entry.
 */
WEBSCENE_API size_t webscene_engine_take_console_message(
    webscene_engine* engine,
    char* destination,
    size_t destination_capacity);
/*
 * Removes one failed asynchronous input dispatch. The UTF-8 payload is
 * `<sequence>\n<kind>\n<error>` so consumers can attribute the JavaScript
 * exception to the exact native input event. A null/short destination reports
 * the required size without consuming the failure.
 */
WEBSCENE_API size_t webscene_engine_take_input_dispatch_failure(
    webscene_engine* engine,
    char* destination,
    size_t destination_capacity);
WEBSCENE_API size_t webscene_engine_copy_last_error(
    const webscene_engine* engine,
    char* destination,
    size_t destination_capacity);
WEBSCENE_API size_t webscene_engine_copy_first_iframe_html(
    const webscene_engine* engine,
    char* destination,
    size_t destination_capacity);
WEBSCENE_API size_t webscene_engine_copy_scene_diagnostics(
    const webscene_engine* engine,
    char* destination,
    size_t destination_capacity);
/*
 * Copies a UTF-8 webscene-native-feature-use-v2 JSON snapshot. Feature and
 * composition observations are counted at native decision points;
 * `complete:false` means one or more inventory categories remain uninstrumented.
 */
WEBSCENE_API size_t webscene_engine_copy_feature_use(
    const webscene_engine* engine,
    char* destination,
    size_t destination_capacity);
/* Copies registered element listener target/type inventory as stable UTF-8 JSON. */
WEBSCENE_API size_t webscene_engine_copy_event_listener_inventory(
    const webscene_engine* engine,
    char* destination,
    size_t destination_capacity);
WEBSCENE_API size_t webscene_engine_copy_canvas_layouts(
    const webscene_engine* engine,
    webscene_canvas_layout* destination,
    size_t destination_capacity);
/*
 * Starts a new immutable-scene diff chain with a complete checkpoint. Call
 * this after the previous scene consumer has stopped, before attaching a new
 * renderer (for example after compositor/context recreation).
 */
WEBSCENE_API uint8_t webscene_engine_request_scene_checkpoint(webscene_engine* engine);
WEBSCENE_API uint8_t webscene_engine_release_canvas_export(
    webscene_engine* engine,
    uint32_t node_id);
WEBSCENE_API const webscene_scene_view* webscene_engine_acquire_latest_scene(webscene_engine* engine);
/*
 * Enables the bounded ordered consumer lane and acquires its oldest pending
 * diff. Unlike acquire_latest, this preserves every base-revision link while
 * allowing the producer to publish one additional immutable diff ahead.
 */
WEBSCENE_API const webscene_scene_view* webscene_engine_acquire_next_scene(webscene_engine* engine);
WEBSCENE_API uint8_t webscene_scene_acknowledge(const webscene_scene_view* scene);
WEBSCENE_API void webscene_scene_release(const webscene_scene_view* scene);
WEBSCENE_API uint8_t webscene_scene_get_header(
    const webscene_scene_view* scene,
    webscene_scene_header* header);
WEBSCENE_API const webscene_scene_command* webscene_scene_get_commands(
    const webscene_scene_view* scene,
    uint32_t* count);
WEBSCENE_API void webscene_engine_get_metrics(
    const webscene_engine* engine,
    webscene_engine_metrics* metrics);
WEBSCENE_API uint8_t webscene_engine_get_input_dispatch_metrics(
    const webscene_engine* engine,
    webscene_input_dispatch_metrics* metrics);
WEBSCENE_API uint8_t webscene_engine_get_animation_frame_metrics(
    const webscene_engine* engine,
    webscene_animation_frame_metrics* metrics);
WEBSCENE_API uint8_t webscene_engine_get_scene_flow_metrics(
    const webscene_engine* engine,
    webscene_scene_flow_metrics* metrics);
WEBSCENE_API uint8_t webscene_engine_get_resize_frame_metrics(
    const webscene_engine* engine,
    webscene_resize_frame_metrics* metrics);
WEBSCENE_API uint8_t webscene_engine_get_resource_cache_metrics(
    const webscene_engine* engine,
    webscene_resource_cache_metrics* metrics);
WEBSCENE_API uint8_t webscene_engine_get_runtime_work_metrics(
    const webscene_engine* engine,
    webscene_runtime_work_metrics* metrics);
WEBSCENE_API uint8_t webscene_engine_set_runtime_work_metrics_enabled(
    webscene_engine* engine,
    uint8_t enabled);
WEBSCENE_API uint8_t webscene_engine_get_process_cache_metrics(
    const webscene_engine* engine,
    webscene_process_cache_metrics* metrics);
WEBSCENE_API uint8_t webscene_engine_get_memory_metrics(
    const webscene_engine* engine,
    webscene_engine_memory_metrics* metrics);

#ifdef __cplusplus
}
#endif
