// TrickyClient OSS — ui layer: widgets + animation helpers
#pragma once
#include <imgui.h>
#include <cstddef>

namespace mc {

// per-frame clock (seconds) for animations — advanced by clickgui_newframe
float ui_time();
void ui_advance_clock(float dt);   // clickgui_newframe

// per-widget animation storage: id -> eased value toward target
float anim_step(ImGuiID id, float target, float dt, float dur);

// horizontal-gradient ROUNDED rect (convex fan with per-vertex colors)
void grad_rounded_rect_h(ImDrawList* dl, const ImVec2& a, const ImVec2& b,
                         ImU32 c1, ImU32 c2, float r);

// UI tick sound (honors ModuleState::opt_tick_sound)
void ui_tick_sound();

// polled keyboard: the overlay feeds ImGui mouse events only; text fields
// (search / hex) read the keyboard straight from GetAsyncKeyState
bool key_edge(int vk);                       // press edge
bool key_repeat(int vk);                     // press + hold autorepeat
void poll_typed(char* buf, size_t cap, bool hex_only);

// text width helper (unscaled font size param)
float ui_tw(ImFont* f, float size, const char* s);

} // namespace mc
