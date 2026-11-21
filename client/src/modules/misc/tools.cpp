// ModCore — modules: MISC tool rows. These are client/GUI options and debug
// utilities rendered through the SAME Module interface: values bind DIRECTLY
// into ModuleState fields (state_b/state_f/state_i), actions are plain rows.
#include "modules/registry.h"
#include "core/log.h"
#include "core/state.h"

#include <windows.h>
#include <cstring>

namespace mc {

// ---- CopyLog: last 200 ring lines -> clipboard -------------------------------
static void action_copy_log() {
    auto lines = Logger::inst().snapshot();
    std::string all;
    size_t skip = lines.size() > 200 ? lines.size() - 200 : 0;
    for (size_t i = skip; i < lines.size(); ++i) { all += lines[i].text; all += "\r\n"; }
    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, all.size() + 1);
        if (hg) { memcpy(GlobalLock(hg), all.c_str(), all.size() + 1);
            GlobalUnlock(hg); SetClipboardData(CF_TEXT, hg); }
        CloseClipboard();
    }
    MC_LOG("log copied to clipboard (%d lines)", (int)lines.size());
}

class TickSounds : public Module {
public:
    TickSounds() { name = "TickSounds"; category = "MISC"; listed = false;
                   toggle_state = &mod().opt_tick_sound; }
};

class HintLine : public Module {
public:
    HintLine() { name = "HintLine"; category = "MISC"; listed = false;
                 toggle_state = &mod().opt_hint; }
};

class LogFilter : public Module {
public:
    LogFilter() { name = "LogFilter"; category = "MISC"; listed = false; has_toggle = false;
                  add_enum("filter", nullptr, 0, 0); }
    // the enum options are set in attach() below (needs a static table)
    void attach() {
        static const char* const opts[] = {"All", "Errors+Warn", "Events"};
        sets[0].opts = opts; sets[0].nopts = 3;
        sets[0].state_i = &mod().log_filter;
        sets[0].cur = mod().log_filter;
    }
};

class LogDepth : public Module {
public:
    LogDepth() { name = "LogDepth"; category = "MISC"; listed = false; has_toggle = false;
                 add_float("depth", 300.f, 30.f, 1000.f, true, "%.0f");
                 sets[0].state_f = &mod().log_depth; sets[0].f = mod().log_depth; }
};

class CopyLog : public Module {
public:
    CopyLog() { name = "CopyLog"; category = "MISC"; listed = false; has_toggle = false;
                action = action_copy_log; }
};

class DebugInfo : public Module {
public:
    DebugInfo() { name = "DebugInfo"; category = "MISC"; listed = false; has_toggle = false;
                  debug_block = true; }
};

Module& mod_ticksounds() { static TickSounds m; return m; }
Module& mod_hintline()   { static HintLine m; return m; }
Module& mod_logfilter()  { static LogFilter m; m.attach(); return m; }
Module& mod_logdepth()   { static LogDepth m; return m; }
Module& mod_copylog()    { static CopyLog m; return m; }
Module& mod_debuginfo()  { static DebugInfo m; return m; }

} // namespace mc
