// TrickyClient OSS — ui layer: widgets + animation helpers (moved verbatim)
#include "ui/widgets.h"
#include "core/state.h"
#include "core/log.h"
#include "ui/theme.h"
#include "imgui_internal.h"   // IM_PI, ImDrawListSharedData (PrimVtx white-pixel UV)
#include <algorithm>
#include <windows.h>
#include <mmsystem.h>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace mc {

static float s_gtime = 0.f;   // seconds clock for animations
float ui_time() { return s_gtime; }
void ui_advance_clock(float dt) { s_gtime += dt; }

// per-widget animation storage: id -> eased value toward target
static std::unordered_map<ImGuiID, float> g_anim;
float anim_step(ImGuiID id, float target, float dt, float dur) {
    float& v = g_anim[id];
    if (v != target) {
        float step = dt / (dur > 0.f ? dur : 0.001f);
        v = v < target ? (v + step > target ? target : v + step)
                       : (v - step < target ? target : v - step);
    }
    return v;
}

// horizontal-gradient ROUNDED rect: convex fan with per-vertex colors
// (AddRectFilledMultiColor has no rounding; decomposing into bands left
// seams because each band interpolated the gradient over its own width)
void grad_rounded_rect_h(ImDrawList* dl, const ImVec2& a, const ImVec2& b,
                         ImU32 c1, ImU32 c2, float r) {
    if (b.x - a.x < 1.f || b.y - a.y < 1.f) return;
    r = std::max(0.f, std::min({ r, (b.x - a.x) * 0.5f, (b.y - a.y) * 0.5f }));
    if (r < 0.5f) { dl->AddRectFilledMultiColor(a, b, c1, c2, c2, c1); return; }
    ImVec2 pt[40]; int n = 0;
    auto arc = [&](float cx, float cy, float a0, float a1) {
        const int seg = 8;   // match AddRectFilled's smooth corners
        for (int i = 0; i <= seg; i++) {
            float an = a0 + (a1 - a0) * i / seg;
            pt[n++] = ImVec2(cx + cosf(an) * r, cy + sinf(an) * r);
        }
    };
    arc(a.x + r, a.y + r, IM_PI, IM_PI * 1.5f);
    arc(b.x - r, a.y + r, IM_PI * 1.5f, IM_PI * 2.f);
    arc(b.x - r, b.y - r, 0.f, IM_PI * 0.5f);
    arc(a.x + r, b.y - r, IM_PI * 0.5f, IM_PI);
    n--;   // last point == first
    ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    float span = std::max(1e-3f, b.x - a.x);
    auto col = [&](const ImVec2& p) { return lerp_col(c1, c2, clamp01((p.x - a.x) / span)); };
    ImU32 cc = col(c);
    const ImVec2& uv = dl->_Data->TexUvWhitePixel;
    dl->PrimReserve(n * 3, n * 3);
    for (int i = 0; i < n; i++) {
        const ImVec2& p1 = pt[i];
        const ImVec2& p2 = pt[(i + 1) % n];
        dl->PrimVtx(p1, uv, col(p1));
        dl->PrimVtx(p2, uv, col(p2));
        dl->PrimVtx(c, uv, cc);
    }
}

// ------------------------------------------------------------- tick sound ----
void ui_tick_sound() {
    if (!mod().opt_tick_sound) return;
    static HWAVEOUT s_hw = nullptr;
    static WAVEHDR s_hdr[2] = {};
    static short s_buf[2][1764] = {};
    static bool s_ok = false, s_tried = false, s_flip = false;
    if (!s_tried) {
        s_tried = true;
        WAVEFORMATEX fmt{};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = 1; fmt.wBitsPerSample = 16;
        fmt.nSamplesPerSec = 44100;
        fmt.nBlockAlign = 2; fmt.nAvgBytesPerSec = 88200;
        s_ok = waveOutOpen(&s_hw, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR;
        if (s_ok) {
            for (int b = 0; b < 2; ++b) {
                for (int i = 0; i < 1764; ++i) {
                    float t = i / 44100.f;
                    float amp = 0.04f * expf(-118.3f * t);
                    if (amp < 0.001f) amp = 0.001f;
                    float sq = (fmodf(t * 2000.f, 1.f) < 0.5f) ? 1.f : -1.f;
                    s_buf[b][i] = (short)(amp * sq * 32767.f);
                }
                s_hdr[b].lpData = (LPSTR)s_buf[b];
                s_hdr[b].dwBufferLength = sizeof(s_buf[b]);
            }
            MC_LOG("clickgui: tick sound ready (waveOut ok)");
        } else {
            MC_LOG("clickgui: no audio device, tick sound disabled");
        }
    }
    if (!s_ok) return;
    int b = s_flip ? 1 : 0;
    waveOutUnprepareHeader(s_hw, &s_hdr[b], sizeof(WAVEHDR));
    waveOutPrepareHeader(s_hw, &s_hdr[b], sizeof(WAVEHDR));
    if (waveOutWrite(s_hw, &s_hdr[b], sizeof(WAVEHDR)) == MMSYSERR_NOERROR)
        s_flip = !s_flip;
}

// ---- polled keyboard -------------------------------------------------------
bool key_edge(int vk) {
    static SHORT prev[256] = {};
    SHORT cur = (GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
    bool edge = cur && !prev[vk];
    prev[vk] = cur;
    return edge;
}
bool key_repeat(int vk) {          // press + hold autorepeat
    static ULONGLONG t0[256] = {}, tlast[256] = {};
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    ULONGLONG now = GetTickCount64();
    if (!down) { t0[vk] = 0; return false; }
    if (!t0[vk]) { t0[vk] = now; tlast[vk] = now; return true; }
    if (now - tlast[vk] > 40 && now - t0[vk] > 350) { tlast[vk] = now; return true; }
    return false;
}
void poll_typed(char* buf, size_t cap, bool hex_only) {
    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    auto push = [&](char c) {
        size_t l = strlen(buf);
        if (l + 1 < cap) { buf[l] = c; buf[l + 1] = 0; }
    };
    for (int vk = 'A'; vk <= 'Z'; vk++) {
        if (!key_edge(vk)) continue;
        char c = shift ? (char)vk : (char)(vk + 32);
        if (hex_only) {
            bool hexch = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            if (!hexch) continue;
        }
        push(c);
    }
    for (int vk = '0'; vk <= '9'; vk++)
        if (key_edge(vk)) push((char)vk);
    if (!hex_only && key_edge(VK_SPACE)) push(' ');
}

float ui_tw(ImFont* f, float size, const char* s) {
    return f->CalcTextSizeA(size, FLT_MAX, -1.f, s).x;
}

} // namespace mc
