// ModCore — game layer: hook installation orchestrator.
// Order matters: signatures resolve FIRST (dllmain does this before calling
// us), then the MinHook tables, then the SDK accessor wiring.
#include "game/hooks/hooks.h"
#include "game/hooks/gamemode.h"
#include "game/hooks/input.h"
#include "game/sigs/sigs.h"
#include "game/sdk/world/Level.hpp"
#include "game/sdk/actor/ecs.hpp"
#include "core/log.h"
#include "game/patches.h"

#include <MinHook.h>

namespace mc {
namespace game {

// per-file installers (defined next to their trampolines)
bool install_gamemode_hooks();   // game/hooks/gamemode.cpp
bool install_level_hooks();      // game/hooks/level.cpp
bool install_client_hooks();     // game/hooks/client.cpp

void install_all() {
    MH_Initialize();
    int files_ok = 0;
    if (install_gamemode_hooks()) files_ok++;
    if (install_level_hooks()) files_ok++;
    if (install_client_hooks()) files_ok++;
    if (install_input_hooks()) files_ok++;
    install_raw_clicker();
    MC_LOG("hooks installed (hook-file groups ok: %d/4)", files_ok);

    // SDK accessor wiring
    sdk::set_actor_list_fn((sdk::t_get_actor_list)sig_get("Level_getRuntimeActorList"));
    sdk::extract_ecs_resolvers();
    patches_prepare();
}

} // namespace game
} // namespace mc
