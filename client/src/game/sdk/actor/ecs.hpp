// ModCore — game SDK: ECS resolver storage.
// p_get_statevec/p_get_yaw/p_resolverA were resolved + kept in the monolith's
// install_hooks(); only the pool resolver is actually consumed (entt lookup
// in ecs.h). The plain getters are kept for SDK completeness.
#pragma once
#include "game/sdk/actor/ecs.h"

namespace mc {
namespace sdk {

typedef void* (*t_get_statevec)(const void*, const void*);
typedef float (*t_get_yaw)(const void*);

extern t_get_statevec p_get_statevec;
extern t_get_yaw      p_get_yaw;
extern resolverA_t    p_resolverA;   // extracted from Actor_getStateVector body (+0x15)

// pull the E8 rel32 resolver out of the getStateVector body (called once at
// hook install); returns false when the expected opcode is missing
bool extract_ecs_resolvers();

} // namespace sdk
} // namespace mc
