// ModCore — in-game overlay (ImGui + DX12) toggled by INSERT
#include "core/log.h"
#include "core/state.h"
#include "core/input.h"
#include "ui/clickgui.h"
#include "ui/fonts.h"
#include "ui/renderer/icons.h"
#include "game/sdk/client/ClientInstance.hpp"   // ci_mouse_grab / platform_grab_block_install
#include "modules/registry.h"               // modules_hud_any

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <imgui.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>
#include <MinHook.h>
#include <mutex>
#include <thread>
#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);


namespace mc {
void ci_mouse_grab(bool grab);
void sdk::platform_grab_block_install();
void clicker_idle_inject();
}

#include <windows.h>
#include <shlobj.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <imgui.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>
#include <MinHook.h>
#include <mutex>
#include <thread>
#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace mc {

// sig accessors
int         sig_count_();
const char* sig_name_(int i);
uintptr_t   sig_addr_(int i);
bool        sig_ok_(int i);

struct HeapDesc : D3D12_DESCRIPTOR_HEAP_DESC {
    HeapDesc(D3D12_DESCRIPTOR_HEAP_TYPE t, UINT n, D3D12_DESCRIPTOR_HEAP_FLAGS f = D3D12_DESCRIPTOR_HEAP_FLAG_NONE) {
        Type = t; NumDescriptors = n; Flags = f; NodeMask = 0;
    }
};

// ---- overlay state ----
static ID3D12Device*              s_dev = nullptr;
static ID3D12CommandQueue*        s_game_queue = nullptr;
static ID3D12CommandAllocator*    s_alloc = nullptr;
static ID3D12GraphicsCommandList* s_list = nullptr;
static ID3D12DescriptorHeap*      s_rtv_heap = nullptr;
static ID3D12DescriptorHeap*      s_srv_heap = nullptr;
static IDXGISwapChain*            s_sc = nullptr;
static UINT s_rtv_step = 0, s_buf_count = 0;
static ID3D12Resource* s_targets[8] = {};
static ID3D12Fence* s_fence = nullptr;          // drains overlay GPU work so the
static HANDLE s_fence_ev = nullptr;             // game can ResizeBuffers safely
static unsigned long long s_fence_val = 0;
static bool s_inited = false, s_failed = false;
static volatile bool g_input_frozen = false; // menu open -> game sees no input
static volatile unsigned long long s_resize_until = 0;
static volatile LONG s_targets_dirty = 0;   // WM_SIZE -> Present releases (thread-safe)
static WNDPROC s_orig_wndproc = nullptr;
static bool s_gui_lb = false, s_gui_rb = false;   // mouse buttons fed to ImGui
static HWND s_hwnd = nullptr;

typedef HRESULT (STDMETHODCALLTYPE *t_Present)(IDXGISwapChain*, UINT, UINT);
typedef void    (STDMETHODCALLTYPE *t_ExecECL)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
static t_Present o_Present = nullptr;
static t_ExecECL o_ExecECL = nullptr;

static HWND find_game_window();
static HWND find_game_window();
static LRESULT CALLBACK wndproc(HWND, UINT, WPARAM, LPARAM);
static BOOL CALLBACK enum_cb(HWND, LPARAM);
    // (input workers moved into the modules layer)

// mouse wheel delta accumulated by wndproc, consumed by the Present thread
static volatile long g_wheel_accum = 0;

static void patch_slot(void** slot, void* detour, void** orig) {
    DWORD op;
    VirtualProtect(slot, 8, PAGE_READWRITE, &op);
    *orig = *slot;
    *slot = detour;
    VirtualProtect(slot, 8, op, &op);
}

// Track EVERY queue calling ExecuteCommandLists; the FIRST one is not
// necessarily the game's graphics queue (multi-queue: compute/copy exist).
// We render on the BUSIEST queue — the one actually presenting frames.
struct QCount { ID3D12CommandQueue* q; unsigned long long n; };
static QCount s_queues[16] = {};
static int s_queue_n = 0;
// temporal correlation: the queue that executed right before Present IS the
// frame presenter. Latching the "busiest" from 3 calls picked a wrong queue
// and crashed the device.
static ID3D12CommandQueue* s_last_ecl_queue = nullptr;
static unsigned long long   s_last_ecl_tick = 0;
static unsigned long long   s_votes[16] = {};

static unsigned long long now_ms_() {
    LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    return (unsigned long long)(c.QuadPart * 1000 / f.QuadPart);
}

static void STDMETHODCALLTYPE h_ExecECL(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* lists) {
    if (q) {
        int i = 0;
        for (; i < s_queue_n; ++i) if (s_queues[i].q == q) break;
        if (i == s_queue_n && s_queue_n < 16) { s_queues[i] = {q, 0}; s_queue_n++; }
        if (i < s_queue_n) s_queues[i].n++;
        s_last_ecl_queue = q;
        s_last_ecl_tick = now_ms_();
    }
    o_ExecECL(q, n, lists);
}

// the presenter = queue that executed within ~4ms before Present; latch after
// enough agreeing votes
static ID3D12CommandQueue* presenter_queue() {
    if (s_last_ecl_queue && now_ms_() - s_last_ecl_tick <= 4) {
        for (int i = 0; i < s_queue_n; ++i)
            if (s_queues[i].q == s_last_ecl_queue) { s_votes[i]++; break; }
    }
    int best = -1;
    unsigned long long bv = 0, total = 0;
    for (int i = 0; i < s_queue_n; ++i) {
        total += s_votes[i];
        if (s_votes[i] > bv) { bv = s_votes[i]; best = i; }
    }
    if (best < 0 || bv < 20 || bv * 10 < total * 8) return nullptr;  // need 80% consensus
    return s_queues[best].q;
}

// release ALL swapchain buffer references: the game's ResizeBuffers FAILS
// (-> device removal -> crash) while anyone still holds GetBuffer() refs.
// This was the real resize/F11/alt-tab crash since the first overlay build.
static void release_targets() {
    for (UINT i = 0; i < 8; ++i)
        if (s_targets[i]) { s_targets[i]->Release(); s_targets[i] = nullptr; }
    if (s_rtv_heap) { s_rtv_heap->Release(); s_rtv_heap = nullptr; }
    s_buf_count = 0;
}

static bool make_targets() {    DXGI_SWAP_CHAIN_DESC desc;
    if (FAILED(s_sc->GetDesc(&desc))) return false;
    // full reset first: stale released pointers in s_targets[] were the
    // resize/f11 crash (use-after-free on the backbuffer in Present)
    for (UINT i = 0; i < 8; ++i)
        if (s_targets[i]) { s_targets[i]->Release(); s_targets[i] = nullptr; }
    if (s_rtv_heap) { s_rtv_heap->Release(); s_rtv_heap = nullptr; }
    s_buf_count = 0;
    UINT n = desc.BufferCount < 8 ? desc.BufferCount : 8;
    HeapDesc rtv_desc(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, n);
    if (FAILED(s_dev->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&s_rtv_heap)))) return false;
    s_rtv_step = s_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = s_rtv_heap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < n; ++i) {
        if (FAILED(s_sc->GetBuffer(i, IID_PPV_ARGS(&s_targets[i])))) {
            for (UINT j = 0; j <= i; ++j)
                if (s_targets[j]) { s_targets[j]->Release(); s_targets[j] = nullptr; }
            s_rtv_heap->Release(); s_rtv_heap = nullptr;
            return false;
        }
        s_dev->CreateRenderTargetView(s_targets[i], nullptr, rtv);
        rtv.ptr += s_rtv_step;
    }
    s_buf_count = n;
    return true;
}


static HRESULT STDMETHODCALLTYPE h_Present(IDXGISwapChain* sc, UINT sync, UINT flags) {
    static bool s_init_started = false;
    if (!s_inited && !s_failed && !s_init_started) {
        // window must be found AND subclassed BEFORE ImGui_ImplWin32_Init
        if (!s_hwnd) {
            s_hwnd = find_game_window();
            if (s_hwnd) {
                s_orig_wndproc = (WNDPROC)SetWindowLongPtrW(s_hwnd, GWLP_WNDPROC, (LONG_PTR)wndproc);
                wchar_t cls[64] = L"?";
                GetClassNameW(s_hwnd, cls, 64);
                MC_LOG("overlay: window subclassed %p class=%ls", (void*)s_hwnd, cls);
            }
        }
        // wait for BOTH: window and a CONFIRMED presenter queue (temporal votes)
        ID3D12CommandQueue* q = presenter_queue();
        if (s_hwnd && q) {
            s_init_started = true;
            s_sc = sc;
            s_game_queue = q;
            {   // log queues once
                char buf[256] = {0}; int len = 0;
                for (int i = 0; i < s_queue_n && i < 6; ++i)
                    len += snprintf(buf + len, sizeof(buf) - len - 1, " q%d=%p:c%llu:v%llu",
                                    i, (void*)s_queues[i].q, s_queues[i].n, s_votes[i]);
                MC_LOG("overlay: queues:%s -> render on %p", buf, (void*)q);
            }
            if (SUCCEEDED(sc->GetDevice(IID_PPV_ARGS(&s_dev)))) {
                do {
                MC_LOG("cg-init: got device %p", (void*)s_dev);
                if (FAILED(s_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&s_alloc)))) break;
                if (FAILED(s_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, s_alloc, nullptr, IID_PPV_ARGS(&s_list)))) break;
                s_list->Close();
                if (FAILED(s_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&s_fence)))) break;
                s_fence_ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                MC_LOG("cg-init: cmd objects ok");
                HeapDesc srv_desc(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
                if (FAILED(s_dev->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&s_srv_heap)))) break;
                if (!make_targets()) break;
                MC_LOG("cg-init: heaps+targets ok (bufs=%u)", s_buf_count);

                IMGUI_CHECKVERSION();
                MC_LOG("cg-init: checkversion ok");
                ImGui::CreateContext();
                MC_LOG("cg-init: context created (ctx=%p)", (void*)ImGui::GetCurrentContext());
                ImGuiIO& io = ImGui::GetIO();
                io.IniFilename = nullptr;
                // 1.91 error recovery: its error TOOLTIP calls ImGui::Begin outside a
                // frame (e.g. when a font file is missing) -> assert crash. Kill it.
                io.ConfigErrorRecoveryEnableTooltip = false;
                io.ConfigErrorRecoveryEnableAssert = false; // log-only, no abort
                ImGui::StyleColorsDark();
                MC_LOG("cg-init: style ok");
                ui_fonts_build();   // embedded Josefin + Montserrat (ui/fonts.cpp)
                ImGui_ImplWin32_Init(s_hwnd);  // never null: guarded above
                MC_LOG("cg-init: win32 backend ok");
                ImGui_ImplDX12_Init(s_dev, s_buf_count, DXGI_FORMAT_R8G8B8A8_UNORM, s_srv_heap,
                                    s_srv_heap->GetCPUDescriptorHandleForHeapStart(),
                                    s_srv_heap->GetGPUDescriptorHandleForHeapStart());
                MC_LOG("cg-init: dx12 backend ok");
                // menu icons into the remaining SRV slots (1..10)
                clickgui_load_textures(s_dev, s_game_queue, s_srv_heap,
                                       s_srv_heap->GetCPUDescriptorHandleForHeapStart(),
                                       s_srv_heap->GetGPUDescriptorHandleForHeapStart(),
                                       s_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV), 1);
                MC_LOG("cg-init: icons ok");
                s_inited = true;
                MC_LOG("overlay: ImGui DX12 ready (bufs=%u, dev=%p, queue=%p)", s_buf_count, (void*)s_dev, (void*)s_game_queue);
            } while (0);
            if (!s_inited) { s_failed = true; MC_ERR("overlay: init failed"); }
            }
        }
    }

    if (s_inited && sc == s_sc && !(flags & DXGI_PRESENT_TEST)) {
        // ---- INSERT / ESC poll (subclasses get displaced by WinUI; polling is reliable)
        static SHORT prevIns = 0;
        SHORT ins = GetAsyncKeyState(VK_INSERT);
        auto& m = mod();
        bool want_close = false;
        if ((ins & 0x8000) && !(prevIns & 0x8000)) {
            want_close = m.overlay_open;
            { std::lock_guard<std::mutex> g(m.mtx); m.overlay_open = !m.overlay_open; }
            MC_LOG("overlay %s (INSERT polled)", m.overlay_open ? "OPEN" : "CLOSE");
        }
        if (!want_close && m.overlay_open && (GetAsyncKeyState(VK_ESCAPE) & 0x8000)) {
            { std::lock_guard<std::mutex> g(m.mtx); m.overlay_open = false; }
            MC_LOG("overlay CLOSE (ESC polled)");
        }
        if (!m.overlay_open) ImGui::GetIO().MouseDrawCursor = false;
        prevIns = ins;

        // ---- re-subclass if WinUI displaced our wndproc; if the game window
        // was DESTROYED (F11 can recreate it), find the new one instead of
        // subclassing a dead hwnd (that nulls s_orig_wndproc)
        static UINT frame = 0;
        if ((++frame % 120) == 0) {
            if (s_hwnd && !IsWindow(s_hwnd)) {
                MC_WARN("overlay: game window destroyed, searching for a new one");
                s_hwnd = find_game_window();
                if (s_hwnd) {
                    s_orig_wndproc = (WNDPROC)SetWindowLongPtrW(s_hwnd, GWLP_WNDPROC, (LONG_PTR)wndproc);
                    MC_LOG("overlay: new window subclassed %p", (void*)s_hwnd);
                }
            } else if (s_hwnd) {
                WNDPROC cur = (WNDPROC)GetWindowLongPtrW(s_hwnd, GWLP_WNDPROC);
                if (cur != wndproc) {
                    s_orig_wndproc = (WNDPROC)SetWindowLongPtrW(s_hwnd, GWLP_WNDPROC, (LONG_PTR)wndproc);
                    MC_WARN("overlay: wndproc was displaced, re-subclassed");
                }
            }
        }

        bool menu_open, hud_elems;
        { auto& m = mod(); std::lock_guard<std::mutex> g(m.mtx); menu_open = m.overlay_open; }
        hud_elems = modules_hud_any();
        if (!menu_open) ImGui::GetIO().MouseDrawCursor = false;
        // HUD elements draw on every frame while in-world (menu closed too)
        bool hud_on = hud_elems;

        // GUI open edge: release the cursor through the GAME's own path
        // (platform releaseMouse) + the OS fallback; block platform re-grabs
        {
            static bool s_prev_open = false;
            if (menu_open && !s_prev_open) {
                sdk::platform_grab_block_install();
                sdk::ci_mouse_grab(false);
                ClipCursor(nullptr);
                int c = 0, n2 = 0;
                while ((c = ShowCursor(TRUE)) < 0 && n2++ < 10) {}
                ImGui::GetIO().MouseDrawCursor = true;   // game can't hide THIS one
                MC_LOG("overlay: cursor released for GUI (show=%d)", c);
            }
            s_prev_open = menu_open;
        }

        // watchdog: the freeze flag must NEVER stick with the menu closed
        {
            static ULONGLONG stuck_since = 0;
            ULONGLONG wnow = GetTickCount64();
            if (g_input_frozen && !menu_open) {
                if (!stuck_since) stuck_since = wnow;
                else if (wnow - stuck_since > 3000) {
                    g_input_frozen = 0;
                    set_input_frozen(false);
                    MC_WARN("overlay: freeze watchdog fired (input restored)");
                    stuck_since = 0;
                }
            } else stuck_since = 0;
        }

        // keep re-releasing the cursor while the menu is open: the platform
        // layer re-grabs behind ClientInstance's back every frame
        g_input_frozen = menu_open ? 1 : 0;   // local mirror (cursor hooks)
        set_input_frozen(menu_open);              // deep freeze (game feed hooks)
        if (menu_open) {
            static ULONGLONG last_release = 0;
            ULONGLONG now2 = GetTickCount64();
            if (now2 - last_release > 500) {
                last_release = now2;
                sdk::platform_grab_block_install();
                sdk::ci_mouse_grab(false);
                ClipCursor(nullptr);
                // diagnostics: is the OS cursor free and where is ImGui looking?
                static ULONGLONG last_diag = 0;
                if (now2 - last_diag > 1000) {
                    last_diag = now2;
                    POINT cp{}; GetCursorPos(&cp);
                    CURSORINFO ci2{}; ci2.cbSize = sizeof(ci2); GetCursorInfo(&ci2);
                    ImGuiIO& io = ImGui::GetIO();
                    if (io.DisplaySize.x <= 0) { } else
                    MC_LOG("cursor diag: os=(%d,%d) flags=%u imgui=(%.0f,%.0f) clicks=%d",
                           (int)cp.x, (int)cp.y, ci2.flags, io.MousePos.x, io.MousePos.y,
                           (int)io.MouseClickedCount[0]);
                }
            }
        }

        // the ImGui frame runs for the MENU or for the HUD; everything below
        // (guards, render) treats them the same. The closing animation also
        // keeps frames alive for ~0.15s after the menu flag drops.
        bool want_draw = menu_open || hud_on || clickgui_animating();

        // crash guards (not GUI): minimized game -> DisplaySize<=0 would assert
        // in NewFrame; alt-tab/fullscreen transitions can swap the buffer set ->
        // stale render targets are use-after-free device crashes
        RECT wrect{};
        if (want_draw && s_hwnd) {
            GetClientRect(s_hwnd, &wrect);
            if (IsIconic(s_hwnd) || wrect.right < 100 || wrect.bottom < 100)
                want_draw = false;
        }
        // WM_SIZE flagged a target release — do it HERE (Present thread owns
        // the targets; the wndproc only sets the flag) and skip this frame.
        if (InterlockedExchange(&s_targets_dirty, 0)) {
            release_targets();
            want_draw = false;
        }
        // WM_SIZE cooldown: GetBuffer/GetDesc racing the game's ResizeBuffers on
        // another thread was the resize/F11 crash. Hands completely off the
        // swapchain until the storm settles.
        if (want_draw && s_resize_until) {
            if ((unsigned long long)GetTickCount64() < s_resize_until) want_draw = false;
            else s_resize_until = 0;
        }
        if (want_draw) {
            DXGI_SWAP_CHAIN_DESC sd;
            if (SUCCEEDED(s_sc->GetDesc(&sd)) && sd.BufferCount != s_buf_count) {
                MC_LOG("overlay: buffers %u -> %u, rebuilding targets", s_buf_count, sd.BufferCount);
                for (UINT i = 0; i < s_buf_count; ++i)
                    if (s_targets[i]) { s_targets[i]->Release(); s_targets[i] = nullptr; }
                if (s_rtv_heap) { s_rtv_heap->Release(); s_rtv_heap = nullptr; }
                if (!make_targets()) want_draw = false;
            }
        }

        // skip when minimized/small: ImGui::NewFrame asserts on DisplaySize<=0
        // (single guard — the duplicated block here read an uninitialized
        //  RECT and let Present run with stale targets DURING a resize)
        if (want_draw && s_hwnd) {
            RECT wrect{};
            GetClientRect(s_hwnd, &wrect);
            if (IsIconic(s_hwnd) || wrect.right < 100 || wrect.bottom < 100)
                want_draw = false;
        }

        if (want_draw) {
            // ImGui is single-threaded: ALL input is polled HERE (Present thread).
            // The old ImGui_ImplWin32_WndProcHandler ran on the window thread and
            // raced NewFrame/Render -> assert crashes (g.WithinFrameScope etc).
            ImGuiIO& io = ImGui::GetIO();
            if (menu_open) {
                POINT pt; GetCursorPos(&pt); ScreenToClient(s_hwnd, &pt);
                io.AddMousePosEvent((float)pt.x, (float)pt.y);
                io.AddMouseButtonEvent(0, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
                io.AddMouseButtonEvent(1, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
                io.AddMouseButtonEvent(2, (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
                long w = InterlockedExchange(&g_wheel_accum, 0);
                if (w) io.AddMouseWheelEvent(0.f, (float)w / (float)WHEEL_DELTA);
                io.AddKeyEvent(ImGuiMod_Ctrl, (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
                io.AddKeyEvent(ImGuiMod_Shift, (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
            }
            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            clickgui_newframe();   // per-frame clock: open/close anim + theme cache
            draw_hud();            // nursultan HUD: watermark / arraylist / info
            if (menu_open || clickgui_animating())
                draw_clickgui();   // nursultan ClickGUI (columns + gradient picker)
            ImGui::Render();

            UINT idx = ((IDXGISwapChain3*)s_sc)->GetCurrentBackBufferIndex();
            if (idx < s_buf_count && s_targets[idx]) {
                ID3D12Resource* bb = s_targets[idx];
                s_alloc->Reset();
                s_list->Reset(s_alloc, nullptr);
                D3D12_RESOURCE_BARRIER bar{};
                bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                bar.Transition.pResource = bb;
                bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                bar.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
                bar.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                s_list->ResourceBarrier(1, &bar);
                D3D12_CPU_DESCRIPTOR_HANDLE rtv = s_rtv_heap->GetCPUDescriptorHandleForHeapStart();
                rtv.ptr += (size_t)idx * s_rtv_step;
                s_list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
                ID3D12DescriptorHeap* heaps[] = { s_srv_heap };
                s_list->SetDescriptorHeaps(1, heaps);
                ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), s_list);
                bar.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                bar.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
                s_list->ResourceBarrier(1, &bar);
                s_list->Close();
                ID3D12CommandList* cls[] = { s_list };
                s_game_queue->ExecuteCommandLists(1, cls);
                // CRITICAL for resize/F11: the game may call ResizeBuffers right
                // after this Present. If our command list (referencing the old
                // backbuffers) is still in flight, the device is removed and the
                // game crashes. Wait for OUR work to drain before returning.
                if (s_fence && s_fence_ev) {
                    s_fence_val++;
                    s_game_queue->Signal(s_fence, s_fence_val);
                    if (SUCCEEDED(s_fence->SetEventOnCompletion(s_fence_val, s_fence_ev)))
                        WaitForSingleObject(s_fence_ev, 100);
                }
            }
        }
    }
    return o_Present(sc, sync, flags);
}

// ================= input freeze + OS-level input emulation =================
// While the menu is open the GAME must see no input at all:
//  1. wndproc: swallow key/mouse DOWN + CHAR (they feed ImGui first), let the
//     UP variants through so held keys/buttons release cleanly, and consume
//     WM_INPUT with DefWindowProc (the game's raw-input handler never runs ->
//     camera frozen; DefWindowProc drains the queue so it cannot back up)
//  2. GetAsyncKeyState / GetKeyState / GetKeyboardState hooked in-process:
//     return "everything up" while frozen. Our own code goes through the
//     originals (o_*) so the GUI poll and the workers keep working.
typedef SHORT (WINAPI *t_GAKS)(int);
typedef SHORT (WINAPI *t_GKS)(int);
typedef BOOL  (WINAPI *t_GKBS)(PBYTE);
static t_GAKS o_GAKS = nullptr;
static t_GKS  o_GKS  = nullptr;
static t_GKBS o_GKBS = nullptr;

static SHORT WINAPI h_GetAsyncKeyState(int vk) {
    if (g_input_frozen) return 0;
    return o_GAKS(vk);
}
static SHORT WINAPI h_GetKeyState(int vk) {
    if (g_input_frozen) return 0;
    return o_GKS(vk);
}
static BOOL WINAPI h_GetKeyboardState(PBYTE buf) {
    if (g_input_frozen && buf) { memset(buf, 0, 256); return TRUE; }
    return o_GKBS(buf);
}
// input_frozen() now lives in core/state.cpp (game-side hooks read it there)
// real_key_down() now lives in core/input.cpp (direct GetAsyncKeyState —
// the user32 key-state MinHooks below stay compile-time disabled)


// AutoSprint worker: modules/movement/autosprint.cpp (started at modules_init)
// GameInput (GDK) reading freeze: game/sdk/client/gameinput.cpp (DORMANT, never called)
// in_world(): core/threadbus.cpp (Level::tick freshness)

static HWND find_game_window() {
    HWND game = nullptr;
    EnumWindows(enum_cb, (LPARAM)&game);
    return game;
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    // INSERT toggle lives ONLY in the Present-thread poll (single source of
    // truth): handling it here too double-toggled the GUI shut instantly.
    if (msg == WM_CLOSE || msg == WM_ENDSESSION)
        MC_LOG("window close requested (msg=0x%x) — выход по крестику, не краш", msg);
    // WM_SIZE: the game calls ResizeBuffers inside its own handler — ALL our
    // GetBuffer() refs MUST be gone before it returns, or ResizeBuffers fails
    // -> device removed -> crash (the resize/F11 crash). Close the menu and
    // hands off the swapchain for 700ms to let the storm settle.
    if (msg == WM_SIZE) {
        static ULONGLONG last_sz = 0;
        ULONGLONG now = GetTickCount64();
        if (now - last_sz > 1000) { last_sz = now; MC_LOG("overlay: WM_SIZE wp=%llu", (unsigned long long)w); }
        { auto& m2 = mod();
          std::lock_guard<std::mutex> g2(m2.mtx);
          if (m2.overlay_open) { m2.overlay_open = false; MC_LOG("overlay CLOSE (resize)"); } }
        // NEVER release targets here: the Present thread may be mid-frame
        // using them -> use-after-free (the resize crash). Flag it instead;
        // h_Present performs the release at a safe point.
        InterlockedExchange(&s_targets_dirty, 1);
        s_resize_until = GetTickCount64() + 700;
    }
    // NOTE: no ImGui_ImplWin32_WndProcHandler here anymore — it ran on the
    // window thread and raced the Present-thread frame (ImGui is not
    // thread-safe). Input is polled in h_Present instead.
    auto& m = mod();
    bool open;
    { std::lock_guard<std::mutex> g(m.mtx); open = m.overlay_open; }
    if (open) {
        if (msg == WM_MOUSEWHEEL) InterlockedAdd(&g_wheel_accum, (long)GET_WHEEL_DELTA_WPARAM(w));
        if ((msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
            msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_CHAR ||
            msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP)
            return 0;
    }
    return CallWindowProcW(s_orig_wndproc, h, msg, w, l);
}

HWND overlay_game_hwnd() { return s_hwnd; }

static BOOL CALLBACK enum_cb(HWND h, LPARAM lp) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h)) {
        wchar_t cls[64] = L"";
        GetClassNameW(h, cls, 64);
        RECT r; GetWindowRect(h, &r);
        static bool logged = false;
        if (!logged) {
            logged = true;
            MC_LOG("overlay: window candidate %p class=%ls %dx%d", (void*)h, cls,
                   (int)(r.right - r.left), (int)(r.bottom - r.top));
        }
        if ((r.right - r.left) > 300 && (r.bottom - r.top) > 200) {
            *(HWND*)lp = h;
            return FALSE;
        }
    }
    return TRUE;
}

// pointer-lock killers: the GDK layer emulates mouse capture by CLIPPING
// the cursor and WARPING it to center; in-world it also hides the cursor via
// ShowCursor(FALSE) and SetCursor(NULL). All four are neutralized while the
// menu owns the cursor (g_input_frozen).
    // pointer-lock killers: the GDK layer emulates mouse capture by CLIPPING
// the cursor and WARPING it to center; in-world it also hides the cursor via
// ShowCursor(FALSE) and SetCursor(NULL). All four are neutralized while the
// menu owns the cursor (g_input_frozen).
typedef BOOL (WINAPI *t_SetCursorPos)(int, int);
typedef BOOL (WINAPI *t_ClipCursor)(const RECT*);
typedef int  (WINAPI *t_ShowCursor)(BOOL);
typedef HCURSOR (WINAPI *t_SetCursor)(HCURSOR);
static t_SetCursorPos o_SetCursorPos = nullptr;
static t_ClipCursor   o_ClipCursor   = nullptr;
static t_ShowCursor   o_ShowCursor   = nullptr;
static t_SetCursor    o_SetCursor    = nullptr;
static BOOL WINAPI h_SetCursorPos(int x, int y) {
    if (g_input_frozen) return TRUE;   // pretend success, do not warp
    return o_SetCursorPos(x, y);
}
static BOOL WINAPI h_ClipCursor(const RECT* r) {
    if (g_input_frozen && r) return TRUE; // block clips (pass un-clips)
    return o_ClipCursor(r);
}
static int WINAPI h_ShowCursor(BOOL show) {
    if (g_input_frozen && !show) return 1; // the game must not hide OUR cursor
    return o_ShowCursor(show);
}
static HCURSOR WINAPI h_SetCursor(HCURSOR c) {
    if (g_input_frozen && !c) c = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    return o_SetCursor(c);
}
void overlay_init() {
    ID3D12Device* dev = nullptr;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev)))) {
        MC_ERR("overlay: D3D12CreateDevice failed"); return;
    }
    ID3D12CommandQueue* q = nullptr;
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q)))) {
        MC_ERR("overlay: dummy queue failed"); return;
    }
    void** qvt = *(void***)q;
    patch_slot(&qvt[10], (void*)h_ExecECL, (void**)&o_ExecECL);
    MC_LOG("overlay: ExecuteCommandLists vtable patched (dummy q=%p)", (void*)q);

    IDXGIFactory2* fact = nullptr;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&fact)))) {
        MC_ERR("overlay: factory failed"); return;
    }
    HWND dummy = CreateWindowExW(0, L"STATIC", L"mcdummy", 0, 0, 0, 8, 8, nullptr, nullptr, nullptr, nullptr);
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = 8; sd.Height = 8; sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1; sd.BufferCount = 2; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    IDXGISwapChain1* sc1 = nullptr;
    if (FAILED(fact->CreateSwapChainForHwnd(q, dummy, &sd, nullptr, nullptr, &sc1))) {
        MC_ERR("overlay: dummy swapchain failed"); return;
    }
    void** svt = *(void***)sc1;
    patch_slot(&svt[8], (void*)h_Present, (void**)&o_Present);
    MC_LOG("overlay: Present vtable patched");
    sc1->Release();

    // window search happens lazily inside h_Present (fixes the hWnd!=0 race)

    // ---- key-state hooks: DISABLED ----
    // These were broken (enable-by-trampoline "4/4/4") since day one, so every
    // session that "worked" ran WITHOUT them. The first build where they
    // actually enabled (16:29) crashed 20 ms after the first WM_SIZE —
    // prime suspect. The game-side MouseDevice/Keyboard::feed hooks do the
    // whole freeze on their own (proven), so this belt-and-suspenders layer
    // stays off until the WM_SIZE crash is attributed elsewhere.
    constexpr bool k_install_key_hooks = false;
    if (k_install_key_hooks) {
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        struct K { const char* n; void* det; void** orig; };
        const K ks[3] = {
            {"GetAsyncKeyState", (void*)h_GetAsyncKeyState, (void**)&o_GAKS},
            {"GetKeyState",      (void*)h_GetKeyState,      (void**)&o_GKS},
            {"GetKeyboardState", (void*)h_GetKeyboardState, (void**)&o_GKBS},
        };
        int enabled = 0;
        for (auto& k : ks) {
            void* target = (void*)GetProcAddress(u32, k.n);
            if (!target) { MC_WARN("overlay: %s not found", k.n); continue; }
            if (MH_CreateHook(target, k.det, k.orig) != MH_OK) { MC_WARN("overlay: create %s failed", k.n); continue; }
            if (MH_EnableHook(target) == MH_OK) enabled++;
        }
        MC_LOG("overlay: key hooks enabled %d/3", enabled);
    } else {
        MC_LOG("overlay: key-state hooks disabled (feed hooks own the freeze)");
    }

    // pointer-lock killers (always on, gated by g_input_frozen)
    {
        HMODULE u32b = GetModuleHandleW(L"user32.dll");
        struct P { const char* n; void* det; void** orig; };
        const P ps[4] = {
            {"SetCursorPos", (void*)h_SetCursorPos, (void**)&o_SetCursorPos},
            {"ClipCursor",   (void*)h_ClipCursor,   (void**)&o_ClipCursor},
            {"ShowCursor",   (void*)h_ShowCursor,   (void**)&o_ShowCursor},
            {"SetCursor",    (void*)h_SetCursor,    (void**)&o_SetCursor},
        };
        int en = 0;
        for (auto& k : ps) {
            void* target = (void*)GetProcAddress(u32b, k.n);
            if (!target) { MC_WARN("overlay: %s not found", k.n); continue; }
            if (MH_CreateHook(target, k.det, k.orig) == MH_OK && MH_EnableHook(target) == MH_OK) en++;
        }
        MC_LOG("overlay: pointer-lock hooks enabled %d/4", en);
    }

    // OS-level input workers live in the modules layer now (AutoSprint)

    dev->Release();
    MC_LOG("overlay: bootstrap done");
}

void overlay_shutdown() {
    if (s_orig_wndproc && s_hwnd) SetWindowLongPtrW(s_hwnd, GWLP_WNDPROC, (LONG_PTR)s_orig_wndproc);
}

} // namespace mc
