// ModCore — game SDK: Options — options.txt readers (same two-path location
// logic as the sprint-key reader)
#include "game/sdk/client/Options.hpp"
#include "core/log.h"

#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace mc {
namespace sdk {

static FILE* open_options_txt() {
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
        if (f) return f;
    }
    return nullptr;
}

float options_fov() {
    static float cached = 0.f;
    static bool tried = false;
    if (!tried) {
        tried = true;
        cached = 70.f;
        FILE* f = open_options_txt();
        if (!f) { MC_LOG("options: options.txt not found, FOV defaults to 70"); return cached; }
        char line[512];
        while (fgets(line, sizeof line, f)) {
            if (strncmp(line, "field_of_view_new:", 18) == 0 ||
                strncmp(line, "field_of_view:", 14) == 0) {
                const char* colon = strchr(line, ':');
                float v = (float)atof(colon + 1);
                if (v > 1.f && v < 170.f) cached = v;
                break;
            }
        }
        fclose(f);
        MC_LOG("options: FOV = %.0f", cached);
    }
    if (cached < 30.f) cached = 30.f;
    if (cached > 110.f) cached = 110.f;
    return cached;
}

} // namespace sdk
} // namespace mc
