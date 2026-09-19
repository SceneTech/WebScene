#pragma once

namespace dawn::native {
class DeviceBase;
}

namespace webscene::dawn_bridge {
void register_device(dawn::native::DeviceBase*) noexcept;
void unregister_device(dawn::native::DeviceBase*) noexcept;
}
