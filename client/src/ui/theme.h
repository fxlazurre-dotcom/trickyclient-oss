// TrickyClient OSS — ui layer: theme (palette, color helpers, two-color accents)
#pragma once
#include <imgui.h>

namespace mc {

// fixed part of the theme (index.html :root)
constexpr ImU32 C_PANEL  = IM_COL32(0x13, 0x10, 0x18, 255);   // --panel
constexpr ImU32 C_ROW    = IM_COL32(0x1B, 0x17, 0x23, 255);   // --row
constexpr ImU32 C_ROW_H  = IM_COL32(0x27, 0x20, 0x33, 255);   // --row-h
constexpr ImU32 C_TXT    = IM_COL32(0xEE, 0xF1, 0xF8, 255);   // --txt
constexpr ImU32 C_MUTED  = IM_COL32(0x8B, 0x95, 0xAA, 255);   // --muted
constexpr ImU32 C_PHOLD  = IM_COL32(0x5F, 0x68, 0x80, 255);   // placeholder
constexpr ImU32 C_BOXOFF = IM_COL32(0x33, 0x2B, 0x3E, 255);   // .box off
constexpr ImU32 C_HUDBG  = IM_COL32(0x17, 0x13, 0x1D, 255);   // hud panel bg
constexpr ImU32 C_TXT_H  = IM_COL32(0xF2, 0xEC, 0xF5, 255);   // hud text

float  clamp01(float v);
float  nsmooth(float x);                    // smoothstep
ImU32  with_a(ImU32 c, float a01);          // SET alpha
ImU32  mul_a(ImU32 c, float f);             // MULTIPLY alpha
ImU32  lerp_col(ImU32 a, ImU32 b, float t);
ImU32  rgb_col(const float* c, float a = 1.f);

// accent cache: refreshed once per frame from ModuleState (acc1/acc2)
void  theme_refresh();
ImU32 th_acc1(float a = 1.f);
ImU32 th_acc2(float a = 1.f);
ImU32 th_accb(float a = 1.f);               // average of the two

// color picker math
void hsv_to_rgb(float h, float s, float v, float* rgb);
void rgb_to_hsv(const float* rgb, float* hsv);

} // namespace mc
