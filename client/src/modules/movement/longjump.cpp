// ModCore — module: LongJump. Sets the vertical velocity on jump edges
// (0.42 = vanilla jump) while Space is physically held.
#include "modules/registry.h"
#include "game/sdk/SDK.hpp"
#include "game/sdk/actor/LocalPlayer.hpp"
#include "game/sdk/actor/components.h"
#include "core/input.h"

#include <windows.h>
#include <cmath>

namespace mc {
using sdk::StateVectorC;
using sdk::RotationC;

class LongJump : public Module {
public:
    LongJump() {
        name = "LongJump"; category = "MOVEMENT";
        add_float("jump vy", 0.12f, 0.1f, 1.5f, false, "%.2f");
    }
    void on_tick(LevelCtx& ctx) override {
        if (!ctx.player || sdk::svc_off() <= 0) return;
        StateVectorC* s = sdk::actor_statevec(ctx.player);
        if (!s) return;

        unsigned long long now = GetTickCount64();
        bool isNewTick = now - last_tick_ms >= 50;
        if (isNewTick) last_tick_ms = now;
        bool onGround = fabsf(s->vy) < 0.01f;
        if (onGround && isNewTick) {
            if (real_key_down(VK_SPACE)) {
                s->vy = sets[0].f;      // 0.42 = vanilla jump
            }
        }
    }
private:
    unsigned long long last_tick_ms = 0;
};

Module& mod_longjump() { static LongJump m; return m; }

} // namespace mc
