// ModCore — module: AimAssist v8.6 (yaw-only magnetism).
// Target scan runs on the game tick; the pull rides the GameInput mouse-getter
// hook: the error is published into the thread bus and applied there as an
// accumulator on top of the out-copy (hw_delta + rate, always correct).
#include "modules/registry.h"
#include "game/sdk/SDK.hpp"
#include "game/sdk/actor/LocalPlayer.hpp"
#include "game/sdk/actor/components.h"
#include "game/sdk/world/Level.hpp"
#include "core/log.h"
#include "core/state.h"
#include "core/threadbus.h"

#include <cmath>

namespace mc {
using sdk::StateVectorC;
using sdk::RotationC;

static float aa_norm180(float a) {
    while (a > 180.f) a -= 360.f;
    while (a < -180.f) a += 360.f;
    return a;
}

class AimAssist : public Module {
public:
    AimAssist() {
        name = "AimAssist"; category = "COMBAT";
        add_float("range",    5.0f, 2.f, 10.f, false, "%.1f");
        add_float("speed",    2.0f, 0.5f, 5.f, false, "%.1f");
        add_float("strength", 0.5f, 0.05f, 1.f, false, "%.2f");
    }
    void on_toggle(bool on) override {
        // the dispatcher no longer calls disabled modules: clear the pull
        // IMMEDIATELY so the input hook stops applying a stale error
        if (!on) { cur.active = false; publish(); }
    }
    void on_tick(LevelCtx& ctx) override {
        bool menu;
        { std::lock_guard<std::mutex> g(mod().mtx); menu = mod().overlay_open; }
        if (!enabled || menu) { cur.active = false; publish(); return; }
        if (!ctx.player || sdk::svc_off() <= 0 || !sdk::actor_list_available()) return;

        StateVectorC* ps = sdk::actor_statevec(ctx.player);
        RotationC*    pr = sdk::actor_rotation(ctx.player);
        if (!ps || !pr) return;
        static volatile LONG s_rotchk = 0;
        if (!InterlockedExchange(&s_rotchk, 1))
            MC_LOG("aimassist: svc=+0x%x pos=(%.1f,%.1f,%.1f) rot: pitch=%.1f yaw=%.1f",
                   sdk::svc_off(), ps->px, ps->py, ps->pz, pr->pitch, pr->yaw);

        if (vec[0]) vec[1] = vec[0];
        sdk::fill_runtime_actors(ctx.level, vec);
        void** beg = (void**)vec[0];
        void** end = (void**)vec[1];
        if (!beg || !end || end < beg || (size_t)(end - beg) > 4096) return;

        const float range = sets[0].f;
        void* player_vt = *(void**)ctx.player;
        // sticky target: keep the current one while valid (target flapping was
        // a big part of the "jerky" feel — direction flipped tick to tick)
        static void* s_sticky = nullptr;
        void* best = nullptr; StateVectorC* bestS = nullptr; float bestD = range;
        float stickyD = range * 1.25f;             // sticky keeps a 25% grace range
        for (void** it = beg; it != end; ++it) {
            void* a = *it;
            if (!a || a == (void*)ctx.player) continue;
            if (!sdk::readable_ptr((uint8_t*)a, (size_t)sdk::svc_off() + 8)) continue;
            if (*(void**)a == player_vt) continue;
            StateVectorC* s = *(StateVectorC**)((uint8_t*)a + sdk::svc_off());
            if (!sdk::readable_ptr(s) || s == ps) continue;
            float dx = s->px - ps->px, dy = s->py - ps->py, dz = s->pz - ps->pz;
            float d2 = dx*dx + dy*dy + dz*dz;
            float lim = (a == s_sticky) ? stickyD : range;
            if (d2 < 1.0f || d2 > lim * lim) continue;
            float d = sqrtf(d2);
            if (a == s_sticky) { best = a; bestS = s; bestD = d; break; }  // sticky wins
            if (d < bestD) { bestD = d; best = a; bestS = s; }
        }
        s_sticky = best;
        if (!best || !bestS) { cur.active = false; publish(); return; }
        // want-angles: eye at +1.62, aim point = target chest (+1.2)
        const float PI = 3.14159265f;
        float ox = ps->px, oy = ps->py + 1.62f, oz = ps->pz;
        float tx = bestS->px, ty = bestS->py + 1.2f, tz = bestS->pz;
        float ddx = tx - ox, ddy = ty - oy, ddz = tz - oz;
        float dh = sqrtf(ddx*ddx + ddz*ddz);
        float wantPitch = -atan2f(ddy, dh) * 180.f / PI;
        float wantYaw = atan2f(ddz, ddx) * 180.f / PI - 90.f;
        (void)wantPitch;                         // yaw-only (user-tuned)
        float wy = aa_norm180(wantYaw - pr->yaw);
        // sanity (close targets explode atan2; transients flip signs)
        float dh2 = (bestS->px - ps->px) * (bestS->px - ps->px) +
                    (bestS->pz - ps->pz) * (bestS->pz - ps->pz);
        bool sane = std::isfinite(wy) && fabsf(wy) < 90.f && dh2 > 2.25f;
        if (!sane) { cur.active = false; publish(); return; }
        // full-stop zone: 0.4 of the hitbox, as seen from here (angular size).
        // Inside it the assist is COMPLETELY off — no hard lock on dead center.
        float dhor = sqrtf(dh2);
        cur.stop_deg = atanf(0.4f / dhor) * (180.f / PI);
        if (cur.stop_deg > 25.f) cur.stop_deg = 25.f;
        // smoothing 0.7/0.3 — the pull must feel like magnetism, not a servo
        s_sy = s_sy ? s_sy * 0.7f + wy * 0.3f : wy;
        cur.e_yaw = s_sy;
        cur.active = true;
        publish();
    }
private:
    void* vec[3] = {0, 0, 0};
    float s_sy = 0;
    AimPull cur;                        // last published pull (params refreshed below)
    void publish() {
        cur.strength = sets[2].f;
        cur.speed    = sets[1].f;
        Bus::inst().publish_aim(cur);
    }
};

Module& mod_aimassist() { static AimAssist m; return m; }

} // namespace mc
