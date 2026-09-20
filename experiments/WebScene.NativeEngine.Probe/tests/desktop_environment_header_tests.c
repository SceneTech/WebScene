#include "webscene_native_engine.h"

#include <stddef.h>
#include <stdint.h>

_Static_assert(WEBSCENE_DESKTOP_ENVIRONMENT_VERSION_1 == 1U,
    "desktop environment version changed");
_Static_assert(sizeof(webscene_desktop_environment_v1) == 64U,
    "desktop environment ABI size changed");
_Static_assert(offsetof(webscene_desktop_environment_v1, display_generation) == 32U,
    "desktop environment display generation offset changed");
_Static_assert(offsetof(webscene_desktop_environment_v1, highlight_text_rgba) == 60U,
    "desktop environment color layout changed");

int main(void)
{
    webscene_desktop_environment_v1 environment = {0};
    environment.struct_size = sizeof(environment);
    environment.version = WEBSCENE_DESKTOP_ENVIRONMENT_VERSION_1;
    environment.flags = WEBSCENE_DESKTOP_ENVIRONMENT_DISPLAY_AVAILABLE_V1;
    environment.preferred_color_scheme = WEBSCENE_PREFERRED_COLOR_SCHEME_LIGHT;
    environment.dpi_x = 96U;
    environment.dpi_y = 96U;
    environment.scale_milli = 1000U;
    return environment.reserved0 == 0U ? 0 : 1;
}
