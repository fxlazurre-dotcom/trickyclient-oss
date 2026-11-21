// TrickyClient OSS — ui layer: fonts (atlas built once at overlay init)
#pragma once
#include <imgui.h>

namespace mc {

// fonts resolved by the overlay at init
struct ClickGuiFonts {
    ImFont* main = nullptr;   // 13px regular (fallback / legacy)
    ImFont* bold = nullptr;   // 13px bold
    ImFont* title = nullptr;  // 28px bold
    ImFont* small = nullptr;  // 12px regular
    ImFont* sbold = nullptr;  // 12px bold
    // Montserrat SemiBold — the nursultan style font, one atlas entry per size
    ImFont* mont12 = nullptr;  // GUI setting labels / badges / chips
    ImFont* mont14 = nullptr;  // GUI module rows, search bar
    ImFont* mont16 = nullptr;  // HUD text (8 logical px * K=2)
    ImFont* mont19 = nullptr;  // GUI column headers
};
extern ClickGuiFonts cg_fonts;

// build the font stack (embedded Josefin 400/700 + Montserrat SemiBold TTF,
// system chain fallback, cyrillic ranges). Call BEFORE creating the device
// objects — it only fills io.Fonts.
void ui_fonts_build();

// one-shot atlas sanity check (call once per frame until TexReady): a font
// stb_truetype could not rasterize loads as an EMPTY ImFont — fall back.
void ui_fonts_guard();

} // namespace mc
