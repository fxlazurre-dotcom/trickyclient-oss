// ModCore — game hooks: Level::tick — telemetry + the module-tick dispatch.
// The hook itself knows nothing about modules: it bumps the bus tick counter
// and dispatches the bus subscribers (modules/registry.cpp registered there).
#include "game/sigs/sigs.h"
#include "game/sdk/SDK.hpp"
#include "game/sdk/actor/LocalPlayer.hpp"
#include "game/sdk/actor/components.h"
#include "core/log.h"
#include "core/threadbus.h"

#include <MinHook.h>
#include <cmath>
#include <windows.h>

namespace mc {
namespace game {
using sdk::StateVectorC;
using sdk::RotationC;

typedef void (*t_level_tick)(void*);
static t_level_tick o_level_tick = nullptr;

// BPS EMA state (horizontal blocks per second for the HUD)
static float s_bps = 0;

static void h_level_tick(void* level) {
    o_level_tick(level);
    Bus& bus = Bus::inst();
    bus.bump_ticks();
    static bool s_first = true;
    if (s_first) { s_first = false; MC_LOG("first Level::tick received (in-world), level=%p", level); }

    // modules (movement/killaura/aimassist) — subscribed via the bus so this
    // layer never includes modules/
    bus.dispatch_tick(level);

    // HUD telemetry: resolve the player HERE — it used to depend on movement
    // modules having validated the player (early-return -> coords stayed "--")
    Telemetry t = bus.telemetry();
    if (void* hlp = sdk::pick_local_player(level)) {
        StateVectorC* hps = sdk::actor_statevec((uint8_t*)hlp);
        if (hps) {
            t.pos_x = hps->px; t.pos_y = hps->py; t.pos_z = hps->pz;
            t.pos_valid = true;
        }
        if (RotationC* hr = sdk::actor_rotation((uint8_t*)hlp)) {
            t.yaw = hr->yaw; t.pitch = hr->pitch;
        }
    }
    // BPS: smoothed horizontal speed (blocks/sec)
    {
        static float s_px = 0, s_pz = 0;
        static ULONGLONG s_ms = 0;
        static bool s_init = false;
        ULONGLONG now_ms = GetTickCount64();
        if (s_init && now_ms > s_ms) {
            float dx = t.pos_x - s_px, dz = t.pos_z - s_pz;
            float dist = sqrtf(dx * dx + dz * dz);
            float inst = dist / ((float)(now_ms - s_ms) / 1000.f);
            if (inst > 30.f) inst = 30.f;              // teleports / world change
            s_bps += (inst - s_bps) * 0.12f;           // EMA smoothing
        }
        s_px = t.pos_x; s_pz = t.pos_z; s_ms = now_ms; s_init = true;
        t.bps = s_bps;
    }
    bus.publish_telemetry(t);
}

bool install_level_hooks() {
    uintptr_t a = sig_get("Level_tick");
    if (!a) { MC_WARN("hook Level_tick: address not resolved"); return false; }
    if (MH_CreateHook((void*)a, (void*)h_level_tick, (void**)&o_level_tick) == MH_OK) {
        MH_EnableHook((void*)a);
        MC_LOG("hook Level_tick installed @0x%llx", (unsigned long long)a);
        return true;
    }
    MC_ERR("hook Level_tick MH_CreateHook failed");
    return false;
}

} // namespace game
} // namespace mc
