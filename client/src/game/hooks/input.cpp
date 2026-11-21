// ModCore — game hooks: input pipeline (moved verbatim from the monolith,
// config sources swapped: ModuleState fields -> thread bus channels)
#include "game/hooks/input.h"
#include "game/sigs/sigs.h"
#include "game/sdk/SDK.hpp"
#include "game/sdk/actor/LocalPlayer.hpp"
#include "core/log.h"
#include "core/state.h"
#include "core/input.h"
#include "core/threadbus.h"
#include "overlay.h"

#include <MinHook.h>
#include <windows.h>
#include <mutex>
#include <thread>
#include <cmath>

namespace mc {
namespace game {
namespace Offsets = sdk::Offsets;

// ---- game-side input freeze: MouseDevice::feed / Keyboard::feed ----
// The game funnels BOTH input backends (win32 messages and the Xbox GameInput
// runtime) into these two functions. Blocking them while the menu is open
// freezes the whole pipeline — camera, movement, clicks — no matter where the
// events came from.
// ARG COUNTS (from the 1.26.52.3 dump): MouseDevice::feed takes EIGHT args
// (this + 7: two bytes, four words, one byte — stack args at [rsp+0x28..0x40]);
// forwarding fewer made the original read stack garbage -> delayed crashes.
// Forwarding MORE than the callee reads is harmless on x64, so use 10.
struct MouseEv { uint64_t t; uint64_t a[8]; };
static MouseEv g_mring[256];
static volatile long g_mpos = 0;
static void mouse_ring_push(const uint64_t a[8]) {
    long p = (g_mpos + 1) & 255;
    g_mring[p].t = GetTickCount64();
    for (int i = 0; i < 8; ++i) g_mring[p].a[i] = a[i];
    g_mpos = p;
}
bool mouse_feed_click_pair(uint64_t down[8], uint64_t up[8]) {
    long end = g_mpos;
    for (int k = 0; k < 250; ++k) {
        long i = (end - k) & 255;
        long j = (i - 1) & 255;
        if (!g_mring[i].t || !g_mring[j].t) break;
        if (g_mring[i].t - g_mring[j].t > 800) continue;
        int diff = -1;
        for (int q = 0; q < 8; ++q) {
            if (g_mring[i].a[q] != g_mring[j].a[q]) {
                if (diff >= 0) { diff = -2; break; }
                diff = q;
            }
        }
        if (diff >= 0) {
            memcpy(down, g_mring[j].a, sizeof(uint64_t) * 8);
            memcpy(up,   g_mring[i].a, sizeof(uint64_t) * 8);
            return true;
        }
    }
    return false;
}
typedef uint64_t (*t_feed10)(void*, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                             uint64_t, uint64_t, uint64_t, uint64_t);
static t_feed10 o_mouse_feed = nullptr, o_kb_feed = nullptr;
// LMB down-edges + VERBATIM button-record templates (v3.5 data-decoded:
// button record = (this,0,0,x,CONST_PTR,buttons,counter,0x200) — a5 is a
// CONSTANT POINTER; only a4 (x) may be refreshed. Stale x whipped the camera.)
volatile ULONGLONG g_last_feed_ms = 0;
volatile LONG g_lmb_feed_count = 0;
volatile LONG g_feed_diag = 0;
static uint64_t g_btn_dn_tpl[8]; static bool g_have_dn = false;
static uint64_t g_btn_up_tpl[8]; static bool g_have_up = false;
static bool clicker_due();

static uint64_t h_mouse_feed(void* s, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e,
                             uint64_t f, uint64_t g, uint64_t h, uint64_t i) {
    if (input_frozen()) return 0;
    { static uint16_t s_prev_btn = 0xffff;
      uint16_t now_btn = (uint16_t)e;
      if ((s_prev_btn & 1) && !(now_btn & 1)) {
          InterlockedIncrement(&g_lmb_feed_count);
          const uint64_t tpl[8] = {(uint64_t)s, a, b, c, d, e, f, g};
          memcpy(g_btn_dn_tpl, tpl, sizeof(tpl)); g_have_dn = true;
          InterlockedExchange(&g_feed_diag, 8);
      }
      if (!(s_prev_btn & 1) && (now_btn & 1)) {
          const uint64_t tpl[8] = {(uint64_t)s, a, b, c, d, e, f, g};
          memcpy(g_btn_up_tpl, tpl, sizeof(tpl)); g_have_up = true;
      }
      s_prev_btn = now_btn; }
    { const uint64_t args[8] = {(uint64_t)s, a, b, c, d, e, f, g}; mouse_ring_push(args); }
    g_last_feed_ms = GetTickCount64();
    { static ULONGLONG s_mov_t = 0;
      uint16_t btn = (uint16_t)e;
      bool held = (btn & 1) == 0;
      ULONGLONG nw = GetTickCount64();
      if (held && nw - s_mov_t > 2000 && g_feed_diag == 0) { s_mov_t = nw; InterlockedExchange(&g_feed_diag, 3); }
      if (InterlockedDecrement((LONG*)&g_feed_diag) >= 0)
          MC_LOG("feedrec: %llx %llx %llx %llx %llx %llx %llx %llx",
                 (unsigned long long)(uintptr_t)s, (unsigned long long)a, (unsigned long long)b,
                 (unsigned long long)c, (unsigned long long)d, (unsigned long long)e,
                 (unsigned long long)f, (unsigned long long)g);
    }
    uint64_t r = o_mouse_feed(s, a, b, c, d, e, f, g, h, i);
    if (false && clicker_due() && g_have_up && g_have_dn) {   // OFF: while-moving injection always drifts the camera; idle-only for now
        uint64_t cu[8], cd[8];
        memcpy(cu, g_btn_up_tpl, sizeof(cu));
        memcpy(cd, g_btn_dn_tpl, sizeof(cd));
        long ip = g_mpos;
        if (g_mring[ip].t) {
            cu[3] = g_mring[ip].a[3];
            cd[3] = g_mring[ip].a[3];
        }
        o_mouse_feed((void*)cu[0], cu[1], cu[2], cu[3], cu[4], cu[5], cu[6], cu[7], 0, 0);
        o_mouse_feed((void*)cd[0], cd[1], cd[2], cd[3], cd[4], cd[5], cd[6], cd[7], 0, 0);
        static volatile LONG s_synth = 0;
        long cnt = InterlockedIncrement(&s_synth);
        if (cnt == 1 || cnt % 25 == 0) MC_LOG("clicker(feed): synthetic click #%ld", cnt);
    }
    return r;
}

static bool clicker_due() {
    ClickerCfg c = Bus::inst().clicker();
    bool menu; { std::lock_guard<std::mutex> g(mod().mtx); menu = mod().overlay_open; }
    if (!c.on || menu) return false;
    if (!Bus::inst().in_world()) return false;
    if (!real_key_down(VK_LBUTTON)) return false;
    static ULONGLONG next = 0;
    ULONGLONG now = GetTickCount64();
    if (now < next) return false;
    double base = 1000.0 / (c.cps > 1.f ? c.cps : 1.f);
    double w = base + c.jitter * ((double)(rand() % 2001) / 1000.0 - 1.0);
    if (w < 45.0) w = 45.0;
    next = now + (ULONGLONG)w;
    return true;
}
static uint64_t h_kb_feed(void* s, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e,
                          uint64_t f, uint64_t g, uint64_t h, uint64_t i) {
    if (input_frozen()) return 0;
    return o_kb_feed(s, a, b, c, d, e, f, g, h, i);
}

// ---- mouse feed capture + replay ----
// most recent adjacent pair that differs in exactly ONE arg = a button click
// (moves differ in several args, clicks flip a single state byte)
uint64_t mouse_feed_call(const uint64_t a[8]) {
    if (!o_mouse_feed) return 0;
    return o_mouse_feed((void*)a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], 0, 0);
}

// idle carrier (mouse still -> no feed events to piggyback on): inject the
// button-record pair from the captured templates with the current x
void clicker_idle_inject() {
    if (GetTickCount64() - g_last_feed_ms < 120) return;   // stream active
    if (!clicker_due()) return;
    if (!g_have_up || !g_have_dn) return;
    uint64_t cu[8], cd[8];
    memcpy(cu, g_btn_up_tpl, sizeof(cu));
    memcpy(cd, g_btn_dn_tpl, sizeof(cd));
    long i = g_mpos;
    if (g_mring[i].t) {
        cu[3] = g_mring[i].a[3];
        cd[3] = g_mring[i].a[3];
    }
    mouse_feed_call(cu);
    mouse_feed_call(cd);
    static volatile LONG s_synth = 0;
    long cnt = InterlockedIncrement(&s_synth);
    if (cnt == 1 || cnt % 25 == 0) MC_LOG("clicker(idle): synthetic click #%ld", cnt);
}

// synthesize a click at the CURRENT position: newest feed event + flip only
// the button-state byte (template learned from the player's last real click).
// Replaying the captured pair as-is replayed STALE deltas -> camera drift.
bool mouse_feed_click_synth(uint64_t dn[8], uint64_t up[8]) {
    static int s_diff = -1;
    static uint64_t s_dv = 0, s_uv = 0;
    static bool s_have = false, s_logged = false;
    if (!s_have) {
        uint64_t a[8], b[8];
        if (!mouse_feed_click_pair(a, b)) return false;
        for (int q = 0; q < 8; ++q)
            if (a[q] != b[q]) { s_diff = q; s_dv = a[q]; s_uv = b[q]; break; }
        if (s_diff < 0) return false;
        s_have = true;
        if (!s_logged) {
            s_logged = true;
            MC_LOG("mouse feed template: diff=arg%d down=%llx up=%llx", s_diff,
                   (unsigned long long)s_dv, (unsigned long long)s_uv);
            MC_LOG("  dn: %llx %llx %llx %llx %llx %llx %llx %llx",
                   (unsigned long long)a[0], (unsigned long long)a[1], (unsigned long long)a[2], (unsigned long long)a[3],
                   (unsigned long long)a[4], (unsigned long long)a[5], (unsigned long long)a[6], (unsigned long long)a[7]);
            MC_LOG("  up: %llx %llx %llx %llx %llx %llx %llx %llx",
                   (unsigned long long)b[0], (unsigned long long)b[1], (unsigned long long)b[2], (unsigned long long)b[3],
                   (unsigned long long)b[4], (unsigned long long)b[5], (unsigned long long)b[6], (unsigned long long)b[7]);
        }
    }
    // positional-arg mask: args that ever differ between consecutive recent
    // events are coordinates/deltas -> zero them so replaying never re-applies
    // stale movement (that dragged the camera when the player moved the mouse)
    static uint64_t s_posmask = 0;
    static ULONGLONG s_mask_t = 0;
    ULONGLONG nowms = GetTickCount64();
    if (nowms - s_mask_t > 1000) {
        s_mask_t = nowms;
        s_posmask = 0;
        long prev = g_mpos;
        for (int k = 1; k < 40; ++k) {
            long ii = (g_mpos - k) & 255;
            if (!g_mring[ii].t || !g_mring[prev].t) break;
            for (int q = 1; q < 8; ++q)          // skip arg0 (this-ptr)
                if (g_mring[ii].a[q] != g_mring[prev].a[q]) s_posmask |= (1ull << q);
            prev = ii;
        }
    }
    long i = g_mpos;
    if (!g_mring[i].t) return false;
    memcpy(dn, g_mring[i].a, sizeof(uint64_t) * 8);
    memcpy(up, g_mring[i].a, sizeof(uint64_t) * 8);
    for (int q = 1; q < 8; ++q)
        if ((s_posmask >> q) & 1ull) { dn[q] = 0; up[q] = 0; }
    dn[s_diff] = s_dv;
    up[s_diff] = s_uv;
    return true;
}

// ---- the clicker phase machine (every injector asks THIS) -------------------
static volatile LONG g_gi_phase = 0;
static volatile ULONGLONG g_gi_phase_until = 0;
static ULONGLONG g_gi_next_click = 0;
int gi_click_phase() {
    ClickerCfg c = Bus::inst().clicker();
    bool menu; { std::lock_guard<std::mutex> g(mod().mtx); menu = mod().overlay_open; }
    if (!c.on || menu) { g_gi_phase = 0; return 0; }
    if (!Bus::inst().in_world()) { g_gi_phase = 0; return 0; }
    if (!sdk::local_player_valid()) { g_gi_phase = 0; return 0; }  // menus never qualify
    if (!real_key_down(VK_LBUTTON)) { g_gi_phase = 0; return 0; }
    ULONGLONG now = GetTickCount64();
    if (g_gi_phase == 1 && now < g_gi_phase_until) return 1;   // release window
    if (g_gi_phase == 1) {   // release over -> fresh press edge, schedule next
        g_gi_phase = 2;
        double base = 1000.0 / (c.cps > 1.f ? c.cps : 1.f);
        double w = base + c.jitter * ((double)(rand() % 2001) / 1000.0 - 1.0);
        if (w < 55.0) w = 55.0;
        g_gi_next_click = now + (ULONGLONG)w;
        return 2;
    }
    if (g_gi_phase == 2 && now < g_gi_next_click) return 2;    // hold pressed
    if (g_gi_phase == 2) {   // next click: begin the release window
        g_gi_phase = 1;
        g_gi_phase_until = now + 45;
        return 1;
    }
    // phase 0 -> arm: first cycle starts with a release (so the game sees an edge)
    g_gi_phase = 1;
    g_gi_phase_until = now + 45;
    return 1;
}

// ---- CLICKER v7 — hook the FUNCTION BODY (0x3b940), not the vtable slot ----
// The game dispatches indirect calls via call [cell] -> jmp/validate rax;
// a wine CFG layer can silently drop foreign vtable targets (v6 fired once
// in a blue moon). With the ORIGINAL pointer left in the slot the dispatch
// validates the legit function and lands on our MinHook trampoline inside.
typedef unsigned (*t_ms14)(void*, void*);
static t_ms14 o_ms14 = nullptr;
static volatile LONG g_ms14n = 0;
static unsigned h_ms14(void* s, void* out) {
    unsigned r = o_ms14(s, out);
    // ---- AimAssist v8.5: accumulator riding on OUT -----------------------
    // The reading objects are a rotating pool of hw SNAPSHOTS (touching them
    // corrupts history -> chaos, v8-v8.4). But the consumer's delta is
    // out_now - out_prev, and OUT is rewritten each poll by the getter's
    // memcpy — so adding our OWN accumulator A after the copy gives
    // delta = hw_delta + (A_now - A_prev) = hw_delta + rate, ALWAYS correct:
    // real mouse passes through, aim steps add on top, nothing ever snaps.
    if (out && sdk::readable_ptr(out, 0x18)) {
        static int64_t s_A = 0;            // our accumulator (lives outside the game)
        AimPull ap = Bus::inst().aim();
        // v8.6: magnetism, not a lock.
        //  strength — pull hardness (fraction of error chased per tick)
        //  speed    — hard cap on the pull rate (px/poll)
        //  zone     — full stop inside 0.4 hitbox (stop_deg), soft fade
        //             from 2x the zone to the zone edge, so the pull melts
        //             away instead of snapping off
        float target = 0.f;
        if (ap.active) {
            float ey = ap.e_yaw;
            float stop = ap.stop_deg;
            float ae = fabsf(ey);
            if (ae > stop) {
                float zf = (ae - stop) / stop;         // 0 at edge .. 1 at 2x
                if (zf > 1.f) zf = 1.f;
                float rr = ey * 0.55f * (0.25f + 0.75f * ap.strength) * zf;
                float cap = 2.5f * ap.speed;           // speed slider = rate cap
                if (rr > cap) rr = cap; if (rr < -cap) rr = -cap;
                target = rr;
            }
        }
        // rate smoothing: the pull itself eases in/out — kills the jerk
        static float s_rate = 0.f;
        s_rate += (target - s_rate) * 0.20f;
        if (fabsf(s_rate) < 0.05f) s_rate = 0.f;
        s_A += (int64_t)(long)lroundf(s_rate);
        *(int64_t*)((uint8_t*)out + 8) += s_A;
        static volatile LONG s_n86 = 0; long k86 = InterlockedIncrement(&s_n86);
        if (k86 <= 10 || (k86 % 4000) == 0)
            MC_LOG("aim(v8.6): #%ld eYaw=%.2f stop=%.1f rate=%.2f A=%lld",
                   k86, ap.e_yaw, ap.stop_deg, s_rate, (long long)s_A);
    }
    if (out && sdk::readable_ptr(out, 4)) {
        uint32_t* buttons = (uint32_t*)out;
        int ph = gi_click_phase();
        // CPS edges (before modifying buttons): synthetic = entering ph2,
        // real = bit0 rising in the unmodified state
        static int s_prev_ph = 0;
        static uint32_t s_prev_real = 0;
        uint32_t real_b = *buttons;
        if (s_prev_ph != 2 && ph == 2) Bus::inst().note_click();
        if ((real_b & 1u) && !(s_prev_real & 1u)) Bus::inst().note_click();
        s_prev_ph = ph; s_prev_real = real_b;
        if (ph == 1) *buttons &= ~1u;       // synthetic release edge
        else if (ph == 2) *buttons |= 1u;   // synthetic press
        long k = InterlockedIncrement(&g_ms14n);
        if (k <= 12 || (k % 600) == 0)
            MC_LOG("clicker(fn14): #%ld phase=%d buttons=%x", k, ph, *buttons);
    }
    return r;
}

// vtable-slot clicker appliers (armed by gameinput instrumentation)
void gi_apply_click(uint64_t out) {
    if (out < 0x10000 || !sdk::readable_ptr((void*)out, 8)) return;
    uint64_t* buttons = (uint64_t*)out;
    int ph = gi_click_phase();
    if (ph == 1) *buttons &= ~1ull;        // synthetic release edge
    else if (ph == 2) *buttons |= 1ull;    // synthetic press
    static volatile LONG g_gi9n = 0;
    long k = InterlockedIncrement(&g_gi9n);
    if (k <= 10 || (k % 600) == 0)
        MC_LOG("clicker(vt9): #%ld phase=%d buttons=%llx", k, ph,
               (unsigned long long)*buttons);
}
static volatile LONG g_gi14n = 0;
void gi_apply_click14(uint64_t out) {
    if (out < 0x10000 || !sdk::readable_ptr((void*)out, 4)) return;
    uint32_t* buttons = (uint32_t*)out;
    int ph = gi_click_phase();
    if (ph == 1) *buttons &= ~1u;         // synthetic release edge
    else if (ph == 2) *buttons |= 1u;     // synthetic press
    long k = InterlockedIncrement(&g_gi14n);
    if (k <= 12 || (k % 600) == 0)
        MC_LOG("clicker(vt14): #%ld phase=%d buttons=%x", k, ph, *buttons);
}

// ================ CLICKER v5: RAW INPUT synthesis (the real path) ================
// Static RE verdict (game dump 1.26.52.3): the game imports RegisterRawInput-
// Devices + GetRawInputData and does NOT import XInput/DInput/SendInput —
// the mouse path is WM_INPUT raw input delivered by wine. The GameInput
// reading getters are never called by the game (the 160Hz sv4 poller writes
// its OUT slot into wine statics — a wine-side bridge, not the game).
// So: hook GetRawInputData IN-PROCESS; a worker posts WM_INPUT with a MAGIC
// lParam to the game's own window; the hook fabricates a RAWMOUSE event with
// RI_MOUSE_LEFT_BUTTON_DOWN/UP on the CPS schedule. PostMessage to our own
// window is purely internal — no X server, no SendInput, works while moving.
#define MC_RID_INPUT_  0x10000003u
#define MC_RID_HEADER_ 0x10000005u
#define MC_MAGIC_BASE_ 0x5C11C000ull
static volatile LONG g_raw_hits = 0;
static volatile LONG g_raw_real_calls = 0, g_raw_real_mouse = 0;
typedef UINT (WINAPI* t_GetRawInputData)(HANDLE, UINT, LPVOID, PUINT, UINT);
static UINT WINAPI h_GetRawInputData(HANDLE hRaw, UINT cmd, LPVOID buf, PUINT sz, UINT hdr_sz);
static t_GetRawInputData o_GetRawInputData;
static void raw_click_worker();

static UINT WINAPI h_GetRawInputData(HANDLE hRaw, UINT cmd, LPVOID buf, PUINT sz, UINT hdr_sz) {
    uintptr_t h = (uintptr_t)hRaw;
    if (h < MC_MAGIC_BASE_) {   // REAL call from the game
        long k = InterlockedIncrement(&g_raw_real_calls);
        if (cmd == MC_RID_INPUT_ && buf && sz && *sz >= 0x40) {
            uint32_t ty = *(uint32_t*)((uint8_t*)buf + 0);
            if (ty == 0) InterlockedIncrement(&g_raw_real_mouse);
            if (k <= 6 || (k % 3000) == 0) {
                uint16_t bf = *(uint16_t*)((uint8_t*)buf + 0x28 + 4);
                MC_LOG("raw(REAL): #%ld type=%u btnflags=%x (mouse=%ld)", k, ty, bf, g_raw_real_mouse);
            }
        } else if (k <= 3) {
            MC_LOG("raw(REAL): #%ld cmd=%x (header query?)", k, cmd);
        }
    }
    if (h >= MC_MAGIC_BASE_ && h < MC_MAGIC_BASE_ + 16) {
        BOOL down = (h & 1) != 0;
        if (cmd == MC_RID_HEADER_) {
            if (!sz) return (UINT)-1;
            if (buf) {
                memset(buf, 0, 0x28);
                ((DWORD*)buf)[0] = 0;        // dwType = RIM_TYPEMOUSE
                ((DWORD*)buf)[1] = 0x40;     // dwSize
            }
            *sz = 0x28;
            return 0x28;
        }
        if (!sz) return (UINT)-1;
        if (!buf) { *sz = 0x40; return 0; }                 // size query
        if (*sz < 0x40) { *sz = 0x40; return (UINT)-1; }    // need bigger buf
        memset(buf, 0, 0x40);
        uint8_t* b = (uint8_t*)buf;
        *(DWORD*)(b + 0x00) = 0;             // dwType = mouse
        *(DWORD*)(b + 0x04) = 0x40;          // dwSize
        *(uint16_t*)(b + 0x28 + 4) = down ? 0x0001 : 0x0002;  // LEFT_DOWN : LEFT_UP
        *sz = 0x40;
        long k = InterlockedIncrement(&g_raw_hits);
        if (k <= 10 || (k % 200) == 0) MC_LOG("clicker(raw): #%ld %s", k, down ? "DOWN" : "UP");
        return 0x40;
    }
    return o_GetRawInputData(hRaw, cmd, buf, sz, hdr_sz);
}
static void raw_click_worker() {
    Sleep(3000);                       // let the game boot and register raw input
    int last = 0;
    for (;;) {
        Sleep(4);
        int ph = gi_click_phase();
        if (ph == last) continue;
        HWND hw = overlay_game_hwnd();
        if (hw) {
            // press edge -> DOWN event; release edge -> UP; drop-to-0 while
            // pressed -> a final UP so the game never sticks
            LPARAM lp;
            if (ph == 2)      lp = (LPARAM)(MC_MAGIC_BASE_ + 1);
            else if (ph == 1) lp = (LPARAM)(MC_MAGIC_BASE_ + 0);
            else              lp = (last == 2) ? (LPARAM)MC_MAGIC_BASE_ : 0;
            if (lp) {
                PostMessageW(hw, 0x00FF /*WM_INPUT*/, 0, lp);
                MC_LOG("clicker(raw): post %d->%d (%s)", last, ph,
                       (lp & 1) ? "DOWN" : "UP");
            }
        }
        last = ph;
    }
}

// ---- installation ------------------------------------------------------------
bool install_input_hooks() {
    struct H { const char* n; void* det; void** orig; } hs[] = {
        {"MouseDevice_feed", (void*)h_mouse_feed, (void**)&o_mouse_feed},
        {"Keyboard_feed",    (void*)h_kb_feed,    (void**)&o_kb_feed},
    };
    int ok = 0;
    for (auto& h : hs) {
        uintptr_t a = sig_get(h.n);
        if (!a) { MC_WARN("hook %s: address not resolved", h.n); continue; }
        if (MH_CreateHook((void*)a, h.det, h.orig) == MH_OK) {
            MH_EnableHook((void*)a);
            ok++;
            MC_LOG("hook %s installed @0x%llx", h.n, (unsigned long long)a);
        } else MC_ERR("hook %s MH_CreateHook failed", h.n);
    }

    // clicker v7: hook the GameInput mouse-state getter BODY (0x3b940).
    // The module may be loaded by now (worker saw it at ~0s); if not, retry
    // from a delayed worker AFTER boot settles (MH_CreateHook freezes all
    // threads — doing it during early boot deadlocks the game).
    {
        HMODULE gim = GetModuleHandleW(L"GameInputRedist.dll");
        if (gim) {
            void* fn = (uint8_t*)gim + Offsets::GI_MOUSE_GETTER_RVA;
            if (MH_CreateHook(fn, (void*)h_ms14, (void**)&o_ms14) == MH_OK) {
                MH_EnableHook(fn);
                MC_LOG("hook GameInput_GetMouseState installed @%p (clicker v7)", fn);
            } else MC_WARN("hook GetMouseState v7 FAILED");
        } else {
            std::thread([]{
                for (int i = 0; i < 60 && !GetModuleHandleW(L"GameInputRedist.dll"); ++i) Sleep(1000);
                Sleep(5000);   // let early boot finish before freezing threads
                HMODULE gim = GetModuleHandleW(L"GameInputRedist.dll");
                if (gim && !o_ms14) {
                    void* fn = (uint8_t*)gim + Offsets::GI_MOUSE_GETTER_RVA;
                    if (MH_CreateHook(fn, (void*)h_ms14, (void**)&o_ms14) == MH_OK) {
                        MH_EnableHook(fn);
                        MC_LOG("hook GameInput_GetMouseState installed (delayed) @%p", fn);
                    }
                }
            }).detach();
        }
    }
    return ok > 0;
}

void install_raw_clicker() {
    // clicker v5: the game reads the mouse via WM_INPUT raw input
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        void* f = (void*)GetProcAddress(u32, "GetRawInputData");
        if (f && MH_CreateHook(f, (void*)h_GetRawInputData, (void**)&o_GetRawInputData) == MH_OK) {
            MH_EnableHook(f);
            MC_LOG("hook GetRawInputData installed @%p (clicker v5 armed)", f);
        } else MC_WARN("hook GetRawInputData FAILED");
    }
    std::thread(raw_click_worker).detach();
}

} // namespace game
} // namespace mc
