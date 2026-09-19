#include "webscene_native_engine.h"
#include <stddef.h>
_Static_assert(sizeof(webscene_scene_acquire_options_v3)==16,"scene options wire size");
_Static_assert(offsetof(webscene_scene_acquire_options_v3,consumer_capabilities)==8,"scene capability alignment");
_Static_assert(sizeof(webscene_scene_acquire_status)==4,"scene status wire size");
_Static_assert(offsetof(webscene_scene_view_v3,required_capabilities)==8,"scene capability prefix");
_Static_assert(offsetof(webscene_scene_view_v3,cpu_view)==16,"scene CPU view prefix");
_Static_assert(offsetof(webscene_scene_view_v3,lease_token)==16+sizeof(void*),"scene lease offset");
_Static_assert(sizeof(webscene_scene_view_v3)==16+2*sizeof(void*),"scene view wire size");
_Static_assert(sizeof(webscene_gpu_image_info_v3)==80,"image metadata wire size");
_Static_assert(offsetof(webscene_gpu_image_info_v3,canvas)==8,"image identity alignment");
_Static_assert(offsetof(webscene_gpu_image_info_v3,width)==56,"image dimensions offset");
_Static_assert(offsetof(webscene_gpu_metal_event_view_v3,borrowed_shared_event)==8,"Metal event pointer prefix");
_Static_assert(offsetof(webscene_gpu_d3d12_shared_image_view_v3,capabilities)==8,"D3D12 capability alignment");
_Static_assert(offsetof(webscene_gpu_d3d12_shared_image_view_v3,borrowed_texture_handle)==16,"D3D12 handle prefix");
_Static_assert(offsetof(webscene_gpu_d3d12_shared_image_view_v3,allocation_generation)==40,"D3D12 generation offset");
_Static_assert(offsetof(webscene_gpu_d3d12_shared_image_view_v3,width)==56,"D3D12 dimensions offset");
_Static_assert(offsetof(webscene_gpu_d3d12_shared_image_view_v3,producer_fence_count)==88,"D3D12 fence count offset");
_Static_assert(offsetof(webscene_gpu_dxgi_fence_view_v3,borrowed_fence_handle)==8,"DXGI fence handle prefix");
_Static_assert(sizeof(webscene_gpu_linux_shared_status_v3)==4,"Linux shared status wire size");
_Static_assert(offsetof(webscene_gpu_linux_shared_image_view_v3,capabilities)==8,"Linux capability alignment");
_Static_assert(offsetof(webscene_gpu_linux_shared_image_view_v3,allocation)==32,"Linux allocation identity offset");
_Static_assert(offsetof(webscene_gpu_linux_shared_image_view_v3,producer_timeline)==56,"Linux producer timeline offset");
_Static_assert(offsetof(webscene_gpu_linux_shared_image_view_v3,width)==72,"Linux image dimensions offset");
_Static_assert(offsetof(webscene_gpu_linux_shared_image_view_v3,memory_handle_type)==96,"Linux memory type offset");
_Static_assert(offsetof(webscene_gpu_linux_shared_image_view_v3,vk_sharing_mode)==132,"Linux Vulkan sharing offset");
_Static_assert(offsetof(webscene_gpu_linux_shared_image_view_v3,producer_layout)==156,"Linux layout offset");
_Static_assert(offsetof(webscene_gpu_linux_shared_image_view_v3,vk_queue_family_indices)==172,"Linux create queue families offset");
_Static_assert(offsetof(webscene_gpu_linux_shared_image_view_v3,device_uuid)==188,"Linux device UUID offset");
_Static_assert(offsetof(webscene_gpu_linux_shared_image_view_v3,plane_count)==220,"Linux plane count offset");
_Static_assert(offsetof(webscene_gpu_linux_plane_view_v3,borrowed_fd)==8,"Linux plane FD offset");
_Static_assert(offsetof(webscene_gpu_linux_plane_view_v3,offset)==16,"Linux plane byte offset alignment");
_Static_assert(offsetof(webscene_gpu_linux_sync_view_v3,borrowed_fd)==16,"Linux wait FD offset");
_Static_assert(offsetof(webscene_gpu_linux_sync_view_v3,ordering_domain)==24,"Linux wait domain alignment");
#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(webscene_gpu_metal_event_view_v3)==24,"Metal event view wire size");
_Static_assert(offsetof(webscene_gpu_metal_event_view_v3,signaled_value)==16,"Metal event timeline alignment");
_Static_assert(sizeof(webscene_gpu_d3d12_shared_image_view_v3)==96,"D3D12 shared image wire size");
_Static_assert(sizeof(webscene_gpu_dxgi_fence_view_v3)==24,"DXGI fence view wire size");
_Static_assert(offsetof(webscene_gpu_dxgi_fence_view_v3,signaled_value)==16,"DXGI fence timeline alignment");
_Static_assert(sizeof(webscene_gpu_linux_shared_image_view_v3)==232,"Linux shared image wire size");
_Static_assert(sizeof(webscene_gpu_linux_plane_view_v3)==24,"Linux plane wire size");
_Static_assert(sizeof(webscene_gpu_linux_sync_view_v3)==40,"Linux wait wire size");
#endif
int main(void) { return WEBSCENE_SCENE_VIEW_VERSION_3==3U ? 0 : 1; }
