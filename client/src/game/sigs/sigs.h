// ModCore — game layer: signature resolution (was in main.cpp)
// sigs.h is GENERATED from data/signatures.toml — see ~/mc-modloader/WORKFLOW.md
#pragma once
#include <cstdint>

namespace mc {

// resolve every SIG_ENTRY against the image; logs "SIG <name> OK/FAIL rva=..."
void resolve_all_signatures();

// resolved address by signature name (0 = not resolved)
uintptr_t sig_get(const char* name);

// accessors for the GUI's DebugInfo block
int         sig_count_();
const char* sig_name_(int i);
uintptr_t   sig_addr_(int i);
bool        sig_ok_(int i);

} // namespace mc
