// TrickyClient OSS — ui layer: the nursultan-style ClickGUI.
// 1:1 port of /new-style (index.html CSS, clickgui.js, mercury-mock.js) over
// ImGui/DX12. The mockup runs at gui scale K=2: every CSS/logical pixel below
// is already multiplied by 2 ("design px"). Theme = two gradient colors
// (acc1/acc2) picked in the GUI's bottom bar; badges/chips use their average.
// Rows/settings render GENERICALLY from the module registry (modules/*.cpp).
#include "ui/clickgui.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/fonts.h"
#include "modules/registry.h"
#include "modules/module.h"
#include "game/sigs/sigs.h"
#include "core/log.h"
#include "core/state.h"

#include <imgui.h>
#include "imgui_internal.h"   // IM_PI, ImDrawListSharedData (PrimVtx white-pixel UV)
#include <windows.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <unordered_map>

namespace mc {

// ============================================================ frame clock =====
// open/close animation (#ui: opacity .18s, scale .94->1; closing .15s -> .96)
static float s_open_raw = 0.f;   // linear progress toward m.overlay_open
static float s_open     = 0.f;   // eased 0..1 (also the global alpha)

void clickgui_newframe() {
    ImGuiIO& io = ImGui::GetIO();
    ui_advance_clock(io.DeltaTime);
    ui_fonts_guard();
    theme_refresh();
    auto& m = mod();
    bool target;
    { std::lock_guard<std::mutex> g(m.mtx); target = m.overlay_open; }
    float tgt = target ? 1.f : 0.f;
    float rate = target ? io.DeltaTime / 0.18f : io.DeltaTime / 0.15f;
    if (s_open_raw < tgt) s_open_raw = std::min(tgt, s_open_raw + rate);
    else if (s_open_raw > tgt) s_open_raw = std::max(tgt, s_open_raw - rate);
    s_open = nsmooth(s_open_raw);
}
bool clickgui_animating() { return s_open_raw > 0.001f; }

// ================================================================ geometry ====
// design px (mockup logical px * K=2)
static const float COL_W   = 224.f;   // .col width 112*2
static const float COL_R   = 12.f;    // border-radius 6*2
static const float COL_PAD = 10.f;    // padding 5*2
static const float GAP     = 16.f;    // #cols gap 8*2
static const float TOP_PAD = 56.f;    // #ui padding-top 28*2
static const float ROW_H   = 30.f;    // .row height 15*2
static const float ROW_R   = 8.f;     // .row border-radius 4*2
static const float ROW_MB  = 6.f;     // .mod margin-bottom 3*2
static const float HDR_H   = 42.f;    // h2 block (8 + 19 + 12 + ...)
static const float SET_H   = 18.f;    // one settings row 9*2
static const float BOT_BTN = 30.f;    // .c-btn 15*2
static const float BOT_R   = 10.f;    // 5*2
static const float BAR_W   = 220.f;   // #bar 110*2
static const float CP_W    = 252.f;   // #cp-pop 126*2

static bool s_hover(const ImVec2& a, const ImVec2& b) {
    return ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(a, b);
}
static bool s_click0(const ImVec2& a, const ImVec2& b) {
    return s_hover(a, b) && ImGui::IsMouseClicked(0);
}
static bool s_click1(const ImVec2& a, const ImVec2& b) {
    return s_hover(a, b) && ImGui::IsMouseClicked(1);
}

// row flash start times (-1 = none)
static std::unordered_map<ImGuiID, float> g_flash;
// expanded-set state per module row
static std::unordered_map<ImGuiID, bool> g_expanded;

// ============================================================ GUI: settings ==
// generic Setting access: state-bound values live under the state mutex,
// module-owned values under the registry mutex
static bool set_get_b(const Setting& s) {
    if (s.state_b) { auto& m = mod(); std::lock_guard<std::mutex> g(m.mtx); return *s.state_b; }
    std::lock_guard<std::mutex> g(modules_mtx()); return s.b;
}
static void set_set_b(Setting& s, bool v) {
    if (s.state_b) { auto& m = mod(); std::lock_guard<std::mutex> g(m.mtx); *s.state_b = v; }
    else { std::lock_guard<std::mutex> g(modules_mtx()); s.b = v; }
    modules_mark_dirty();
}
static float set_get_f(const Setting& s) {
    if (s.state_f) { auto& m = mod(); std::lock_guard<std::mutex> g(m.mtx); return *s.state_f; }
    std::lock_guard<std::mutex> g(modules_mtx()); return s.f;
}
static void set_set_f(Setting& s, float v) {
    if (s.state_f) { auto& m = mod(); std::lock_guard<std::mutex> g(m.mtx); *s.state_f = v; }
    else { std::lock_guard<std::mutex> g(modules_mtx()); s.f = v; }
    modules_mark_dirty();
}
static int set_get_i(const Setting& s) {
    if (s.state_i) { auto& m = mod(); std::lock_guard<std::mutex> g(m.mtx); return *s.state_i; }
    std::lock_guard<std::mutex> g(modules_mtx()); return s.cur;
}
static void set_set_i(Setting& s, int v) {
    if (s.state_i) { auto& m = mod(); std::lock_guard<std::mutex> g(m.mtx); *s.state_i = v; }
    else { std::lock_guard<std::mutex> g(modules_mtx()); s.cur = v; }
    modules_mark_dirty();
}

struct SliderActive { Module* mod; int set_idx; bool on; };
static SliderActive s_slider = {};

// draw one expanded settings block under a module row; returns block height
static float draw_sets(ImDrawList* dl, Module& md, const ImVec2& c_tl,
                       const ImVec2& c_br, float alpha, float sc, ImGuiID id) {
    (void)id;
    ImGuiIO& io = ImGui::GetIO();
    float y = c_tl.y;
    auto txt12 = [&](const char* s, const ImVec2& p, ImU32 col) {
        dl->AddText(cg_fonts.mont12, 12.f * sc, p, col, s);
    };

    if (md.debug_block) {
        // DebugInfo block: signatures summary + recent log lines
        int total = sig_count_(), okc = 0;
        char line[160];
        for (int i = 0; i < total; i++) if (sig_ok_(i)) okc++;
        snprintf(line, sizeof line, "sigs %d/%d ok", okc, total);
        txt12(line, ImVec2(c_tl.x, y), with_a(C_TXT, alpha));
        y += SET_H * sc;
        int shown = 0;
        for (int i = 0; i < total && shown < 2; i++) {
            if (sig_ok_(i)) continue;
            snprintf(line, sizeof line, "[-] %s", sig_name_(i));
            txt12(line, ImVec2(c_tl.x + 8.f * sc, y), with_a(IM_COL32(240, 90, 90, 255), alpha));
            y += SET_H * sc; shown++;
        }
        auto lines = Logger::inst().snapshot();
        int want = 5, drawn = 0;
        for (auto it = lines.rbegin(); it != lines.rend() && drawn < want; ++it, ++drawn) {
            snprintf(line, sizeof line, "%s", it->text);
            if (strlen(line) > 44) { line[42] = '~'; line[43] = 0; }
            txt12(line, ImVec2(c_tl.x + 8.f * sc, y), with_a(C_MUTED, alpha * 0.9f));
            y += SET_H * sc;
        }
        return y - c_tl.y;
    }

    for (int si = 0; si < md.nsets; si++) {
        Setting& sd = md.sets[si];
        if (sd.kind == Setting::BOOL) {                         // ---- bool ----
            ImVec2 a(c_tl.x, y), b(c_br.x, y + SET_H * sc);
            bool v = set_get_b(sd);
            // box 16x16 right
            ImVec2 ba(b.x - 16.f * sc, y + (SET_H - 16.f) * 0.5f * sc),
                   bb(b.x, ba.y + 16.f * sc);
            if (s_click0(a, b)) { set_set_b(sd, !v); ui_tick_sound(); v = !v; }
            dl->AddRectFilled(ba, bb, v ? th_accb(alpha) : with_a(C_BOXOFF, alpha), 4.f * sc);
            if (v) {  // CSS check: two strokes, rotate(40deg)
                dl->AddLine(ImVec2(ba.x + 4.4f*sc, ba.y + 8.6f*sc), ImVec2(ba.x + 6.8f*sc, ba.y + 11.4f*sc), with_a(IM_COL32(255,255,255,255), alpha), 2.2f * sc);
                dl->AddLine(ImVec2(ba.x + 6.8f*sc, ba.y + 11.4f*sc), ImVec2(ba.x + 12.f*sc, ba.y + 4.4f*sc), with_a(IM_COL32(255,255,255,255), alpha), 2.2f * sc);
            }
            txt12(sd.name, ImVec2(a.x, y + (SET_H - 12.f) * 0.5f * sc), with_a(IM_COL32(255,255,255,255), alpha));
            y += (SET_H + 10.f) * sc;
        } else if (sd.kind == Setting::FLOAT) {                 // ---- float ----
            float v = set_get_f(sd);
            char tval[24];
            snprintf(tval, sizeof tval, sd.fmt ? sd.fmt : "%.1f", v);
            // head: label + value badge
            float vw = ui_tw(cg_fonts.mont12, 11.f * sc, tval) + 12.f * sc;
            txt12(sd.name, ImVec2(c_tl.x, y + 3.f * sc), with_a(IM_COL32(255,255,255,255), alpha));
            dl->AddRectFilled(ImVec2(c_br.x - vw, y), ImVec2(c_br.x, y + 14.f * sc), th_accb(alpha), 4.f * sc);
            dl->AddText(cg_fonts.mont12, 11.f * sc, ImVec2(c_br.x - vw + 6.f * sc, y + 2.f * sc), with_a(IM_COL32(255,255,255,255), alpha), tval);
            y += SET_H * sc;
            // track
            ImVec2 ta(c_tl.x, y), tb(c_br.x, y + SET_H * sc);
            float frac = clamp01((v - sd.lo) / (sd.hi - sd.lo));
            bool hot = s_hover(ta, tb);
            if (s_click0(ta, tb))
                s_slider = { &md, si, true };
            if (s_slider.on && s_slider.mod == &md && s_slider.set_idx == si && ImGui::IsMouseDown(0)) {
                float fx = clamp01((io.MousePos.x - ta.x) / std::max(1.f, tb.x - ta.x));
                float nv = sd.lo + fx * (sd.hi - sd.lo);
                float st = (sd.hi - sd.lo) <= 2.f ? 0.01f : ((sd.hi - sd.lo) <= 100.f ? 0.1f : 1.f);
                if (sd.inte) nv = roundf(nv);
                else nv = roundf(nv / st) * st;
                nv = std::max(sd.lo, std::min(sd.hi, nv));
                if (nv != v) set_set_f(sd, nv);
                v = nv; frac = clamp01((v - sd.lo) / (sd.hi - sd.lo));
            }
            if (!ImGui::IsMouseDown(0)) s_slider.on = false;
            (void)hot;
            float cy = y + SET_H * 0.5f * sc;
            dl->AddRectFilled(ImVec2(ta.x, cy - 2.f*sc), ImVec2(tb.x, cy + 2.f*sc), mul_a(IM_COL32(255,255,255,255), alpha * 0.20f), 2.f * sc);
            if (frac > 0.001f) {
                ImVec2 f1(ta.x, cy - 2.f*sc), f2(ta.x + (tb.x - ta.x) * frac, cy + 2.f*sc);
                dl->AddRectFilledMultiColor(f1, f2, th_acc1(alpha), th_acc2(alpha), th_acc2(alpha), th_acc1(alpha));
            }
            dl->AddCircleFilled(ImVec2(ta.x + (tb.x - ta.x) * frac, cy), 7.f * sc, with_a(IM_COL32(255,255,255,255), alpha), 12);
            y += (SET_H + 10.f) * sc;
        } else if (sd.kind == Setting::ENUM && sd.opts) {       // ---- enum ----
            int cur = set_get_i(sd);
            char idx[8]; snprintf(idx, sizeof idx, "%d/%d", cur + 1, sd.nopts);
            txt12(sd.name, ImVec2(c_tl.x, y + 3.f * sc), with_a(IM_COL32(255,255,255,255), alpha));
            float iw = ui_tw(cg_fonts.mont12, 11.f * sc, idx);
            dl->AddText(cg_fonts.mont12, 11.f * sc, ImVec2(c_br.x - iw, y + 2.f * sc), with_a(C_MUTED, alpha), idx);
            y += SET_H * sc;
            float cx = c_tl.x;
            for (int oi = 0; oi < sd.nopts; oi++) {
                const char* o = sd.opts[oi];
                float ow = ui_tw(cg_fonts.mont12, 11.f * sc, o) + 16.f * sc;
                if (cx + ow > c_br.x && cx > c_tl.x) { cx = c_tl.x; y += SET_H * sc + 4.f * sc; }
                ImVec2 a(cx, y), b(cx + ow, y + 18.f * sc);
                bool sel = (oi == cur);
                bool hov = s_hover(a, b);
                if (s_click0(a, b)) { set_set_i(sd, oi); ui_tick_sound(); }
                dl->AddRectFilled(a, b, sel ? th_accb(alpha)
                                  : mul_a(IM_COL32(255,255,255,255), alpha * (hov ? 0.14f : 0.07f)), 4.f * sc);
                dl->AddText(cg_fonts.mont12, 11.f * sc, ImVec2(a.x + 8.f * sc, a.y + 3.f * sc),
                            with_a(IM_COL32(255,255,255,255), alpha), o);
                cx += ow + 4.f * sc;
            }
            y += (18.f + 12.f) * sc;
        }
    }
    return y - c_tl.y;
}

// ============================================================ GUI: the menu ==
static char s_search[64] = "";
static bool s_search_focus = false;

// color picker state
static bool  s_cp_open = false;
static int   s_cp_slot = 1;
static float s_cp_hsv[3] = { 285.f, 0.78f, 1.f };
static char  s_cp_hex[10] = "#cf3bff";
static bool  s_cp_hex_focus = false;
static int   s_cp_drag = 0;    // 0 none, 1 sv, 2 hue

static void cp_apply_hsv_to_state() {
    float rgb[3];
    hsv_to_rgb(s_cp_hsv[0], s_cp_hsv[1], s_cp_hsv[2], rgb);
    auto& m = mod();
    std::lock_guard<std::mutex> g(m.mtx);
    float* dst = s_cp_slot == 1 ? m.acc1 : m.acc2;
    for (int i = 0; i < 3; i++) dst[i] = rgb[i];
    snprintf(s_cp_hex, sizeof s_cp_hex, "#%02x%02x%02x",
             (int)(rgb[0]*255+0.5f), (int)(rgb[1]*255+0.5f), (int)(rgb[2]*255+0.5f));
}
static void cp_load_slot(int slot) {
    s_cp_slot = slot;
    auto& m = mod();
    std::lock_guard<std::mutex> g(m.mtx);
    const float* src = slot == 1 ? m.acc1 : m.acc2;
    float hsv[3]; rgb_to_hsv(src, hsv);
    s_cp_hsv[0] = hsv[0]; s_cp_hsv[1] = hsv[1]; s_cp_hsv[2] = hsv[2];
    snprintf(s_cp_hex, sizeof s_cp_hex, "#%02x%02x%02x",
             (int)(src[0]*255+0.5f), (int)(src[1]*255+0.5f), (int)(src[2]*255+0.5f));
}
static bool hex_valid(const char* s) {
    if (s[0] != '#') return false;
    for (int i = 1; i <= 6; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return s[7] == 0;
}
static void cp_apply_hex() {
    if (!hex_valid(s_cp_hex)) return;
    unsigned v = 0;
    sscanf(s_cp_hex + 1, "%x", &v);
    float rgb[3] = { ((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f };
    float hsv[3]; rgb_to_hsv(rgb, hsv);
    s_cp_hsv[0] = hsv[0]; s_cp_hsv[1] = hsv[1]; s_cp_hsv[2] = hsv[2];
    auto& m = mod();
    std::lock_guard<std::mutex> g(m.mtx);
    float* dst = s_cp_slot == 1 ? m.acc1 : m.acc2;
    for (int i = 0; i < 3; i++) dst[i] = rgb[i];
}

// module row ON state (toggle_state tools read the bound state field)
static bool module_on(Module& m) {
    if (m.toggle_state) { auto& st = mod(); std::lock_guard<std::mutex> g(st.mtx); return *m.toggle_state; }
    return m.enabled;
}

void draw_clickgui() {
    auto& m = mod();
    ImGuiIO& io = ImGui::GetIO();
    if (s_open <= 0.001f && !m.overlay_open) return;

    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    const float A = s_open;                    // global alpha
    const float os = 0.94f + 0.06f * s_open;   // open scale (0.94 -> 1)

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, 0);
    ImGui::PushStyleColor(ImGuiCol_Border, 0);
    ImGui::Begin("##nursultan", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoNav);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool live = s_open_raw > 0.999f;     // interactions only when fully open

    // ESC unfocuses text fields first (the menu close itself is polled in
    // the overlay and always wins while the menu is open)
    if (live && key_edge(VK_ESCAPE) && (s_search_focus || s_cp_hex_focus)) {
        s_search_focus = false; s_cp_hex_focus = false;
    }
    // search field: typed characters via polled keyboard
    if (live && s_search_focus) {
        poll_typed(s_search, sizeof(s_search), false);
        if (key_repeat(VK_BACK)) { size_t l = strlen(s_search); if (l) s_search[l - 1] = 0; }
        if (key_edge(VK_RETURN)) s_search_focus = false;
    }
    // hex field: hex chars only, applies on the full #rrggbb form
    if (live && s_cp_hex_focus && s_cp_open) {
        poll_typed(s_cp_hex, sizeof(s_cp_hex), true);
        size_t l = strlen(s_cp_hex);
        if (l > 7) s_cp_hex[7] = 0;             // "#rrggbb" max
        if (hex_valid(s_cp_hex)) cp_apply_hex();
        if (key_repeat(VK_BACK)) { l = strlen(s_cp_hex); if (l > 1) s_cp_hex[l - 1] = 0; }
        if (key_edge(VK_RETURN)) s_cp_hex_focus = false;
    }

    // ---- layout: columns of categories, centered at the top --------------
    int NCATS = 0;
    const char* const* cats = modules_categories(&NCATS);
    int NMODS = 0;
    Module* const* all = modules_all(&NMODS);
    float total_w = NCATS * COL_W + (NCATS - 1) * GAP;
    float zoom = total_w > W * 0.96f ? (W * 0.96f / total_w) : 1.f;
    const float sc = zoom * os;                     // geometry scale
    // design->screen mapping: columns centered, vertical pinned to the top
    auto T = [&](ImVec2 p) {
        float x = W * 0.5f + (p.x - total_w * 0.5f) * zoom;
        float y = TOP_PAD + p.y;
        return ImVec2(W * 0.5f + (x - W * 0.5f) * os, y * os);
    };

    const char* q = s_search[0] ? s_search : nullptr;
    char qlower[64]; qlower[0] = 0;
    if (q) { snprintf(qlower, sizeof qlower, "%s", q); for (char* c = qlower; *c; ++c) *c = (char)tolower(*c); }

    float dt = io.DeltaTime;
    static float col_scroll[8] = {};
    float max_col_bottom = TOP_PAD + 42.f + 220.f;

    for (int ci = 0; ci < NCATS; ci++) {
        const char* cname = cats[ci];
        float dx = ci * (COL_W + GAP);
        ImVec2 col_tl = T(ImVec2(dx, 0.f)), col_br = T(ImVec2(dx + COL_W, 2000.f));
        // column height depends on content; first pass measures
        struct Item { Module* md; float sets_h; };
        Item items[16]; int nitems = 0;
        float content_h = 0;
        for (int mi = 0; mi < NMODS && nitems < 16; mi++) {
            Module& md = *all[mi];
            if (strcmp(md.category, cname) != 0) continue;
            if (qlower[0]) {
                char low[64]; snprintf(low, sizeof low, "%s", md.name);
                for (char* c = low; *c; ++c) *c = (char)tolower(*c);
                if (!strstr(low, qlower)) continue;
            }
            ImGuiID id = ImGui::GetID(md.name);
            float sets_h = 0.f;
            if (md.debug_block) sets_h = 8 * (SET_H + 2.f);
            else for (int si = 0; si < md.nsets; si++) {
                const Setting& sd = md.sets[si];
                sets_h += (SET_H + 10.f);
                if (sd.kind == Setting::FLOAT) sets_h += (SET_H + 10.f);
                if (sd.kind == Setting::ENUM && sd.opts) sets_h += 30.f;
            }
            float op = anim_step(id + 1, g_expanded[id] ? 1.f : 0.f, dt, 0.24f);
            content_h += ROW_H + ROW_MB + (sets_h + 12.f) * op;
            items[nitems++] = { &md, sets_h };
        }
        float list_h = std::min(content_h, std::max(240.f, H - 620.f));
        float col_h = COL_PAD + HDR_H + list_h + COL_PAD;
        (void)col_h;
        col_br = T(ImVec2(dx + COL_W, 62.f + list_h));
        max_col_bottom = std::max(max_col_bottom, col_br.y + 44.f);

        // column panel + soft drop shadow (box-shadow 0 6px 24px rgba(0,0,0,.45))
        dl->AddRectFilled(ImVec2(col_tl.x - 1, col_tl.y + 3), ImVec2(col_br.x + 1, col_br.y + 6), mul_a(IM_COL32(0,0,0,255), A * 0.22f), COL_R * sc);
        dl->AddRectFilled(ImVec2(col_tl.x - 3, col_tl.y + 7), ImVec2(col_br.x + 3, col_br.y + 12), mul_a(IM_COL32(0,0,0,255), A * 0.13f), COL_R * sc);
        dl->AddRectFilled(ImVec2(col_tl.x - 7, col_tl.y + 12), ImVec2(col_br.x + 7, col_br.y + 22), mul_a(IM_COL32(0,0,0,255), A * 0.07f), COL_R * sc);
        dl->AddRectFilled(col_tl, col_br, with_a(C_PANEL, A), COL_R * sc);

        // header: category name, centered, white, uppercase
        float cw = ui_tw(cg_fonts.mont19, 19.f * sc, cname);
        dl->AddText(cg_fonts.mont19, 19.f * sc, ImVec2((col_tl.x + col_br.x - cw) * 0.5f, col_tl.y + 12.f * sc),
                    with_a(IM_COL32(255,255,255,255), A), cname);

        // list area with per-column wheel scroll
        ImVec2 lst_tl(col_tl.x + COL_PAD, col_tl.y + COL_PAD + HDR_H);
        ImVec2 lst_br(col_br.x - COL_PAD, col_br.y - COL_PAD);
        if (live && s_hover(col_tl, col_br) && io.MouseWheel != 0.f)
            col_scroll[ci] = std::max(0.f, col_scroll[ci] - io.MouseWheel * 40.f);
        float max_scroll = std::max(0.f, content_h - (list_h - 0.f));
        if (col_scroll[ci] > max_scroll) col_scroll[ci] = max_scroll;

        dl->PushClipRect(lst_tl, lst_br, true);
        float yy = lst_tl.y - col_scroll[ci];
        for (int ii = 0; ii < nitems; ii++) {
            Module& md = *items[ii].md;
            ImGuiID id = ImGui::GetID(md.name);
            bool expanded = g_expanded[id];
            float op = anim_step(ImGuiID(id + 1), expanded ? 1.f : 0.f, dt, 0.24f);
            ImVec2 a(lst_tl.x, yy), b(lst_br.x, yy + ROW_H * sc);
            if (b.y < lst_tl.y - 60.f || a.y > lst_br.y + 60.f) {
                // offscreen: still advance
                yy += (ROW_H + ROW_MB) * sc + (items[ii].sets_h + 12.f) * sc * op;
                continue;
            }
            bool has_sets = md.nsets > 0 || md.debug_block;
            bool on = module_on(md);
            bool hov = live && s_hover(a, b);

            // interactions
            ImVec2 ar_a(b.x - 36.f * sc, a.y), ar_b(b.x, b.y);   // arrow zone
            if (live) {
                if (has_sets && s_click1(a, b)) { g_expanded[id] = !expanded; ui_tick_sound(); expanded = g_expanded[id]; }
                if (has_sets && s_click0(ar_a, ar_b)) { g_expanded[id] = !expanded; ui_tick_sound(); expanded = g_expanded[id]; }
                else if (s_click0(a, b)) {
                    if (md.has_toggle || md.toggle_state) modules_toggle(&md);
                    if (md.action) md.action();
                    g_flash[id] = ui_time();
                    ui_tick_sound();
                }
            }

            // row bg + hover bg
            dl->AddRectFilled(a, b, with_a(hov && !on ? C_ROW_H : C_ROW, A), ROW_R * sc);
            // ::before gradient layer (scaleX .55 -> 1, opacity 0 -> .92/1)
            float g = anim_step(ImGuiID(id + 2), (on || hov) ? 1.f : 0.f, dt, 0.23f);
            if (g > 0.003f) {
                float gw = (b.x - a.x) * std::min(1.f, 0.55f + 0.45f * g);
                float al = (on ? 1.f : 0.92f) * g;
                grad_rounded_rect_h(dl, a, ImVec2(a.x + gw, b.y),
                                    with_a(th_acc1(1), A * al), with_a(th_acc2(1), A * al), ROW_R * sc);
            }
            // ON glow (box-shadow 0 0 4px --acc-glow)
            if (on) {
                dl->AddRect(ImVec2(a.x - 1.f, a.y - 1.f), ImVec2(b.x + 1.f, b.y + 1.f), th_accb(A * 0.55f), ROW_R * sc, 0, 2.0f * sc);
                dl->AddRect(ImVec2(a.x - 3.f, a.y - 3.f), ImVec2(b.x + 3.f, b.y + 3.f), th_accb(A * 0.22f), ROW_R * sc, 0, 2.0f * sc);
            }
            // flash overlay (rowflash .26s)
            auto fl = g_flash.find(id);
            if (fl != g_flash.end()) {
                float age = ui_time() - fl->second;
                if (age < 0.26f)
                    dl->AddRectFilled(a, b, with_a(IM_COL32(255,255,255,255), 0.55f * (1.f - age / 0.26f) * A), ROW_R * sc);
                else g_flash.erase(fl);
            }
            // centered label
            const char* disp = md.name;
            float nw = ui_tw(cg_fonts.mont14, 14.f * sc, disp);
            dl->AddText(cg_fonts.mont14, 14.f * sc,
                        ImVec2((a.x + b.x - nw) * 0.5f, a.y + (ROW_H - 14.f) * 0.5f * sc),
                        with_a(IM_COL32(255,255,255,255), A), disp);
            // arrow chevron
            if (has_sets) {
                ImVec2 c((ar_a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
                float s2 = 3.2f * sc, o2 = 2.4f * sc;
                ImU32 cwr = with_a(IM_COL32(255,255,255,255), A);
                float th = 2.2f * sc;
                if (op > 0.5f) {   // expanded: ^
                    dl->AddLine(ImVec2(c.x - s2, c.y + o2), ImVec2(c.x, c.y - o2), cwr, th);
                    dl->AddLine(ImVec2(c.x, c.y - o2), ImVec2(c.x + s2, c.y + o2), cwr, th);
                } else {           // collapsed: v
                    dl->AddLine(ImVec2(c.x - s2, c.y - o2), ImVec2(c.x, c.y + o2), cwr, th);
                    dl->AddLine(ImVec2(c.x, c.y + o2), ImVec2(c.x + s2, c.y - o2), cwr, th);
                }
            }
            yy += ROW_H * sc;
            // expanded settings (max-height animated)
            if (op > 0.003f) {
                float sh = (items[ii].sets_h + 12.f) * sc * op;
                ImVec2 s_tl(a.x + 6.f * sc, yy + 2.f * sc), s_br(b.x - 6.f * sc, yy + sh);
                dl->PushClipRect(ImVec2(s_tl.x, lst_tl.y), lst_br, true);
                draw_sets(dl, md, s_tl, s_br, std::min(1.f, op * 1.4f) * A, sc, id);
                dl->PopClipRect();
            }
            yy += ROW_MB + (items[ii].sets_h + 12.f) * sc * op;
        }
        dl->PopClipRect();
    }

    // ---- bottom controls: [dot1] [search bar] [dot2] ----------------------
    // the whole #ui scales with the open animation; the bar follows
    auto TS = [&](ImVec2 p) { return ImVec2(W * 0.5f + (p.x - W * 0.5f) * os, p.y * os); };
    float bar_y = max_col_bottom * os;
    float total_bw = BOT_BTN + 10.f + BAR_W + 10.f + BOT_BTN;
    float bx0 = W * 0.5f - total_bw * 0.5f * zoom;
    ImVec2 btn1_a = TS(ImVec2(bx0, bar_y)), btn1_b = TS(ImVec2(bx0 + BOT_BTN * zoom, bar_y + BOT_BTN * zoom));
    ImVec2 bar_a = TS(ImVec2(btn1_b.x + 10.f * zoom, bar_y)),
           bar_b = TS(ImVec2(btn1_b.x + (10.f + BAR_W) * zoom, bar_y + BOT_BTN * zoom));
    ImVec2 btn2_a = TS(ImVec2(bar_b.x + 10.f * zoom, bar_y)),
           btn2_b = TS(ImVec2(bar_b.x + (10.f + BOT_BTN) * zoom, bar_y + BOT_BTN * zoom));

    bool hov_b1 = live && s_hover(btn1_a, btn1_b);
    bool hov_b2 = live && s_hover(btn2_a, btn2_b);
    bool hov_bar = live && s_hover(bar_a, bar_b);
    if (live) {
        if (s_click0(btn1_a, btn1_b)) { if (s_cp_open && s_cp_slot == 1) s_cp_open = false; else { cp_load_slot(1); s_cp_open = true; } ui_tick_sound(); }
        if (s_click0(btn2_a, btn2_b)) { if (s_cp_open && s_cp_slot == 2) s_cp_open = false; else { cp_load_slot(2); s_cp_open = true; } ui_tick_sound(); }
        if (s_click0(bar_a, bar_b)) s_search_focus = !s_search_focus;
        if (ImGui::IsMouseClicked(0) && !hov_bar && s_search_focus) s_search_focus = false;
    }
    dl->AddRectFilled(btn1_a, btn1_b, with_a(hov_b1 ? C_ROW_H : C_ROW, A), BOT_R * sc);
    dl->AddRectFilled(bar_a, bar_b, with_a(C_ROW, A), BOT_R * sc);
    dl->AddRectFilled(btn2_a, btn2_b, with_a(hov_b2 ? C_ROW_H : C_ROW, A), BOT_R * sc);
    // dots
    float dot = 18.f * zoom;
    dl->AddRectFilled(ImVec2((btn1_a.x + btn1_b.x - dot) * 0.5f, (btn1_a.y + btn1_b.y - dot) * 0.5f),
                      ImVec2((btn1_a.x + btn1_b.x + dot) * 0.5f, (btn1_a.y + btn1_b.y + dot) * 0.5f),
                      th_acc1(A), 6.f * sc);
    dl->AddRectFilled(ImVec2((btn2_a.x + btn2_b.x - dot) * 0.5f, (btn2_a.y + btn2_b.y - dot) * 0.5f),
                      ImVec2((btn2_a.x + btn2_b.x + dot) * 0.5f, (btn2_a.y + btn2_b.y + dot) * 0.5f),
                      th_acc2(A), 6.f * sc);
    // search text (centered) / placeholder
    {
        const char* ph = "module name";
        if (s_search[0]) {
            float tw2 = ui_tw(cg_fonts.mont14, 14.f * sc, s_search);
            dl->AddText(cg_fonts.mont14, 14.f * sc, ImVec2((bar_a.x + bar_b.x - tw2) * 0.5f, bar_a.y + 8.f * sc),
                        with_a(IM_COL32(255,255,255,255), A), s_search);
        } else {
            float tw2 = ui_tw(cg_fonts.mont14, 14.f * sc, ph);
            dl->AddText(cg_fonts.mont14, 14.f * sc, ImVec2((bar_a.x + bar_b.x - tw2) * 0.5f, bar_a.y + 8.f * sc),
                        with_a(C_PHOLD, A), ph);
        }
        if (s_search_focus && fmodf(ui_time(), 1.f) < 0.5f && !s_search[0]) {
            // caret hint: thin underline
            dl->AddRectFilled(ImVec2((bar_a.x + bar_b.x) * 0.5f - 1, bar_b.y - 3),
                              ImVec2((bar_a.x + bar_b.x) * 0.5f + 1, bar_b.y - 2), mul_a(IM_COL32(255,255,255,150), A));
        }
    }

    // ---- color picker popup (above the bottom bar) -------------------------
    if (s_cp_open) {
        float cp_h = 12.f + 26.f + 10.f + 136.f + 10.f + 14.f + 10.f + 30.f + 12.f;
        ImVec2 cp_a(W * 0.5f - CP_W * 0.5f, bar_y - cp_h * zoom - 40.f);
        ImVec2 cp_b(cp_a.x + CP_W, cp_a.y + cp_h * zoom);
        if (live && ImGui::IsMouseClicked(0) && !s_hover(cp_a, cp_b) &&
            !s_hover(btn1_a, btn1_b) && !s_hover(btn2_a, btn2_b)) s_cp_open = false;

        dl->AddRectFilled(ImVec2(cp_a.x - 3, cp_a.y + 3), ImVec2(cp_b.x + 3, cp_b.y + 6), with_a(IM_COL32(0,0,0,110), A), 12.f * sc);
        dl->AddRectFilled(cp_a, cp_b, with_a(C_PANEL, A), 12.f * sc);
        dl->AddRect(cp_a, cp_b, mul_a(IM_COL32(255,255,255,20), A), 12.f * sc, 0, 1.f);

        float px = cp_a.x + 12.f, py = cp_a.y + 12.f;
        float inner_w = CP_W - 24.f;
        // header: tabs + reset
        const char* tabs[2] = {"Color 1", "Color 2"};
        for (int ti = 0; ti < 2; ti++) {
            float tabw = ui_tw(cg_fonts.mont12, 11.f * sc, tabs[ti]) + 20.f * sc;
            ImVec2 ta(px, py), tb(px + tabw, py + 20.f * sc);
            bool act = s_cp_slot == ti + 1;
            if (live && s_click0(ta, tb)) cp_load_slot(ti + 1);
            dl->AddRectFilled(ta, tb, act ? th_accb(A) : with_a(C_ROW, A), 6.f * sc);
            dl->AddText(cg_fonts.mont12, 11.f * sc, ImVec2(ta.x + 10.f * sc, ta.y + 4.f * sc),
                        with_a(act ? IM_COL32(255,255,255,255) : C_MUTED, A), tabs[ti]);
            px += tabw + 6.f * sc;
        }
        const char* rst = "Reset";
        float rw = ui_tw(cg_fonts.mont12, 11.f * sc, rst);
        ImVec2 ra(cp_b.x - 12.f - rw, py + 3.f * sc), rb(cp_b.x - 12.f, py + 16.f * sc);
        if (live && s_click0(ra, rb)) {
            auto& mm = mod(); std::lock_guard<std::mutex> g(mm.mtx);
            mm.acc1[0] = 207.f/255.f; mm.acc1[1] = 59.f/255.f; mm.acc1[2] = 1.f;
            mm.acc2[0] = 1.f; mm.acc2[1] = 138.f/255.f; mm.acc2[2] = 42.f/255.f;
            cp_load_slot(s_cp_slot);
        }
        dl->AddText(cg_fonts.mont12, 11.f * sc, ImVec2(ra.x, ra.y), with_a(C_MUTED, A), rst);
        py += 26.f + 10.f;

        // SV area
        ImVec2 sv_a(cp_a.x + 12.f, py), sv_b(cp_a.x + 12.f + inner_w, py + 136.f);
        float hue_rgb[3]; hsv_to_rgb(s_cp_hsv[0], 1.f, 1.f, hue_rgb);
        ImU32 hue_col = rgb_col(hue_rgb);
        if (live && s_click0(sv_a, sv_b)) s_cp_drag = 1;
        if (s_cp_drag == 1 && !ImGui::IsMouseDown(0)) s_cp_drag = 0;
        if (live && s_cp_drag == 1) {
            s_cp_hsv[1] = clamp01((io.MousePos.x - sv_a.x) / std::max(1.f, sv_b.x - sv_a.x));
            s_cp_hsv[2] = 1.f - clamp01((io.MousePos.y - sv_a.y) / std::max(1.f, sv_b.y - sv_a.y));
            cp_apply_hsv_to_state();
        }
        dl->AddRectFilled(sv_a, sv_b, hue_col, 4.f * sc);
        dl->AddRectFilledMultiColor(sv_a, sv_b, mul_a(IM_COL32(255,255,255,255), A), mul_a(IM_COL32(255,255,255,0), A),
                                    mul_a(IM_COL32(255,255,255,0), A), mul_a(IM_COL32(255,255,255,255), A));
        dl->AddRectFilledMultiColor(sv_a, sv_b, mul_a(IM_COL32(0,0,0,0), A), mul_a(IM_COL32(0,0,0,0), A),
                                    mul_a(IM_COL32(0,0,0,255), A), mul_a(IM_COL32(0,0,0,255), A));
        dl->AddRect(ImVec2(sv_a.x + (sv_b.x - sv_a.x) * s_cp_hsv[1] - 6,
                           sv_a.y + (sv_b.y - sv_a.y) * (1.f - s_cp_hsv[2]) - 6),
                    ImVec2(sv_a.x + (sv_b.x - sv_a.x) * s_cp_hsv[1] + 6,
                           sv_a.y + (sv_b.y - sv_a.y) * (1.f - s_cp_hsv[2]) + 6),
                    with_a(IM_COL32(255,255,255,255), A), 6.f, 0, 1.6f);
        py += 136.f + 10.f;

        // hue strip
        ImVec2 h_a(cp_a.x + 12.f, py), h_b(cp_a.x + 12.f + inner_w, py + 14.f);
        if (live && s_click0(h_a, h_b)) s_cp_drag = 2;
        if (s_cp_drag == 2 && !ImGui::IsMouseDown(0)) s_cp_drag = 0;
        if (live && s_cp_drag == 2) {
            s_cp_hsv[0] = clamp01((io.MousePos.x - h_a.x) / std::max(1.f, h_b.x - h_a.x)) * 360.f;
            cp_apply_hsv_to_state();
        }
        {
            static const ImU32 stops[7] = {
                IM_COL32(255,0,0,255), IM_COL32(255,255,0,255), IM_COL32(0,255,0,255),
                IM_COL32(0,255,255,255), IM_COL32(0,0,255,255), IM_COL32(255,0,255,255),
                IM_COL32(255,0,0,255) };
            float seg = (h_b.x - h_a.x) / 6.f;
            for (int i = 0; i < 6; i++) {
                ImVec2 s1(h_a.x + seg * i, h_a.y), s2(h_a.x + seg * (i + 1), h_b.y);
                dl->AddRectFilledMultiColor(s1, s2, with_a(stops[i], A), with_a(stops[i+1], A),
                                            with_a(stops[i+1], A), with_a(stops[i], A));
            }
            float hx = h_a.x + (h_b.x - h_a.x) * (s_cp_hsv[0] / 360.f);
            dl->AddRectFilled(ImVec2(hx - 5, h_a.y - 2), ImVec2(hx + 5, h_b.y + 2), with_a(IM_COL32(255,255,255,255), A), 4.f * sc);
        }
        py += 14.f + 10.f;

        // hex field
        ImVec2 hx_a(cp_a.x + 12.f, py), hx_b(cp_b.x - 12.f, py + 30.f);
        if (live && s_click0(hx_a, hx_b)) s_cp_hex_focus = true;
        else if (live && ImGui::IsMouseClicked(0) && !s_hover(hx_a, hx_b)) s_cp_hex_focus = false;
        dl->AddRectFilled(hx_a, hx_b, with_a(C_ROW, A), 6.f * sc);
        if (s_cp_hex_focus) dl->AddRect(hx_a, hx_b, th_accb(A * 0.8f), 6.f * sc, 0, 1.2f);
        float hw = ui_tw(cg_fonts.mont12, 11.f * sc, s_cp_hex);
        dl->AddText(cg_fonts.mont12, 11.f * sc, ImVec2((hx_a.x + hx_b.x - hw) * 0.5f, py + 9.f * sc),
                    with_a(IM_COL32(255,255,255,255), A), s_cp_hex);
    }

    // hint line (mockup #hint)
    if (m.opt_hint) {
        dl->AddText(cg_fonts.mont12, 12.f, ImVec2(10.f, H - 22.f),
                    with_a(C_PHOLD, 0.9f), "INSERT - menu | drag HUD elements while the menu is open");
    }

    ImGui::End();
}

} // namespace mc
