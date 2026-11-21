// ModCore — core layer: state storage + input-freeze flag
#include "core/state.h"

namespace mc {

ModuleState& mod() { static ModuleState m; return m; }

static std::atomic<bool> g_input_frozen{false};
bool input_frozen() { return g_input_frozen.load(std::memory_order_relaxed); }
void set_input_frozen(bool on) { g_input_frozen.store(on, std::memory_order_relaxed); }

} // namespace mc
