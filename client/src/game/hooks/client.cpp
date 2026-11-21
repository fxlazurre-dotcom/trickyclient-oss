// ModCore — game hooks: ClientInstance — local player capture + cursor grabs.
// ClientInstance::onPlayerLoaded(this=ci, LocalPlayer& player) fires on world
// enter. ClientInstance::grabCursor(this=ci) fires on focus/mouse-grab.
#include "game/sigs/sigs.h"
#include "game/hooks/gamemode.h"   // invalidate_gm_context
#include "game/sdk/SDK.hpp"
#include "game/sdk/actor/LocalPlayer.hpp"
#include "core/log.h"
#include "core/state.h"

#include <MinHook.h>

namespace mc {
namespace game {

typedef void (*t_ci_onPlayerLoaded)(void*, void*);
static t_ci_onPlayerLoaded o_ci_onPlayerLoaded = nullptr;
static void h_ci_onPlayerLoaded(void* ci, void* player) {
    sdk::set_client_instance(ci);
    if (player && sdk::readable_ptr(player, 0x400)) {
        // respawn / dimension change: everything cached INSIDE the old player
        // (GameModeSub discovery, captured attack ctx) is dangling now
        if (sdk::local_player() && sdk::local_player() != player)
            invalidate_gm_context();
        sdk::set_local_player(player);
        MC_LOG("onPlayerLoaded: ci=%p player=%p", ci, player);
    }
    o_ci_onPlayerLoaded(ci, player);
}

typedef void (*t_ci_grab)(void*);
static t_ci_grab o_ci_grab = nullptr;
static void h_ci_grab(void* ci) {
    if (input_frozen()) {   // no re-grab while our GUI owns the cursor
        static long n = 0;
        if (++n <= 3) MC_LOG("grabCursor blocked while GUI open (#%ld)", n);
        return;
    }
    if (!sdk::client_instance() && ci) {
        sdk::set_client_instance(ci);
        MC_LOG("ClientInstance captured via grabCursor: %p", ci);
    }
    o_ci_grab(ci);
}

bool install_client_hooks() {
    struct H { const char* n; void* det; void** orig; } hs[] = {
        {"ClientInstance_onPlayerLoaded", (void*)h_ci_onPlayerLoaded, (void**)&o_ci_onPlayerLoaded},
        {"ClientInstance_grabCursor",     (void*)h_ci_grab,           (void**)&o_ci_grab},
    };
    int ok = 0;
    for (auto& h : hs) {
        uintptr_t a = sig_get(h.n);
        if (!a) { MC_WARN("hook %s: address not resolved", h.n); continue; }
        if (MH_CreateHook((void*)a, h.det, h.orig) == MH_OK) {
            MH_EnableHook((void*)a);
            ok++;
            MC_LOG("hook %s installed @0x%llx", h.n, (unsigned long long)a);
        } else MC_ERR("hook %s MH_CreateHook failed", h.n);
    }
    return ok > 0;
}

} // namespace game
} // namespace mc
