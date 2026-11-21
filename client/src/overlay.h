// ModCore — overlay entry points (Present hook + ImGui bootstrap)
#pragma once
#include <windows.h>

namespace mc {

void overlay_init();
void overlay_shutdown();
HWND overlay_game_hwnd();     // the game window the clicker posts WM_INPUT to

}
