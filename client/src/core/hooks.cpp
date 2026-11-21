// ModCore — core layer: hook plumbing (moved verbatim from the monolith)
#include "core/hooks.h"
#include "core/log.h"
#include <windows.h>

namespace mc {

bool patch_slot_vt(void** slot, void* detour, void** orig) {
    DWORD op = 0;
    BOOL ok = VirtualProtect(slot, 8, PAGE_EXECUTE_READWRITE, &op);
    if (!ok) ok = VirtualProtect(slot, 8, PAGE_READWRITE, &op);
    if (!ok) { MC_WARN("patch_slot_vt: VirtualProtect refused @%p", (void*)slot); return false; }
    *orig = *slot;
    *slot = detour;
    VirtualProtect(slot, 8, op, &op);
    return true;
}

static void* g_patched_vts[12]; static int g_n_patched_vts = 0;
bool vt_already_patched(void* vt) {
    for (int i = 0; i < g_n_patched_vts; ++i) if (g_patched_vts[i] == vt) return true;
    if (g_n_patched_vts < 12) g_patched_vts[g_n_patched_vts++] = vt;
    return false;
}

} // namespace mc
