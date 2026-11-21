// ModCore — module registry: the static module table (dispatch order == the
// monolith's per-tick order: movement first, then combat), bus subscription,
// generic toggle, ArrayList source and ini persistence.
#include "modules/registry.h"
#include "modules/module.h"
#include "game/sdk/SDK.hpp"
#include "game/sdk/actor/LocalPlayer.hpp"
#include "core/log.h"
#include "core/state.h"
#include "core/threadbus.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <thread>
#include <atomic>

namespace mc {

// module instances (one definition per module file)
Module& mod_speed();         Module& mod_longjump();      Module& mod_autosprint();
Module& mod_killaura();      Module& mod_autoclicker();   Module& mod_aimassist();
Module& mod_ticksounds();    Module& mod_hintline();      Module& mod_logfilter();
Module& mod_logdepth();      Module& mod_copylog();       Module& mod_debuginfo();
Module& mod_hud_watermark(); Module& mod_hud_arraylist(); Module& mod_hud_info();
Module& mod_blockesp();               // modules/visual/blockesp.cpp
void autosprint_worker_start();   // modules/movement/autosprint.cpp

static Module* g_table[] = {
    // MOVEMENT (listed in the ArrayList)
    &mod_speed(), &mod_longjump(), &mod_autosprint(),
    // COMBAT (listed)
    &mod_killaura(), &mod_autoclicker(), &mod_aimassist(),
    // VISUAL (3D overlay)
    &mod_blockesp(),
    // MISC tools (client options; not listed)
    &mod_ticksounds(), &mod_hintline(), &mod_logfilter(), &mod_logdepth(),
    &mod_copylog(), &mod_debuginfo(),
    // HUD elements (not listed)
    &mod_hud_watermark(), &mod_hud_arraylist(), &mod_hud_info(),
};
static const char* const g_cats[] = { "COMBAT", "MOVEMENT", "VISUAL", "MISC", "HUD" };

static std::mutex g_mtx;
static std::atomic<bool> g_dirty{false};

// ---- setting factory helpers (Module) --------------------------------------
static Setting& mod_slot(Module* m, const char* n) {
    static Setting spill{};               // guard target — NEVER stored for real use
    if (m->nsets >= (int)(sizeof(m->sets)/sizeof(m->sets[0]))) {
        MC_WARN("module %s: setting '%s' dropped (sets[] full)", m->name, n);
        return spill;
    }
    return m->sets[m->nsets++];
}

Setting& Module::add_bool(const char* n, bool v) {
    Setting& s = mod_slot(this, n); s.kind = Setting::BOOL; s.name = n; s.b = v; return s;
}
Setting& Module::add_float(const char* n, float v, float lo, float hi, bool inte, const char* fmt) {
    Setting& s = mod_slot(this, n); s.kind = Setting::FLOAT; s.name = n; s.f = v;
    s.lo = lo; s.hi = hi; s.inte = inte; s.fmt = fmt; return s;
}
Setting& Module::add_enum(const char* n, const char* const* opts, int nopts, int cur) {
    Setting& s = mod_slot(this, n); s.kind = Setting::ENUM; s.name = n;
    s.opts = opts; s.nopts = nopts; s.cur = cur; return s;
}
Setting& Module::setting(const char* n) {
    for (int i = 0; i < nsets; i++) if (!strcmp(sets[i].name, n)) return sets[i];
    return sets[0];
}

// ---- table access ----------------------------------------------------------
std::mutex& modules_mtx() { return g_mtx; }
Module* const* modules_all(int* n) { *n = (int)(sizeof(g_table)/sizeof(g_table[0])); return g_table; }
const char* const* modules_categories(int* n) { *n = (int)(sizeof(g_cats)/sizeof(g_cats[0])); return g_cats; }
Module* mod_find(const char* name) {
    int n; Module* const* all = modules_all(&n);
    for (int i = 0; i < n; i++) if (!strcmp(all[i]->name, name)) return all[i];
    return nullptr;
}

// ---- toggling ---------------------------------------------------------------
void modules_toggle(Module* m) {
    bool on;
    if (m->toggle_state) {
        std::lock_guard<std::mutex> g(mod().mtx);
        *m->toggle_state = !*m->toggle_state;
        on = *m->toggle_state;
    } else {
        std::lock_guard<std::mutex> g(g_mtx);
        m->enabled = !m->enabled;
        on = m->enabled;
        m->on_toggle(on);
    }
    MC_LOG("%s %s", m->name, on ? "ON" : "OFF");
    modules_mark_dirty();
}
void modules_mark_dirty() { g_dirty.store(true); }

// ---- HUD helpers -------------------------------------------------------------
bool modules_hud_any() {
    int n; Module* const* all = modules_all(&n);
    for (int i = 0; i < n; i++)
        if (!strcmp(all[i]->category, "HUD") && all[i]->enabled) return true;
    return false;
}
std::vector<std::string> modules_arraylist() {
    std::vector<std::string> out;
    int n; Module* const* all = modules_all(&n);
    std::lock_guard<std::mutex> g(g_mtx);
    for (int i = 0; i < n; i++) {
        const Module* m = all[i];
        if (m->listed && m->enabled && m->has_toggle) out.push_back(m->name);
    }
    return out;
}

// ---- tick dispatch (bus subscriber) ------------------------------------------
void modules_dispatch_tick(void* level) {
    int n; Module* const* all = modules_all(&n);
    std::lock_guard<std::mutex> g(g_mtx);        // settings stay stable during the tick
    LevelCtx ctx;
    ctx.level = level;
    sdk::set_last_level(level);   // RE console diagnostics (blk/hit)
    ctx.player = (uint8_t*)sdk::pick_local_player(level);
    for (int i = 0; i < n; i++) {
        Module* m = all[i];
        if (m->enabled) m->on_tick(ctx);
    }
}

// ---- ini persistence ------------------------------------------------------------
// runtime/tricky.ini — a tiny key=value format ([core] + one section per module)
static const char* k_ini_path = "Z:/home/florixx/mc-modloader/runtime/tricky.ini";

static void ini_save() {
    FILE* f = fopen(k_ini_path, "w");
    if (!f) { MC_WARN("ini: cannot write %s", k_ini_path); return; }
    { std::lock_guard<std::mutex> g(mod().mtx); auto& m = mod();
      fprintf(f, "[core]\nacc1=%.4f,%.4f,%.4f\nacc2=%.4f,%.4f,%.4f\nmenu_key=%u\n"
                 "tick_sound=%d\nhint=%d\nlog_filter=%d\nlog_depth=%.0f\n",
              m.acc1[0], m.acc1[1], m.acc1[2], m.acc2[0], m.acc2[1], m.acc2[2],
              m.menu_key, (int)m.opt_tick_sound, (int)m.opt_hint, m.log_filter, m.log_depth); }
    int n; Module* const* all = modules_all(&n);
    std::lock_guard<std::mutex> g(g_mtx);
    for (int i = 0; i < n; i++) {
        Module* m = all[i];
        fprintf(f, "[%s]\n", m->name);
        // NOTE: `enabled` is deliberately NOT persisted — modules must not
        // silently re-arm after a restart/crash (that looked like "wrong
        // modules on" in the ArrayList). Only their settings survive.
        for (int s = 0; s < m->nsets; s++) {
            const Setting& st = m->sets[s];
            if (st.state_b || st.state_f || st.state_i) continue;   // bound to core state
            if (st.kind == Setting::BOOL) fprintf(f, "%s=%d\n", st.name, (int)st.b);
            else if (st.kind == Setting::FLOAT) fprintf(f, "%s=%.3f\n", st.name, st.f);
            else fprintf(f, "%s=%d\n", st.name, st.cur);
        }
        if (!strcmp(m->category, "HUD")) fprintf(f, "pos=%.1f,%.1f\n", m->hud_x, m->hud_y);
    }
    fclose(f);
    g_dirty.store(false);
    MC_LOG("ini: saved %s", k_ini_path);
}

static void ini_load() {
    FILE* f = fopen(k_ini_path, "r");
    if (!f) { MC_LOG("ini: no config yet (%s)", k_ini_path); return; }
    char line[256];
    Module* cur = nullptr;
    while (fgets(line, sizeof line, f)) {
        size_t l = strlen(line);
        while (l && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = 0;
        if (!l || line[0] == ';') continue;
        if (line[0] == '[') {
            char* end = strchr(line, ']');
            if (!end) continue;
            *end = 0;
            cur = strcmp(line + 1, "core") == 0 ? nullptr : mod_find(line + 1);
            continue;
        }
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char* key = line, *val = eq + 1;
        if (!cur) {   // [core]
            std::lock_guard<std::mutex> g(mod().mtx); auto& m = mod();
            if (!strcmp(key, "acc1")) sscanf(val, "%f,%f,%f", &m.acc1[0], &m.acc1[1], &m.acc1[2]);
            else if (!strcmp(key, "acc2")) sscanf(val, "%f,%f,%f", &m.acc2[0], &m.acc2[1], &m.acc2[2]);
            else if (!strcmp(key, "menu_key")) sscanf(val, "%u", &m.menu_key);
            else if (!strcmp(key, "tick_sound")) m.opt_tick_sound = atoi(val) != 0;
            else if (!strcmp(key, "hint")) m.opt_hint = atoi(val) != 0;
            else if (!strcmp(key, "log_filter")) m.log_filter = atoi(val);
            else if (!strcmp(key, "log_depth")) m.log_depth = (float)atof(val);
            continue;
        }
        std::lock_guard<std::mutex> g(g_mtx);
        if (!strcmp(key, "enabled")) continue;   // not persisted (see ini_save)
        if (!strcmp(key, "pos")) { sscanf(val, "%f,%f", &cur->hud_x, &cur->hud_y); continue; }
        for (int s = 0; s < cur->nsets; s++) {
            Setting& st = cur->sets[s];
            if (strcmp(st.name, key)) continue;
            if (st.kind == Setting::BOOL) st.b = atoi(val) != 0;
            else if (st.kind == Setting::FLOAT) st.f = (float)atof(val);
            else st.cur = atoi(val);
            break;
        }
    }
    fclose(f);
    MC_LOG("ini: loaded %s", k_ini_path);
}

// autosave worker (15 s cadence when dirty; the game may never run DLL detach)
static void ini_worker() {
    for (;;) {
        Sleep(15000);
        if (g_dirty.load()) ini_save();
    }
}

// ---- init / shutdown --------------------------------------------------------------
void modules_init() {
    ini_load();
    Bus::inst().subscribe_tick(&modules_dispatch_tick);
    autosprint_worker_start();          // NOT in the module ctor (loader lock)
    std::thread(ini_worker).detach();
    MC_LOG("modules: %zu registered", sizeof(g_table)/sizeof(g_table[0]));
}
void modules_shutdown() { ini_save(); }

} // namespace mc
