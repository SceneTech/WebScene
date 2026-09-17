#if defined(WEBSCENE_TEST_LEGACY_SPECIFIED_VALUE)
#include "webscene_css_specified_value.h"
#else
#include "webscene_css_specified_ir.h"
#endif
#include "generated/webscene_css_supported_properties.inc"

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
    static_assert(native_typed_property_identity_catalog.size() == 201U);
    static_assert(native_storage_only_property_catalog.size() == 54U);
    static_assert(cssom_supported_property_catalog.size() == 214U);

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
    std::cout << "CSS property identity tests passed";
#if defined(WEBSCENE_TEST_LEGACY_SPECIFIED_VALUE)
    std::cout << " (legacy specified-value header)";
#endif
    std::cout << '\n';
    return 0;
}
