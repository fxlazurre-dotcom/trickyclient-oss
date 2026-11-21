// ModCore — game SDK: Level — runtime actor list access
#include "game/sdk/world/Level.hpp"

namespace mc {
namespace sdk {

static t_get_actor_list p_get_actor_list = nullptr;
void set_actor_list_fn(t_get_actor_list f) { p_get_actor_list = f; }
bool actor_list_available() { return p_get_actor_list != nullptr; }
void fill_runtime_actors(void* level, void** vec3) {
    if (p_get_actor_list) p_get_actor_list(level, vec3);
}

} // namespace sdk
} // namespace mc
