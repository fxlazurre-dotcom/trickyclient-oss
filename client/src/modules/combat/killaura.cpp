// ModCore — module: KillAura. Hits every entity inside the radius each tick.
// Actor enumeration via Level::getRuntimeActorList, positions through the
// Actor-ECS StateVector component, attacks through the real GameModeSub
// derived-vtable guard; the context is auto-found inside the player.
#include "modules/registry.h"
#include "game/sdk/SDK.hpp"
#include "game/sdk/actor/LocalPlayer.hpp"
#include "game/sdk/actor/components.h"
#include "game/sdk/world/Level.hpp"
#include "game/hooks/gamemode.h"
#include "core/log.h"
#include "core/state.h"

#include <windows.h>
#include <cmath>

namespace mc {
using sdk::StateVectorC;
using sdk::RotationC;

class KillAura : public Module {
public:
    KillAura() {
        name = "KillAura"; category = "COMBAT";
        add_float("radius", 3.0f, 1.f, 6.f, false, "%.1f");
    }
    void on_tick(LevelCtx& ctx) override {
        { std::lock_guard<std::mutex> g(mod().mtx);
          if (mod().overlay_open) return; }
        if (!ctx.player || sdk::svc_off() <= 0) return;
        if (!game::attack_hooks_ready()) return;
        if (!sdk::actor_list_available()) return;
        void* ka_gm = game::find_gamemode_sub();     // auto-arm: no manual attack needed
        if (!ka_gm) return;
        if (InterlockedCompareExchange(&busy, 1, 0)) return;   // one sweep at a time
        game::ka_set_busy(true);

        const float radius = sets[0].f;
        StateVectorC* ps = sdk::actor_statevec(ctx.player);
        if (!ps) { busy = 0; game::ka_set_busy(false); return; }

        // reuse the vector; "clear" by resetting end (capacity stays, no reallocs)
        if (vec[0]) vec[1] = vec[0];
        sdk::fill_runtime_actors(ctx.level, vec);
        void** beg = (void**)vec[0];
        void** end = (void**)vec[1];
        if (!beg || !end || end < beg || (size_t)(end - beg) > 4096) { busy = 0; game::ka_set_busy(false); return; }

        // self-discriminators (belt and suspenders): same component, same vtable,
        // or standing INSIDE us (dist < 1 block) — all mean "that is me"
        void* player_vt = *(void**)ctx.player;
        // attack context: REAL captured args win (the proven path); ka_gm is the
        // auto-arm fallback (validated same-class player)
        void* cap_ths = nullptr, *cap_a3 = nullptr;
        void* atk_a3 = game::attack_ctx(&cap_ths, &cap_a3) ? cap_a3 : ka_gm;
        static long s_detail = 0;                 // verbose first hits
        const float r2 = radius * radius;
        int hits = 0, checked = 0;
        for (void** it = beg; it != end; ++it) {
            void* a = *it;
            if (!a || a == (void*)ctx.player) continue;
            if (!sdk::readable_ptr((uint8_t*)a, (size_t)sdk::svc_off() + 8)) continue;
            if (*(void**)a == player_vt) continue;                 // same class = LocalPlayer
            StateVectorC* s = *(StateVectorC**)((uint8_t*)a + sdk::svc_off());
            if (!sdk::readable_ptr(s)) continue;
            if (s == ps) continue;                                 // same ECS component
            float dx = s->px - ps->px, dy = s->py - ps->py, dz = s->pz - ps->pz;
            float d2 = dx*dx + dy*dy + dz*dz;
            if (d2 > r2 || d2 < 1.0f) continue;                    // out of range / inside us = self
            ++checked;
            if (checked > 24) break;
            float hitpos[3] = { s->px, s->py + 1.f, s->pz };
            if (s_detail < 6) {
                ++s_detail;
                MC_LOG("killaura: atk#%ld vt=%llx pos=(%.1f,%.1f,%.1f) ctx=%s",
                       s_detail,
                       (unsigned long long)((uintptr_t)*(void**)a - (uintptr_t)GetModuleHandleW(nullptr)),
                       s->px, s->py, s->pz, (atk_a3 == ka_gm) ? "gm" : "captured");
            }
            game::gm_sub_attack_call(ka_gm, a, atk_a3, hitpos);
            if (s_detail <= 6) MC_LOG("killaura: atk ok");
            ++hits;
        }
        if (hits) {
            static long sw = 0;
            if (++sw <= 6) MC_LOG("killaura: hits=%d, swinging", hits);
            game::swing_arm();          // swing AFTER the attacks (game's own order)
        }
        busy = 0;
        game::ka_set_busy(false);
        static long s_sweeps = 0;
        ++s_sweeps;
        if (s_sweeps <= 12 || (hits && (s_sweeps % 200) == 0))
            MC_LOG("killaura: sweep #%ld hit %d actor(s) (list=%d)", s_sweeps, hits, (int)(end - beg));
    }
private:
    void* vec[3] = {0, 0, 0};          // std::vector<Actor*> {begin,end,cap}
    volatile LONG busy = 0;
};

Module& mod_killaura() { static KillAura m; return m; }

} // namespace mc
