// ModCore — game hooks: input pipeline.
//  - MouseDevice::feed / Keyboard::feed (capture ring + game-side freeze)
//  - the clicker phase machine (gi_click_phase) shared by every injector
//  - GameInput mouse-state getter BODY hook (0x3b940): AimAssist accumulator
//    rides on the out-copy; the clicker phase flips buttons bit0; CPS edges
//  - GetRawInputData hook + worker (clicker v5 raw path)
// wine/GDK note: OS-injected clicks (SendInput) never reach GameInput (evdev),
// so the autoclicker REPLAYS the game's own events / answers the poll instead.
#pragma once
#include <cstdint>

namespace mc {
namespace game {

// click schedule: 0 idle, 1 force release, 2 force press
int  gi_click_phase();
// apply the click phase to a u32/u64 buttons qword (vtable-slot clickers)
void gi_apply_click(uint64_t out);
void gi_apply_click14(uint64_t out);

bool install_input_hooks();   // feed hooks + the ms14 MinHook (with delayed retry)
void install_raw_clicker();   // GetRawInputData hook + worker thread

}
}
