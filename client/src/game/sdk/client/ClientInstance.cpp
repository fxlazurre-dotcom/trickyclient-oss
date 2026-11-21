// ModCore — game SDK: ClientInstance cursor management.
// While the menu owns the cursor the overlay releases it through the GAME's
// own path (platform releaseMouse) and blocks every re-grab.
#include "game/sdk/client/ClientInstance.hpp"
#include "game/sdk/SDK.hpp"
#include "game/sdk/actor/LocalPlayer.hpp"
#include "game/sigs/sigs.h"
#include "core/log.h"
#include "core/hooks.h"
#include "core/state.h"
#include <windows.h>

namespace mc {
namespace sdk {
// ClientInstance::grabMouse/releaseMouse — vtable 310/311 (Rune ClientInstance.hpp)
static void* g_client_instance_grab_check = nullptr;
void ci_mouse_grab(bool grab) {
    void* ci = sdk::client_instance();
    if (!ci) return;
    void** vt = *(void***)ci;
    // sanity for this build: [310] must be our hooked grabCursor address
    static bool checked = false, ok = false;
    if (!checked) {
        checked = true;
        uintptr_t grabAddr = sig_get("ClientInstance_grabCursor");
        ok = grabAddr && (uintptr_t)vt[Offsets::VT_CI_GRABCURSOR] == grabAddr;
        MC_LOG("ci vtable: [310]=%p grabSig=%p -> %s", (void*)vt[Offsets::VT_CI_GRABCURSOR], (void*)grabAddr, ok ? "OK" : "MISMATCH");
        g_client_instance_grab_check = (void*)grabAddr;
    }
    if (!ok) return;
    if (grab) {
        ((void (*)(void*))vt[Offsets::VT_CI_GRABCURSOR])(ci);   // grabCursor (guard passes while playing)
        return;
    }
    // release: ClientInstance::releaseMouse is GUARDED (ci->vt[162] via CFG)
    // and no-ops during gameplay — call AppPlatform::releaseMouse directly:
    // platform = ci+0x1a8, vtable slot 0x440/8 = 136 (from its tail call)
    void* platform = *(void**)((char*)ci + Offsets::CI_PLATFORM_OFF);
    if (!sdk::readable_ptr(platform, 8)) { MC_WARN("ci+0x1a8 not readable"); return; }
    void** pvt = *(void***)platform;
    static bool pcheck = false;
    if (!pcheck) {
        pcheck = true;
        uintptr_t rel = sig_get("AppPlatformGDK_releaseMouse");
        MC_LOG("platform vtable: [136]=%p relSig=%p -> %s", (void*)pvt[Offsets::VT_PLATFORM_RELEASEMOUSE], (void*)rel,
               rel && (uintptr_t)pvt[Offsets::VT_PLATFORM_RELEASEMOUSE] == rel ? "OK" : "MISMATCH");
    }
    ((void (*)(void*))pvt[Offsets::VT_PLATFORM_RELEASEMOUSE])(platform);
}

// platform-level grab block: the game re-grabs the cursor behind
// ClientInstance::grabCursor's back. Patch the platform vtable slot 135
// (AppPlatform::grabMouse) so every grab attempt is dropped while the
// menu owns the cursor.
typedef void (*t_plat_fn)(void*);
static t_plat_fn o_plat_grabmouse = nullptr;
static void h_plat_grabmouse(void* self) {
    if (input_frozen()) return;   // no grabs while the menu is open
    o_plat_grabmouse(self);
}
void platform_grab_block_install() {
    static bool done = false;
    if (done) return;
    void* ci = sdk::client_instance();
    if (!ci) return;
    void* platform = *(void**)((char*)ci + Offsets::CI_PLATFORM_OFF);
    if (!sdk::readable_ptr(platform, 8)) return;
    void** pvt = *(void***)platform;
    patch_slot_vt(&pvt[Offsets::VT_PLATFORM_GRABMOUSE], (void*)h_plat_grabmouse, (void**)&o_plat_grabmouse);
    MC_LOG("platform grabMouse patched: slot135 %p -> %p", (void*)o_plat_grabmouse, (void*)h_plat_grabmouse);
    done = true;
}
} // namespace sdk
} // namespace mc
