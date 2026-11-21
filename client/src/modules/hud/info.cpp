// ModCore — HUD module: Info. XYZ / CPS text lines (accent label + white
// value + shadow), bottom-left by default; "--" until real telemetry.
#include "modules/registry.h"
#include "core/threadbus.h"

#include <cmath>
#include <cstdio>

namespace mc {

class HudInfo : public Module {
public:
    HudInfo() {
        name = "Info"; category = "HUD"; listed = false;
        add_bool("xyz", true);
        add_bool("cps", true);
        add_float("size", 8.0f, 5.f, 14.f, false, "%.1f");
        add_float("opacity", 235.f, 60.f, 255.f, true, "%.0f");
    }
    void on_hud(HudCtx& c) override {
        const float fs = sets[2].f * 2.f;
        const float lh = fs * 1.2f;
        Telemetry t = Bus::inst().telemetry();
        char lxyz[48], lcps[16];
        // "--" until the game-thread telemetry has actually read the player
        snprintf(lxyz, sizeof lxyz, "%s", t.pos_valid ? "" : "--");
        if (t.pos_valid)
            snprintf(lxyz, sizeof lxyz, "%d, %d, %d",
                     (int)floorf(t.pos_x), (int)floorf(t.pos_y), (int)floorf(t.pos_z));
        snprintf(lcps, sizeof lcps, "%d", Bus::inst().cps());
        struct Line { const char* l; const char* v; };
        Line lines[2]; int nl = 0;
        if (sets[0].b) lines[nl++] = {"XYZ", lxyz};
        if (sets[1].b) lines[nl++] = {"CPS", lcps};
        if (!nl) return;
        float Wd = 0;
        for (int i = 0; i < nl; i++) {
            char buf[64]; snprintf(buf, sizeof buf, "%s %s", lines[i].l, lines[i].v);
            Wd = std::max(Wd, c.tw(c, buf, fs));
        }
        float step = lh + 2.f;
        float h = std::max(lh, nl * step - 2.f);
        // bottom-left anchor on first use (mock: anchor bl, x=2 y=-14)
        if (hud_x < 0.f || hud_y < 0.f) {
            hud_x = 4.f;
            hud_y = c.H - h - 28.f;
        }
        c.drag(c, "if", &hud_x, &hud_y, Wd, h);
        for (int i = 0; i < nl; i++) {
            float y = hud_y + i * step;
            float lw = c.text(c, lines[i].l, hud_x, y, fs, c.acc1, true);
            char sp[2] = " ";
            c.text(c, sp, hud_x + lw, y, fs, 0xFFFFFFFFu, true);
            c.text(c, lines[i].v, hud_x + lw + c.tw(c, sp, fs), y, fs, 0xFFFFFFFFu, true);
        }
    }
};

Module& mod_hud_info() { static HudInfo m; return m; }

} // namespace mc
