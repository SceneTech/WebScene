#pragma once
#if defined(_WIN32)
#include "d3d12_canvas_images.h"
#include "image_lease_abi.h"
#include "nt_handle.h"
#include <vector>

namespace webscene::graphics {
// Immutable import snapshot for a D3D12/Dawn consumer. The producer allocation
// remains anchored by the separate image consumer; this object owns only the NT
// handle duplicates needed to import that allocation and its producer fences.
class d3d12_shared_image_consumer {
    struct fence_wait {
        owned_nt_handle handle;
        uint64_t value{};
    };
    owned_nt_handle texture_;
    std::vector<fence_wait> waits_;
    image_metadata metadata_{};
    adapter_luid adapter_{};
    uint64_t allocation_bytes_{};
public:
    HANDLE texture_handle() const noexcept { return texture_.get(); }
    size_t fence_count() const noexcept { return waits_.size(); }
    HANDLE fence_handle(size_t index) const noexcept { return waits_[index].handle.get(); }
    uint64_t fence_value(size_t index) const noexcept { return waits_[index].value; }
    const image_metadata& metadata() const noexcept { return metadata_; }
    adapter_luid adapter() const noexcept { return adapter_; }
    uint64_t allocation_bytes() const noexcept { return allocation_bytes_; }

    static HRESULT create(const webscene_gpu_image_consumer_v3* image,
        std::unique_ptr<d3d12_shared_image_consumer>& output) {
        if (!image || output) return E_INVALIDARG;
        const auto& source=d3d12_canvas_images::resolve(image->value);
        auto candidate=std::make_unique<d3d12_shared_image_consumer>();
        candidate->texture_=owned_nt_handle::duplicate(source.borrowed_handle());
        candidate->metadata_=image->value.describe();
        candidate->adapter_=source.adapter();
        candidate->allocation_bytes_=source.allocation_bytes();
        if (!candidate->adapter_.valid) return E_INVALIDARG;
        if (image->dependencies) {
            const auto count=image->dependencies->count();
            if (count>UINT32_MAX || count>candidate->waits_.max_size()) return E_OUTOFMEMORY;
            candidate->waits_.reserve(count);
            for (size_t index=0;index<count;++index) {
                void* borrowed=nullptr;
                uint64_t value=0;
                if (!image->dependencies->dxgi_fence(index,borrowed,value)
                    || !win32_nt_handle_ops::valid(static_cast<HANDLE>(borrowed)))
                    return E_INVALIDARG;
                candidate->waits_.push_back(
                    {owned_nt_handle::duplicate(static_cast<HANDLE>(borrowed)),value});
            }
        }
        output=std::move(candidate);
        return S_OK;
    }
};
} // namespace webscene::graphics
#endif
