// ModCore — game hooks: GameMode attack paths + context discovery
#pragma once
#include <cstdint>

namespace mc {
namespace game {

// both attack paths hooked (base + sub) — KillAura requires this
bool  attack_hooks_ready();
// replay a captured/auto-armed attack through the Sub_attack trampoline
bool  gm_sub_attack_call(void* gm, void* target, void* a3, const float* hit);
// the last REAL attack's {this, gm-context} pair (the proven path)
bool  attack_ctx(void** ths, void** a3);
// auto-arm fallback: GameModeSub object found inside the local player.
// The cache is RE-VALIDATED against the current local player every call and
// invalidated on (re)spawn — a stale gm inside a FREED player was the crash.
void* find_gamemode_sub();
// player (re)loaded: forget the discovered gm + captured attack ctx
void  invalidate_gm_context();
// player swing (game's own post-attack call order)
void  swing_arm();
// GUI debug button support (currently unused by the UI)
void  call_lkm();
bool  has_atk_target();
// suppress the "manual attack" event spam while KillAura sweeps
void  ka_set_busy(bool b);

bool  install_gamemode_hooks();

}
}
