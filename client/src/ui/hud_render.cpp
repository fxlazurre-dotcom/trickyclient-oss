// TrickyClient OSS — ui layer: HUD render context + dispatch.
// The HUD modules (modules/hud/*.cpp) draw through HudCtx helpers — they never
// include imgui. Everything below is a verbatim port of the monolith's HUD
// primitives (hud.js): text+shadow, panel, drag with dashed outline, stepped bg.
#include "ui/clickgui.h"
#include "ui/theme.h"
#include "ui/fonts.h"
#include "modules/registry.h"
#include "modules/module.h"
#include "core/state.h"

#include <imgui.h>
#include <cstring>
#include <cmath>

namespace mc {

static float h_tw(HudCtx& c, const char* s, float fs);

// text with optional shadow (black .55 at +0.7 logical) — hud.js F.t
static float h_text(HudCtx& c, const char* s, float x, float y, float fs, unsigned col, bool shadow) {
    ImDrawList* dl = (ImDrawList*)c.dl;
    if (shadow)
        dl->AddText(cg_fonts.mont16, fs, ImVec2(x + 1.4f, y + 1.4f), IM_COL32(0, 0, 0, 140), s);
    dl->AddText(cg_fonts.mont16, fs, ImVec2(x, y), col, s);
    return h_tw(c, s, fs);
}
static float h_tw(HudCtx& c, const char* s, float fs) {
    return cg_fonts.mont16->CalcTextSizeA(fs, FLT_MAX, -1.f, s).x;
}
// panel: rounded #17131D, opacity from settings, outline forced OFF (mock init)
static void h_rect(HudCtx& c, float x, float y, float w, float h, float r, unsigned col) {
    ((ImDrawList*)c.dl)->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), col, r);
}
static void h_rectg(HudCtx& c, float x, float y, float w, float h, unsigned c1, unsigned c2) {
    ((ImDrawList*)c.dl)->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x + w, y + h),
                                                 c1, c2, c2, c1);
}
static void h_line(HudCtx& c, float x0, float y0, float x1, float y1, unsigned col, float th) {
    ((ImDrawList*)c.dl)->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), col, th);
}
static void h_circle(HudCtx& c, float x, float y, float r, unsigned col) {
    ((ImDrawList*)c.dl)->AddCircleFilled(ImVec2(x, y), r, col, 12);
}
static unsigned h_lerp(HudCtx&, unsigned a, unsigned b, float t) {
    return lerp_col(a, b, t);
}
static unsigned h_with_a(HudCtx&, unsigned c, float a01) { return with_a(c, a01); }
static unsigned h_mul_a(HudCtx&, unsigned c, float f) { return mul_a(c, f); }

// drag while the menu is open; dashed outline hint (#ff5fa8, dash 4/3)
static void h_drag(HudCtx& c, const char* id, float* px, float* py, float w, float h) {
    ImDrawList* dl = (ImDrawList*)c.dl;
    if (!c.menu_open) return;
    ImGuiIO& io = ImGui::GetIO();
    static const char* s_drag = nullptr;
    static float s_ox = 0, s_oy = 0;
    bool inside = io.MousePos.x >= *px && io.MousePos.x <= *px + w &&
                  io.MousePos.y >= *py && io.MousePos.y <= *py + h;
    if (io.MouseClicked[0] && inside && !s_drag) {
        s_drag = id; s_ox = *px - io.MousePos.x; s_oy = *py - io.MousePos.y;
    }
    if (s_drag && strcmp(s_drag, id) == 0) {
        if (io.MouseDown[0]) {
            *px = io.MousePos.x + s_ox; *py = io.MousePos.y + s_oy;
            float mx = io.DisplaySize.x - w - 4.f, my = io.DisplaySize.y - h - 4.f;
            if (*px < 4.f) *px = 4.f; if (*py < 4.f) *py = 4.f;
            if (*px > mx) *px = mx;  if (*py > my) *py = my;
        } else s_drag = nullptr;
    }
    const float dash = 8.f, gap = 6.f;
    ImU32 col = IM_COL32(255, 95, 168, 140);
    for (float x = *px; x < *px + w; x += dash + gap) {
        float x2 = x + dash; if (x2 > *px + w) x2 = *px + w;
        dl->AddLine(ImVec2(x, *py - 5.f), ImVec2(x2, *py - 5.f), col, 1.f);
        dl->AddLine(ImVec2(x, *py + h + 5.f), ImVec2(x2, *py + h + 5.f), col, 1.f);
    }
    for (float y = *py; y < *py + h; y += dash + gap) {
        float y2 = y + dash; if (y2 > *py + h) y2 = *py + h;
        dl->AddLine(ImVec2(*px - 5.f, y), ImVec2(*px - 5.f, y2), col, 1.f);
        dl->AddLine(ImVec2(*px + w + 5.f, y), ImVec2(*px + w + 5.f, y2), col, 1.f);
    }
}

// rounded stepped polygon (arraylist background) — hud.js roundedPoly
static void h_stepped(HudCtx& c, float pos_x, float pos_y, const float* rws, const float* ry,
                      int n, float w, float h, float r, unsigned fill) {
    if (n <= 0 || !rws || !ry) return;   // empty list must never reach the fan
    ImDrawList* dl = (ImDrawList*)c.dl;
    float X0 = pos_x, Y0 = pos_y;
    struct P { float x, y; };
    P pts[64];
    int np = 0;
    auto X = [&](float x) { return X0 + x; };
    pts[np++] = {X(0), Y0};
    pts[np++] = {X(rws[0]), Y0};
    for (int i = 0; i < n && np < 62; i++) {
        float bot = (i == n - 1) ? h : ry[i];
        pts[np++] = {X(rws[i]), Y0 + bot};
        if (i < n - 1) pts[np++] = {X(rws[i + 1]), Y0 + bot};
    }
    pts[np++] = {X(0), Y0 + h};
    // rounded corners via bezier joints
    dl->PathClear();
    auto mid = [](const P& a, const P& b) { return P{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}; };
    P m0 = mid(pts[0], pts[1]);
    dl->PathLineTo(ImVec2(m0.x, m0.y));
    for (int i = 1; i <= np; i++) {
        P p = pts[i % np], a = pts[i - 1], b = pts[(i + 1) % np];
        P mm = mid(p, b);
        float d1 = hypotf(p.x - a.x, p.y - a.y), d2 = hypotf(p.x - b.x, p.y - b.y);
        float rr = std::max(0.f, std::min(r, std::min(d1, d2) * 0.5f));
        float l1 = d1 > 1e-6f ? rr / d1 : 0.f, l2 = d2 > 1e-6f ? rr / d2 : 0.f;
        P t1{p.x + (a.x - p.x) * l1, p.y + (a.y - p.y) * l1};
        P t2{p.x + (b.x - p.x) * l2, p.y + (b.y - p.y) * l2};
        dl->PathLineTo(ImVec2(t1.x, t1.y));
        dl->PathBezierQuadraticCurveTo(ImVec2(p.x, p.y), ImVec2(t2.x, t2.y));
        dl->PathLineTo(ImVec2(mm.x, mm.y));
    }
    dl->PathFillConvex(fill);
}

void draw_hud() {
    if (!modules_hud_any()) return;
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    HudCtx c;
    c.dl = dl;
    c.W = io.DisplaySize.x; c.H = io.DisplaySize.y;
    c.t = (float)ImGui::GetTime();
    { auto& m = mod(); std::lock_guard<std::mutex> g(m.mtx); c.menu_open = m.overlay_open; }
    c.acc1 = th_acc1();
    c.acc2 = th_acc2();
    c.bg_col = C_HUDBG;
    c.txt_col = C_TXT_H;
    c.fps = (float)io.Framerate;
    c.tw = h_tw; c.text = h_text;
    c.rect = h_rect; c.rectg = h_rectg; c.line = h_line; c.circle = h_circle;
    c.stepped = h_stepped; c.drag = h_drag;
    c.lerp = h_lerp; c.with_a = h_with_a; c.mul_a = h_mul_a;

    int n; Module* const* all = modules_all(&n);
    for (int i = 0; i < n; i++) {
        Module* m = all[i];
        const char* cat = m->category;
        if (m->enabled && (!strcmp(cat, "HUD") || !strcmp(cat, "VISUAL")))
            m->on_hud(c);
    }
}

} // namespace mc
