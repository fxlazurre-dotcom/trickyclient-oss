// ModCore — HUD module: Watermark. Rounded panel + gradient client name
// (acc1 -> lerp(acc1, white, .3)) + live "<N> fps" suffix; draggable.
#include "modules/registry.h"

namespace mc {

class HudWatermark : public Module {
public:
    HudWatermark() {
        name = "Watermark"; category = "HUD"; listed = false;
        add_bool("info", true);
        add_float("size", 8.0f, 5.f, 14.f, false, "%.1f");
        add_float("opacity", 235.f, 60.f, 255.f, true, "%.0f");
    }
    void on_hud(HudCtx& c) override {
        const float fs = sets[1].f * 2.f;
        const float lh = fs * 1.2f;
        const float op = sets[2].f / 255.f;
        const char* name = "TrickyClient";
        char extra[32] = "";
        if (sets[0].b) {          // "--" until the FPS counter warms up
            int fps = (int)(c.fps + 0.5f);
            snprintf(extra, sizeof extra, "  %s fps", fps < 1 ? "--" : "");
            if (fps >= 1) snprintf(extra, sizeof extra, "  %d fps", fps);
        }
        float w = c.tw(c, name, fs) + (extra[0] ? c.tw(c, extra, fs) : 0.f) + 24.f;
        float h = lh + 14.f;
        if (hud_x + w > c.W - 4.f) hud_x = c.W - 4.f - w < 4.f ? 4.f : c.W - 4.f - w;
        c.rect(c, hud_x, hud_y, w, h, 6.f, c.with_a(c, c.bg_col, op));
        c.drag(c, "wm", &hud_x, &hud_y, w, h);
        // name: gradient acc1 -> lerp(acc1, white, .3), glow acc1 0.30
        float total = c.tw(c, name, fs), adv = 0.f;
        unsigned g2 = c.lerp(c, c.acc1, 0xFFFFFFFFu, 0.3f);
        for (const char* p = name; *p; ++p) {
            char one[2] = {*p, 0};
            float cw = c.tw(c, one, fs);
            c.text(c, one, hud_x + 12.f + adv + 1.2f, hud_y + 7.f + 1.2f, fs,
                   c.mul_a(c, c.acc1, 0.30f), false);
            c.text(c, one, hud_x + 12.f + adv, hud_y + 7.f, fs,
                   c.lerp(c, c.acc1, g2, total > 0 ? adv / total : 0.f), false);
            adv += cw;
        }
        if (extra[0])
            c.text(c, extra, hud_x + 12.f + adv, hud_y + 7.f, fs, c.txt_col, false);
    }
};

Module& mod_hud_watermark() { static HudWatermark m; return m; }

} // namespace mc
