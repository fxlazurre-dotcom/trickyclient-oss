// ModCore — core layer: hook plumbing shared by overlay + game instrumentation
#pragma once

namespace mc {

// vtable slot patch: VirtualProtect + pointer swap.
// PAGE_EXECUTE_READWRITE keeps the page executable during the write (a RW-only
// window made in-flight 160Hz calls fault -> dead LMB). Some wine mappings
// refuse exec-write: fall back to RW (short window) and NEVER write if both
// protects fail (read-only page -> crash).
bool patch_slot_vt(void** slot, void* detour, void** orig);

// one registry for every vtable we touch (no double-patching)
bool vt_already_patched(void* vt);

}
