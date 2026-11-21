// ModCore — module: AutoSprint. OS-level worker (Rune KeySpammerLoop recipe):
// taps the sprint key (bind read from options.txt) while W is held. The first
// tap fires the moment W goes down — the idle poll runs at 3 ms.
// wine/GDK: OS key events DO reach the game's keyboard path (unlike mouse
// SendInput), so keybd_event works here.
#include "modules/registry.h"
#include "core/log.h"
#include "core/state.h"
#include "core/input.h"
#include "core/threadbus.h"

#include <windows.h>
#include <shlobj.h>
#include <thread>
#include <cstring>
#include <cstdio>

namespace mc {
Module& mod_autosprint();   // defined below

// ---- sprint key: read the bind from options.txt like Rune does ----
static unsigned sprint_key_vk() {
    static unsigned cached = 0; static bool tried = false;
    if (!tried) {
        tried = true;
        char app[MAX_PATH] = {0}, apploc[MAX_PATH] = {0};
        SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, app);
        SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, apploc);
        const char* rel1 = "\\Minecraft Bedrock\\Users\\Shared\\games\\com.mojang\\minecraftpe\\options.txt";
        const char* rel2 = "\\Packages\\Microsoft.MinecraftUWP_8wekyb3d8bbwe\\LocalState\\games\\com.mojang\\minecraftpe\\options.txt";
        const char* paths[2] = { app, apploc };
        const char* rels[2]  = { rel1, rel2 };
        for (int i = 0; i < 2; ++i) {
            if (!paths[i][0]) continue;
            std::string full = std::string(paths[i]) + rels[i];
            FILE* f = fopen(full.c_str(), "r");
            if (!f) continue;
            char line[512];
            while (fgets(line, sizeof line, f)) {
                if (strncmp(line, "keyboard_type_0_key.sprint:", 27) == 0) {
                    int code = atoi(line + 27);
                    fclose(f);
                    unsigned vk = (unsigned)MapVirtualKeyA(code, MAPVK_VSC_TO_VK);
                    if (code == 82) vk = 'R'; // Rune's special case
                    if (!vk) vk = VK_CONTROL;
                    cached = vk;
                    MC_LOG("sprint key: options.txt code=%d -> VK 0x%02X (%s)", code, vk, full.c_str());
                    return cached;
                }
            }
            fclose(f);
        }
        cached = VK_CONTROL;
        MC_LOG("sprint key: options.txt not found, defaulting to VK_CONTROL");
    }
    return cached;
}

class AutoSprint : public Module {
public:
    AutoSprint() {
        name = "AutoSprint"; category = "MOVEMENT";
        add_float("tap interval", 30.0f, 20.f, 200.f, true, "%.0f");
        // NOTE: the worker is NOT started here — this ctor runs during DLL
        // static init (loader lock); autosprint_worker_start() is called from
        // modules_init() instead
    }
    static void worker() {
        for (;;) {
            bool on, menu;
            float tap_ms;
            Module& m = mod_autosprint();
            { std::lock_guard<std::mutex> g(modules_mtx());
              on = m.enabled; tap_ms = m.sets[0].f; }
            { std::lock_guard<std::mutex> g(mod().mtx); menu = mod().overlay_open; }
            if (!on || menu || !real_key_down('W') || !Bus::inst().in_world()) {
                Sleep(3); continue;
            }
            unsigned vk = sprint_key_vk();
            UINT sc = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
            keybd_event((BYTE)vk, (BYTE)sc, 0, 0);
            Sleep(10);
            keybd_event((BYTE)vk, (BYTE)sc, KEYEVENTF_KEYUP, 0);
            Sleep(tap_ms < 20.f ? 20.f : tap_ms);
        }
    }
};

Module& mod_autosprint() { static AutoSprint m; return m; }

void autosprint_worker_start() {
    std::thread(AutoSprint::worker).detach();
    MC_LOG("sprint worker started");
}

} // namespace mc
