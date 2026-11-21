// TrickyClient OSS — ui layer: font stack (moved verbatim from overlay.cpp)
#include "ui/fonts.h"
#include "ui/fonts/res_fonts.h"
#include "ui/fonts/res_mont.h"
#include "core/log.h"

#include <windows.h>

namespace mc {

ClickGuiFonts cg_fonts;

void ui_fonts_build() {
    ImGuiIO& io = ImGui::GetIO();
    // font stack: EMBEDDED Josefin Sans 400/700 (the mockup's font, OFL) with
    // the system segoe/tahoma/arial chain as the fallback; cyrillic ranges
    // for mixed text
    using tricky::res::josefin_regular;
    using tricky::res::josefin_bold;
    ImFont* jmain = io.Fonts->AddFontFromMemoryTTF((void*)josefin_regular, sizeof(josefin_regular), 15.f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
    ImFont* jbold = io.Fonts->AddFontFromMemoryTTF((void*)josefin_bold, sizeof(josefin_bold), 15.f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
    ImFont* jtitle = io.Fonts->AddFontFromMemoryTTF((void*)josefin_bold, sizeof(josefin_bold), 28.f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
    ImFont* jsmall = io.Fonts->AddFontFromMemoryTTF((void*)josefin_regular, sizeof(josefin_regular), 12.f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
    (void)jmain; (void)jbold; (void)jtitle; (void)jsmall;
    MC_LOG("cg-init: josefin main=%p bold=%p title=%p small=%p",
           (void*)jmain, (void*)jbold, (void*)jtitle, (void*)jsmall);
    const ImWchar* ranges = io.Fonts->GetGlyphRangesCyrillic();
    MC_LOG("cg-init: ranges ok");
    const char* reg[] = { "C:\\windows\\Fonts\\segoeui.ttf",
                          "C:\\windows\\Fonts\\tahoma.ttf",
                          "C:\\windows\\Fonts\\arial.ttf", nullptr };
    const char* bld[] = { "C:\\windows\\Fonts\\segoeuib.ttf",
                          "C:\\windows\\Fonts\\tahomabd.ttf",
                          "C:\\windows\\Fonts\\arialbd.ttf", nullptr };
    auto load = [&](const char** chain, float px) -> ImFont* {
        for (int i = 0; chain[i]; ++i) {
            if (GetFileAttributesA(chain[i]) == INVALID_FILE_ATTRIBUTES)
                continue; // skip missing files silently
            MC_LOG("cg-init: AddFont %s @%.0f", chain[i], px);
            if (ImFont* f = io.Fonts->AddFontFromFileTTF(chain[i], px, nullptr, ranges))
                return f;
        }
        return nullptr;
    };
    cg_fonts.main  = jmain ? jmain : load(reg, 13.f);
    cg_fonts.bold  = jbold ? jbold : load(bld, 13.f);
    cg_fonts.title = jtitle ? jtitle : load(bld, 28.f);
    cg_fonts.small = jsmall ? jsmall : cg_fonts.main;
    // nursultan style: Montserrat SemiBold at the exact atlas sizes the new
    // GUI/HUD uses (guaranteed crisp glyphs). NOTE: the .otf from the mockup
    // folder is a CFF build that stb_truetype cannot rasterize — the embedded
    // res_mont.h is the TrueType variant (do not replace it).
    {
        using tricky::res::montserrat_semibold;
        auto mk = [&](float px) -> ImFont* {
            return io.Fonts->AddFontFromMemoryTTF((void*)montserrat_semibold,
                                                  sizeof(montserrat_semibold), px, nullptr,
                                                  io.Fonts->GetGlyphRangesCyrillic());
        };
        cg_fonts.mont12 = mk(12.f);
        cg_fonts.mont14 = mk(14.f);
        cg_fonts.mont16 = mk(16.f);
        cg_fonts.mont19 = mk(19.f);
        MC_LOG("cg-init: montserrat 12=%p 14=%p 16=%p 19=%p", (void*)cg_fonts.mont12,
               (void*)cg_fonts.mont14, (void*)cg_fonts.mont16, (void*)cg_fonts.mont19);
        ImFont* fb = cg_fonts.mont16 ? cg_fonts.mont16 : cg_fonts.main;
        if (!cg_fonts.mont12) cg_fonts.mont12 = fb;
        if (!cg_fonts.mont14) cg_fonts.mont14 = fb;
        if (!cg_fonts.mont16) cg_fonts.mont16 = fb;
        if (!cg_fonts.mont19) cg_fonts.mont19 = fb;
    }
    MC_LOG("cg-init: fonts main=%p bold=%p title=%p", (void*)cg_fonts.main, (void*)cg_fonts.bold, (void*)cg_fonts.title);
    if (!cg_fonts.main)  { cg_fonts.main  = io.Fonts->AddFontDefault(); MC_WARN("overlay: main font fallback"); }
    if (!cg_fonts.bold)  cg_fonts.bold  = cg_fonts.main;
    if (!cg_fonts.title) cg_fonts.title = cg_fonts.bold;
}

void ui_fonts_guard() {
    ImGuiIO& io = ImGui::GetIO();
    // one-shot sanity check after the atlas build: a font that stb_truetype
    // could not rasterize (e.g. a CFF/OTTO build) loads as an EMPTY ImFont —
    // fall back to the working stack instead of rendering nothing
    static bool s_font_checked = false;
    if (s_font_checked || !io.Fonts->TexReady) return;
    s_font_checked = true;
    auto has_glyphs = [](ImFont* f) {
        return f && f->CalcTextSizeA(f->FontSize, FLT_MAX, -1.f, "TrickyClient").x > 1.f;
    };
    if (!has_glyphs(cg_fonts.mont16)) {
        MC_WARN("clickgui: montserrat atlas is empty (unsupported outlines?), falling back");
        ImFont* fb = cg_fonts.main;
        cg_fonts.mont12 = fb; cg_fonts.mont14 = fb; cg_fonts.mont16 = fb; cg_fonts.mont19 = fb;
    }
}

} // namespace mc
