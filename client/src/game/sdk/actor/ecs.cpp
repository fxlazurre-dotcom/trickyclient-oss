// ModCore — game SDK: ECS resolver storage + body extraction (was install_hooks part)
#include "game/sdk/actor/ecs.hpp"
#include "game/sigs/sigs.h"
#include "core/log.h"

namespace mc {
namespace sdk {

t_get_statevec p_get_statevec = nullptr;
t_get_yaw      p_get_yaw = nullptr;
resolverA_t    p_resolverA = nullptr;

bool extract_ecs_resolvers() {
    p_get_statevec = (t_get_statevec)sig_get("Actor_getStateVector");
    p_get_yaw      = (t_get_yaw)sig_get("Actor_getYaw");
    MC_LOG("accessors: statevec=%p yaw=%p", (void*)p_get_statevec, (void*)p_get_yaw);
    if (p_get_statevec) {
        uint8_t* b = (uint8_t*)p_get_statevec;
        if (b[0x15] == 0xE8) {                       // mov edx,hash; call resolverA
            int32_t rel = *(int32_t*)(b + 0x16);
            p_resolverA = (resolverA_t)(b + 0x1A + rel);
            MC_LOG("layout-A pool resolver extracted @0x%llx",
                   (unsigned long long)p_resolverA);
            return true;
        }
        MC_WARN("statevec resolver extraction failed (byte@0x15=%02x)", b[0x15]);
    }
    return false;
}

} // namespace sdk
} // namespace mc
