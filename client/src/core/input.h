// ModCore — core layer: DLL-internal key state.
// The user32 key-state MinHooks are compile-time DISABLED (see overlay.cpp:
// they were the prime suspect of the WM_SIZE crash), so the direct
// GetAsyncKeyState path IS the real one.
#pragma once

namespace mc {
bool real_key_down(int vk);
}
