#include "webscene_pointer_input.h"

#include <cassert>
#include <string_view>

using namespace webscene_native;

int main()
{
    static_assert(sizeof(webscene_input_event) == 48U);
    const auto legacy = decode_pointer_input_metadata(0U);
    assert(legacy.has_value());
    assert(legacy->legacy);
    assert(legacy->device == WEBSCENE_POINTER_DEVICE_MOUSE);
    assert(legacy->pointer_id == 1U && legacy->primary);

    const auto flags = WEBSCENE_INPUT_POINTER_METADATA_VERSION_1
        | (WEBSCENE_POINTER_DEVICE_TOUCH
            << WEBSCENE_INPUT_POINTER_DEVICE_SHIFT)
        | WEBSCENE_INPUT_POINTER_PRIMARY
        | (7U << WEBSCENE_INPUT_POINTER_ID_SHIFT);
    const auto touch = decode_pointer_input_metadata(flags);
    assert(touch.has_value() && !touch->legacy);
    assert(touch->device == WEBSCENE_POINTER_DEVICE_TOUCH);
    assert(touch->pointer_id == 7U && touch->primary);
    assert(std::string_view{pointer_device_name(touch->device)} == "touch");

    assert(!decode_pointer_input_metadata(
        2U << WEBSCENE_INPUT_POINTER_METADATA_VERSION_SHIFT));
    assert(!decode_pointer_input_metadata(
        WEBSCENE_INPUT_POINTER_METADATA_VERSION_1));
    assert(!decode_pointer_input_metadata(
        WEBSCENE_INPUT_POINTER_METADATA_VERSION_1
            | (3U << WEBSCENE_INPUT_POINTER_DEVICE_SHIFT)
            | (1U << WEBSCENE_INPUT_POINTER_ID_SHIFT)));
}
