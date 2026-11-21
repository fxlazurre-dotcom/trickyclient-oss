// ModCore — core layer: the thread bus implementation
#include "core/threadbus.h"
#include <windows.h>

namespace mc {

// ---- click ring (was hud_note_click/hud_cps in the monolith) ---------------
// clicks counted by EDGES: a synthetic press = the phase machine ENTERING 2
// (it HOLDS 2 between clicks — counting polls inside it saturated the ring),
// a real press = out-buttons bit0 rising BEFORE our synthetic OR.
static ULONGLONG  g_click_ms[64] = {};
static volatile LONG g_click_wr = 0;

void Bus::note_click() {
    long i = InterlockedIncrement(&g_click_wr) - 1;
    g_click_ms[i & 63] = GetTickCount64();
}
int Bus::cps() {
    ULONGLONG now = GetTickCount64(); int c = 0;
    for (int i = 0; i < 64; i++)
        if (g_click_ms[i] && now > g_click_ms[i] && now - g_click_ms[i] < 1000) c++;
    return c;
}

// ---- telemetry --------------------------------------------------------------
void Bus::publish_telemetry(const Telemetry& t) {
    std::lock_guard<std::mutex> g(tx_);
    t_ = t;
}
// attack/use counters (atomics; read by debug tooling)
static long long g_evt_lkm = 0, g_evt_pkm = 0;
void Bus::note_lkm() { __atomic_add_fetch(&g_evt_lkm, 1, __ATOMIC_RELAXED); }
void Bus::note_pkm() { __atomic_add_fetch(&g_evt_pkm, 1, __ATOMIC_RELAXED); }

void Bus::bump_ticks() {
    std::lock_guard<std::mutex> g(tx_);
    t_.ticks++;
    tick_seen_ms_ = GetTickCount64();
}

Telemetry Bus::telemetry() {
    std::lock_guard<std::mutex> g(tx_);
    return t_;
}

bool Bus::in_world() {
    unsigned long long seen;
    { std::lock_guard<std::mutex> g(tx_); seen = tick_seen_ms_; }
    return GetTickCount64() - seen < 2000;   // a tick within the last 2 s
}

// ---- aim / clicker channels ---------------------------------------------------
void Bus::publish_aim(const AimPull& a)      { std::lock_guard<std::mutex> g(ax_); a_ = a; }
AimPull Bus::aim()                           { std::lock_guard<std::mutex> g(ax_); return a_; }
void Bus::publish_clicker(const ClickerCfg& c){ std::lock_guard<std::mutex> g(cx_); c_ = c; }
ClickerCfg Bus::clicker()                    { std::lock_guard<std::mutex> g(cx_); return c_; }

// ---- tick subscribers ---------------------------------------------------------
void Bus::subscribe_tick(void (*fn)(void*)) {
    std::lock_guard<std::mutex> g(sx_);
    subs_.push_back(fn);
}
void Bus::dispatch_tick(void* level) {
    std::vector<void(*)(void*)> local;
    { std::lock_guard<std::mutex> g(sx_); local = subs_; }
    for (auto fn : local) fn(level);
}

} // namespace mc
