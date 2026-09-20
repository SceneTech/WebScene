#pragma once

#include "webscene_native_engine.h"

#include <cstdint>
#include <optional>

namespace webscene_native {

struct pointer_input_metadata final {
    webscene_pointer_device_kind device{WEBSCENE_POINTER_DEVICE_MOUSE};
    std::uint32_t pointer_id{1U};
    bool primary{true};
    bool legacy{};
};

inline std::optional<pointer_input_metadata> decode_pointer_input_metadata(
    std::uint32_t flags) noexcept
{
    const auto version = (flags & WEBSCENE_INPUT_POINTER_METADATA_VERSION_MASK)
        >> WEBSCENE_INPUT_POINTER_METADATA_VERSION_SHIFT;
    if (version == 0U) {
        return pointer_input_metadata{
            WEBSCENE_POINTER_DEVICE_MOUSE, 1U, true, true};
    }
    if (version != 1U) return std::nullopt;
    const auto device = (flags & WEBSCENE_INPUT_POINTER_DEVICE_MASK)
        >> WEBSCENE_INPUT_POINTER_DEVICE_SHIFT;
    const auto pointer_id = (flags & WEBSCENE_INPUT_POINTER_ID_MASK)
        >> WEBSCENE_INPUT_POINTER_ID_SHIFT;
    if (device > WEBSCENE_POINTER_DEVICE_PEN || pointer_id == 0U)
        return std::nullopt;
    return pointer_input_metadata{
        static_cast<webscene_pointer_device_kind>(device),
        pointer_id,
        (flags & WEBSCENE_INPUT_POINTER_PRIMARY) != 0U,
        false};
}

inline const char* pointer_device_name(
    webscene_pointer_device_kind device) noexcept
{
    switch (device) {
    case WEBSCENE_POINTER_DEVICE_TOUCH: return "touch";
    case WEBSCENE_POINTER_DEVICE_PEN: return "pen";
    default: return "mouse";
    }
}

inline constexpr bool is_contact_pointer(
    webscene_pointer_device_kind device) noexcept
{
    return device == WEBSCENE_POINTER_DEVICE_TOUCH
        || device == WEBSCENE_POINTER_DEVICE_PEN;
}

static_assert((WEBSCENE_INPUT_POINTER_METADATA_VERSION_MASK
    | WEBSCENE_INPUT_POINTER_DEVICE_MASK
    | WEBSCENE_INPUT_POINTER_PRIMARY
    | WEBSCENE_INPUT_POINTER_ID_MASK) == 0xff800000U);
static_assert(((WEBSCENE_INPUT_POINTER_METADATA_VERSION_MASK
    | WEBSCENE_INPUT_POINTER_DEVICE_MASK
    | WEBSCENE_INPUT_POINTER_PRIMARY
    | WEBSCENE_INPUT_POINTER_ID_MASK) & 0x007fffffU) == 0U);

} // namespace webscene_native
