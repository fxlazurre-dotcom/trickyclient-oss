// ModCore — core layer: shared client state (theme, UI options, freeze flag).
// Module settings NO LONGER live here: modules own their Setting arrays
// (modules/module.h) and game-thread telemetry lives in core/threadbus.
#pragma once
#include <mutex>
#include <atomic>

namespace mc {

struct ModuleState {
    std::mutex mtx;
    // AC patch toggles (patch application lives in game/patches.cpp; the
    // toggles have no GUI yet — kept wired exactly as before)
    bool  ac_field_validator = false;
    bool  ac_correction_tick = false;
    bool  ac_passenger_stats = false;
    bool  ac_stats_events = false;
    bool  ac_interp_system = false;
    bool  ac_predsync_system = false;
    bool  ac_drop_moveplayer = false;
    // ---- nursultan theme: TWO gradient colors drive the whole GUI + HUD ----
    // accb (badges/chips/checkboxes) and acc-glow are derived as their average
    float acc1[3] = { 207.f/255.f,  59.f/255.f, 255.f/255.f };  // #cf3bff
    float acc2[3] = { 255.f/255.f, 138.f/255.f,  42.f/255.f };  // #ff8a2a
    float accent[3] = { 235.f/255.f, 5.f/255.f, 90.f/255.f }; // legacy single-accent (unused)
    // client options (bound by MISC tool rows in the GUI)
    unsigned menu_key = 0x2D;      // INSERT/DELETE hardwired in the overlay poll
    bool  opt_tick_sound  = true;  // UI tick sound on interactions
    bool  opt_hint = true;         // "INSERT / DELETE - toggle menu" line
    int   log_filter = 0;          // 0 all, 1 errors/warnings, 2 events
    float log_depth = 300;         // lines shown in the log viewer
    // menu visibility (INSERT poll in the overlay writes it)
    bool  overlay_open = false;
};

ModuleState& mod();

// ---- menu-open deep freeze ------------------------------------------------
// Owned in the CORE layer so the game-side hooks (MouseDevice::feed etc.) can
// check it without reaching up into the UI. The overlay sets it while the
// menu owns the cursor; feed hooks + GameInput freeze read it.
bool input_frozen();
void set_input_frozen(bool on);

} // namespace mc
