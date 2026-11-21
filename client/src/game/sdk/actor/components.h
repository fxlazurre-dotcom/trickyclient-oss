// ModCore — game SDK: Actor ECS component layouts — VERIFIED on 1.26.52.3
// (legacy Actor layout of RuneClient 1.26.20 — svc@0x218, aabb@0x220, rot@0x228 —
//  does NOT hold here; components are reached through the entt registry)
#pragma once
#include <cstdint>

namespace mc {
namespace sdk {

// stride-36 pool slot layouts (see ecs.h for the paged lookup)
struct StateVectorC { float px, py, pz; float ox, oy, oz; float vx, vy, vz; };
struct RotationC    { float pitch, yaw; float opitch, oyaw; };

} // namespace sdk
} // namespace mc
