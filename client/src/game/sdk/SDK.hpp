// ModCore — game layer: SDK common types + verified offsets (RuneClient style).
// Rules (RESTRUCTURE.md §3/§8): every number carries a status —
//   VERIFIED = sigtool/RVA/vtable-slot/live-log confirmed on 1.26.52.3
//   GUESS    = NOT to be used in logic until verified
#pragma once
#include <cstdint>
#include <windows.h>

namespace mc {

// passed to Module::on_tick; `player` is resolved (and validated) per dispatch
struct LevelCtx {
    void*    level = nullptr;
    uint8_t* player = nullptr;
};

namespace sdk {

// readable-pointer guard used all over the RE code
inline bool readable_ptr(const void* p, size_t len = 32) {
    if ((uintptr_t)p < 0x10000) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    return mbi.State == MEM_COMMIT &&
           (uintptr_t)p + len <= ((uintptr_t)mbi.BaseAddress + mbi.RegionSize);
}

// component hashes (BedrockDynamicSDK Actor.json) — VERIFIED
constexpr uint32_t HASH_STATE_VECTOR  = 0x1B5D5238;
constexpr uint32_t HASH_ROTATION      = 0x75DF36B7;
constexpr uint32_t HASH_ACTOR_TYPE    = 0x4F6BA419;
constexpr uint32_t HASH_AABB          = 0xBAC1B3CF;

struct Vec3 { float x, y, z; };

// ---- verified offsets/vtables for 1.26.52.3 (base 0x140000000) -------------
namespace Offsets {

// GameModeSub derived vtable (rva) — the attack-context object inside the
// player is found by scanning player+0x800..0x1200 for a pointer whose
// object has this vtable. VERIFIED (live capture + dump RE)
constexpr uintptr_t VT_GAMEMODE_SUB   = 0xe827600;
constexpr int  PLAYER_GM_SCAN_LO      = 0x800;
constexpr int  PLAYER_GM_SCAN_HI      = 0x1200;
constexpr int  GM_STORED_PLAYER_OFF   = 0x8;    // [gm+8] = player — VERIFIED
constexpr int  PLAYER_LEVEL_OFF       = 0x1d8;  // [player+0x1d8] = level — VERIFIED

// LocalPlayer vtable slots — VERIFIED on 1.26.52.3:
//   the game's own attack handler (rva 0x8342a) calls gm->vt[15] attack and
//   THEN player->vt[60](player, 1) — the swing. (Rune's vt[110] does NOT
//   exist here — calling it crashed.)
constexpr int  VT_PLAYER_SWING        = 60;
constexpr int  VT_PLAYER_MIN_SLOTS    = 111;    // readability guard for the vtable

// ClientInstance — VERIFIED
constexpr int  CI_PLATFORM_OFF        = 0x1a8;  // ci+0x1a8 = AppPlatform*
constexpr int  VT_CI_GRABCURSOR       = 310;    // cross-checked vs sig at runtime
constexpr int  VT_PLATFORM_GRABMOUSE  = 135;
constexpr int  VT_PLATFORM_RELEASEMOUSE = 136;  // from releaseMouse tail call

// GameInputRedist.dll (the GDK input runtime BOTH backends converge into)
constexpr uintptr_t GI_MOUSE_GETTER_RVA = 0x3b940; // slot-14 body — VERIFIED (clicker v7)
constexpr uintptr_t GI_VT_READING_A     = 0x95cb0; // reading object interface A
constexpr uintptr_t GI_VT_READING_B     = 0x95c10; // interface B (thunks, this-8)
constexpr uintptr_t GI_VT_SINGLETON     = 0x95ed8;
constexpr unsigned  GI_HEAP_VTS[4]      = { 0x97998, 0x97ca0, 0x990f0, 0x95898 };
constexpr size_t    GI_IMAGE_SPAN       = 0x170000; // vtables must live in-image

// player vtables (game exe), used to reject same-class actors in aura scans
constexpr uintptr_t VT_LEVEL           = 0xe7a7aa0;   // VERIFIED (slot check)
constexpr uintptr_t VT_PLAYER          = 0xe8e1bc0;   // VERIFIED

// StateVector/Rotation component pointer offset inside the player — NOT fixed:
// discovered at runtime by the probe (0x1E0..0x260, Rune 26.20 had 0x218).
// GUESS until pick_local_player() validates it for this session.
constexpr int  SVC_SCAN_LO             = 0x1E0;
constexpr int  SVC_SCAN_HI             = 0x260;

} // namespace Offsets
} // namespace sdk
} // namespace mc
