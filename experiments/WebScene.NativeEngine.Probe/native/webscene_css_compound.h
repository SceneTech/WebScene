#pragma once
#include "webscene_css_matching.h"
#include "webscene_native_resource_url.h"
#include <atomic>

namespace webscene_native::css {
#if defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_BENCHMARK_COUNTERS)
inline std::atomic<uint64_t> selector_sibling_positional_matches{0U};
inline std::atomic<uint64_t> selector_sibling_scans{0U};
inline std::atomic<uint64_t> selector_sibling_vector_materializations{0U};
inline std::atomic<uint64_t> selector_sibling_pointer_copies{0U};
#endif

inline bool is_hyperlink_source(const dom_node& node)
{
    constexpr std::string_view svg_namespace = "http://www.w3.org/2000/svg";
    const auto namespace_uri = node.namespace_uri();
    return node.attributes.contains("href")
        && ((node.tag == "a"
                && (namespace_uri == dom_node::html_namespace_uri
                    || namespace_uri == svg_namespace))
            || (node.tag == "area"
                && namespace_uri == dom_node::html_namespace_uri));
}

inline bool local_link_matches(
    const dom_node& node,
    std::string_view effective_base,
    std::string_view document_url)
{
    constexpr size_t maximum_url_bytes = 8192U;
    if (!is_hyperlink_source(node)
        || effective_base.empty() || document_url.empty()
        || effective_base.size() > maximum_url_bytes
        || document_url.size() > maximum_url_bytes) return false;
    const auto href = node.attributes.find("href");
    if (href == node.attributes.end() || href->second.size() > maximum_url_bytes)
        return false;
    std::string target;
    if (href->second.empty()) {
        target = effective_base;
    } else if (href->second.front() == '#') {
        target = effective_base.substr(0U, effective_base.find('#'));
        target += href->second;
    } else if (href->second.front() == '?') {
        target = effective_base.substr(0U, effective_base.find_first_of("?#"));
        target += href->second;
    } else {
        target = resources::resolve_url(href->second, std::string(effective_base));
    }
    if (target.empty() || target.size() > maximum_url_bytes) return false;
    const auto without_fragment = [](std::string_view value) {
        const auto fragment = value.find('#');
        return value.substr(0U, fragment);
    };
    return without_fragment(target) == without_fragment(document_url);
}

// The host supplies document state, recursive queries, class-token caching and
// a plain URL hash. Compound evaluation itself has no V8 dependency.
template<typename Host>
inline bool compound_matches(const Host& host,const dom_node& node,
    const compiled_css_compound& selector,const dom_node* scope_root=nullptr,
    bool stable_features_only=false)
    {
        const auto& document=host.document;
        if (!selector.valid) return false;
        // The probe intentionally collapses each browsing context's HTML and
        // BODY boxes into a single native root node.  Browser CSS still targets
        // that box with selectors such as `html.theme-dark`, so let the virtual
        // root participate as both tags.  A frame document is parented beneath
        // its owning iframe in the unified native tree.
        const auto explicit_document_element = node.tag == "html"
            && (node.parent == nullptr || node.parent == &document.body()
                || node.parent->tag == "iframe");
        const auto has_explicit_document_element = node.tag == "body"
            && std::any_of(node.children.begin(), node.children.end(), [](const auto* child) {
                return child != nullptr && child->tag == "html";
            });
        const auto is_document_root = explicit_document_element
            || (node.tag == "body"
                && (node.parent == nullptr || node.parent->tag == "iframe")
                && !has_explicit_document_element);
        // A pseudo-element is a different CSS box, not the originating DOM
        // element.  Treating it as the element makes rules such as
        // `.scroller::-webkit-scrollbar { display: none }` hide the scroller
        // itself, which is catastrophic for toolbar layout.
        if (selector.pseudo_element) {
            return false;
        }

        if (selector.namespace_uri.has_value()
            && node.namespace_uri() != *selector.namespace_uri) {
            return false;
        }

        if (!selector.tag.empty()) {
            auto wanted_tag = selector.tag;
            if (!node.xml_mode) wanted_tag = ascii_lower(wanted_tag);
            if (wanted_tag != node.tag
                && !(wanted_tag == "html" && is_document_root && node.tag == "body")) return false;
        }
        for (const auto& [marker, wanted] : selector.identities) {
            if (marker == '#') {
                if (node.id_attribute != wanted) return false;
            } else if (!host.has_class(node, wanted)) {
                return false;
            }
        }
        for (const auto& attribute : selector.attributes) {
            if (!attribute_matches(node, attribute)) return false;
        }
        // Structural invalidation must also visit subjects that stopped
        // matching a pseudo after a removal. Identity/attribute tests remain
        // useful filters, but testing current pseudos would lose old matches.
        if (stable_features_only) return true;

        const auto is_element = [](const dom_node* candidate) {
            return candidate != nullptr && !candidate->tag.starts_with('#');
        };
        const auto record_sibling_visit = [&] {
            if constexpr (requires { host.record_css_positional_sibling_visit(); })
                host.record_css_positional_sibling_visit();
        };
#if !defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_SCAN_EXPERIMENT)
        std::vector<const dom_node*> element_siblings;
        auto element_siblings_ready = false;
        const auto siblings = [&]() -> const std::vector<const dom_node*>& {
            if (element_siblings_ready) return element_siblings;
            element_siblings_ready = true;
#if defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_BENCHMARK_COUNTERS)
            selector_sibling_vector_materializations.fetch_add(
                1U, std::memory_order_relaxed);
#endif
            if (node.parent != nullptr) {
                element_siblings.reserve(node.parent->children.size());
                for (const auto* child : node.parent->children) {
                    record_sibling_visit();
#if defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_BENCHMARK_COUNTERS)
                    selector_sibling_scans.fetch_add(1U, std::memory_order_relaxed);
#endif
                    if (is_element(child)) {
                        element_siblings.push_back(child);
#if defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_BENCHMARK_COUNTERS)
                        selector_sibling_pointer_copies.fetch_add(
                            1U, std::memory_order_relaxed);
#endif
                    }
                }
            }
            return element_siblings;
        };
#else
        struct sibling_positions final {
            size_t count{0U};
            size_t position{0U};
            bool node_found{false};
        };
        sibling_positions element_positions;
        sibling_positions same_type_positions;
        auto element_positions_ready = false;
        auto same_type_positions_ready = false;
        const auto element_sibling_summary = [&]() -> const sibling_positions& {
            if (element_positions_ready) return element_positions;
            element_positions_ready = true;
            if (node.parent == nullptr) return element_positions;
            for (const auto* child : node.parent->children) {
                record_sibling_visit();
#if defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_BENCHMARK_COUNTERS)
                selector_sibling_scans.fetch_add(1U, std::memory_order_relaxed);
#endif
                if (!is_element(child)) continue;
                ++element_positions.count;
                if (child != &node) continue;
                element_positions.node_found = true;
                element_positions.position = element_positions.count;
            }
            return element_positions;
        };
        const auto same_type_sibling_summary = [&]() -> const sibling_positions& {
            if (same_type_positions_ready) return same_type_positions;
            same_type_positions_ready = true;
            if (node.parent == nullptr) return same_type_positions;
            for (const auto* child : node.parent->children) {
                record_sibling_visit();
#if defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_BENCHMARK_COUNTERS)
                selector_sibling_scans.fetch_add(1U, std::memory_order_relaxed);
#endif
                if (!is_element(child) || child->tag != node.tag) continue;
                ++same_type_positions.count;
                if (child != &node) continue;
                same_type_positions.node_found = true;
                same_type_positions.position = same_type_positions.count;
            }
            return same_type_positions;
        };
#endif

        for (const auto& pseudo : selector.pseudos) {
            const std::string_view name(pseudo.name);
            const std::string_view argument(pseudo.argument);
            if constexpr (requires { host.css_positional_summary(node, false); }) {
                const bool positional = name == "first-child" || name == "last-child"
                    || name == "only-child" || name == "nth-child" || name == "nth-last-child"
                    || name == "first-of-type" || name == "last-of-type" || name == "only-of-type"
                    || name == "nth-of-type" || name == "nth-last-of-type";
                if (positional) {
                    // A matching pass is immutable. Index each sibling list once
                    // instead of scanning/copying it again for every subject.
                    const auto values = host.css_positional_summary(node, name.ends_with("of-type"));
                    if (values.has_value()) {
#if defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_BENCHMARK_COUNTERS)
                        selector_sibling_positional_matches.fetch_add(1U, std::memory_order_relaxed);
#endif
                        if (!values->node_found) return false;
                        if (name.starts_with("first-") && values->position != 1U) return false;
                        if (name.starts_with("last-") && values->position != values->count) return false;
                        if (name.starts_with("only-") && values->count != 1U) return false;
                        if (name.starts_with("nth-")) {
                            const auto position = name.starts_with("nth-last-")
                                ? values->count - values->position + 1U : values->position;
                            if (!nth_matches(argument, static_cast<int>(position))) return false;
                        }
                        continue;
                    }
                }
            }
            const auto form_control = node.tag == "button" || node.tag == "input"
                || node.tag == "select" || node.tag == "textarea"
                || node.tag == "option" || node.tag == "optgroup" || node.tag == "fieldset";
            if (name == "root") {
                if (!is_document_root) return false;
            } else if (name == "scope") {
                if (scope_root == nullptr ? !is_document_root : scope_root != &node) return false;
            } else if (name == "first-child") {
#if defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_BENCHMARK_COUNTERS)
                selector_sibling_positional_matches.fetch_add(1U, std::memory_order_relaxed);
#endif
#if !defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_SCAN_EXPERIMENT)
                const auto& values = siblings();
                const auto position = std::find(values.begin(), values.end(), &node);
                if (node.parent == nullptr || position != values.begin()) return false;
#else
                const auto& values = element_sibling_summary();
                if (node.parent == nullptr || !values.node_found
                    || values.position != 1U) return false;
#endif
            } else if (name == "last-child") {
#if defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_BENCHMARK_COUNTERS)
                selector_sibling_positional_matches.fetch_add(1U, std::memory_order_relaxed);
#endif
#if !defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_SCAN_EXPERIMENT)
                const auto& values = siblings();
                const auto position = std::find(values.begin(), values.end(), &node);
                if (node.parent == nullptr || position == values.end()
                    || position + 1 != values.end()) return false;
#else
                const auto& values = element_sibling_summary();
                if (node.parent == nullptr || !values.node_found
                    || values.position != values.count) return false;
#endif
            } else if (name == "only-child") {
#if defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_BENCHMARK_COUNTERS)
                selector_sibling_positional_matches.fetch_add(1U, std::memory_order_relaxed);
#endif
#if !defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_SCAN_EXPERIMENT)
                if (node.parent == nullptr || siblings().size() != 1U) return false;
#else
                const auto& values = element_sibling_summary();
                if (node.parent == nullptr || !values.node_found
                    || values.count != 1U) return false;
#endif
            } else if (name == "nth-child" || name == "nth-last-child"
                || name == "nth-of-type" || name == "nth-last-of-type") {
#if defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_BENCHMARK_COUNTERS)
                selector_sibling_positional_matches.fetch_add(1U, std::memory_order_relaxed);
#endif
                if (node.parent == nullptr) return false;
#if !defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_SCAN_EXPERIMENT)
                auto values = siblings();
#if defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_BENCHMARK_COUNTERS)
                selector_sibling_vector_materializations.fetch_add(
                    1U, std::memory_order_relaxed);
                selector_sibling_pointer_copies.fetch_add(
                    values.size(), std::memory_order_relaxed);
#endif
                if (name == "nth-of-type" || name == "nth-last-of-type") {
                    std::erase_if(values, [&](const auto* sibling) {
                        return sibling->tag != node.tag;
                    });
                }
                const auto position = std::find(values.begin(), values.end(), &node);
                if (position == values.end()) return false;
                const auto from_end = name == "nth-last-child"
                    || name == "nth-last-of-type";
                const auto one_based_position = static_cast<int>(from_end
                    ? std::distance(position, values.end())
                    : std::distance(values.begin(), position) + 1);
#else
                const auto of_type = name == "nth-of-type"
                    || name == "nth-last-of-type";
                const auto& values = of_type
                    ? same_type_sibling_summary()
                    : element_sibling_summary();
                if (!values.node_found) return false;
                const auto from_end = name == "nth-last-child"
                    || name == "nth-last-of-type";
                const auto one_based_position = static_cast<int>(
                    from_end ? values.count - values.position + 1U : values.position);
#endif
                if (!nth_matches(argument, one_based_position)) return false;
            } else if (name == "first-of-type" || name == "last-of-type"
                || name == "only-of-type") {
#if defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_BENCHMARK_COUNTERS)
                selector_sibling_positional_matches.fetch_add(1U, std::memory_order_relaxed);
#endif
                if (node.parent == nullptr) return false;
#if !defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_SCAN_EXPERIMENT)
                std::vector<const dom_node*> same_type;
#if defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_BENCHMARK_COUNTERS)
                selector_sibling_vector_materializations.fetch_add(
                    1U, std::memory_order_relaxed);
#endif
                for (const auto* sibling : siblings()) {
                    if (sibling->tag == node.tag) {
                        same_type.push_back(sibling);
#if defined(WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_BENCHMARK_COUNTERS)
                        selector_sibling_pointer_copies.fetch_add(
                            1U, std::memory_order_relaxed);
#endif
                    }
                }
                const auto same_position = std::find(same_type.begin(), same_type.end(), &node);
                if (name == "first-of-type" && same_position != same_type.begin()) return false;
                if (name == "last-of-type"
                    && (same_position == same_type.end() || same_position + 1 != same_type.end())) return false;
                if (name == "only-of-type" && same_type.size() != 1U) return false;
#else
                const auto& values = same_type_sibling_summary();
                if (!values.node_found) return false;
                if (name == "first-of-type" && values.position != 1U) return false;
                if (name == "last-of-type"
                    && values.position != values.count) return false;
                if (name == "only-of-type" && values.count != 1U) return false;
#endif
            } else if (name == "empty") {
                if (!node.text_content.empty() || std::any_of(
                        node.children.begin(), node.children.end(), [](const dom_node* child) {
                            return child != nullptr && (child->kind == dom_node_kind::element
                                || (child->kind == dom_node_kind::text && !child->text_content.empty()));
                        })) return false;
            } else if (name == "enabled") {
                if (!form_control || css::is_actually_disabled(document,node)) return false;
            } else if (name == "disabled") {
                if (!form_control || !css::is_actually_disabled(document,node)) return false;
            } else if (name == "required") {
                if (!forms::required_applies(node)
                    || !node.attributes.contains("required")) return false;
            } else if (name == "optional") {
                if (!forms::required_applies(node)
                    || node.attributes.contains("required")) return false;
            } else if (name == "read-write") {
                if (!css::is_read_write(document,node)) return false;
            } else if (name == "read-only") {
                if (css::is_read_write(document,node)) return false;
            } else if (name == "valid") {
                if (forms::validity_state(document,node) != forms::simple_validity_state::valid) return false;
            } else if (name == "invalid") {
                if (forms::validity_state(document,node) != forms::simple_validity_state::invalid) return false;
            } else if (name == "user-valid") {
                if (!node.form_control().user_validity_interacted
                    || forms::validity_state(document,node) != forms::simple_validity_state::valid) return false;
            } else if (name == "user-invalid") {
                if (!node.form_control().user_validity_interacted
                    || forms::validity_state(document,node) != forms::simple_validity_state::invalid) return false;
            } else if (name == "placeholder-shown") {
                if (!forms::supports_placeholder_selector(node)
                    || !node.attributes.contains("placeholder")
                    || !forms::text_value_empty(node)) return false;
            } else if (name == "lang") {
                if (!css::language_matches(document,node,argument)) return false;
            } else if (name == "dir") {
                if (!css::direction_matches(document,node,argument)) return false;
            } else if (name == "target") {
                const auto hash=host.selector_target_hash();
                if (!hash || !css::target_matches(node,*hash)) return false;
            } else if (name == "link" || name == "any-link") {
                if (!css::is_hyperlink_source(node)) return false;
            } else if (name == "local-link") {
                // The path-depth functional form remains outside this slice.
                if (!argument.empty()) return false;
                if constexpr (requires { host.selector_local_link_matches(node); }) {
                    if (!host.selector_local_link_matches(node)) return false;
                } else {
                    return false;
                }
            } else if (name == "visited") {
                // WebScene does not retain browsing history. Fail closed rather
                // than expose host navigation state through selector matching.
                return false;
            } else if (name == "checked") {
                if (!css::checked_matches(node)) return false;
            } else if (name == "default") {
                if (!css::default_matches(document,node)) return false;
            } else if (name == "indeterminate") {
                if (!css::indeterminate_matches(document,node)) return false;
            } else if (name == "in-range") {
                if (forms::range_state(node) != forms::numeric_range_state::in_range) return false;
            } else if (name == "out-of-range") {
                if (forms::range_state(node) != forms::numeric_range_state::out_of_range) return false;
            } else if (name == "hover" || name == "active" || name == "focus"
                || name == "focus-visible" || name == "focus-within") {
                if (!css::interaction_matches(document,node,name,
                    host.selector_interaction_state(),host.is_text_control(&node))) return false;
            } else if (name == "state") {
                if constexpr (requires { host.custom_state_matches(node, argument); }) {
                    if (!host.custom_state_matches(node, argument)) return false;
                } else {
                    return false;
                }
            } else if (name == "not") {
                if (pseudo.compiled_argument != nullptr) {
                    if (!pseudo.compiled_argument_valid
                        || host.css_compiled_selector_list_matches(
                            node, *pseudo.compiled_argument, scope_root)) return false;
                } else if (css_selector_list_any(argument, [&](std::string_view item) {
                    return host.css_selector_matches(node, item, scope_root);
                })) return false;
            } else if (name == "is" || name == "where") {
                if (pseudo.compiled_argument != nullptr) {
                    if (!pseudo.compiled_argument_valid
                        || !host.css_compiled_selector_list_matches(
                            node, *pseudo.compiled_argument, scope_root)) return false;
                } else if (!css_selector_list_any(argument, [&](std::string_view item) {
                    return host.css_selector_matches(node, item, scope_root);
                })) return false;
            } else if (name == "has") {
                const auto any = pseudo.compiled_argument != nullptr
                    ? pseudo.compiled_argument_valid
                        && host.css_compiled_relative_selector_list_matches(
                            node, *pseudo.compiled_argument)
                    : css_selector_list_any(argument, [&](std::string_view relative) {
                        return !relative.empty()
                            && host.css_relative_selector_matches(node, relative);
                    });
                if (!any) return false;
            } else {
                // Stateful and vendor pseudo-classes are not active unless the
                // native DOM explicitly models that state.  Matching them as
                // the base element applies hover/focus/active CSS constantly.
                return false;
            }
        }
        return true;
    }

} // namespace webscene_native::css
