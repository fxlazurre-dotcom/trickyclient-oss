// ModCore — module interface.
// One file = one module. The GUI renders ANY module generically from this
// interface (tables SETS_*/MODS_*/BOOL_BINDS/find_bool are gone); a new module
// is one .cpp + one line in modules/registry.cpp.
#pragma once
#include <cstdint>

namespace mc {

struct LevelCtx;                  // game/sdk/SDK.hpp (level ptr + resolved player)
struct ModuleState;

// ---------------------------------------------------------------- settings --
struct Setting {
    enum Kind { BOOL, FLOAT, ENUM } kind = BOOL;
    const char* name = "";
    // value storage (one of these is live per kind)
    bool   b = false;
    float  f = 0;
    int    cur = 0;               // enum index
    // float range / formatting
    float  lo = 0, hi = 1;
    bool   inte = false;
    const char* fmt = "%.1f";
    // enum options
    const char* const* opts = nullptr;
    int    nopts = 0;
    // optional DIRECT binding into ModuleState (client options edited through
    // MISC tool rows — the GUI reads/writes the state field under its mutex)
    bool*  state_b = nullptr;
    float* state_f = nullptr;
    int*   state_i = nullptr;
};

// ------------------------------------------------------------- HUD context --
// Filled by ui/hud_render.cpp: HUD modules draw through these helpers and
// never include imgui themselves (keeps the modules layer renderer-agnostic).
struct HudCtx {
    void*    dl;                 // ImDrawList* (opaque here)
    float    W, H;               // screen size (px)
    float    t;                  // clock (s)
    bool     menu_open;          // drag enabled
    unsigned acc1, acc2;         // theme colors (ABGR, like ImU32)
    unsigned bg_col;             // HUD panel bg (#17131D)
    unsigned txt_col;            // HUD text (#F2ECF5)
    float    fps;                // ImGui frame counter (for the watermark)
    // helpers
    float    (*tw)(HudCtx&, const char* s, float fs);
    float    (*text)(HudCtx&, const char* s, float x, float y, float fs, unsigned col, bool shadow);
    void     (*rect)(HudCtx&, float x, float y, float w, float h, float r, unsigned col);
    void     (*rectg)(HudCtx&, float x, float y, float w, float h, unsigned c1, unsigned c2);
    void     (*line)(HudCtx&, float x0, float y0, float x1, float y1, unsigned col, float th);
    void     (*circle)(HudCtx&, float x, float y, float r, unsigned col);
    void     (*stepped)(HudCtx&, float x, float y, const float* rws, const float* ry,
                        int n, float w, float h, float r, unsigned fill);
    void     (*drag)(HudCtx&, const char* id, float* px, float* py, float w, float h);
    unsigned (*lerp)(HudCtx&, unsigned a, unsigned b, float t);
    unsigned (*with_a)(HudCtx&, unsigned c, float a01);   // SET alpha
    unsigned (*mul_a)(HudCtx&, unsigned c, float f);      // MULTIPLY alpha
};

// ------------------------------------------------------------------ module --
class Module {
public:
    const char* name = "";
    const char* category = "";
    bool  enabled = false;
    bool  has_toggle = true;       // false: info/action/debug row (not toggleable)
    bool  listed = true;           // shows up in the HUD ArrayList when enabled
    bool  debug_block = false;     // DebugInfo block (sigs summary + log tail)
    bool* toggle_state = nullptr;  // tool rows: toggle a ModuleState bool instead
    void (*action)() = nullptr;    // row click action (CopyLog)
    float hud_x = -1.f, hud_y = -1.f;   // HUD element position (screen px; <0 = anchor)
    Setting sets[16];           // hard cap (guarded by add_* clamps)
    int    nsets = 0;

    virtual ~Module() = default;
    virtual void on_tick(LevelCtx& ctx) {}        // game thread (Level::tick)
    virtual void on_hud(HudCtx& ctx) {}           // render thread
    virtual void on_toggle(bool on) {}            // enable/disable edge

    Setting& add_bool(const char* n, bool v);
    Setting& add_float(const char* n, float v, float lo, float hi, bool inte, const char* fmt);
    Setting& add_enum(const char* n, const char* const* opts, int nopts, int cur);
    Setting& setting(const char* n);
};

// smoothstep used by HUD animations
inline float mod_nsmooth(float x) { x = x < 0.f ? 0.f : x > 1.f ? 1.f : x; return x * x * (3.f - 2.f * x); }

} // namespace mc
