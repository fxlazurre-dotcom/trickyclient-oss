// ModCore — game SDK: ClientInstance cursor management
#pragma once
namespace mc {
namespace sdk {
// release the cursor through the game (menu open edge + periodic re-release)
void ci_mouse_grab(bool grab);
// patch AppPlatform::grabMouse so every re-grab is dropped while the menu is open
void platform_grab_block_install();
}
}
