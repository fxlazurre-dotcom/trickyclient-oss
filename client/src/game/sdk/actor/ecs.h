// ModCore — ECS component access (entt sparse set, replicated from Actor_getYaw @0x204d960)
#pragma once
#include "game/sdk/SDK.hpp"
#include <cstdint>

namespace mc {
namespace sdk {

// wrapper layouts observed in 1.26.52.3:
//   ActorWrapperA { ?@0, registry@0x8, entity_id@0x10 }        (RuntimeActorList / getStateVector arg)
//   ActorWrapperB { ?@0, ?, registry@0x10, entity_id@0x18 }    (getYaw arg)

struct WrapA { void* pad; void* registry; uint32_t eid; };
struct WrapB { void* pad[2]; void* registry; uint32_t eid; };

inline uintptr_t registry_of(const void* w, bool layoutB) {
    return *(uintptr_t*)((uint8_t*)w + (layoutB ? 0x10 : 0x8));
}
inline uint32_t eid_of(const void* w, bool layoutB) {
    return *(uint32_t*)((uint8_t*)w + (layoutB ? 0x18 : 0x10));
}

// resolver used by Actor_getStateVector (0x43765f0): pool* resolverA(registry, hash)
typedef void* (*resolverA_t)(void*, uint32_t);

// entt paged lookup replicated EXACTLY from Actor_getStateVector (0x43765f0):
//   idx=id&0x3FFFF; page=idx>>11; pages=[pool+8], end=[pool+0x10]
//   pg=[pages+page*8]; packed=[pg+(idx&0x7FF)*4]; version check vs (id&0xFFFC0000)^0x3FFFE
//   k=(packed&0x3FF80)>>4; slot=packed&0x7F; component=[ [pool+0x50]+k ] + slot*36
inline void* resolve_component_A(void* registry, uint32_t id, uint32_t hash, resolverA_t res) {
    if (!registry || !res) return nullptr;
    uint8_t* pool = (uint8_t*)res(registry, hash);
    if (!pool) return nullptr;
    uint32_t idx = id & 0x3FFFF;
    uint32_t page = idx >> 11;
    uintptr_t pages = *(uintptr_t*)(pool + 0x08);
    uintptr_t pend  = *(uintptr_t*)(pool + 0x10);
    if (page >= ((pend - pages) >> 3)) return nullptr;
    uintptr_t pg = *(uintptr_t*)(pages + (uintptr_t)page * 8);
    if (!pg) return nullptr;
    uint32_t ei = idx & 0x7FF;
    uint32_t packed = *(uint32_t*)(pg + (uintptr_t)ei * 4);
    if (((id & 0xFFFC0000u) ^ packed) > 0x3FFFE) return nullptr;
    uintptr_t comps = *(uintptr_t*)(pool + 0x50);
    uintptr_t base  = *(uintptr_t*)(comps + (uintptr_t)((packed & 0x3FF80) >> 4));
    if (!base) return nullptr;
    uint32_t slot = packed & 0x7F;
    return (void*)(base + (uintptr_t)slot * 36);
}

// legacy aliases (stride-36 pool slot layouts now live in components.h)

} // namespace sdk
} // namespace mc
