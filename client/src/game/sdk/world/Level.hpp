// ModCore — game SDK: Level — runtime actor list access
#pragma once
#include <cstdint>

namespace mc {
namespace sdk {

typedef void (*t_get_actor_list)(void*, void*);

// filled by install_hooks() from the Level_getRuntimeActorList signature
void set_actor_list_fn(t_get_actor_list f);
bool actor_list_available();
// caller owns the vector storage: pass a void* vec[3] = {begin,end,cap};
// reset it (vec[1]=vec[0]) before the call to reuse capacity without reallocs
void fill_runtime_actors(void* level, void** vec3);

} // namespace sdk
} // namespace mc
