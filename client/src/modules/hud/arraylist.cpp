// ModCore — HUD module: ArrayList. Enabled modules, width-sorted, stepped
// rounded background, animated sine-flow color wave (acc1 -> acc2), gradient
// side line with glow. ALWAYS visible while enabled: falls back to demo rows
// when nothing is on (like the mock's edit-mode placeholder list).
#include "modules/registry.h"

#include <vector>
#include <algorithm>
#include <cmath>

namespace mc {

class HudArrayList : public Module {
public:
    HudArrayList() {
        name = "ArrayList"; category = "HUD"; listed = false;
        add_bool("animate", true);
        add_bool("line", true);
        add_float("size", 8.0f, 5.f, 14.f, false, "%.1f");
        add_float("opacity", 235.f, 60.f, 255.f, true, "%.0f");
    }
    void on_hud(HudCtx& c) override {
        const float fs = sets[2].f * 2.f;
        const float lh = fs * 1.2f;
        const float op = sets[3].f / 255.f;
        std::vector<std::string> owned = modules_arraylist();
        std::vector<const char*> names;
        for (const std::string& n : owned) names.push_back(n.c_str());
        // 0 modules on -> nothing to draw (the stepped-bg helper
        // dereferences rws[0], an EMPTY list must never reach it)
        if (!names.size()) return;
        // width sort (desc)
        std::sort(names.begin(), names.end(), [&](const char* a, const char* b) {
            return c.tw(c, a, fs) > c.tw(c, b, fs);
        });
        int n = (int)names.size();
        const float rh = lh + 2.f, padY = 6.f, lineW = 3.f, lineGap = 3.f, padX = 4.f;
        float maxw = 0;
        for (auto& nm : names) maxw = std::max(maxw, c.tw(c, nm, fs));
        float w = maxw + padX + lineW + lineGap + padX;
        float h = n * rh + padY * 2.f;
        if (hud_x + w > c.W - 4.f) hud_x = c.W - 4.f - w < 4.f ? 4.f : c.W - 4.f - w;
        // row colors: animated sin flow or static spread
        std::vector<unsigned> cols(n);
        std::vector<float> rws(n);
        for (int i = 0; i < n; i++) {
            float k = sets[0].b ? mod_nsmooth((sinf(c.t * 1.1f + i * 0.5f) + 1.f) * 0.5f)
                                : mod_nsmooth(n > 1 ? (float)i / (float)(n - 1) : 0.f);
            cols[i] = c.lerp(c, c.acc1, c.acc2, k);
            rws[i] = c.tw(c, names[i], fs) + padX + lineW + lineGap + padX;
        }
        float ry[64];
        for (int i = 0; i < n; i++) ry[i] = padY + (i + 1) * rh;
        c.stepped(c, hud_x, hud_y, rws.data(), ry, n, w, h, 6.f, c.with_a(c, c.bg_col, op));
        c.drag(c, "al", &hud_x, &hud_y, w, h);
        // gradient side line (per-row color slices + glow)
        if (sets[1].b) {
            float lx = hud_x;
            c.rect(c, lx - 2.f, hud_y + 2.f, lineW + 4.f, h - 4.f, 0.f, c.mul_a(c, cols[0], 0.25f));
            for (int i = 0; i < n; i++) {
                float y0 = hud_y + padY + i * rh;
                c.rect(c, lx, y0, lineW, rh, 0.f, cols[i]);
            }
        }
        // names
        for (int i = 0; i < n; i++) {
            float ty = hud_y + padY + i * rh + (rh - fs) * 0.5f;
            c.text(c, names[i], hud_x + padX + lineW + lineGap, ty, fs, cols[i], false);
        }
    }
};

Module& mod_hud_arraylist() { static HudArrayList m; return m; }

} // namespace mc
