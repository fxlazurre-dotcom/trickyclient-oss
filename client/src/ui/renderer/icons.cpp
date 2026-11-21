// TrickyClient OSS — ui layer: menu icon upload (moved verbatim; the
// nursultan GUI draws vectorially — these stay for SRV heap layout compat)
#include "ui/renderer/icons.h"
#include "ui/renderer/res_icons.h"
#include "core/log.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "ui/renderer/stb_image.h"

#include <d3d12.h>
#include <cstring>

namespace mc {

enum IconId {
    I_GEAR = 0, I_ARROW_DOWN, I_ARROW_UP, I_COPY, I_PASTE,
    I_TROLL_HEAD, I_TROLL_STRIPES,
    I_TAB_COMBAT, I_TAB_MOVEMENT, I_TAB_VISUALS, I_TAB_MISC, I_TAB_LUA, I_TAB_SKINS,
    I_TB_SEARCH, I_TB_SAVE, I_TB_CONFIGS, I_TB_ABOUT, I_TB_CLOSE,
    ICON_COUNT
};
struct Icon { ImTextureID id = 0; int w = 0, h = 0; };
static Icon g_icons[ICON_COUNT];
static_assert(sizeof(ImTextureID) == 8, "ImTextureID must hold a gpu handle");

bool clickgui_load_textures(ID3D12Device* dev, ID3D12CommandQueue* queue,
                            ID3D12DescriptorHeap* srv_heap,
                            const D3D12_CPU_DESCRIPTOR_HANDLE& cpu_start,
                            const D3D12_GPU_DESCRIPTOR_HANDLE& gpu_start,
                            unsigned slot_step, unsigned first_slot) {
    struct Data { const uint8_t* ptr; size_t len; const char* name; };
    const Data sources[ICON_COUNT] = {
        {tricky::res::icon_gear,          sizeof(tricky::res::icon_gear),          "gear"},
        {tricky::res::icon_arrow_down,    sizeof(tricky::res::icon_arrow_down),    "arrow_down"},
        {tricky::res::icon_arrow_up,      sizeof(tricky::res::icon_arrow_up),      "arrow_up"},
        {tricky::res::icon_copy,          sizeof(tricky::res::icon_copy),          "copy"},
        {tricky::res::icon_paste,         sizeof(tricky::res::icon_paste),         "paste"},
        {tricky::res::icon_troll_head,    sizeof(tricky::res::icon_troll_head),    "troll_head"},
        {tricky::res::icon_troll_stripes,       sizeof(tricky::res::icon_troll_stripes),       "troll_stripes"},
        {tricky::res::icon_tab_combat,    sizeof(tricky::res::icon_tab_combat),    "tab_combat"},
        {tricky::res::icon_tab_movement,  sizeof(tricky::res::icon_tab_movement),  "tab_movement"},
        {tricky::res::icon_tab_visuals,   sizeof(tricky::res::icon_tab_visuals),   "tab_visuals"},
        {tricky::res::icon_tab_misc,      sizeof(tricky::res::icon_tab_misc),      "tab_misc"},
        {tricky::res::icon_tab_lua,       sizeof(tricky::res::icon_tab_lua),       "tab_lua"},
        {tricky::res::icon_tab_skins,     sizeof(tricky::res::icon_tab_skins),     "tab_skins"},
        {tricky::res::icon_tb_search,     sizeof(tricky::res::icon_tb_search),     "tb_search"},
        {tricky::res::icon_tb_save,       sizeof(tricky::res::icon_tb_save),       "tb_save"},
        {tricky::res::icon_tb_configs,    sizeof(tricky::res::icon_tb_configs),    "tb_configs"},
        {tricky::res::icon_tb_about,      sizeof(tricky::res::icon_tb_about),      "tb_about"},
        {tricky::res::icon_tb_close,      sizeof(tricky::res::icon_tb_close),      "tb_close"},
    };

    ID3D12CommandAllocator* alloc = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    HANDLE ev = nullptr;
    static ID3D12Resource* keep_tex[ICON_COUNT] = {};
    ID3D12Resource* ups[ICON_COUNT] = {};
    bool ok = false;
    do {
        if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)))) break;
        if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, IID_PPV_ARGS(&list)))) break;
        if (FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) break;
        ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);

        int loaded = 0;
        for (int i = 0; i < ICON_COUNT; ++i) {
            int w = 0, h = 0, comp = 0;
            stbi_uc* px = stbi_load_from_memory(sources[i].ptr, (int)sources[i].len, &w, &h, &comp, 4);
            if (!px) { MC_WARN("clickgui: icon %s decode failed", sources[i].name); continue; }
            for (int p = 0; p < w * h; ++p) { px[p*4+0] = 255; px[p*4+1] = 255; px[p*4+2] = 255; }

            D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            rd.Width = (UINT64)w; rd.Height = (UINT)h;
            rd.DepthOrArraySize = 1; rd.MipLevels = 1;
            rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

            ID3D12Resource* tex = nullptr;
            if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                    IID_PPV_ARGS(&tex)))) {
                stbi_image_free(px); MC_WARN("clickgui: icon %s tex alloc failed", sources[i].name); continue;
            }

            const UINT row_pitch = (UINT)((w * 4 + 255) & ~255);
            const UINT64 upload_sz = (UINT64)row_pitch * h;
            D3D12_HEAP_PROPERTIES up{}; up.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC ub{};
            ub.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            ub.Width = upload_sz; ub.Height = 1;
            ub.DepthOrArraySize = 1; ub.MipLevels = 1;
            ub.Format = DXGI_FORMAT_UNKNOWN; ub.SampleDesc.Count = 1;
            ub.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            ID3D12Resource* upbuf = nullptr;
            if (FAILED(dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &ub,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                    IID_PPV_ARGS(&upbuf)))) {
                tex->Release(); stbi_image_free(px); continue;
            }
            void* map = nullptr;
            if (SUCCEEDED(upbuf->Map(0, nullptr, &map))) {
                for (int y = 0; y < h; ++y)
                    memcpy((uint8_t*)map + (size_t)y * row_pitch, px + (size_t)y * w * 4, (size_t)w * 4);
                upbuf->Unmap(0, nullptr);
            }
            stbi_image_free(px);

            D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = tex; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = upbuf; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            src.PlacedFootprint.Footprint.RowPitch = row_pitch;
            src.PlacedFootprint.Footprint.Width = w; src.PlacedFootprint.Footprint.Height = h;
            src.PlacedFootprint.Footprint.Depth = 1;
            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

            D3D12_RESOURCE_BARRIER bar{};
            bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            bar.Transition.pResource = tex;
            bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            bar.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            bar.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            list->ResourceBarrier(1, &bar);

            D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
            sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            sv.Texture2D.MipLevels = 1;
            D3D12_CPU_DESCRIPTOR_HANDLE cpu = cpu_start;
            cpu.ptr += (size_t)(first_slot + i) * slot_step;
            dev->CreateShaderResourceView(tex, &sv, cpu);

            g_icons[i].w = w; g_icons[i].h = h;
            g_icons[i].id = (ImTextureID)(gpu_start.ptr + (size_t)(first_slot + i) * slot_step);

            keep_tex[i] = tex;
            ups[i] = upbuf;
            loaded++;
        }

        list->Close();
        ID3D12CommandList* cls[] = { list };
        queue->ExecuteCommandLists(1, cls);
        fence->SetEventOnCompletion(1, ev);
        queue->Signal(fence, 1);
        WaitForSingleObject(ev, 2000);
        for (int i = 0; i < ICON_COUNT; ++i) if (ups[i]) ups[i]->Release();
        MC_LOG("clickgui: %d/%d icons uploaded", loaded, ICON_COUNT);
        ok = loaded == ICON_COUNT;
    } while (0);

    if (list) list->Release();
    if (alloc) alloc->Release();
    if (fence) fence->Release();
    if (ev) CloseHandle(ev);
    return ok;
}
} // namespace mc
