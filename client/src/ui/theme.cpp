// TrickyClient OSS — ui layer: theme (moved verbatim from the monolith)
#include "ui/theme.h"
#include "core/state.h"
#include <cmath>
#include <algorithm>

namespace mc {

float clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
ImU32 with_a(ImU32 c, float a01) {          // SET alpha
    return (c & 0x00FFFFFFu) | ((ImU32)(ImU8)(a01 * 255.f) << 24);
}
ImU32 mul_a(ImU32 c, float f) {              // MULTIPLY alpha
    return (c & 0x00FFFFFFu) | ((ImU32)(ImU8)(((c >> 24) & 255) * clamp01(f)) << 24);
}
float nsmooth(float x) { x = clamp01(x); return x * x * (3.f - 2.f * x); }

ImU32 lerp_col(ImU32 a, ImU32 b, float t) {
    t = clamp01(t);
    int ar = (int)(a & 255), ag = (int)((a >> 8) & 255), ab = (int)((a >> 16) & 255), aa = (int)((a >> 24) & 255);
    int br = (int)(b & 255), bg = (int)((b >> 8) & 255), bb = (int)((b >> 16) & 255), ba = (int)((b >> 24) & 255);
    return IM_COL32(ar + (int)((br - ar) * t), ag + (int)((bg - ag) * t),
                    ab + (int)((bb - ab) * t), aa + (int)((ba - aa) * t));
}

// ---- theme cache (refreshed once per frame in clickgui_newframe) ----------
static float g_t1[3] = { 207.f/255.f, 59.f/255.f, 1.f };
static float g_t2[3] = { 1.f, 138.f/255.f, 42.f/255.f };
ImU32 rgb_col(const float* c, float a) {
    return IM_COL32((int)(c[0]*255.f+0.5f), (int)(c[1]*255.f+0.5f), (int)(c[2]*255.f+0.5f), (int)(a*255.f+0.5f));
}
void theme_refresh() {
    auto& m = mod();
    std::lock_guard<std::mutex> g(m.mtx);
    for (int i = 0; i < 3; i++) { g_t1[i] = m.acc1[i]; g_t2[i] = m.acc2[i]; }
}
ImU32 th_acc1(float a)  { return rgb_col(g_t1, a); }
ImU32 th_acc2(float a)  { return rgb_col(g_t2, a); }
ImU32 th_accb(float a) {  // average of the two (clickgui.js applyThemeColors)
    float c[3]; for (int i = 0; i < 3; i++) c[i] = (g_t1[i] + g_t2[i]) * 0.5f;
    return rgb_col(c, a);
}

void hsv_to_rgb(float h, float s, float v, float* rgb) {
    h = fmodf(h, 360.f); if (h < 0) h += 360.f;
    float c = v * s, x = c * (1.f - fabsf(fmodf(h / 60.f, 2.f) - 1.f)), m = v - c;
    float r = 0, g = 0, b = 0;
    if      (h < 60)  { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else              { r = c; b = x; }
    rgb[0] = r + m; rgb[1] = g + m; rgb[2] = b + m;
}
void rgb_to_hsv(const float* rgb, float* hsv) {
    float r = rgb[0], g = rgb[1], b = rgb[2];
    float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    float h = 0;
    if (d > 1e-6f) {
        if (mx == r) h = 60.f * fmodf((g - b) / d, 6.f);
        else if (mx == g) h = 60.f * ((b - r) / d + 2.f);
        else h = 60.f * ((r - g) / d + 4.f);
    }
    if (h < 0) h += 360.f;
    hsv[0] = h; hsv[1] = mx > 1e-6f ? d / mx : 0.f; hsv[2] = mx;
}

} // namespace mc
