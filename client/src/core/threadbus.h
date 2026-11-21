// ModCore — core layer: the thread bus.
// Channels between threads WITHOUT upward layer knowledge:
//   game-thread  -> UI        : telemetry (pos/rot/bps/ticks), click ring (CPS)
//   modules      -> input hk  : aim pull parameters (AimAssist -> GetMouseState hook)
//   modules/UI   -> input hk  : clicker config (AutoClicker -> phase machine)
//   game/hooks   -> modules   : level-tick subscriber dispatch (modules register
//                               here so the game layer never includes modules/)
#pragma once
#include <mutex>
#include <vector>

namespace mc {

struct Telemetry {
    float pos_x = 0, pos_y = 0, pos_z = 0;
    float yaw = 0, pitch = 0;
    float bps = 0;                  // horizontal blocks per second (smoothed)
    bool  pos_valid = false;        // true once a real position was read
    long long ticks = 0;            // Level::tick counter
};

struct AimPull {
    bool  active = false;           // error valid -> the input hook pulls
    float e_yaw = 0;                // yaw error (deg, signed)
    float stop_deg = 1.5f;          // full-stop zone (angular hitbox fraction)
    float strength = 0.5f;          // pull hardness 0.05..1
    float speed = 2.0f;             // rate cap scale
};

struct ClickerCfg {
    bool  on = false;
    float cps = 8.f, jitter = 1.5f;
};

class Bus {
public:
    static Bus& inst() { static Bus b; return b; }

    // ---- telemetry (game thread publishes, UI snapshots) ----
    void    publish_telemetry(const Telemetry& t);
    Telemetry telemetry();
    void    bump_ticks();               // Level::tick counter + freshness
    void    note_lkm(); void note_pkm();// attack/use counters (event hooks)
    // in-world = a Level::tick within the last 2 s (game truth; the OS
    // cursor-visibility check lied under wine/Hyprland)
    bool    in_world();

    // ---- clicks (input thread notes, UI reads CPS) ----
    void    note_click();               // a press EDGE (real or synthetic)
    int     cps();                      // clicks in the last 1000 ms

    // ---- aim pull (AimAssist module -> GameInput getter hook) ----
    void    publish_aim(const AimPull& a);
    AimPull aim();

    // ---- clicker config (AutoClicker module -> phase machine) ----
    void    publish_clicker(const ClickerCfg& c);
    ClickerCfg clicker();

    // ---- level-tick subscribers (game/hooks -> modules, inverted dep) ----
    void    subscribe_tick(void (*fn)(void* level));
    void    dispatch_tick(void* level);

private:
    Bus() = default;
    std::mutex          tx_;              // telemetry
    Telemetry           t_;
    unsigned long long  tick_seen_ms_ = 0;

    std::mutex          ax_;              // aim
    AimPull             a_;
    std::mutex          cx_;              // clicker
    ClickerCfg          c_;

    std::mutex          sx_;              // tick subscribers
    std::vector<void(*)(void*)> subs_;
};

} // namespace mc
