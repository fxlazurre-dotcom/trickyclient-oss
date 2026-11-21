// ModCore — entry point: initialization ORDER + unload.
//   DllMain: logger open -> bootstrap thread (2 s sleep for the game's
//   startup decrypt/mapping) -> bootstrap():
//     1. image base/size  (game/sigs/scan)
//     2. resolve_all_signatures  (game/sigs — BEFORE any hook)
//     3. modules_init      (registry + bus subscription + workers — before
//                           hooks so the tick dispatcher exists when
//                           Level::tick first fires)
//     4. game::install_all (MinHook tables + sdk wiring + patch slots)
//     5. gameinput worker  (probe + instrumentation, waits for the module)
//     6. overlay_init      (DX12 Present hook + cursor management)
#include "core/log.h"
#include "core/state.h"
#include "game/sigs/scan.h"
#include "game/sigs/sigs.h"
#include "game/hooks/hooks.h"
#include "game/sdk/client/gameinput.h"
#include "game/sdk/client/reconsole.h"
#include "modules/registry.h"
#include "overlay.h"

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <thread>

namespace mc {

// ---- diagnostics: everything alive inside the wine prefix ----
static void log_wine_processes() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) { MC_WARN("proc snapshot failed"); return; }
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    std::string list; int cnt = 0;
    if (Process32FirstW(snap, &pe)) {
        do { ++cnt; for (const wchar_t* q = pe.szExeFile; *q; ++q) list += (char)*q; list += ' '; } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    MC_LOG("wine processes (%d): %s", cnt, list.c_str());
}
static void log_input_modules() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) { MC_WARN("module snapshot failed"); return; }
    MODULEENTRY32W me{}; me.dwSize = sizeof(me);
    std::string hits; int total = 0;
    auto contains = [](const std::string& h, const char* needle) {
        std::string lo = h; for (auto& ch : lo) ch = (char)tolower((unsigned char)ch);
        return lo.find(needle) != std::string::npos; };
    if (Module32FirstW(snap, &me)) {
        do { ++total;
            std::string name; for (const wchar_t* q = me.szModule; *q; ++q) name += (char)*q;
            if (contains(name, "input") || contains(name, "hid") || contains(name, "xgame"))
                { hits += name; hits += ' '; }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    MC_LOG("modules total=%d, input-related: %s", total, hits.c_str());
}

static void bootstrap() {
    // image
    MODULEINFO mi{};
    HMODULE h = GetModuleHandleW(nullptr);
    GetModuleInformation(GetCurrentProcess(), h, &mi, sizeof(mi));
    image_set((uintptr_t)mi.lpBaseOfDll, mi.SizeOfImage);
    MC_LOG("ModCore bootstrap: base=0x%llx size=0x%llx (%s)",
           (unsigned long long)image_base(), (unsigned long long)image_size(),
           image_base() == 0x140000000 ? "preferred base, RVAs match" : "REBASED!");
    log_wine_processes();
    log_input_modules();

    // GameInput worker: wait for GameInputRedist.dll, probe + instrument, then
    // start the RE console (re.cmd file polling)
    std::thread([]{
        int waited = 0;
        while (GetModuleHandleW(L"GameInputRedist.dll") == nullptr) {
            Sleep(1000); ++waited;
            if (waited >= 180) { MC_WARN("gameinput: module never appeared, worker bailing"); return; }
        }
        MC_LOG("gameinput: worker start (waited %ds for module)", waited);
        game::gameinput_probe();
        MC_LOG("gameinput: probe finished, instrumenting");
        game::gameinput_instrument();
        MC_LOG("gameinput: worker done");
        std::thread(game::re_console_thread).detach();
        MC_LOG("re console: watching runtime/re.cmd");
    }).detach();

    resolve_all_signatures();
    modules_init();
    game::install_all();
    overlay_init();
    MC_LOG("ModCore ready");
}

} // namespace mc

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        HANDLE guard = CreateMutexW(nullptr, TRUE, L"Local\\ModCore_1265203");
        if (guard && GetLastError() == ERROR_ALREADY_EXISTS) {
        MC_WARN("double injection refused (mutex exists)");
            return TRUE;   // already loaded: do nothing, don't double-patch
        }
        mc::Logger::inst().open("Z:/home/florixx/mc-modloader/runtime/mcmod.log");
        MC_LOG("=== ModCore.dll loaded into pid=%lu ===", GetCurrentProcessId());
        CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            Sleep(2000); // let the game finish startup decrypt/mapping
            mc::bootstrap();
            return 0;
        }, nullptr, 0, nullptr);
    } else if (reason == DLL_PROCESS_DETACH) {
        MC_LOG("ModCore unload (reserved=%p -> %s)", reserved,
               reserved ? "process exit" : "FreeLibrary");
        mc::modules_shutdown();
        mc::overlay_shutdown();
    }
    return TRUE;
}
