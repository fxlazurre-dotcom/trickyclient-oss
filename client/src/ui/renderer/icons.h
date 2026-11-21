// TrickyClient OSS — ui layer: menu icon upload (decode embedded PNGs into
// SRV slots of the shared shader-visible heap)
#pragma once
#include <imgui.h>

struct ID3D12Device;
struct ID3D12DescriptorHeap;
struct D3D12_CPU_DESCRIPTOR_HANDLE;
struct D3D12_GPU_DESCRIPTOR_HANDLE;
struct ID3D12CommandQueue;

namespace mc {

// decode embedded icons and place SRVs at slots [first_slot ..] of the shared
// shader-visible heap; executes a one-shot upload on the presenter queue
bool clickgui_load_textures(ID3D12Device* dev, ID3D12CommandQueue* queue,
                            ID3D12DescriptorHeap* srv_heap,
                            const D3D12_CPU_DESCRIPTOR_HANDLE& cpu_start,
                            const D3D12_GPU_DESCRIPTOR_HANDLE& gpu_start,
                            unsigned slot_step, unsigned first_slot);

} // namespace mc
