#if defined(WEBSCENE_TEST_LEGACY_SPECIFIED_VALUE)
#include "webscene_css_specified_value.h"
#else
#include "webscene_css_specified_ir.h"
#include "webscene_css_specified_coverage.h"
#include "webscene_css_specified_serialization.h"
#endif
#include "generated/webscene_css_supported_properties.inc"
#if !defined(WEBSCENE_TEST_LEGACY_SPECIFIED_VALUE)
#include "webscene_css_property_mask.h"
#include "webscene_css_variables.h"
#endif

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
#include <string_view>

using namespace webscene_native::css;

namespace {

std::atomic<size_t> allocation_count{0U};

void require(bool condition, std::string_view message, std::string_view name = {})
{
    if (condition) return;
    std::cerr << "CSS property identity test failed: " << message;
    if (!name.empty()) std::cerr << " (" << name << ')';
    std::cerr << '\n';
    std::exit(1);
}

bool is_typed(std::string_view name)
{
    return std::ranges::any_of(
        native_typed_property_identity_catalog,
        [name](const auto& entry) { return entry.name == name; });
}

bool is_storage_only(std::string_view name)
{
    return std::ranges::find(native_storage_only_property_catalog, name)
        != native_storage_only_property_catalog.end();
}

std::string ascii_upper(std::string_view value)
{
    std::string result(value);
    for (auto& character : result) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - ('a' - 'A'));
        }
    }
    return result;
}

} // namespace

void* operator new(std::size_t size)
{
    allocation_count.fetch_add(1U, std::memory_order_relaxed);
    if (auto* pointer = std::malloc(size)) return pointer;
    throw std::bad_alloc{};
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* pointer) noexcept
{
    std::free(pointer);
}

void operator delete[](void* pointer) noexcept
{
    std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}

int main()
{
    static_assert(static_cast<uint16_t>(css_property_id::unknown) == 0U);
    static_assert(static_cast<uint16_t>(css_property_id::custom) == 1U);
    static_assert(static_cast<uint16_t>(css_property_id::contain_intrinsic_size) == 145U);
    static_assert(static_cast<uint16_t>(css_property_id::object_position) == 148U);
    static_assert(static_cast<uint16_t>(css_property_id::user_select) == 149U);
    static_assert(static_cast<uint16_t>(css_property_id::overscroll_behavior) == 150U);
    static_assert(static_cast<uint16_t>(css_property_id::overscroll_behavior_x) == 151U);
    static_assert(static_cast<uint16_t>(css_property_id::overscroll_behavior_y) == 152U);
    static_assert(static_cast<uint16_t>(css_property_id::isolation) == 153U);
    static_assert(static_cast<uint16_t>(css_property_id::will_change) == 154U);
    static_assert(static_cast<uint16_t>(css_property_id::text_wrap) == 155U);
    static_assert(static_cast<uint16_t>(css_property_id::caret_color) == 156U);
    static_assert(static_cast<uint16_t>(css_property_id::touch_action) == 159U);
    static_assert(native_typed_property_identity_catalog.size() == 235U);
    static_assert(native_storage_only_property_catalog.size() == 62U);
    static_assert(cssom_supported_property_catalog.size() == 257U);
    static_assert(cssom_style_template_property_accessor_count == 470U);

    for (const auto& entry : native_typed_property_identity_catalog) {
        require(property_id(entry.name) == entry.id, "typed name maps to its generated id", entry.name);
        const auto upper = ascii_upper(entry.name);
        require(property_id(upper) == entry.id, "ASCII case folding preserves the generated id", entry.name);
    }

    allocation_count.store(0U, std::memory_order_relaxed);
    uint64_t identity_checksum = 0U;
    for (const auto& entry : native_typed_property_identity_catalog) {
        identity_checksum += static_cast<uint16_t>(property_id(entry.name));
    }
    const auto lowercase_lookup_allocations = allocation_count.load(std::memory_order_relaxed);
    require(identity_checksum != 0U, "typed lookup checksum remains observable");
    require(lowercase_lookup_allocations == 0U,
        "lower-case property lookup stays allocation-free");

    for (const auto name : native_storage_only_property_catalog) {
        require(property_id(name) == css_property_id::unknown,
            "storage-only name is not silently promoted to a typed property", name);
    }

    for (const auto& entry : cssom_supported_property_catalog) {
        const auto typed = is_typed(entry.css_name);
        const auto storage_only = is_storage_only(entry.css_name);
        require(typed != storage_only,
            "every exposed CSSOM name has exactly one native classification", entry.css_name);
    }

    require(property_id("--Theme") == css_property_id::custom,
        "custom properties retain their case-sensitive token identity");
    require(property_id("definitely-not-a-property") == css_property_id::unknown,
        "unknown names remain unknown");
#if !defined(WEBSCENE_TEST_LEGACY_SPECIFIED_VALUE)
    const std::vector<std::string> ordered_variables{"--theme", "--accent"};
    const std::unordered_set<std::string> indexed_variables{"--theme", "--accent"};
    require(variable_collection_contains(ordered_variables, std::string{"--accent"})
            && !variable_collection_contains(ordered_variables, std::string{"--missing"})
            && variable_collection_contains(indexed_variables, std::string{"--accent"})
            && !variable_collection_contains(indexed_variables, std::string{"--missing"}),
        "variable membership supports ordered and indexed containers");

    std::array<bool, static_cast<size_t>(css_property_id::touch_action) + 1U>
        audited_mask_ids{};
    for (const auto& entry : native_typed_property_identity_catalog) {
        const auto index = static_cast<size_t>(entry.id);
        if (audited_mask_ids[index]) continue;
        audited_mask_ids[index] = true;
        require((property_mask(entry.name) != 0U)
                == generated_property_has_modeled_mask(entry.id),
            "native mask coverage agrees with explicit property classification",
            entry.name);
    }
    require(native_inherited_property_catalog.size() == 24U,
        "native inherited-property classification remains complete");
    require(generated_property_inherits_by_default("color")
            && generated_property_inherits_by_default("direction")
            && generated_property_inherits_by_default("text-wrap")
            && generated_property_inherits_by_default("caret-color")
            && generated_property_inherits_by_default("-webkit-font-smoothing")
            && !generated_property_inherits_by_default("display")
            && !generated_property_inherits_by_default("touch-action")
            && !generated_property_inherits_by_default("width"),
        "generated inheritance lookup preserves inherited and non-inherited sentinels");
    for (const auto& entry : effective_property_metadata_catalog) {
        require(property_mask(entry.name) != 0U,
            "every effective-property metadata entry has modeled storage", entry.name);
        require(std::ranges::any_of(
                cssom_supported_property_catalog,
                [&](const auto& supported) { return supported.css_name == entry.name; }),
            "every effective-property metadata entry is CSSOM-exposed", entry.name);
    }
#endif
    for (size_t index = 2U; index < native_property_grammar_catalog.size(); ++index) {
        const auto property = static_cast<css_property_id>(index);
        const auto grammar = generated_property_grammar(property);
        std::string_view value;
        specified_css_kind expected{specified_css_kind::invalid};
        switch (grammar) {
        case native_property_grammar::keyword:
            value = "auto"; expected = specified_css_kind::keyword; break;
        case native_property_grammar::component_list:
            value = "auto"; expected = specified_css_kind::component_list; break;
        case native_property_grammar::length:
            value = "1px"; expected = specified_css_kind::length; break;
        case native_property_grammar::length_list_4:
        case native_property_grammar::length_list_2:
            value = "1px"; expected = specified_css_kind::length_list; break;
        case native_property_grammar::color:
            value = "red"; expected = specified_css_kind::color; break;
        case native_property_grammar::complex:
        case native_property_grammar::special:
            continue;
        }
        const auto compiled = compile_specified_value(property, value);
        require(compiled.fully_typed() && compiled.kind == expected,
            "generated simple grammar dispatch agrees with specified-value implementation");
    }
#if !defined(WEBSCENE_TEST_LEGACY_SPECIFIED_VALUE)
    static_assert(specified_property_samples.size() == 161U);
    std::array<bool, static_cast<size_t>(css_property_id::touch_action) + 1U> sampled{};
    for (const auto& sample : specified_property_samples) {
        const auto property = property_id(sample.name);
        const auto index = static_cast<size_t>(property);
        require(index > 1U && index < sampled.size(),
            "specified-value sample has a typed property", sample.name);
        require(!sampled[index], "specified-value sample is unique by property id", sample.name);
        sampled[index] = true;
        const auto compiled = compile_specified_value(property, sample.value);
        require(compiled.fully_typed(), "specified-value sample compiles", sample.name);
        const auto bytes = encode_specified_value(compiled);
        const auto decoded = decode_specified_value(bytes);
        require(decoded.fully_typed() && decoded.kind == compiled.kind,
            "specified-value sample round-trips its serialization kind", sample.name);
        require(encode_specified_value(decoded) == bytes,
            "specified-value serialization is byte-stable after decode", sample.name);
        if (compiled.kind == specified_css_kind::wide_keyword
            || compiled.kind == specified_css_kind::deferred) continue;
        switch (generated_property_grammar(property)) {
        case native_property_grammar::keyword:
            require(compiled.kind == specified_css_kind::keyword,
                "keyword metadata agrees with compiled IR", sample.name); break;
        case native_property_grammar::component_list:
            require(compiled.kind == specified_css_kind::component_list,
                "component-list metadata agrees with compiled IR", sample.name); break;
        case native_property_grammar::length:
            require(compiled.kind == specified_css_kind::length,
                "length metadata agrees with compiled IR", sample.name); break;
        case native_property_grammar::length_list_4:
        case native_property_grammar::length_list_2:
            require(compiled.kind == specified_css_kind::length_list,
                "length-list metadata agrees with compiled IR", sample.name); break;
        case native_property_grammar::color:
            require(compiled.kind == specified_css_kind::color,
                "color metadata agrees with compiled IR", sample.name); break;
        case native_property_grammar::complex:
        case native_property_grammar::special:
            break;
        }
    }
    require(std::ranges::all_of(
        sampled.begin() + 2U, sampled.end(), [](bool value) { return value; }),
        "every typed property id has specified-value and serialization coverage");
    require(specified_ir_schema_complete(), "complete specified-value schema remains valid");
#endif
    std::cout << "CSS property identity tests passed";
#if defined(WEBSCENE_TEST_LEGACY_SPECIFIED_VALUE)
    std::cout << " (legacy specified-value header)";
#endif
    std::cout << '\n';
    return 0;
}
