#include "webscene_native_engine.h"
#include "webscene_native_dom.h"
#include "webscene_embed_fallback.h"

#include <ixwebsocket/IXGetFreePort.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocketServer.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "generated/webscene_css_supported_properties.inc"

#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#endif
#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace {

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "webscene_native_engine_tests: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message)
{
    if (!condition) fail(message);
}

uint64_t service_worker_test_current_rss_bytes();

uint8_t measure_baseline_fixture_text(
    void*,
    const char* text,
    size_t text_length,
    const char*,
    size_t,
    float font_size,
    int32_t,
    float,
    float,
    webscene_text_metrics* metrics)
{
    if (metrics == nullptr || metrics->struct_size < sizeof(webscene_text_metrics)) return 0;
    metrics->advance_width = static_cast<float>(text_length) * font_size * 0.5F;
    if (font_size >= 20.0F) {
        metrics->ascent = 10.0F;
        metrics->descent = 10.0F;
    } else {
        metrics->ascent = 9.0F;
        metrics->descent = 1.0F;
    }
    metrics->leading = 0.0F;
    return text == nullptr ? 0 : 1;
}

// Keep one test executable and one translation unit while grouping coverage by
// feature; shared fixtures remain visible without additional test-only APIs.
#include "native_v8_runtime_document_tests.inc"
#include "native_v8_runtime_test_support.inc"
#include "native_v8_runtime_semantic_snapshot_tests.inc"
#include "native_v8_runtime_semantic_delta_tests.inc"
#include "native_v8_runtime_semantic_action_tests.inc"
#include "native_v8_runtime_semantic_live_region_tests.inc"
#include "native_v8_runtime_lifecycle_tests.inc"
#include "native_v8_runtime_indexeddb_tests.inc"
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
#include "native_v8_runtime_inspector_tests.inc"
#endif
#include "native_v8_runtime_interop_tests.inc"
#include "native_v8_runtime_input_tests.inc"
#include "native_file_service_tests.inc"
#include "native_file_system_access_tests.inc"
#include "native_table_cell_copy_tests.inc"
#include "native_v8_runtime_resource_tests.inc"
#include "native_v8_runtime_drag_drop_tests.inc"
#include "native_v8_runtime_outbound_drag_tests.inc"
#include "native_v8_runtime_service_worker_tests.inc"
#include "native_v8_runtime_stream_fetch_tests.inc"
#include "native_v8_runtime_css_mask_resource_tests.inc"
#include "native_v8_runtime_response_cookie_tests.inc"
#include "native_v8_runtime_diagnostics_tests.inc"
#include "native_resource_failure_diagnostics_tests.inc"
#include "native_v8_runtime_css_layout_tests.inc"
#include "native_v8_runtime_dimension_variables_tests.inc"
#include "native_go_to_overflow_tests.inc"
#include "native_v8_runtime_media_query_tests.inc"
#include "native_v8_runtime_animation_cssom_tests.inc"
#include "native_v8_runtime_layout_scene_tests.inc"
#include "native_v8_runtime_canvas_tests.inc"
#include "native_v8_runtime_frame_scheduling_tests.inc"
#include "native_css_invalidation_tests.inc"
#include "native_youtube_embed_tests.inc"
#include "native_v8_runtime_browser_dom_tests.inc"
#include "native_v8_runtime_url_static_tests.inc"
#include "native_v8_runtime_abort_signal_tests.inc"
#include "native_v8_runtime_broadcast_channel_tests.inc"
#include "native_v8_runtime_resize_observer_tests.inc"
#include "native_v8_runtime_rendering_metrics_tests.inc"
#include "native_v8_runtime_websocket_tests.inc"
int main()
{
#if defined(_WIN32)
    _putenv_s("WEBSCENE_PROBE_PROFILE_STARTUP", "1");
#else
    setenv("WEBSCENE_PROBE_PROFILE_STARTUP", "1", 1);
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    require(
        (webscene_engine_get_build_features()
            & WEBSCENE_ENGINE_BUILD_FEATURE_CERTIFICATION) != 0,
        "certification build did not advertise certification telemetry");
#else
    require(
        (webscene_engine_get_build_features()
            & WEBSCENE_ENGINE_BUILD_FEATURE_CERTIFICATION) == 0,
        "ordinary build unexpectedly advertised certification telemetry");
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
    require(
        (webscene_engine_get_build_features()
            & WEBSCENE_ENGINE_BUILD_FEATURE_V8_INSPECTOR) != 0,
        "Inspector flavor did not advertise V8 Inspector support");
#else
    require(
        (webscene_engine_get_build_features()
            & WEBSCENE_ENGINE_BUILD_FEATURE_V8_INSPECTOR) == 0,
        "ordinary V8 runtime unexpectedly advertised Inspector support");
#endif
    require(webscene_engine_prewarm() != 0, "V8 prewarm failed");
    if (const auto* filter = std::getenv("WEBSCENE_NATIVE_ENGINE_TEST_FILTER");
        filter != nullptr) {
        const auto selected = std::string_view(filter);
        if (selected == "indexeddb") {
            test_indexeddb_runtime_contract();
            return 0;
        }
        if (selected == "web-storage") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr,
                "Web Storage focused engine creation failed");
            test_session_storage_in_outer_and_frame_contexts(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "navigation-realm") {
            test_navigation_replaces_top_level_realm();
            return 0;
        }
        if (selected == "history-same-document") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr,
                "same-document History engine creation failed");
            test_same_document_history_state_and_url_mutation(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "iframe-navigation-lifecycle") {
            test_same_origin_iframe_navigation_document_replacement();
            test_restricted_cross_origin_nested_window_proxy();
            return 0;
        }
        if (selected == "query-iframe-worker-bootstrap") {
            test_query_iframe_worker_extension_host_bootstrap();
            return 0;
        }
        if (selected == "window-find-selection") {
          test_nested_window_find_selection();
          return 0;
        }
        if (selected == "nested-window-focus") {
          test_nested_window_focus_ownership();
          return 0;
        }
        if (selected == "nested-pointer-coordinates") {
          test_nested_pointer_coordinate_projection();
          return 0;
        }
        if (selected == "nested-context-menu") {
          test_nested_context_menu_handoff();
          return 0;
        }
        if (selected == "nested-anchor-activation") {
          test_nested_anchor_activation_handoff();
          return 0;
        }
        if (selected == "nested-drag-drop") {
          test_nested_drag_drop_routing_and_retirement();
          return 0;
        }
        if (selected == "outbound-drag") {
          auto* engine = webscene_engine_create(0);
          require(engine != nullptr, "outbound drag engine creation failed");
          test_outbound_drag_browser_contract_and_retirement(engine);
          webscene_engine_destroy(engine);
          return 0;
        }
        if (selected == "nested-keyboard-handoff") {
          test_nested_keyboard_handoff_and_focus_traversal();
          return 0;
        }
        if (selected == "idle-v8-platform") {
            test_idle_v8_foreground_completion();
            return 0;
        }
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        if (selected == "iframe-worker-inspector-detach") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr,
                "iframe Worker Inspector engine creation failed");
            test_v8_inspector_raw_cdp_session(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
#endif
        if (selected == "dom-token-list") {
            test_dom_token_list_collection_performance_gate();
            return 0;
        }
        if (selected == "stylesheet-mutation-performance") {
            test_batched_stylesheet_rule_mutation_performance();
            return 0;
        }
        if (selected == "nested-style-rule-cssom") {
            test_nested_style_rule_cssom_performance_and_lifecycle();
            return 0;
        }
        if (selected == "adopted-stylesheets") {
            test_adopted_stylesheet_multi_root_contract_and_lifecycle();
            return 0;
        }
        if (selected == "iframe-sandbox") {
            test_iframe_sandbox_dom_token_list_security_and_lifecycle_gate();
            return 0;
        }
        if (selected == "file-system-access") {
            test_native_file_system_access_picker_and_handle_contract();
            test_native_file_system_access_lifecycle_and_performance();
            return 0;
        }
        if (selected == "file-system-handle-permissions") {
            test_native_file_system_access_picker_and_handle_contract();
            return 0;
        }
        if (selected == "file-system-handle-lifetime") {
            test_native_file_system_access_lifecycle_and_performance();
            return 0;
        }
        if (selected == "file-system-directory-resolve") {
            test_native_file_system_access_picker_and_handle_contract();
            return 0;
        }
        if (selected == "resource-cache-prefetch") {
            test_resource_cache_reuse_across_engine_generations();
            test_parser_resource_cache_partitioning();
            test_process_wide_resource_load_single_flight();
            test_cross_engine_single_flight_keeps_set_cookie_responses_private();
            test_resource_cache_policy_matrix();
            return 0;
        }
        if (selected == "service-worker-lifecycle") {
            test_service_worker_lifecycle_performance_and_teardown_gate();
            return 0;
        }
        if (selected == "service-worker-clients") {
            test_service_worker_client_navigation_generation_gate();
            test_service_worker_client_queue_performance_and_memory_gate();
            return 0;
        }
        if (selected == "service-worker-fetch") {
            test_readable_stream_body_and_fetch_event_vertical();
            return 0;
        }
        if (selected == "service-worker-resource-plane") {
            test_cache_storage_and_controlled_fetch_broker();
            return 0;
        }
        if (selected == "service-worker-host-stream") {
            test_service_worker_host_message_streaming();
            return 0;
        }
        if (selected == "service-worker-fetch-abort") {
            test_controlled_fetch_abort_and_retirement();
            return 0;
        }
        if (selected == "service-worker-range-cache") {
            test_controlled_range_cache_headers_and_body_budget();
            return 0;
        }
        if (selected == "service-worker-connected-resources") {
            test_controlled_connected_resource_interception();
            return 0;
        }
        if (selected == "service-worker-markdown-resources") {
            test_vscode_markdown_css_image_resource_broker();
            return 0;
        }
        if (selected == "service-worker-parser-resources") {
            test_controlled_parser_and_nested_frame_resources();
            return 0;
        }
        if (selected == "rounded-icon-surfaces") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "rounded icon surface engine creation failed");
            test_rounded_icon_surfaces_publish_stable_commands_across_selection_mutation(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "worker-configuration-order") {
            test_worker_starts_after_engine_configuration();
            return 0;
        }
        if (selected == "discrete-wheel-scroll") {
            test_discrete_wheel_scroll_uses_bounded_frame_animation();
            return 0;
        }
        if (selected == "dom-punctuation-keyboard") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "keyboard event engine creation failed");
            test_dom_punctuation_keyboard_event_identity(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "text-control-select-all") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr,
                "text-control select-all engine creation failed");
            test_text_control_select_all_default_action(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "shortcut-activation") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr,
                "shortcut activation engine creation failed");
            test_native_shortcut_activation_focus_retarget_and_publication(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if(selected=="modal-backdrop") {
            auto* focused_engine=webscene_engine_create(0);
            require(focused_engine!=nullptr,"backdrop engine creation failed");
            test_modal_backdrop_scene(focused_engine);
            webscene_engine_destroy(focused_engine);return 0;
        }
        if (selected == "word-wrap") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr,
                "word-break/overflow-wrap engine creation failed");
            test_word_break_and_overflow_wrap_layout(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "word-wrap-performance") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr,
                "word-break performance engine creation failed");
            test_word_break_layout_performance_gate(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "object-fit-performance") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr,
                "object-fit performance engine creation failed");
            test_object_fit_scene_performance_gate(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "user-select-performance") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr,
                "user-select performance engine creation failed");
            test_user_select_performance_gate(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if(selected=="placeholder-pseudo") {
            auto* focused_engine=webscene_engine_create(0);
            require(focused_engine!=nullptr,"placeholder pseudo engine creation failed");
            test_placeholder_pseudo_color_and_opacity_reach_scene(focused_engine);
            webscene_engine_destroy(focused_engine);return 0;
        }
        if(selected=="details-content-pseudo") {
            auto* focused_engine=webscene_engine_create(0);
            require(focused_engine!=nullptr,"details-content pseudo engine creation failed");
            test_details_content_pseudo_static_open_close_layout(focused_engine);
            webscene_engine_destroy(focused_engine);return 0;
        }
        if (selected == "fragment-attach") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "fragment attach engine creation failed");
            test_fragment_append_applies_structural_selectors_after_atomic_attachment(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if(selected=="details-content-transition") {
            auto* focused_engine=webscene_engine_create(0);
            require(focused_engine!=nullptr,"details-content transition engine creation failed");
            test_details_content_keyword_size_transition(focused_engine);
            webscene_engine_destroy(focused_engine);return 0;
        }
        if (selected == "paint-only-cascade") {
            auto* focused_engine=webscene_engine_create(0);
            require(focused_engine != nullptr,"paint invalidation engine creation failed");
            test_paint_only_stylesheet_recascade_does_not_force_layout(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "hover-dependency-cache") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr,
                "hover dependency cache engine creation failed");
            test_hover_invalidation_updates_functional_and_sibling_subjects(
                focused_engine);
            test_hover_dependency_matching_reuses_compiled_triggers(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "recursive-selector-cache") {
            test_recursive_functional_selector_survives_cache_eviction();
            return 0;
        }
        if (selected == "namespace-attribute-selectors") {
            test_namespace_qualified_attribute_selectors_and_lifecycle();
            return 0;
        }
        if (selected == "functional-namespace-selectors") {
            test_functional_selector_namespace_context_and_lifecycle();
            return 0;
        }
        if (selected == "all-unset") {
            auto* focused_engine=webscene_engine_create(0);
            require(focused_engine != nullptr,"reset test engine creation failed");
            test_all_unset_resets_modeled_control_properties(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "relative-stylesheet-resource") {
            test_relative_stylesheet_background_uses_stylesheet_address();
            return 0;
        }
        if (selected == "shared-shadow-values") {
            auto* focused_engine=webscene_engine_create(0);
            require(focused_engine != nullptr,"shadow test engine creation failed");
            resize(focused_engine,800,600,1U);
            wait_for_consumed_inputs(focused_engine,1U,"shadow test viewport resize was not consumed");
            execute(focused_engine,"void 0","shared-shadow-bootstrap.js");
            test_outer_box_shadow_reaches_elevated_scene(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "select-popup-context") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "select popup engine creation failed");
            test_collapsed_single_select_native_activation(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "select-principal-scene") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "select principal scene engine creation failed");
            test_collapsed_select_principal_scene_contract(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "async-save-publication") { test_async_save_acknowledgement_publishes_without_pointer_input(); return 0; }
        if (selected == "youtube-embed") { test_youtube_embed_fallback(); return 0; }
        if (selected == "table-cell-copy") { test_table_cell_click_copies_text_to_host(); return 0; }
        if (selected == "primary-middle-paste") { test_primary_selection_middle_paste(nullptr); return 0; }
        if (selected == "resource-failure-diagnostics") { test_resource_failure_diagnostics(); return 0; }
        if (selected == "response-header-cookie") {
            test_response_header_cookie_contracts();
            test_durable_profile_restart_contract();
            test_request_headers_reach_resource_callback_v5();
            test_parallel_resource_prefetch();
            test_fetch_carries_document_origin_to_resource_host();
            test_tradingview_save_acknowledgement_uses_multipart_post();
            return 0;
        }
        if (selected == "media-query-reentrant") { test_media_query_callback_can_create_more_queries(); return 0; }
        if (selected == "media-query-targeted-recascade") {
            test_media_query_resize_recascades_only_affected_subtrees();
            test_media_query_matching_scales_linearly();
            test_media_query_candidate_scaling();
            test_media_query_non_inherited_root_cascade_work();
            test_media_query_custom_property_consumer_cascade_work();
            test_media_query_variable_reference_metadata_work();
            test_media_query_inherited_variable_consumer_replay_work();
            test_media_query_inheritance_candidate_metadata_work();
            test_media_query_inherited_value_propagation_work();
            return 0;
        }
        if (selected == "media-query-candidate-scaling") { test_media_query_candidate_scaling(); return 0; }
        if (selected == "attribute-invalidation-scope") {
            test_attribute_invalidation_scopes_subject_and_descendant_rules();
            return 0;
        }
        if (selected == "css-invalidation-scaling") {
            test_compiled_subject_index_scaling();
            test_cascade_layer_mutation_scaling();
            test_variadic_child_vector_scaling();
            test_character_data_stable_style_scaling();
            test_text_topology_css_work_scaling();
            test_compiled_css_invalidation_scaling();
            test_compiled_css_route_scaling();
            test_compiled_css_route_scaling(true, true);
            return 0;
        }
        if (selected == "nested-functional-selectors") {
            test_nested_functional_selector_scaling();
            return 0;
        }
        if (selected == "relative-sibling-has") {
            test_relative_sibling_has_scaling();
            return 0;
        }
        if (selected == "live-form-state") {
            test_live_form_state_selectors_and_scaling();
            return 0;
        }
        if (selected == "css-subject-index-scaling") {
            test_compiled_subject_index_scaling();
            return 0;
        }
        if (selected == "css-cascade-layer-scaling") {
            test_cascade_layer_mutation_scaling();
            return 0;
        }
        if (selected == "css-structural-scaling") {
            test_compiled_css_route_scaling(true);
            return 0;
        }
        if (selected == "css-structural-rule-scaling") {
            test_compiled_css_route_scaling(true, true);
            return 0;
        }
        if (selected == "css-character-data-scaling") {
            test_character_data_stable_style_scaling();
            return 0;
        }
        if (selected == "css-text-topology-scaling") {
            test_text_topology_css_work_scaling();
            return 0;
        }
        if (selected == "dom-variadic-detach-scaling") {
            test_variadic_child_vector_scaling();
            return 0;
        }
        if (selected == "custom-element-checkpoints") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "custom-element checkpoint engine creation failed");
            test_custom_element_mutation_reactions_are_pay_for_use(focused_engine);
            test_autonomous_custom_element_lifecycle(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "runtime-diagnostics") {
            test_runtime_diagnostics();
            test_runtime_diagnostics_frame_and_failure();
            return 0;
        }
        if (selected == "feature-use") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr,
                "feature-use test engine creation failed");
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            execute(focused_engine, R"JS(
                (() => {
                  const style = document.createElement('style');
                  style.textContent = '.feature-use-cache-probe { display: flex; }';
                  document.head.appendChild(style);
                  for (let index = 0; index < 2; ++index) {
                    const target = document.createElement('div');
                    target.className = 'feature-use-cache-probe';
                    document.body.appendChild(target);
                  }
                })()
            )JS", "feature-use-cache-probe.js");
            require(
                evaluate(focused_engine,
                    "document.querySelector('.feature-use-cache-probe').offsetWidth >= 0",
                    "feature-use-cache-barrier.js") == "true",
                "feature-use cache fixture did not settle");
            const auto report = feature_use(focused_engine);
            const auto observation = report.find(
                R"("feature":"property:display","classification":"supported","count":2,"source":"style-application")");
            require(
                observation != std::string::npos
                    && report.find(
                        R"("feature":"property:display","classification":"supported")",
                        observation + 1U) == std::string::npos,
                "repeated supported CSS declarations did not retain one structured observation: "
                    + report);
#else
            test_unsupported_features_are_reported_at_native_decision_points(
                focused_engine);
#endif
            webscene_engine_destroy(focused_engine);
            return 0;
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (selected == "css-style-template-install") {
            test_css_style_template_installation_guard();
            return 0;
        }
#endif
        if (selected == "host-pointer-exit") {
            test_host_pointer_exit_clears_tooltip_without_another_move();
            return 0;
        }
        if (selected == "resize-observer-retina-export"
            || selected == "resize-observer-device-scale") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused observer engine creation failed");
            if (selected == "resize-observer-retina-export") {
                test_resize_observer_device_pixel_canvas_export(focused_engine);
            } else {
                test_resize_observer_device_scale_only_delivery(focused_engine);
            }
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "elliptical-corner-radii") {
            test_elliptical_scene_metadata_is_cold_and_scalar_compatible();
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_elliptical_corner_radii_reach_cssom(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "event-listener-options") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_event_listener_options_reach_native_input_and_resize(focused_engine);
            test_synthetic_window_resize_dispatch_uses_outer_listener_registry(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "node-iterator") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "NodeIterator engine creation failed");
            test_node_iterator_dompurify_and_filter_contracts(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "html-anchor-element") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr,
                "HTMLAnchorElement engine creation failed");
            test_dom_element_constructor_identity(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "node-iterator-performance") {
            test_node_iterator_isolated_scaling_and_memory_gate();
            return 0;
        }
        if (selected == "performance-timeline") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr,
                "Performance Timeline engine creation failed");
            test_native_performance_timeline_identity(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "websocket-file-reader") {
            test_native_websocket_browser_api();
            test_native_websocket_protocol_handshake_timing();
            test_native_file_reader_task_source_fairness();
            return 0;
        }
        if (selected == "stylesheet-cssom") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr,
                "stylesheet CSSOM engine creation failed");
            test_native_mutable_stylesheet_cssom(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "monaco-view-lines") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr,
                "Monaco view-line engine creation failed");
            test_monaco_view_line_dom_mutations(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "node-has-child-nodes") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "Node.hasChildNodes engine creation failed");
            test_node_has_child_nodes_contract_and_performance(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "desktop-host-capabilities") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr,
                "desktop host capability engine creation failed");
            test_document_direction_and_visibility_are_native_properties();
            test_pointer_cursor_and_external_anchor_host_handoff(focused_engine);
            test_table_cell_click_copies_text_to_host();
            test_youtube_embed_fallback();
            test_typed_window_host_request_performance(focused_engine);
            test_clipboard_write_text_host_handoff(focused_engine);
            test_clipboard_read_host_completion(focused_engine);
            test_native_clipboard_shortcut_events(focused_engine);
            test_clipboard_maximum_payload_gate(focused_engine);
            test_clipboard_small_round_trip_performance(focused_engine);
            test_fullscreen_host_completion(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "host-driven-close-veto") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr,
                "host-driven close engine creation failed");
            test_host_driven_window_close_lifecycle(focused_engine);
            webscene_engine_destroy(focused_engine);
            focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr,
                "script close engine creation failed");
            test_script_window_close_lifecycle(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "input-diagnostics-pointer-focus") {
            test_document_direction_and_visibility_are_native_properties();
            test_native_legacy_clipboard_completion_stress(nullptr);
            test_native_pending_legacy_clipboard_shutdown(nullptr);
            return 0;
        }
        if (selected == "media-query-list") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_media_query_list_tracks_outer_and_frame_viewport_breakpoints(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "scroll-offset-fast-path") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            for (auto attempt = 0; attempt < 100; ++attempt) {
                const auto* scene = webscene_engine_acquire_latest_scene(focused_engine);
                if (scene != nullptr) {
                    webscene_scene_acknowledge(scene);
                    webscene_scene_release(scene);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            test_scroll_offset_changes_translate_retained_geometry_without_layout(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "go-to-overflow") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_go_to_tab_lines_and_calendar_scroll_ranges(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "compact-go-to-grid") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_compact_go_to_fixed_grid_tracks_preserve_trailing_space(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "auto-fill-minmax-grid") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_auto_fill_minmax_custom_property_tracks_keep_their_sizing(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "grid-template-areas") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused grid engine creation failed");
            test_named_grid_template_areas_layout_cssom_and_mutation(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "grid-auto-span-intrinsic") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused grid engine creation failed");
            test_grid_item_spanning_auto_rows_contributes_its_height(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "grid-auto-max-stretch") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused grid engine creation failed");
            test_grid_auto_maximum_tracks_stretch_remaining_space(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "nested-css-modal-grid") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused nested CSS engine creation failed");
            test_nested_css_absolute_modal_grid_geometry(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "scrollbar-style-drag") {
            auto* focused_engine = webscene_engine_create(64);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_authored_scrollbar_style_and_thumb_drag(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "scrollbar-state-scale") {
            auto* focused_engine = webscene_engine_create(64);
            require(focused_engine != nullptr, "focused scrollbar scale engine creation failed");
            test_scrollbar_thumb_state_scaling(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "toolbar-overflow-navigation") {
            auto* focused_engine = webscene_engine_create(64);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_table_minimum_width_reaches_toolbar_overflow_navigation(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "dynamic-percentage-flex-list") {
            auto* focused_engine = webscene_engine_create(64);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_dynamic_percentage_flex_list_keeps_intrinsic_inline_contribution(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "document-create-event") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_document_create_event_and_init_event(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "concurrent-input-producers") {
            test_concurrent_input_producers_remain_consumable();
            return 0;
        }
        if (selected == "controlled-switch") {
            test_controlled_switch_native_activation_matches_browser_semantics();
            return 0;
        }
        if (selected == "repeated-switch-scenes") {
            test_tradingview_switch_repeated_transitions_publish_dense_scenes();
            return 0;
        }
        if (selected == "tradingview-settings-scroll") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            execute(focused_engine, "void 0", "focused-settings-scene-bootstrap.js");
            resize(focused_engine, 800, 734, 1U);
            wait_for_consumed_inputs(
                focused_engine, 1U, "focused settings viewport resize was not consumed");
            test_tradingview_settings_dialog_clips_and_scrolls_middle_region(
                focused_engine);
            test_tradingview_settings_panel_switch_recomputes_scroll_range(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "tradingview-property-table-spacing") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_tradingview_property_table_preserves_control_row_spacing(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "tradingview-compact-property-table") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_tradingview_compact_property_table_expands_wrapped_grid_rows(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "native-file-service") {
            auto* engine=webscene_engine_create(0);
            require(engine!=nullptr,"file engine creation failed");
            test_native_file_service(engine);
            webscene_engine_destroy(engine);
            return 0;
        }
        if (selected == "tradingview-opacity-border") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            execute(focused_engine, "void 0", "focused-opacity-scene-bootstrap.js");
            test_css_linear_gradient_reaches_the_retained_scene(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "fixed-portal-stacking") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            resize(focused_engine, 420, 300, 1U);
            wait_for_consumed_inputs(
                focused_engine, 1U, "focused portal viewport resize was not consumed");
            test_fixed_portal_descendant_stays_in_ancestor_stacking_context(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "absolute-virtual-row-scroll") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_absolute_virtualized_rows_follow_ancestor_scroll_container(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "fixed-auto-height-dialog") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_fixed_auto_height_dialog_with_max_height_expands_flex_content(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "wrapper-retention-recascade") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_connected_style_recascade_skips_detached_wrapper_retention(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "post-message-style-batching") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_window_post_message_coalesces_style_recascade(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "resize-attribute-style-batching") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_resize_event_coalesces_attribute_selector_recascades(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "dimension-variable-compatibility") {
            const std::array tests{
                test_dimension_custom_property_recascade,
                test_dimension_custom_property_inheritance,
                test_geometry_variable_positions,
                test_tradingview_settings_subgrid_keeps_controls_on_their_rows,
                test_responsive_positioned_sizing,
                test_attribute_selector_invalidation,
                test_shadow_dom_composed_runtime_geometry,
                test_inline_relative_line_height_uses_cascaded_font_size,
                test_font_relative_box_lengths_follow_inherited_font_context,
                test_important_custom_property_cascade_reaches_paint,
                test_detached_style_retains_text_and_activates_when_connected,
                test_flex_gap_and_variable_text_metrics,
                test_calc_percent_with_pixel_offset,
                test_window_post_message_coalesces_style_recascade
            };
            for (const auto test : tests) {
                auto* engine = webscene_engine_create(0);
                require(engine != nullptr, "compatibility engine creation failed");
                test(engine);
                webscene_engine_destroy(engine);
            }
            test_outer_dynamic_recascade_preserves_iframe_cascade();
            return 0;
        }
        if (selected == "dimension-inheritance") {
            auto* engine = webscene_engine_create(0);
            require(engine != nullptr, "inheritance engine creation failed");
            test_dimension_custom_property_inheritance(engine);
    test_geometry_variable_positions(engine);
            webscene_engine_destroy(engine);
            return 0;
        }
        if (selected == "dimension-variables") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_dimension_custom_property_recascade(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "detached-dom-gc") {
            test_dom_listener_callback_retirement();
            test_reconnected_panel_subtree_reclamation();
            test_low_memory_reclaims_small_detached_dom_batches();
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            resize(focused_engine, 320, 120, 1U);
            test_detached_dom_wrappers_do_not_permanently_root_nodes(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "panel-subtree-reclamation") {
            test_dom_listener_callback_retirement();
            test_reconnected_panel_subtree_reclamation();
            return 0;
        }
        if (selected == "panel-subtree-reclamation-unregister-control") {
            test_reconnected_panel_subtree_reclamation(true);
            return 0;
        }
        if (selected == "tradingview-svg-checker") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_tradingview_repeating_svg_checker_background_reaches_scene(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "active-chart-pseudo-border") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_active_chart_generated_border_tracks_active_class(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "canvas-prefix-hint") { test_canvas_prefix_hint_tracks_append_and_reset(); return 0; }
        if (selected == "canvas-text-metrics") {
            test_canvas_text_metrics_use_host_font_axes();
            return 0;
        }
        if (selected == "canvas-svg-image") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_svg_dom_parser_preserves_fill_rule(focused_engine);
            test_svg_stylesheet_typography_reaches_scene(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "frame-resource-base-url") {
            test_dynamic_frame_resources_use_each_document_base_url();
            return 0;
        }
        if (selected == "datafeed-symbol-search") {
            test_tradingview_datafeed_iframe_symbol_search_round_trip();
            return 0;
        }
        if (selected == "canvas-blob-url-download") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_pointer_cursor_and_external_anchor_host_handoff(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "tradingview-save-acknowledgement") {
            test_tradingview_save_acknowledgement_uses_multipart_post();
            return 0;
        }
        if (selected == "iframe-preparation") {
            test_iframe_preparation_discovers_subresources_during_outer_script();
            return 0;
        }
        if (selected == "frame-lifecycle") {
            test_deferred_frame_script_observes_window_dom_content_loaded();
            return 0;
        }
        if (selected == "iframe-cooperative") {
            test_cooperative_iframe_hydration_yields_and_isolates_cascade();
            return 0;
        }
        if (selected == "iframe-replacement-layout") {
            test_loaded_iframe_replaces_provisional_layout_root();
            return 0;
        }
        if (selected == "iframe-dynamic-recascade") {
            test_outer_dynamic_recascade_preserves_iframe_cascade();
            return 0;
        }
        if (selected == "scrollspy-primitives") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_scrollspy_product_neutral_primitives(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "window-scroll-primitives") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_window_scroll_primitives(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "empty-inline-geometry") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_empty_inline_element_does_not_stretch_cross_size(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "textarea-value-lifecycle") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_textarea_child_text_value_lifecycle(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "dom-traversal-cloning-primitives") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_dom_traversal_cloning_primitives(focused_engine);
            test_specialized_content_properties(focused_engine);
            test_child_collection_item(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "html-select-add") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "HTMLSelectElement.add engine creation failed");
            test_html_select_add_contract(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "dom-box-dimensions-primitives") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_dom_box_dimensions_primitives(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "positional-selector-siblings") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_positional_selector_sibling_semantics(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "tradingview-symbol-search-grid") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_tradingview_symbol_search_display_contents_rows_join_parent_grid(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "native-text-input") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_native_text_input_focus_events_and_caret(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "raf-aligned-mouse-moves") {
            test_mouse_moves_are_raf_aligned_at_compositor_cadence();
            return 0;
        }
        if (selected == "host-clock-keyframes") {
            auto* focused_engine = webscene_engine_create(64);
            require(focused_engine != nullptr, "keyframe test engine creation failed");
            // These regressions share a host-clock timeline starting with transitions.
            test_opacity_and_color_transitions_use_host_clock_and_dispatch_events(focused_engine);
            test_inline_transition_longhands_survive_dynamic_parse_and_recascade(focused_engine);
            test_opacity_keyframes_use_host_clock_with_staggered_infinite_delays(focused_engine);
            test_rotation_keyframes_use_host_clock_and_wrap_continuously(focused_engine);
            test_filter_keyframes_use_host_clock_and_retained_paint(focused_engine);
            test_clipped_offscreen_keyframes_do_not_keep_host_frame_clock_alive(focused_engine);
            test_animation_direction_maps_cycles_and_terminal_boundaries(focused_engine);
            test_animation_fill_maps_before_after_and_preserves_timeline(focused_engine);
            test_animation_play_state_holds_and_resumes_host_time(focused_engine);
            test_multiple_animation_lists_coordinate_bounded_tracks(focused_engine);
            test_zero_time_animation_tracks_settle_without_frame_demand(focused_engine);
            test_transform_keyframes_compose_retained_geometry_and_paint(focused_engine);
            test_generated_pseudo_animations_share_owner_lifecycle(focused_engine);
            test_bounded_web_animations_share_host_clock_and_lifecycle(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "finite-keyframe-animation-end") {
            auto* focused_engine = webscene_engine_create(64);
            require(focused_engine != nullptr, "keyframe test engine creation failed");
            test_finite_rotation_keyframe_dispatches_animation_end(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "animation-logical-inset-isolation") {
            auto* focused_engine = webscene_engine_create(64);
            require(focused_engine != nullptr, "keyframe test engine creation failed");
            test_finite_rotation_keyframe_dispatches_animation_end(focused_engine);
            webscene_engine_destroy(focused_engine);
            focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr,
                "logical inset transition engine creation failed");
            test_logical_inset_transition_smooths_throttled_pointer_updates(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "logical-inset-transition") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_logical_inset_transition_smooths_throttled_pointer_updates(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "catalog-hover-layout") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_hover_recascade_preserves_horizontal_catalog_row(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "hover-invalidation") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_hover_invalidation_updates_functional_and_sibling_subjects(
                focused_engine);
            test_hover_moves_between_block_and_display_contents_child(
                focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "inherited-box-sizing") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_universal_box_sizing_inherits_from_document_element(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "raster-image-rounded-clip") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_percentage_radius_reaches_raster_image_scene_clip(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        if (selected == "fragment-replacement-script-lifecycle") {
            auto* focused_engine = webscene_engine_create(0);
            require(focused_engine != nullptr, "focused engine creation failed");
            test_fragment_replacement_and_inline_script_lifecycle(focused_engine);
            webscene_engine_destroy(focused_engine);
            return 0;
        }
        fail(std::string("unknown WEBSCENE_NATIVE_ENGINE_TEST_FILTER: ") + filter);
    }
    test_binary_reverse_callback_is_leased_and_completed();
    test_generated_binary_cross_context_promise();
    test_shared_isolate_reuses_destroyed_context_slot();
    test_flex_baseline_uses_host_font_metrics();
    {
        auto* focused_engine = webscene_engine_create(0);
        require(focused_engine != nullptr, "nested CSS regression engine creation failed");
        test_nested_css_absolute_modal_grid_geometry(focused_engine);
        webscene_engine_destroy(focused_engine);
    }
    test_flex_baseline_moves_descendant_pseudo_paint_boxes();
    test_merged_inline_fragment_uses_contextual_host_advance();
    test_flattened_inline_fragment_honors_text_alignment();
    test_viewport_hit_testing_traverses_zero_height_document_root();
    test_document_direction_and_visibility_are_native_properties();
    test_hidden_document_defers_presentation_work();
    test_animation_runtime_is_cold_for_static_nodes();
    test_textual_style_state_is_cold_and_copy_on_write();
    test_elliptical_scene_metadata_is_cold_and_scalar_compatible();
    test_table_and_form_state_are_cold_for_ordinary_nodes();
    test_shadow_dom_state_is_document_cold_and_pay_for_use();
    test_document_clear_releases_and_reinitializes_node_pool();
    test_detached_node_pool_reuses_swept_blocks_without_growing();
    test_native_id_lookup_tracks_creation_erasure_and_clear();
    test_compact_attribute_collection_preserves_map_semantics();
    test_component_catalog_mounts_interacts_and_unmounts();
    test_out_of_flow_client_geometry_reuse_is_scoped();
    test_screen_tracks_viewport();
    test_zero_command_engine_starts_with_clean_scene();
    test_document_start_ordering_storage_and_fail_closed_errors();
    test_four_navigation_workers_enter_startup_concurrently();
    test_initial_document_images_are_loaded();
    test_parallel_resource_prefetch();
    test_fetch_carries_document_origin_to_resource_host();
    test_initial_non_javascript_script_is_inert();
    test_slow_fetch_does_not_block_loading_state_publication();
    test_tradingview_datafeed_iframe_symbol_search_round_trip();
    test_tradingview_save_acknowledgement_uses_multipart_post();
    test_failed_tradingview_save_keeps_dirty_label();
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
    test_inspector_navigation_resets_context_group();
#endif
    test_navigation_replaces_top_level_realm();
    test_document_script_failure_remains_diagnostic();
    test_outer_document_lifecycle_for_editor_bootstrap();
    test_event_listener_exceptions_do_not_abort_document_load();
    test_timer_error_handler_preserves_later_tasks();
    test_host_pointer_exit_clears_tooltip_without_another_move();
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    test_css_style_template_installation_guard();
#endif
    test_runtime_diagnostics();
    test_resource_failure_diagnostics();
    test_runtime_diagnostics_frame_and_failure();
    test_media_query_callback_can_create_more_queries();
    test_media_query_resize_recascades_only_affected_subtrees();
    test_media_query_candidate_scaling();
    test_media_query_non_inherited_root_cascade_work();
    test_media_query_custom_property_consumer_cascade_work();
    test_media_query_variable_reference_metadata_work();
    test_media_query_inherited_variable_consumer_replay_work();
    test_media_query_inheritance_candidate_metadata_work();
    test_media_query_inherited_value_propagation_work();
    test_media_query_matching_scales_linearly();
    test_attribute_invalidation_scopes_subject_and_descendant_rules();
    test_relative_sibling_has_scaling();
    test_live_form_state_selectors_and_scaling();
    test_compiled_subject_index_scaling();
    test_cascade_layer_mutation_scaling();
    test_compiled_css_invalidation_scaling();
    test_character_data_stable_style_scaling();
    test_text_topology_css_work_scaling();
    test_variadic_child_vector_scaling();
    test_compiled_css_route_scaling();
    test_compiled_css_route_scaling(true, true);
    test_concurrent_input_producers_remain_consumable();
    test_dom_implementation_create_html_document();
    test_mixed_continuous_input_backlog_is_coalesced();
    test_pressed_drag_moves_remain_dispatchable_after_threshold();
    test_mouse_moves_are_raf_aligned_at_compositor_cadence();
    test_canvas_prefix_hint_tracks_append_and_reset();
    test_controlled_switch_native_activation_matches_browser_semantics();
    test_tradingview_switch_repeated_transitions_publish_dense_scenes();
    test_loaded_document_keeps_html_and_body_cascade_distinct();
    test_relative_stylesheet_background_uses_stylesheet_address();
    test_resource_cache_reuse_across_engine_generations();
    test_parser_resource_cache_partitioning();
    test_parsed_css_rule_payloads_are_shared_across_live_engines();
    test_process_wide_resource_load_single_flight();
    test_resource_cache_policy_matrix();
    test_due_timer_precedes_dynamic_resource_wave();
    test_dynamic_frame_resources_use_each_document_base_url();
    test_query_iframe_worker_extension_host_bootstrap();
    test_iframe_preparation_discovers_subresources_during_outer_script();
    test_deferred_frame_script_observes_window_dom_content_loaded();
    test_worker_starts_after_engine_configuration();
    test_cooperative_iframe_hydration_yields_and_isolates_cascade();
    test_loaded_iframe_replaces_provisional_layout_root();
    test_youtube_embed_fallback();
    test_outer_dynamic_recascade_preserves_iframe_cascade();
    test_idle_v8_foreground_completion();
    test_animation_frame_demand_emits_idle_to_active_edges();
    test_dynamic_stylesheet_custom_properties_preserve_cascade_order();
    test_persistent_compilation_cache_reuse();
    test_executed_compilation_units_enrich_persistent_cache();
    test_process_wide_compilation_single_flight();
    test_canvas_text_metrics_use_host_font_axes();
    test_recursive_functional_selector_survives_cache_eviction();
    test_namespace_qualified_attribute_selectors_and_lifecycle();
    test_functional_selector_namespace_context_and_lifecycle();
    test_indexeddb_runtime_contract();
    auto* engine = webscene_engine_create(64);
    require(engine != nullptr, "engine creation failed");
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
    test_v8_inspector_raw_cdp_session(engine);
    test_v8_inspector_shutdown_releases_paused_engine();
#else
    require(
        webscene_engine_inspector_is_available(engine) == 0,
        "ordinary V8 runtime unexpectedly exposed Inspector support");
#endif
    test_binary_interop_result_is_leased_and_pooled(engine);
    test_binary_interop_preserves_json_edge_semantics(engine);
    test_generated_binary_invocation_uses_tagged_arguments(engine);
    test_binary_interop_stress_when_requested(engine);
    require(
        webscene_engine_request_low_memory(engine) != 0,
        "engine rejected an asynchronous low-memory request");
    test_engine_memory_metrics_are_worker_snapshots(engine);
    test_hidden_engine_reclamation_is_debounced_and_cancelable(engine);
    test_native_websocket_browser_api();
    test_native_websocket_protocol_handshake_timing();
    test_native_file_reader_task_source_fairness();
    execute(
        engine,
        "if (typeof IntersectionObserver !== 'function' || "
        "typeof IntersectionObserverEntry !== 'function') "
        "throw new Error('IntersectionObserver bootstrap missing')",
        "intersection-observer-bootstrap.js");
    test_dimension_custom_property_recascade(engine);
    test_logical_size_properties_map_to_horizontal_box_axes(engine);
    test_dimension_custom_property_inheritance(engine);
    test_geometry_variable_positions(engine);
    test_modal_backdrop_scene(engine);
    test_word_break_and_overflow_wrap_layout(engine);
    test_placeholder_pseudo_color_and_opacity_reach_scene(engine);
    test_details_content_pseudo_static_open_close_layout(engine);
    test_details_content_keyword_size_transition(engine);
    test_responsive_positioned_sizing(engine);
    test_compact_go_to_fixed_grid_tracks_preserve_trailing_space(engine);
    test_named_grid_template_areas_layout_cssom_and_mutation(engine);
    test_grid_item_spanning_auto_rows_contributes_its_height(engine);
    test_grid_auto_maximum_tracks_stretch_remaining_space(engine);
    test_go_to_tab_lines_and_calendar_scroll_ranges(engine);
    test_media_query_list_tracks_outer_and_frame_viewport_breakpoints(engine);
    test_responsive_unset_restores_auto_inset(engine);
    test_preferred_color_scheme_updates_css_and_match_media(engine);
    test_resize_listener_receives_window_event(engine);
    test_absolute_portal_centers_against_positioned_ancestor(engine);
    test_attribute_selector_invalidation(engine);
    test_attribute_selector_list_requires_authored_attribute(engine);
    test_script_raw_text_does_not_create_style_descendants(engine);
    test_attribute_selector_operators(engine);
    test_replace_child_advances_attribute_selector_iteration(engine);
    test_insert_before_preserves_tree_identity_and_atomicity(engine);
    test_related_tree_mutations_preserve_identity_and_atomicity(engine);
    test_contextual_fragment_exposes_parent_node_members(engine);
    test_live_range_boundary_contract(engine);
    test_custom_highlight_registry_contract(engine);
    test_custom_highlight_retained_paint_contract(engine);
    test_selection_pseudo_retained_paint_contract(engine);
    test_custom_element_mutation_reactions_are_pay_for_use(engine);
    test_autonomous_custom_element_lifecycle(engine);
    test_shadow_dom_composed_runtime_geometry(engine);
    test_monaco_browser_primitives(engine);
    test_monaco_view_line_dom_mutations(engine);
    test_class_list_is_same_live_object(engine);
    test_bounded_css_named_color_palette();
    test_visibility_inherits_for_computed_style_and_focus(engine);
    test_hover_specificity_preserves_visible_theme_icon(engine);
    test_hover_recascade_preserves_horizontal_catalog_row(engine);
    test_complex_is_specificity_ignores_non_element_siblings(engine);
    test_inline_relative_line_height_uses_cascaded_font_size(engine);
    test_empty_inline_element_does_not_stretch_cross_size(engine);
    test_br_keeps_inline_block_on_its_own_line(engine);
    test_hover_invalidation_updates_functional_and_sibling_subjects(engine);
    test_hover_dependency_matching_reuses_compiled_triggers(engine);
    test_hover_moves_between_block_and_display_contents_child(engine);
    test_single_fractional_grid_track_stays_one_column(engine);
    test_auto_fill_minmax_custom_property_tracks_keep_their_sizing(engine);
    test_tradingview_symbol_search_display_contents_rows_join_parent_grid(engine);
    test_tradingview_settings_subgrid_keeps_controls_on_their_rows(engine);
    test_tradingview_property_table_preserves_control_row_spacing(engine);
    test_tradingview_compact_property_table_expands_wrapped_grid_rows(engine);
    test_footer_grid_direction_and_focus_within_state(engine);
    test_active_chart_generated_border_tracks_active_class(engine);
    test_non_rendered_dom_nodes_do_not_create_layout_items(engine);
    test_calc_percent_with_pixel_offset(engine);
    test_calc_viewport_units_with_pixel_offsets_bound_fixed_boxes(engine);
    test_current_color_and_color_mix_reach_element_and_pseudo_paint(engine);
    test_hsl_and_pseudo_gradients_reach_color_plane_paint(engine);
    test_adjacent_inline_spans_wrap_through_generated_whitespace(engine);
    test_flex_basis_reserves_fixed_track(engine);
    test_flex_icon_metadata_card_and_title_geometry(engine);
    test_absolute_flex_child_uses_container_static_position(engine);
    test_absolute_inset_stretch_accounts_for_negative_margins(engine);
    test_flex_flow_shorthand_controls_layout_and_cssom(engine);
    test_font_relative_box_lengths_follow_inherited_font_context(engine);
    test_floats_share_a_bounded_formatting_line(engine);
    test_wrapped_flex_resolves_each_line_independently(engine);
    test_zero_height_flex_item_grows_and_hit_tests_descendants(engine);
    test_empty_non_growing_flex_item_collapses_main_axis(engine);
    test_empty_bordered_flex_items_keep_intrinsic_cross_size(engine);
    test_appending_child_invalidates_empty_selector(engine);
    test_inline_block_preserves_vertical_padding(engine);
    test_pointer_hit_targets_and_related_targets_are_elements(engine);
    test_user_select_pointer_default_action(engine);
    test_pointer_cursor_and_external_anchor_host_handoff(engine);
    test_nested_drag_drop_routing_and_retirement();
    test_outbound_drag_browser_contract_and_retirement(engine);
    test_typed_window_host_request_performance(engine);
    {
        auto* close_engine = webscene_engine_create(0);
        require(close_engine != nullptr,
            "host-driven close engine creation failed");
        test_host_driven_window_close_lifecycle(close_engine);
        webscene_engine_destroy(close_engine);
    }
    {
        auto* close_engine = webscene_engine_create(0);
        require(close_engine != nullptr,
            "script close engine creation failed");
        test_script_window_close_lifecycle(close_engine);
        webscene_engine_destroy(close_engine);
    }
    test_enter_dispatches_browser_keypress_for_interval_commit(engine);
    test_css_linear_gradient_reaches_the_retained_scene(engine);
    {
        auto* file_engine=webscene_engine_create(0);
        require(file_engine!=nullptr,"file service fixture creation failed");
        test_native_file_service(file_engine);
        webscene_engine_destroy(file_engine);
    }
    test_z_index_orders_positioned_siblings_in_scene(engine);
    test_popup_portal_tooltip_escapes_non_stacking_positioned_wrapper(engine);
    test_fixed_portal_descendant_stays_in_ancestor_stacking_context(engine);
    test_transform_origin_keywords_cascade_independently_from_inline_transform(engine);
    test_transform_translate_calc_arguments_preserve_nested_functions(engine);
    test_zero_depth_translate3d_positions_fixed_coach_mark(engine);
    test_transform_transition_uses_host_clock_for_translate_and_scale(engine);
    test_transform_transition_interpolates_from_none(engine);
    test_cssom_serializes_resolved_numbers_without_trailing_zeroes(engine);
    test_cssom_serializes_inline_hex_colors(engine);
    test_cssom_padding_assignment_updates_longhands_and_geometry(engine);
    test_cssom_border_assignment_updates_longhands_and_geometry(engine);
    test_logical_inline_borders_reach_geometry(engine);
    test_hidden_subtree_retains_computed_height_without_boxes(engine);
    test_cssom_z_index_survives_connection_and_recascade(engine);
    test_important_custom_property_cascade_reaches_paint(engine);
    test_detached_style_retains_text_and_activates_when_connected(engine);
    test_outer_box_shadow_reaches_elevated_scene(engine);
    test_segmented_rounded_borders_share_an_unclipped_join(engine);
    test_flex_gap_and_variable_text_metrics(engine);
    test_native_overflow_scrolling_and_nowrap(engine);
    test_scroll_offset_changes_translate_retained_geometry_without_layout(engine);
    test_authored_scrollbar_style_and_thumb_drag(engine);
    test_absolute_virtualized_rows_follow_ancestor_scroll_container(engine);
    test_rounded_overflow_visual_fixture_geometry(engine);
    test_elliptical_corner_radii_reach_cssom(engine);
    test_row_flex_vertical_scroll_extent_remains_bounded(engine);
    test_toolbar_scroll_chevrons_use_single_rotation(engine);
    test_table_minimum_width_reaches_toolbar_overflow_navigation(engine);
    test_dynamic_percentage_flex_list_keeps_intrinsic_inline_contribution(engine);
    test_root_document_overflow_scrolls_and_paints_overlay(engine);
    test_table_menu_row_cells_stay_horizontal_and_centered(engine);
    test_semantic_table_auto_layout_and_intrinsic_cell_content(engine);
    test_fixed_table_distributes_excess_after_percentage_columns(engine);
    test_implicit_grid_contains_scrollable_table(engine);
    test_auto_height_flex_popup_expands_overflowing_flex_child(engine);
    test_fixed_auto_height_dialog_with_max_height_expands_flex_content(engine);
    test_custom_time_element_keeps_layout_dialog_metadata_inline(engine);
    test_auto_height_overflow_auto_does_not_paint_phantom_scrollbar(engine);
    test_constrained_column_flex_scroll_item_keeps_footer_inside(engine);
    test_tradingview_settings_dialog_clips_and_scrolls_middle_region(engine);
    test_tradingview_settings_panel_switch_recomputes_scroll_range(engine);
    test_tradingview_symbol_info_auto_height_has_no_scrollbar(engine);
    test_later_dom_overlay_background_paints_above_retained_canvas(engine);
    test_multiple_canvas_pane_backgrounds_remain_below_retained_layers(engine);
    test_canvas_text_max_width_is_preserved_in_scene(engine);
    test_canvas_path_even_odd_fill_rule_reaches_scene(engine);
    test_canvas_path_even_odd_clip_rule_reaches_scene(engine);
    test_canvas_fill_rect_emits_only_relevant_paint_state(engine);
    test_canvas_path_2d_add_path_does_not_fill_stale_current_path(engine);
    test_canvas_line_dash_and_path_2d_arc_are_native(engine);
    test_canvas_ellipse_records_complete_path_command(engine);
    test_detached_canvas_descendants_leave_native_scene(engine);
    test_compound_root_selector_applies_dark_custom_palette(engine);
    test_adjacent_inline_runs_share_wrapped_lines(engine);
    test_default_line_breaking_does_not_split_unbreakable_tokens(engine);
    test_inline_flex_preserves_padding_and_line_box(engine);
    test_document_position(engine);
    test_dom_box_dimensions_primitives(engine);
    test_dom_traversal_cloning_primitives(engine);
    test_specialized_content_properties(engine);
    test_child_collection_item(engine);
    test_fragment_replacement_and_inline_script_lifecycle(engine);
    test_fragment_append_applies_structural_selectors_after_atomic_attachment(engine);
    test_textarea_child_text_value_lifecycle(engine);
    test_secondary_click(engine);
    test_primary_click_mouse_event_detail(engine);
    test_event_listener_options_reach_native_input_and_resize(engine);
    test_listener_added_during_dispatch_waits_for_next_event(engine);
    test_node_filter_tree_walker_focus_navigation(engine);
    test_node_iterator_dompurify_and_filter_contracts(engine);
    test_node_iterator_isolated_scaling_and_memory_gate();
    test_node_has_child_nodes_contract_and_performance(engine);
    test_dom_token_list_collection_performance_gate();
    test_iframe_sandbox_dom_token_list_security_and_lifecycle_gate();
    test_table_cell_click_copies_text_to_host();
    test_synthetic_window_resize_dispatch_uses_outer_listener_registry(engine);
    test_document_create_event_and_init_event(engine);
    test_native_mouseup_honors_immediate_propagation_stop(engine);
    test_generated_idl_attributes_are_prototype_accessors(engine);
    test_document_links_is_a_live_named_html_collection(engine);
    test_scrollspy_product_neutral_primitives(engine);
    test_native_performance_timeline_identity(engine);
    test_native_mutable_stylesheet_cssom(engine);
    test_clipboard_write_text_host_handoff(engine);
    test_media_devices_enumeration_host_handoff();
    test_media_capture_track_host_handoff();
    test_clipboard_read_host_completion(engine);
    test_native_clipboard_shortcut_events(engine);
    test_primary_selection_middle_paste(engine);
    test_primary_selection_publication(engine);
    test_native_image_clipboard_maximum_payload(engine);
    test_native_legacy_clipboard_completion_stress(engine);
    test_native_pending_legacy_clipboard_shutdown(engine);
    test_clipboard_maximum_payload_gate(engine);
    test_clipboard_small_round_trip_performance(engine);
    test_fullscreen_host_completion(engine);
    test_component_library_dom_discovery_primitives(engine);
    test_document_id_index_preserves_tree_and_root_semantics(engine);
    test_dom_selector_apis_throw_syntax_error_for_invalid_selectors(engine);
    test_positional_selector_sibling_semantics(engine);
    test_dropdown_runtime_primitives(engine);
    test_collapsed_select_principal_scene_contract(engine);
    test_collapsed_single_select_native_activation(engine);
    test_input_dispatch_failures_are_attributed_and_consumable(engine);
    test_animation_frame_dispatch_is_attributed();
    test_runtime_work_is_attributed();
    test_scene_flow_is_attributed();
    test_read_only_evaluation_does_not_publish_scene();
    test_identical_custom_property_writes_do_not_schedule_visual_work();
    test_async_save_acknowledgement_publishes_without_pointer_input();
    test_ordered_scene_consumer_preserves_two_diff_chain();
    test_keyboard_and_pointer_focus_modality();
    test_navigator_platform_and_wheel_modifiers(engine);
    test_discrete_wheel_scroll_uses_bounded_frame_animation();
    test_resize_precedes_new_viewport_pointer_input(engine);
    test_generated_pseudo_element_opacity(engine);
    test_tradingview_split_color_swatch_uses_pseudo_border_triangle(engine);
    test_negative_z_after_paints_behind_svg_content(engine);
    test_svg_current_color_is_resolved_before_scene_serialization(engine);
    test_svg_stylesheet_typography_reaches_scene(engine);
    test_svg_preserve_aspect_ratio_reaches_scene_serialization(engine);
    test_svg_view_box_keeps_foreign_attribute_case_and_origin(engine);
    test_positive_z_before_paints_above_lower_z_child(engine);
    test_element_opacity_emits_isolated_group(engine);
    test_svg_background_image_reaches_scene_with_position_and_size(engine);
    test_css_raster_mask_provider_envelope_and_fail_closed_contract();
    test_controlled_css_raster_mask_publication_and_stale_rejection();
    test_rounded_icon_surfaces_publish_stable_commands_across_selection_mutation(engine);
    test_tradingview_repeating_svg_checker_background_reaches_scene(engine);
    test_image_elements_load_and_reach_scene(engine);
    test_percentage_radius_reaches_raster_image_scene_clip(engine);
    test_virtual_html_root_inherits_font_metrics(engine);
    test_font_shorthand_inherit_resets_control_metrics(engine);
    test_webkit_font_smoothing_inherits_into_text_scene(engine);
    test_all_unset_resets_modeled_control_properties(engine);
    test_inherited_range_fill_relative_inline_and_atomic_baselines(engine);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    test_startup_profile_names_scripts_and_tasks();
#endif
    test_native_text_input_focus_events_and_caret(engine);
    test_dom_punctuation_keyboard_event_identity(engine);
    test_text_control_select_all_default_action(engine);
    test_native_shortcut_activation_focus_retarget_and_publication(engine);
    test_chart_printable_key_does_not_duplicate_into_newly_focused_search_input(engine);
    test_svg_dom_parser_preserves_fill_rule(engine);
    test_frame_script_dom_presence(engine);
#if defined(WEBSCENE_NATIVE_ENGINE_HTML5EVER)
    test_html5ever_frame_does_not_duplicate_authored_loading_indicator(engine);
#endif
    test_dom_element_constructor_identity(engine);
    test_provisional_frame_focus_and_document_event_identity(engine);
    test_initial_frame_document_write_and_hidden_style(engine);
    test_reconnected_panel_subtree_reclamation();
    test_dom_listener_callback_retirement();
    test_semantic_snapshot_nested_state_lifecycle_and_bounds();
    test_semantic_delta_workbench_and_nested_generation_contract();
    test_semantic_typed_delta_companion_contract();
    test_semantic_action_dom_contract_and_retirement();
    test_semantic_live_regions_dom_order_busy_and_lifecycle();
    test_low_memory_reclaims_small_detached_dom_batches();
    test_detached_dom_wrappers_do_not_permanently_root_nodes(engine);
    test_connected_style_recascade_skips_detached_wrapper_retention(engine);
    test_resize_updates_device_pixel_ratio(engine);
    test_resize_observer_device_pixel_canvas_export(engine);
    test_resize_observer_device_scale_only_delivery(engine);
    test_session_storage_in_outer_and_frame_contexts(engine);
    test_window_post_message_is_queued(engine);
    test_window_post_message_coalesces_style_recascade(engine);
    test_url_static_helpers_contract();
    test_abort_signal_composition_contract();
    test_origin_partitioned_broadcast_channel_contract();
    test_resize_event_coalesces_attribute_selector_recascades(engine);
    test_cross_frame_post_message_and_window_frames(engine);
    test_frame_resize_preserves_outer_percentage_height(engine);
    test_inner_window_load_acknowledgement(engine);
    test_animation_frame_uses_host_frame(engine);
    test_animation_frame_callback_list_timestamp_and_cancellation(engine);
    test_animation_frame_pending_callbacks_keep_timestamp(engine);
    test_pointer_input_precedes_following_render_opportunity();
    test_resize_and_frame_form_one_rendering_opportunity();
    test_unsupported_features_are_reported_at_native_decision_points(engine);
    test_cssom_paint_invalidation_does_not_force_layout(engine);
    test_client_rect_reads_reuse_unrelated_out_of_flow_layout(engine);
    test_paint_only_stylesheet_recascade_does_not_force_layout(engine);
    webscene_engine_destroy(engine);
    engine = webscene_engine_create(64);
    require(engine != nullptr, "transition regression engine creation failed");
    test_opacity_and_color_transitions_use_host_clock_and_dispatch_events(engine);
    test_inline_transition_longhands_survive_dynamic_parse_and_recascade(engine);
    test_opacity_keyframes_use_host_clock_with_staggered_infinite_delays(engine);
    test_registered_custom_property_keyframes_update_dependent_paint();
    test_starting_style_entry_and_discrete_display_transitions();
    test_rotation_keyframes_use_host_clock_and_wrap_continuously(engine);
    test_filter_keyframes_use_host_clock_and_retained_paint(engine);
    test_clipped_offscreen_keyframes_do_not_keep_host_frame_clock_alive(engine);
    test_finite_rotation_keyframe_dispatches_animation_end(engine);
    test_animation_direction_maps_cycles_and_terminal_boundaries(engine);
    test_animation_fill_maps_before_after_and_preserves_timeline(engine);
    test_animation_play_state_holds_and_resumes_host_time(engine);
    test_multiple_animation_lists_coordinate_bounded_tracks(engine);
    test_zero_time_animation_tracks_settle_without_frame_demand(engine);
    test_transform_keyframes_compose_retained_geometry_and_paint(engine);
    test_generated_pseudo_animations_share_owner_lifecycle(engine);
    test_bounded_web_animations_share_host_clock_and_lifecycle(engine);
    // This regression uses fixed host timestamps beginning at 2700 ms. Give it
    // a fresh timeline so earlier animation tests cannot make those frames stale.
    webscene_engine_destroy(engine);
    engine = webscene_engine_create(0);
    require(engine != nullptr, "logical inset transition engine creation failed");
    test_logical_inset_transition_smooths_throttled_pointer_updates(engine);
    webscene_engine_destroy(engine);
    test_binary_interop_result_outlives_engine();
    test_semantic_snapshot_caps_performance_and_engine_retirement();
    test_semantic_delta_overflow_full_snapshot_recovery();
    test_semantic_delta_virtualized_tree_100_cycle_memory_and_p95();
    test_semantic_action_queue_caps_and_worker_budget();
    test_semantic_live_region_caps_and_worker_budget();
    return 0;
}
