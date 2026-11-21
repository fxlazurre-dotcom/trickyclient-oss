// ModCore — game SDK: frame-transform capture implementation
#include "game/sdk/render/LevelRenderer.hpp"
#include "game/sdk/SDK.hpp"
#include "game/sdk/actor/LocalPlayer.hpp"
#include "game/sigs/sigs.h"
#include "core/log.h"

#include <MinHook.h>
#include <windows.h>
#include <cstring>
#include <mutex>
#include <vector>

namespace mc {
namespace sdk {

// offsets from the reference client (same 1.26.x family, live-verified there)
constexpr int CI_VIEW_MATRIX     = 0x420;   // glm::mat4 (column-major)
constexpr int CI_GET_LEVELRENDERER = 187;   // ci vtable slot
constexpr int LR_RENDERER_PLAYER = 0x468;
constexpr int LRP_CAMERA_POS     = 0x9CC;   // Vec3
constexpr int LRP_FOV_X          = 0xF58;   // float
constexpr int LRP_FOV_Y          = 0xF6C;   // float
constexpr int kTransformDelay    = 3;       // = swapchain buffer count

static std::mutex g_ft_mtx;
static std::vector<render::ViewTransform> g_ft_queue;
static render::ViewTransform g_ft_last;     // latest valid (fallback)

typedef void (*t_setup_and_render)(void*, void*);
static t_setup_and_render o_setup_and_render = nullptr;

static void capture_frame_transform() {
    render::ViewTransform t;
    uint8_t* ci = (uint8_t*)client_instance();
    if (!ci || !readable_ptr(ci, CI_VIEW_MATRIX + 64)) return;
    // LevelRenderer via the ClientInstance vtable
    void** vt = *(void***)ci;
    if (!readable_ptr(vt, (CI_GET_LEVELRENDERER + 1) * 8)) return;
    void* lr = ((void* (*)(void*))vt[CI_GET_LEVELRENDERER])(ci);
    if (!lr || !readable_ptr((uint8_t*)lr, LR_RENDERER_PLAYER + 8)) return;
    void* lrp = *(void**)((uint8_t*)lr + LR_RENDERER_PLAYER);
    if (!lrp || !readable_ptr((uint8_t*)lrp, LRP_FOV_Y + 4)) return;

    memcpy(t.m, ci + CI_VIEW_MATRIX, sizeof(t.m));
    memcpy(t.origin, (uint8_t*)lrp + LRP_CAMERA_POS, sizeof(t.origin));
    t.fov[0] = *(float*)((uint8_t*)lrp + LRP_FOV_X);
    t.fov[1] = *(float*)((uint8_t*)lrp + LRP_FOV_Y);
    t.valid = true;

    std::lock_guard<std::mutex> g(g_ft_mtx);
    g_ft_last = t;
    g_ft_queue.push_back(t);
    if (g_ft_queue.size() > 8) g_ft_queue.erase(g_ft_queue.begin());
    static bool s_logged = false;
    if (!s_logged) {
        s_logged = true;
        MC_LOG("render: frame transform captured (cam %.1f %.1f %.1f, fov %.3f/%.3f)",
               t.origin[0], t.origin[1], t.origin[2], t.fov[0], t.fov[1]);
    }
}

static volatile LONG g_sar_calls = 0;
static void h_setup_and_render(void* screenView, void* mcuirc) {
    InterlockedIncrement(&g_sar_calls);
    capture_frame_transform();
    o_setup_and_render(screenView, mcuirc);
}

render::ViewTransform render_frame_transform() {
    std::lock_guard<std::mutex> g(g_ft_mtx);
    // drain to the delay depth: the front entry matches the PRESENTED frame
    while (g_ft_queue.size() > (size_t)kTransformDelay)
        g_ft_queue.erase(g_ft_queue.begin());
    if (!g_ft_queue.empty()) return g_ft_queue.front();
    return g_ft_last;
}

long render_sar_calls() { return g_sar_calls; }

void install_render_hook() {
    uintptr_t a = sig_get("ScreenView_setupAndRender");
    if (!a) { MC_WARN("render: setupAndRender sig missing"); return; }
    if (MH_CreateHook((void*)a, (void*)h_setup_and_render, (void**)&o_setup_and_render) == MH_OK) {
        MH_EnableHook((void*)a);
        MC_LOG("hook ScreenView_setupAndRender installed (frame transform capture)");
    } else MC_WARN("render: MH_CreateHook failed");
}

} // namespace sdk
} // namespace mc
