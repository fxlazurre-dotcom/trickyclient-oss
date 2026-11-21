// ModCore — module: AutoClicker. The clicking itself lives in the game input
// layer (GameInput reading clicker + raw-input path); this module owns the
// settings and publishes them into the thread bus for the phase machine.
#include "modules/registry.h"
#include "core/threadbus.h"

namespace mc {

class AutoClicker : public Module {
public:
    AutoClicker() {
        name = "AutoClicker"; category = "COMBAT";
        add_float("cps",    8.0f, 2.f, 20.f, false, "%.0f");
        add_float("jitter", 1.5f, 0.f, 5.f,  false, "%.1f");
    }
    void on_tick(LevelCtx&) override { publish(); }
    void on_toggle(bool) override { publish(); }
private:
    void publish() {
        ClickerCfg c;
        c.on = enabled;
        c.cps = sets[0].f;
        c.jitter = sets[1].f;
        Bus::inst().publish_clicker(c);
    }
};

Module& mod_autoclicker() { static AutoClicker m; return m; }

} // namespace mc
