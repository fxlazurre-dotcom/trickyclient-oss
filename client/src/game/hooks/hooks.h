// ModCore — game layer: hook installation orchestrator.
// Each hooks/*.cpp owns its MinHook table {sig name, detour, &trampoline};
// install_all() runs them in the verified order (sig-resolve BEFORE hooks).
#pragma once

namespace mc {
namespace game {

// full bootstrap of every game-side hook (called once from dllmain)
void install_all();

}
}
