// ModCore — game SDK: local player discovery + validation.
// Written by ClientInstance::onPlayerLoaded (game/hooks/client.cpp); the
// heuristic probe (cache + ClientInstance field scan + svc-offset discovery)
// lives here — moved VERBATIM from the monolith (do not retune the gates).
#pragma once
#include <cstdint>

namespace mc {
namespace sdk {

struct StateVectorC;
struct RotationC;

// storage
void  set_local_player(void* p);
void* local_player();
void  set_client_instance(void* ci);
void* client_instance();
bool  local_player_valid();          // non-null + readable + sane headroom

// svc offset (StateVectorC* inside the player; discovered at runtime)
int   svc_off();
void  set_last_level(void* level);   // reconsole/blk diagnostics
void* last_level();
unsigned long long player_loaded_ms();   // world-load timestamp (scan grace)
void  reset_player_probe();          // GUI/debug: forget and re-discover

// validation (also discovers g_svc_off on first success)
bool  validate_player(uint8_t* p);
bool  wrapper_looks_sane(const void* w);

// cache + ClientInstance scan (ci+0x8..0x800) — the canonical resolver
void* pick_local_player(void* level);

// component accessors (null until svc_off() discovered)
StateVectorC* actor_statevec(uint8_t* a);
RotationC*    actor_rotation(uint8_t* a);

} // namespace sdk
} // namespace mc
