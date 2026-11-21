// ModCore — module: Speed. Writes VELOCITY after physics (Rune logic):
// WASD direction * boost * air friction into the StateVector component.
#include "modules/registry.h"
#include "game/sdk/SDK.hpp"
#include "game/sdk/actor/LocalPlayer.hpp"
#include "game/sdk/actor/components.h"
#include "core/state.h"

#include <windows.h>
#include <cmath>

namespace mc {
using sdk::StateVectorC;
using sdk::RotationC;

class Speed : public Module {
public:
    Speed() {
        name = "Speed"; category = "MOVEMENT";
        add_float("boost", 0.6f, 0.05f, 1.5f, false, "%.2f");
    }
    void on_tick(LevelCtx& ctx) override {
        if (!ctx.player || sdk::svc_off() <= 0) return;
        StateVectorC* s = sdk::actor_statevec(ctx.player);
        RotationC* r = sdk::actor_rotation(ctx.player);
        if (!s) return;

        unsigned long long now = GetTickCount64();
        bool isNewTick = now - last_tick_ms >= 50;
        if (isNewTick) last_tick_ms = now;

        float yaw = r ? r->yaw : 0.f;
        bool onGround = fabsf(s->vy) < 0.01f;
        if (isNewTick) {
            if (onGround) friction = 1.0f;
            else friction *= 0.975f;
        }

        // WASD direction (Rune logic)
        float moveYaw = yaw;
        bool w = GetAsyncKeyState('W') & 0x8000, skey = GetAsyncKeyState('S') & 0x8000;
        bool k = GetAsyncKeyState('A') & 0x8000, d = GetAsyncKeyState('D') & 0x8000;
        bool moving = w || skey || k || d;
        if (!moving) return;
        if (w && k) moveYaw -= 45.f; else if (w && d) moveYaw += 45.f;
        else if (skey && k) moveYaw -= 135.f; else if (skey && d) moveYaw += 135.f;
        else if (skey) moveYaw += 180.f; else if (k) moveYaw -= 90.f;
        else if (d) moveYaw += 90.f;

        float sp = sets[0].f * friction;
        float rad = (moveYaw + 90.f) * 3.14159265f / 180.f;
        s->vx = cosf(rad) * sp;
        s->vz = sinf(rad) * sp;
    }
private:
    float friction = 1.0f;
    unsigned long long last_tick_ms = 0;
};

Module& mod_speed() { static Speed m; return m; }

} // namespace mc
