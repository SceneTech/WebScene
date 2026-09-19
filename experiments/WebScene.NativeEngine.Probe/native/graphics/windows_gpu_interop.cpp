#include "../webscene_native_engine.h"
#if defined(_WIN32) && defined(WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS)
#include "d3d11_scene_consumer.h"
#include "d3d12_shared_image_consumer.h"
#include "windows_gpu_adapter.h"
#endif

int32_t webscene_gpu_d3d11_supported_v3(void* device) {
#if defined(_WIN32) && defined(WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS)
    if(!device)return E_INVALIDARG;
    try {
        auto host=static_cast<ID3D11Device*>(device);
        webscene::graphics::adapter_luid actual;
        auto status=webscene::graphics::query_adapter_luid(host,actual);if(FAILED(status))return status;
        auto selected=webscene::graphics::windows_gpu_adapter_luid();
        if(actual.low!=selected.LowPart||actual.high!=selected.HighPart)return DXGI_ERROR_UNSUPPORTED;
        Microsoft::WRL::ComPtr<ID3D11Device5> device5;
        status=host->QueryInterface(IID_PPV_ARGS(&device5));if(FAILED(status))return status;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;host->GetImmediateContext(&context);
        Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context4;
        status=context.As(&context4);if(FAILED(status))return status;
        Microsoft::WRL::ComPtr<ID3D11Fence> fence;
        return device5->CreateFence(0,D3D11_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence));
    }catch(...){return E_FAIL;}
#else
    return static_cast<int32_t>(0x80004001U);
#endif
}

int32_t webscene_gpu_d3d11_import_v3(webscene_gpu_image_consumer_v3* consumer,
    void* device,void** owner,void** texture) {
    if(owner)*owner=nullptr;if(texture)*texture=nullptr;
#if defined(_WIN32) && defined(WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS)
    if(!owner||!texture)return E_INVALIDARG;
    try {
        std::unique_ptr<webscene::graphics::d3d11_scene_consumer> candidate;
        const auto status=webscene::graphics::d3d11_scene_consumer::create(consumer,
            static_cast<ID3D11Device*>(device),candidate);
        if(FAILED(status))return status;
        *texture=candidate->texture();*owner=candidate.release();return S_OK;
    }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
    catch(...){return E_INVALIDARG;}
#else
    return static_cast<int32_t>(0x80004001U); // E_NOTIMPL; no graphics dependency.
#endif
}
int32_t webscene_gpu_d3d11_seal_v3(void* owner) {
#if defined(_WIN32) && defined(WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS)
    return owner?static_cast<webscene::graphics::d3d11_scene_consumer*>(owner)->seal():E_INVALIDARG;
#else
    return static_cast<int32_t>(0x80004001U);
#endif
}
int32_t webscene_gpu_d3d11_poll_v3(void* owner) {
#if defined(_WIN32) && defined(WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS)
    return owner?static_cast<webscene::graphics::d3d11_scene_consumer*>(owner)->poll():E_INVALIDARG;
#else
    return static_cast<int32_t>(0x80004001U);
#endif
}
void webscene_gpu_d3d11_destroy_v3(void* owner) {
#if defined(_WIN32) && defined(WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS)
    delete static_cast<webscene::graphics::d3d11_scene_consumer*>(owner);
#endif
}

int32_t webscene_gpu_d3d12_acquire_shared_v3(
    const webscene_gpu_image_consumer_v3* consumer,void** owner,
    webscene_gpu_d3d12_shared_image_view_v3* result) {
    if (owner) *owner=nullptr;
#if defined(_WIN32) && defined(WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS)
    if (!owner || !result || result->struct_size<sizeof(*result) || result->version!=3)
        return E_INVALIDARG;
    result->capabilities=0;
    result->borrowed_texture_handle=nullptr;
    result->allocation_bytes=0;
    result->allocation=0;
    result->allocation_generation=0;
    result->content_serial=0;
    result->width=result->height=0;
    result->format=result->alpha=result->color_space=result->orientation=0;
    result->adapter_luid_low=0;
    result->adapter_luid_high=0;
    result->producer_fence_count=0;
    result->reserved=0;
    try {
        std::unique_ptr<webscene::graphics::d3d12_shared_image_consumer> candidate;
        const auto status=webscene::graphics::d3d12_shared_image_consumer::create(consumer,candidate);
        if (FAILED(status)) return status;
        const auto metadata=candidate->metadata();
        const auto adapter=candidate->adapter();
        const auto fence_count=candidate->fence_count();
        result->capabilities=WEBSCENE_GPU_D3D12_SHARED_NT_HANDLE_V3
            |WEBSCENE_GPU_D3D12_SHARED_TEXTURE2D_V3
            |WEBSCENE_GPU_D3D12_SHARED_SIMULTANEOUS_ACCESS_V3
            |WEBSCENE_GPU_D3D12_SHARED_EXPLICIT_RETIREMENT_V3;
        if (fence_count) result->capabilities|=WEBSCENE_GPU_D3D12_SHARED_PRODUCER_FENCES_V3;
        result->borrowed_texture_handle=candidate->texture_handle();
        result->allocation_bytes=candidate->allocation_bytes();
        result->allocation=metadata.allocation;
        result->allocation_generation=metadata.allocation_generation;
        result->content_serial=metadata.content_serial;
        result->width=metadata.width;
        result->height=metadata.height;
        result->format=static_cast<uint32_t>(metadata.format);
        result->alpha=static_cast<uint32_t>(metadata.alpha);
        result->color_space=static_cast<uint32_t>(metadata.color_space);
        result->orientation=static_cast<uint32_t>(metadata.orientation);
        result->adapter_luid_low=adapter.low;
        result->adapter_luid_high=adapter.high;
        result->producer_fence_count=static_cast<uint32_t>(fence_count);
        *owner=candidate.release();
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (const std::system_error& error) {
        const auto value=static_cast<DWORD>(error.code().value());
        return value ? HRESULT_FROM_WIN32(value) : E_FAIL;
      } catch (const std::invalid_argument&) { return E_INVALIDARG; }
      catch (...) { return E_FAIL; }
#else
    return static_cast<int32_t>(0x80004001U); // E_NOTIMPL; no graphics dependency.
#endif
}

uint8_t webscene_gpu_d3d12_get_producer_fence_v3(
    const void* owner,uint32_t index,webscene_gpu_dxgi_fence_view_v3* result) {
    if (!result || result->struct_size<sizeof(*result) || result->version!=3) return 0;
    result->borrowed_fence_handle=nullptr;
    result->signaled_value=0;
#if defined(_WIN32) && defined(WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS)
    if (!owner) return 0;
    const auto image=static_cast<const webscene::graphics::d3d12_shared_image_consumer*>(owner);
    if (index>=image->fence_count()) return 0;
    result->borrowed_fence_handle=image->fence_handle(index);
    result->signaled_value=image->fence_value(index);
    return 1;
#else
    return 0;
#endif
}

void webscene_gpu_d3d12_release_shared_v3(void* owner) {
#if defined(_WIN32) && defined(WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS)
    delete static_cast<webscene::graphics::d3d12_shared_image_consumer*>(owner);
#endif
}
