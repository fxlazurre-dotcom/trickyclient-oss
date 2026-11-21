// ModCore — game SDK: local player discovery (moved verbatim from the monolith)
#include "game/sdk/actor/LocalPlayer.hpp"
#include "game/sdk/SDK.hpp"
#include "game/sdk/actor/components.h"
#include "core/log.h"

#include <cmath>
#include <windows.h>

namespace mc {
namespace sdk {

static void* g_local_player = nullptr;    // written by onPlayerLoaded / probe
static void* g_client_instance = nullptr;
static int   g_svc_off = -1;              // discovered at runtime (Rune: 0x218)

void set_local_player(void* p) { g_local_player = p; }
void* local_player()          { return g_local_player; }
void set_client_instance(void* ci) { g_client_instance = ci; }
void* client_instance()       { return g_client_instance; }
bool local_player_valid() {
    return g_local_player && readable_ptr(g_local_player, 0x400);
}
int  svc_off() { return g_svc_off; }

// probe-reset for the GUI button
void reset_player_probe() {
    g_svc_off = -1;
    MC_LOG("player probe reset by user (svc offset will re-discover)");
}

bool wrapper_looks_sane(const void* w) {
    if ((uintptr_t)w < 0x10000 || (uintptr_t)w & 7) return false;
    __int64 eid = *(__int64*)((uint8_t*)w + 0x10);
    if (eid < 0 || eid > 0x2000000) return false;
    void* reg = *(void**)((uint8_t*)w + 0x8);
    if (!reg) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(reg, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT || mbi.RegionSize < 0x1000) return false;
    return true;
}

// validate svc offset on a candidate player pointer
bool validate_player(uint8_t* p) {
    if (!readable_ptr(p, 0x400)) return false;
    if (g_svc_off > 0) {
        StateVectorC* s = *(StateVectorC**)(p + g_svc_off);
        if (!(readable_ptr(s) && fabsf(s->px) < 3e7f && fabsf(s->pz) < 3e7f &&
              s->py > 1.0f && s->py < 3000.f))
            return false;
        RotationC* rr = *(RotationC**)(p + g_svc_off + 0x10);
        return readable_ptr(rr) && std::isfinite(rr->pitch) && std::isfinite(rr->yaw) &&
               fabsf(rr->pitch) <= 95.f && fabsf(rr->yaw) <= 185.f;
    }
    for (int off = Offsets::SVC_SCAN_LO; off <= Offsets::SVC_SCAN_HI; off += 8) {
        StateVectorC* s = *(StateVectorC**)(p + off);
        if (!readable_ptr(s)) continue;
        if (fabsf(s->px) < 3e7f && fabsf(s->pz) < 3e7f &&
            s->py > 1.0f && s->py < 3000.f &&
            fabsf(s->vx) < 30.f && fabsf(s->vy) < 30.f) {
            RotationC* rr = *(RotationC**)(p + off + 0x10);
            if (!(readable_ptr(rr) && std::isfinite(rr->pitch) && std::isfinite(rr->yaw) &&
                  fabsf(rr->pitch) <= 95.f && fabsf(rr->yaw) <= 185.f))
                continue;
            g_svc_off = off;
            MC_LOG("svc offset discovered: player+0x%x (pos %.1f %.1f %.1f)",
                   off, s->px, s->py, s->pz);
            return true;
        }
    }
    return false;
}

// find the local player: cached one, or hunt it inside ClientInstance fields
void* pick_local_player(void* /*level*/) {
    if (g_local_player && validate_player((uint8_t*)g_local_player))
        return g_local_player;
    g_local_player = nullptr;
    if (!g_client_instance) return nullptr;
    uint8_t* ci = (uint8_t*)g_client_instance;
    if (!readable_ptr(ci, 0x800)) return nullptr;
    static int hint = -1;
    if (hint > 0) {
        void* p = *(void**)(ci + hint);
        if (p && validate_player((uint8_t*)p)) { g_local_player = p; return p; }
    }
    for (int off = 0x8; off <= 0x800; off += 8) {
        void* p = *(void**)(ci + off);
        if (!p || !readable_ptr((uint8_t*)p, 0x400)) continue;
        if (validate_player((uint8_t*)p)) {
            hint = off;
            g_local_player = p;
            MC_LOG("local player found in ci+0x%x -> %p", off, p);
            return p;
        }
    }
    return nullptr;
}

StateVectorC* actor_statevec(uint8_t* a) {
    if (g_svc_off < 0) return nullptr;
    StateVectorC* s = *(StateVectorC**)(a + g_svc_off);
    return readable_ptr(s) ? s : nullptr;
}

RotationC* actor_rotation(uint8_t* a) {
    if (g_svc_off < 0) return nullptr;
    RotationC* r = *(RotationC**)(a + g_svc_off + 0x10);
    if (!readable_ptr(r)) return nullptr;
    if (fabsf(r->pitch) > 180.f || fabsf(r->yaw) > 360.f) return nullptr;
    return r;
}

} // namespace sdk
} // namespace mc
