// ModCore — core layer: DLL-internal key state
#include "core/input.h"
#include <windows.h>

namespace mc {

bool real_key_down(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

} // namespace mc
