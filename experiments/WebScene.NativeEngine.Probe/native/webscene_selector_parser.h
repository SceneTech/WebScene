#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace webscene_native {

struct selector_syntax_attribute final {
    std::string local_name;
    // Zero is any namespace; one is the exact namespace_url (including empty).
    uint8_t namespace_kind{0};
    std::string namespace_url;
    // Zero is presence, followed by =, ~=, |=, ^=, *=, and $=.
    uint8_t operator_kind{0};
    std::string value;
    // Zero is sensitive, one ASCII-insensitive, two explicit-sensitive, and
    // three follows HTML enumerated-attribute matching rules.
    uint8_t case_sensitivity{0};
};

struct selector_syntax_selector final {
    std::string serialized;
    uint32_t specificity{0};
    std::vector<std::string> compounds;
    std::vector<std::vector<selector_syntax_attribute>> attributes;
    std::vector<char> combinators;
};

struct selector_syntax_metrics final {
    uint64_t duration_ns{0};
    uint64_t rust_allocation_count{0};
    uint64_t rust_peak_bytes{0};
    uint64_t rust_retained_bytes{0};
    bool compilation_cache_hit{false};
};

struct selector_syntax_output final {
    std::vector<selector_syntax_selector> selectors;
    selector_syntax_metrics metrics;
    std::string error;

    explicit operator bool() const noexcept { return error.empty(); }
};

struct selector_namespace_context final {
    std::string default_namespace;
    std::unordered_map<std::string, std::string> prefixes;
    bool has_default_namespace{false};

    std::string cache_key() const;
};

void set_selector_syntax_compilation_cache_directory(std::string directory);
void clear_selector_syntax_process_cache();
uint64_t selector_syntax_process_cache_hits() noexcept;
uint64_t selector_syntax_persistent_cache_hits() noexcept;
uint64_t selector_syntax_compilation_count() noexcept;

selector_syntax_output parse_selector_syntax(std::string_view input);
selector_syntax_output parse_selector_syntax(
    std::string_view input,
    const selector_namespace_context& namespaces);

} // namespace webscene_native
