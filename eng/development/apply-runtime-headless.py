#!/usr/bin/env python3
"""One-shot branch edit transport; removed after applying the reviewed changes."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
NATIVE = ROOT / 'experiments/WebScene.NativeEngine.Probe/native'


def replace(path, before, after, count=1):
    path = ROOT / path
    text = path.read_text()
    found = text.count(before)
    if found != count:
        raise RuntimeError(f'{path}: expected {count} anchors, found {found}: {before[:100]}')
    path.write_text(text.replace(before, after))


def graphics(name):
    return 'experiments/WebScene.NativeEngine.Probe/native/graphics/' + name


replace(graphics('webgpu_canvas_interop.h'), 'none,iosurface,dxgi', 'none,iosurface,dxgi,offscreen')
replace(graphics('platform_webgpu_canvas.h'), '\n#endif\n', '''
#elif defined(__linux__)
#include "dawn_offscreen_canvas_host.h"
namespace webscene::graphics {
using platform_dawn_canvas_host=dawn_offscreen_canvas_host;
using platform_dawn_scene_snapshot=dawn_offscreen_scene_snapshot;
inline constexpr auto platform_canvas_interop=webgpu_canvas_interop::offscreen;
inline constexpr auto platform_canvas_backend=wgpu::BackendType::Vulkan;
inline auto make_platform_webgpu_canvas_host(std::shared_ptr<platform_dawn_canvas_host> provider,
    std::function<image_metadata()> metadata) {return make_offscreen_webgpu_canvas_host(std::move(provider),std::move(metadata));}
}
#endif
''')

images = graphics('dawn_canvas_images.h')
replace(images, '#include <webgpu/webgpu_cpp.h>', '#include <webgpu/webgpu_cpp.h>\n#include <vector>')
replace(images, 'struct slot { wgpu::Texture texture; image_metadata metadata{}; uint64_t bytes{}; };',
        'struct slot { wgpu::Texture texture; image_metadata metadata{}; uint64_t bytes{}; wgpu::TextureUsage usage{}; std::vector<wgpu::TextureFormat> view_formats; };')
replace(images, 'std::optional<frame> acquire(image_metadata metadata) {',
        'std::optional<frame> acquire(image_metadata metadata, const wgpu::TextureDescriptor* requested = nullptr) {')
replace(images, '        const uint64_t pixels=uint64_t(metadata.width)*metadata.height;', '''        // Runtime GPUCanvasConfiguration must retain the requested usage and
        // view formats. CopySrc is an internal capability for explicit capture.
        const auto usage = requested ? requested->usage | wgpu::TextureUsage::CopySrc
            : wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding
                | wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::CopyDst;
        std::vector<wgpu::TextureFormat> view_formats;
        if (requested) {
            if (requested->dimension != wgpu::TextureDimension::e2D || requested->mipLevelCount != 1
                || requested->sampleCount != 1 || requested->size.depthOrArrayLayers != 1
                || requested->size.width != metadata.width || requested->size.height != metadata.height
                || requested->format != format || requested->nextInChain)
                throw std::invalid_argument("Invalid offscreen canvas texture descriptor");
            if (requested->viewFormatCount) {
                if (!requested->viewFormats) throw std::invalid_argument("Missing canvas view formats");
                view_formats.assign(requested->viewFormats, requested->viewFormats + requested->viewFormatCount);
            }
        }
        const uint64_t pixels=uint64_t(metadata.width)*metadata.height;''')
replace(images, '&& slot.metadata.height==metadata.height && slot.metadata.format==metadata.format;',
        '&& slot.metadata.height==metadata.height && slot.metadata.format==metadata.format\n            && slot.usage==usage && slot.view_formats==view_formats;')
replace(images, '''            descriptor.usage=wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding
                | wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::CopyDst;''', '''            descriptor.usage=usage;
            descriptor.viewFormatCount=view_formats.size();
            descriptor.viewFormats=view_formats.data();''')
replace(images, '        slot.metadata=metadata;',
        '        slot.metadata=metadata; slot.usage=usage; slot.view_formats=std::move(view_formats);')
replace(images, '    size_t busy_images() const { return pool_.busy_images(); }',
        '    size_t busy_images() const { return pool_.busy_images(); }\n    image_lease_pool::occupancy inspect_occupancy() const { return pool_.inspect_occupancy(); }')

# Linux offscreen images are captured from a completion/capture thread; request
# Dawn\'s native synchronization capability without exposing it as a JS feature.
prepared = graphics('webgpu_prepared_device_descriptor.h')
replace(prepared, '#include <memory>', '#include <memory>\n#include <cstdlib>\n#include <string_view>')
replace(prepared, '        error=webgpu_device_request_error::none; return result;', '''        if(interop==webgpu_canvas_interop::offscreen) {
            if(!adapter.HasFeature(wgpu::FeatureName::ImplicitDeviceSynchronization)) return {};
            wgpu::AdapterInfo info{};
            if(adapter.GetInfo(&info)!=wgpu::Status::Success) return {};
            const auto* setting=std::getenv("WEBSCENE_HEADLESS_FORCE_SOFTWARE_ADAPTER");
            const bool software=setting && std::string_view(setting)=="1";
            const bool hardware=info.adapterType==wgpu::AdapterType::DiscreteGPU || info.adapterType==wgpu::AdapterType::IntegratedGPU;
            if(software ? info.adapterType!=wgpu::AdapterType::CPU : !hardware) return {};
            features.push_back(wgpu::FeatureName::ImplicitDeviceSynchronization);
        }
        error=webgpu_device_request_error::none; return result;''')
events = graphics('dawn_event_service.h')
replace(events, '    bool closed_{};', '''    bool closed_{};
    static wgpu::Instance create_instance() {
#if defined(__linux__)
        // Offscreen completion/capture uses bounded waits independently of RAF.
        constexpr auto feature=wgpu::InstanceFeatureName::TimedWaitAny;
        wgpu::InstanceDescriptor descriptor{};
        descriptor.requiredFeatureCount=1; descriptor.requiredFeatures=&feature;
        return wgpu::CreateInstance(&descriptor);
#else
        return wgpu::CreateInstance();
#endif
    }''')
replace(events, 'instance_(wgpu::CreateInstance())', 'instance_(create_instance())')

# These shared canvas sections previously had only IOSurface and DXGI hosts.
# Extend the platform alternative, not OS-specific Metal/COM/media sections.
for path in [NATIVE/'webscene_v8_runtime.cpp', *NATIVE.glob('webscene_v8_runtime_*.inc'), NATIVE/'webscene_native_engine_worker.inc']:
    text=path.read_text()
    changed=text.replace('(defined(__APPLE__) || defined(_WIN32))', '(defined(__APPLE__) || defined(_WIN32) || defined(__linux__))')
    changed=changed.replace('#if defined(__APPLE__) || defined(_WIN32)\n', '#if defined(__APPLE__) || defined(_WIN32) || defined(__linux__)\n')
    if changed!=text:
        print('Extended shared graphics guards:',path.relative_to(ROOT))
        path.write_text(changed)

canvas='experiments/WebScene.NativeEngine.Probe/native/webscene_v8_runtime_canvas.inc'
replace(canvas, '                auto provider=std::make_shared<platform_dawn_canvas_host>(64ULL*1024*1024,self->webgpu_wake);', '''                auto provider=std::make_shared<platform_dawn_canvas_host>(64ULL*1024*1024,self->webgpu_wake);
#if defined(__linux__)
                provider->set_instance(self->graphics->dawn().instance());
#endif''')
replace(canvas, '            document.publish_gpu_canvas_image(*canvas.node,std::make_shared<webscene_gpu_image_lease_v3>(std::move(*image)));', '''#if defined(__linux__)
            document.publish_gpu_canvas_image(*canvas.node,std::make_shared<webscene_gpu_image_lease_v3>(std::move(image->image),image->dependencies));
#else
            document.publish_gpu_canvas_image(*canvas.node,std::make_shared<webscene_gpu_image_lease_v3>(std::move(*image)));
#endif''')
runtime='experiments/WebScene.NativeEngine.Probe/native/webscene_v8_runtime.cpp'
replace(runtime, '''#if defined(_WIN32)
            wgpu::BackendType::D3D12,
#else
            wgpu::BackendType::Undefined,''', '''#if defined(_WIN32)
            wgpu::BackendType::D3D12,
#elif defined(__linux__)
            wgpu::BackendType::Vulkan,
#else
            wgpu::BackendType::Undefined,''')
worker='experiments/WebScene.NativeEngine.Probe/native/webscene_native_engine_worker.inc'
replace(worker, '''#else
            return decision==WEBSCENE_WEBGPU_IOSURFACE''', '''#elif defined(__linux__)
            return decision==WEBSCENE_WEBGPU_OFFSCREEN ? webscene::graphics::webgpu_canvas_interop::offscreen : webscene::graphics::webgpu_canvas_interop::none;
#else
            return decision==WEBSCENE_WEBGPU_IOSURFACE''')
api='experiments/WebScene.NativeEngine.Probe/native/webscene_native_engine.h'
replace(api, 'WEBSCENE_WEBGPU_DXGI = 2 };', 'WEBSCENE_WEBGPU_DXGI = 2, WEBSCENE_WEBGPU_OFFSCREEN = 3 };')
replace(api, ' * Current implementation supports IOSURFACE only on graphics-enabled macOS. */',
        ' * OFFSCREEN selects retained same-device Vulkan images on Linux; it is not\n * permission to present to X11/Wayland or to use an untrusted document. */')

cmake='experiments/WebScene.NativeEngine.Probe/CMakeLists.txt'
path=ROOT/cmake
text=path.read_text()
# Only change branches whose body explicitly configures ANGLE. Dawn Vulkan on
# Linux has no EGL dependency. All Windows ANGLE production remains enabled.
text,count=re.subn(r'if\(APPLE\)(\s*\n\s*set\(WEBSCENE_GRAPHICS_ENABLE_ANGLE 0\))',r'if(NOT WIN32)\1',text)
if count!=1: raise RuntimeError('ANGLE configuration anchor not found')
text=text.replace('if(NOT APPLE)\n', 'if(WIN32)\n')
path.write_text(text)

# Route only installed libstdc++ SDKs to their matching launcher. Existing
# LLVM22/libc++ source/installed toolchain behavior is preserved.
toolchain=ROOT/'src/WebScene.Sdk/cmake/WebSceneToolchain.cmake'
text=toolchain.read_text()
toolchain.write_text('''if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/WebSceneLinuxProfile.cmake")
  include("${CMAKE_CURRENT_LIST_DIR}/WebSceneLinuxProfile.cmake")
  if(WebScene_SDK_CXX_ABI STREQUAL "libstdc++")
    include("${CMAKE_CURRENT_LIST_DIR}/WebSceneLinuxSystemToolchain.cmake")
    return()
  endif()
endif()
''' + text)

for name in ['eng/development/apply-runtime-headless.py',
             '.github/workflows/runtime-headless-integrate.yml',
             '.github/workflows/runtime-headless-bootstrap.yml']:
    (ROOT/name).unlink(missing_ok=True)
print('Runtime canvas edits applied; temporary branch transport removed.')
