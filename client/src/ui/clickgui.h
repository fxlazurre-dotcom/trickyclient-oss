// TrickyClient OSS — nursultan-style ClickGUI + HUD entry points
// (public API unchanged from the monolith; the overlay calls these)
#pragma once

namespace mc {

// per-frame clock tick (call once per ImGui frame, before draw_*)
void clickgui_newframe();

// the whole menu (call inside an ImGui frame)
void draw_clickgui();
// HUD elements (call inside an ImGui frame, also with the menu closed)
void draw_hud();
// true while the open/close animation is still playing (menu may already be
// flagged closed — overlay keeps drawing until this returns false)
bool clickgui_animating();

} // namespace mc
