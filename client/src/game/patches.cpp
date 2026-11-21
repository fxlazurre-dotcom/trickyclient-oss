// ModCore — game layer: AC byte patches (Rune recipe "anticheat neutralizer").
// DORMANT by default: every toggle is false and nothing calls set_ac_patches()
// until a GUI for them returns. Kept wired exactly as in the monolith.
#include "game/sigs/sigs.h"
#include "core/log.h"
#include "core/state.h"

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

namespace mc {

struct Patch { const char* name; uintptr_t addr; uint8_t orig[8]; int len; bool on; };
static Patch g_patches[6];
static int   g_npatches = 0;

// registry init (called once from game::install_all)
void patches_prepare() {
    const char* pn[6] = {"AC_PlayerAuthInput_field_validator",
                         "AC_TickServerPlayerMovementCorrection",
                         "AC_CheckPlayerPassengerMovementStats",
                         "AC_PlayerMovementStatsEventSystem",
                         "MC_InterpolationSystem_tick",
                         "MC_PredictionSync_tick"};
    for (int i = 0; i < 6; ++i) {
        g_patches[i].name = pn[i];
        g_patches[i].addr = sig_get(pn[i]);
        if (g_patches[i].addr) g_npatches++;
    }
    MC_LOG("AC patch slots ready: %d/6", g_npatches);
}

static void apply_patch(Patch& p, const char* bytes_str, bool on) {
    if (!p.addr) return;
    if (on == p.on) return;
    // parse bytes
    uint8_t nb[8]; int nb_len = 0;
    for (const char* s = bytes_str; *s && nb_len < 8;) {
        while (*s == ' ') ++s;
        if (!*s) break;
        int v = 0;
        for (int k = 0; k < 2 && *s; ++k, ++s) {
            int c = *s;
            int d = (c >= '0' && c <= '9') ? c - '0' : (c | 32) - 'a' + 10;
            v = v * 16 + d;
        }
        nb[nb_len++] = (uint8_t)v;
    }
    if (!p.len) { memcpy(p.orig, (void*)p.addr, nb_len); p.len = nb_len; }

    // freeze all other threads: patching code another thread is executing mid-
    // instruction crashes the process (that was the Anticheat-tab crash)
    HANDLE threads[256]; DWORD nthreads = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    DWORD self = GetCurrentThreadId();
    DWORD pid = GetCurrentProcessId();
    if (snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te{ sizeof(te) };
        if (Thread32First(snap, &te)) {
            do {
                if (te.th32OwnerProcessID == pid && te.th32ThreadID != self && nthreads < 256)
                    if (HANDLE t = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID)) {
                        if (SuspendThread(t) != (DWORD)-1) threads[nthreads++] = t;
                        else CloseHandle(t);
                    }
            } while (Thread32Next(snap, &te));
        }
        CloseHandle(snap);
    }
    MC_LOG("PATCH %s: %d threads frozen, writing %d bytes @0x%llx",
           p.name, (int)nthreads, on ? nb_len : p.len, (unsigned long long)p.addr);

    DWORD op;
    VirtualProtect((void*)p.addr, 16, PAGE_EXECUTE_READWRITE, &op);
    memcpy((void*)p.addr, on ? nb : p.orig, on ? nb_len : p.len);
    VirtualProtect((void*)p.addr, 16, op, &op);
    FlushInstructionCache(GetCurrentProcess(), (void*)p.addr, 16);

    for (DWORD i = 0; i < nthreads; ++i) { ResumeThread(threads[i]); CloseHandle(threads[i]); }
    p.on = on;
    MC_LOG("PATCH %s -> %s (threads resumed)", p.name, on ? "ON" : "OFF");
}

void set_ac_patches() {
    // called ONLY from the render thread (GUI). No mutex here — callers must
    // not hold ModuleState::mtx (patch state is render-thread-private anyway).
    auto& m = mod();
    if (g_npatches >= 1) apply_patch(g_patches[0], "B0 01 C3", m.ac_field_validator);
    if (g_npatches >= 2) apply_patch(g_patches[1], "C3",       m.ac_correction_tick);
    if (g_npatches >= 3) apply_patch(g_patches[2], "C3",       m.ac_passenger_stats);
    if (g_npatches >= 4) apply_patch(g_patches[3], "C3",       m.ac_stats_events);
    if (g_npatches >= 5) apply_patch(g_patches[4], "C3",       m.ac_interp_system);
    if (g_npatches >= 6) apply_patch(g_patches[5], "C3",       m.ac_predsync_system);
}

} // namespace mc
