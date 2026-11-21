// ModCore — game hooks: GameMode (attack paths, tick) + MovePlayer packet drop.
// Attack-context discovery (GameModeSub inside the player) and the swing live
// here too: they are glued to the Sub_attack trampoline + captured ABI.
#include "game/hooks/gamemode.h"
#include "game/sigs/sigs.h"
#include "game/sdk/SDK.hpp"
#include "game/sdk/actor/LocalPlayer.hpp"
#include "core/log.h"
#include "core/state.h"
#include "core/threadbus.h"
#include "game/sigs/scan.h"
#include "game/sdk/world/BlockSource.hpp"

#include <MinHook.h>
#include <windows.h>
#include <mutex>

namespace mc {
namespace game {
namespace Offsets = sdk::Offsets;

// ---- GameMode::attack(GameMode*, Actor&, Vec3 const& hitPos) -> bool -------
typedef bool (*t_gm_attack)(void*, void*, const float*);
static t_gm_attack o_gm_attack = nullptr;
// cache of the last real attack — reused by the GUI "Call LKM" button
static void*    last_atk_gm    = nullptr;
static void*    last_atk_actor = nullptr;
static float    last_atk_hit[3] = {0};

static bool h_gm_attack(void* gm, void* actor, const float* hit) {
    Bus::inst().note_lkm();
    last_atk_gm = gm; last_atk_actor = actor;
    if (hit) { last_atk_hit[0]=hit[0]; last_atk_hit[1]=hit[1]; last_atk_hit[2]=hit[2]; }
    MC_EVT("LKM attack gm=%p actor=%p hit=(%.2f, %.2f, %.2f)", gm, actor,
           hit ? hit[0] : 0.f, hit ? hit[1] : 0.f, hit ? hit[2] : 0.f);
    return o_gm_attack(gm, actor, hit);
}

void call_lkm() {
    if (!o_gm_attack) { MC_WARN("call_lkm: attack not hooked"); return; }
    if (!last_atk_gm || !last_atk_actor) {
        MC_WARN("call_lkm: no cached target — сделай один реальный ЛКМ по мобу сначала");
        return;
    }
    MC_LOG("call_lkm -> GameMode::attack(gm=%p, actor=%p)", last_atk_gm, last_atk_actor);
    bool r = o_gm_attack(last_atk_gm, last_atk_actor, last_atk_hit);
    Bus::inst().note_lkm();
    MC_EVT("LKM (synthetic) attack returned %d", (int)r);
}

bool has_atk_target() { return last_atk_gm && last_atk_actor; }

// ---- GameModeSub::attack — the VIRTUAL entry (vtable[15], rva 0x25a0570) ----
// Guard thunk tail-jumps INTO the base body at +0x10, so a hook on the body
// start (GameMode_attack 0x2599ca0) never sees virtual callers. Hooked only
// to COUNT attacks + CAPTURE the attack context; client attacks go via
// inventory transactions anyway.
typedef bool (*t_gm_attack4)(void*, void*, void*, const float*);
static t_gm_attack4 o_gm_sub_attack = nullptr;
static volatile LONG g_ka_busy_note = 0;
// Guard-thunk ABI (rva 0x25a0570): attack(this=ctx, A2, ACTOR(r8, [x+0xc8]==1
// gate), hit). Which of arg2/arg3 is the actor is VERIFIED on the first real
// attack, then cached for KillAura replays.
// VERIFIED ABI (dump RE + live captures, 1.26.52.3):
//   attack3(this=ctx, TARGET=a2, gm=a3, hit=r9) — virtual slot 15 of the
//   derived GameModeSub vtable (rva 0xe827600), guard-thunk body @0x25a0570.
//   The guard validates [a3+0xc8]==1 and feeds a3 as the body's `this`
//   (body walks [a3+8]=player -> [player+0x1d8]=level). a2 = the TARGET
//   actor (varies per attack), this/a3 stay constant per session.
static void* ka_atk_this = nullptr;  // captured `this` (guard path)
static void* ka_atk_a3   = nullptr;  // captured gm-context (passes the guard)
static volatile LONG ka_armed = 0;
// Rune-style: GameMode* lives in a player field (26.20: +0xAA0). Ours is
// FOUND at runtime: scan player fields for an object whose vtable == the
// derived GameModeSub table (game base + 0xe827600). Then attacks go through
// the BODY (0x2599ca0) with this=gm, flag=1 — the exact ABI the guard thunk
// produces — so no manual attack is ever needed.
static void* ka_gm = nullptr;
static int   ka_gm_off = -1;

static bool h_gm_sub_attack(void* ths, void* a2, void* a3, const float* hit) {
    Bus::inst().note_lkm();
    // NOTE: the guard's [a3+0xc8]==1 check is the BLOCK condition (jne ->
    // attack proceeds). Capture everything readable; the guard re-validates
    // on each replay anyway.
    if (sdk::readable_ptr((uint8_t*)a3, 0xd0) && sdk::readable_ptr(ths, 16) &&
        sdk::readable_ptr((uint8_t*)a2, 32)) {
        ka_atk_this = ths;
        ka_atk_a3   = a3;
        last_atk_gm = ths; last_atk_actor = a2;
        if (!InterlockedExchange(&ka_armed, 1))
            MC_LOG("killaura: ARMED — this=%p a3=%p target=%p hit=%p", ths, a3, a2, (const void*)hit);
    }
    static volatile LONG s_ra_n = 0;
    long rak = InterlockedIncrement(&s_ra_n);
    if (rak <= 3) {
        void* ra = __builtin_return_address(0);
        HMODULE exe = GetModuleHandleW(nullptr);
        MC_LOG("killaura: real attack #%ld caller ra=%p (rva 0x%llx)",
               rak, ra, (unsigned long long)((uintptr_t)ra - (uintptr_t)exe));
    }
    if (!g_ka_busy_note)
        MC_EVT("manual attack this=%p target=%p hit=%p", ths, a2, (const void*)hit);
    return o_gm_sub_attack(ths, a2, a3, hit);
}

// arm swing: player vtable slot — VERIFIED vt[60](player, 1) (the game's own
// attack handler order); Rune's vt[110] does NOT exist in 1.26.52.3 (crashed)
static volatile LONG ka_swing_n = 0;
void swing_arm() {
    void* player = sdk::local_player();
    if (!player || !sdk::readable_ptr((uint8_t*)player, 8)) return;
    void** vt = *(void***)player;
    if (!sdk::readable_ptr(vt, Offsets::VT_PLAYER_MIN_SLOTS * 8)) return;
    void* fn = vt[60];
    HMODULE exe = GetModuleHandleW(nullptr);
    if (!exe || (uint8_t*)fn < (uint8_t*)exe ||
        (uint8_t*)fn >= (uint8_t*)exe + image_size())
        return;                                   // not game code -> refuse
    void* sfn = vt[Offsets::VT_PLAYER_SWING];
    if ((uint8_t*)sfn < (uint8_t*)exe || (uint8_t*)sfn >= (uint8_t*)exe + image_size())
        return;
    typedef void (*t_swing)(void*, unsigned char);
    ((t_swing)sfn)(player, 1);
    long k = InterlockedIncrement(&ka_swing_n);
    if (k <= 5) MC_LOG("killaura: swing #%ld (vt[60] @%p)", k, sfn);
}

void invalidate_gm_context() {
    if (ka_gm || ka_atk_this)
        MC_LOG("killaura: GameModeSub context invalidated (player reloaded)");
    ka_gm = nullptr; ka_gm_off = -1;
    ka_atk_this = nullptr; ka_atk_a3 = nullptr;
    ka_armed = 0;
    sdk::block_source_invalidate();   // the old dimension's BlockSource is gone too
}

void* find_gamemode_sub() {
    void* player = sdk::local_player();
    if (ka_gm) {
        // the cache points INSIDE a player object: [gm+8] must still be the
        // CURRENT local player, else the old player was freed (respawn /
        // dimension change) and the pointer is dangling
        if (sdk::readable_ptr((uint8_t*)ka_gm, 0x10) &&
            *(void**)((uint8_t*)ka_gm + Offsets::GM_STORED_PLAYER_OFF) == player)
            return ka_gm;
        MC_WARN("killaura: cached GameModeSub is stale (stored player %p != current %p), rediscovering",
                *(void**)((uint8_t*)ka_gm + (sdk::readable_ptr((uint8_t*)ka_gm, 0x10) ? Offsets::GM_STORED_PLAYER_OFF : 0)), player);
        ka_gm = nullptr; ka_gm_off = -1;
    }
    if (!player || !sdk::readable_ptr((uint8_t*)player, 0x1400)) return nullptr;
    HMODULE exe = GetModuleHandleW(nullptr);
    if (!exe) return nullptr;
    void* want_vt = (uint8_t*)exe + Offsets::VT_GAMEMODE_SUB;
    uint8_t* pl = (uint8_t*)player;
    for (int off = Offsets::PLAYER_GM_SCAN_LO; off <= Offsets::PLAYER_GM_SCAN_HI; off += 8) {
        void* p = *(void**)(pl + off);
        if (!p || !sdk::readable_ptr((uint8_t*)p, 16)) continue;
        if (*(void**)p != want_vt) continue;
        // the attack body walks [gm+8]=player -> [player+0x1d8]=level: require
        // the stored player to be the SAME CLASS as ours, else that walk is
        // unsafe (that is what crashed the game on first hit)
        void* gmp = *(void**)((uint8_t*)p + Offsets::GM_STORED_PLAYER_OFF);
        if (!gmp || !sdk::readable_ptr((uint8_t*)gmp, 0x200)) continue;
        if (*(void**)gmp != *(void**)player) continue;
        ka_gm = p; ka_gm_off = off;
        MC_LOG("killaura: GameModeSub found at player+0x%x -> %p (stored player same-class OK)", off, p);
        return ka_gm;
    }
    return nullptr;
}

bool attack_ctx(void** ths, void** a3) {
    if (!ka_atk_this || !ka_atk_a3) return false;
    // the capture can outlive its object (server-side gm after a respawn):
    // re-check readability before handing it to a replay
    if (!sdk::readable_ptr((uint8_t*)ka_atk_a3, 0xd0) || !sdk::readable_ptr(ka_atk_this, 16)) {
        ka_atk_this = nullptr; ka_atk_a3 = nullptr;
        return false;
    }
    if (ths) *ths = ka_atk_this;
    if (a3) *a3 = ka_atk_a3;
    return true;
}

bool attack_hooks_ready() { return o_gm_attack && o_gm_sub_attack; }

bool gm_sub_attack_call(void* gm, void* target, void* a3, const float* hit) {
    if (!o_gm_sub_attack) return false;
    return o_gm_sub_attack(gm, target, a3, hit);
}

void ka_set_busy(bool b) { g_ka_busy_note = b ? 1 : 0; }

// ---- GameMode::startDestroyBlock --------------------------------------------
typedef bool (*t_gm_startdestroy)(void*, const void*, uint8_t, void*);
static t_gm_startdestroy o_gm_startdestroy = nullptr;
static bool h_gm_startdestroy(void* gm, const void* pos, uint8_t face, void* destroyed) {
    const int32_t* bp = (const int32_t*)pos;
    MC_EVT("LKM startDestroyBlock pos=(%d,%d,%d) face=%u", bp[0], bp[1], bp[2], face);
    return o_gm_startdestroy(gm, pos, face, destroyed);
}

// ---- GameMode::tick — module tick (work lives in Level::tick) ---------------
typedef void (*t_gm_tick)(void*);
static t_gm_tick o_gm_tick = nullptr;
static void h_gm_tick(void* gm) {
    o_gm_tick(gm);
    // modules + telemetry live in h_level_tick (Rune-style velocity writes)
}

// ---- packet-level correction drop (Rune recipe) ------------------------------
// MinecraftPackets::createPacket(PacketID) -> shared_ptr<Packet> (sret: rcx=buf, rdx=id)
typedef void* (*t_create_packet)(void*, uint32_t);
static t_create_packet p_create_packet = nullptr;
typedef void (*t_pkt_handler)(void*, void*, void*, void*);
static t_pkt_handler o_move_handler = nullptr;

static void h_move_handler(void* self, void* netId, void* callback, void* pktShared) {
    bool drop = false;
    if (pktShared) {
        void* raw = *(void**)pktShared;          // shared_ptr<Packet>::get()
        if (raw) {
            void** vt = *(void***)raw;
            if (vt && vt[2]) {
                uint8_t id = ((uint8_t(*)(void*))vt[2])(raw);   // Packet::getID (slot 2)
                if (id == 0x13 /* MOVE_PLAYER */) {
                    bool on;
                    { auto& m = mod(); std::lock_guard<std::mutex> g(m.mtx); on = m.ac_drop_moveplayer; }
                    if (on) {
                        drop = true;
                        static long long n = 0;
                        if ((++n % 30) == 1) {
                            uint8_t* b = (uint8_t*)raw;
                            MC_EVT("DROP MovePlayerPacket #%lld bytes: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                                   n, b[0x10],b[0x11],b[0x12],b[0x13],b[0x14],b[0x15],b[0x16],b[0x17],
                                   b[0x18],b[0x19],b[0x1a],b[0x1b]);
                        }
                    }
                }
            }
        }
    }
    if (!drop) o_move_handler(self, netId, callback, pktShared);
}

static void install_packet_hook() {
    p_create_packet = (t_create_packet)sig_get("MinecraftPackets_createPacket");
    if (!p_create_packet) { MC_WARN("createPacket sig missing — packet drop disabled"); return; }
    uint8_t retbuf[32] = {0};
    p_create_packet(retbuf, 0x13);              // MOVE_PLAYER
    void* pkt = *(void**)retbuf;
    if (!pkt) { MC_WARN("createPacket(MOVE_PLAYER) returned null"); return; }
    MC_LOG("MovePlayerPacket instance: %p vt=%p", pkt, *(void**)pkt);
    // Rune layout: handler ptr @ packet+0x20 (validate nearby offsets)
    for (uint32_t off : {0x20u, 0x18u, 0x28u}) {
        void** handlerObj = *(void***)((uint8_t*)pkt + off);
        if (!handlerObj || (uintptr_t)handlerObj < 0x10000) continue;
        void** hvt = *(void***)handlerObj;
        if (!hvt || (uintptr_t)hvt < 0x10000) continue;
        void* handleFn = hvt[1];
        if (!handleFn) continue;
        if (MH_CreateHook(handleFn, (void*)h_move_handler, (void**)&o_move_handler) == MH_OK &&
            MH_EnableHook(handleFn) == MH_OK) {
            MC_LOG("MovePlayer handler hooked via pkt+0x%x: handler=%p fn=%p", off, handlerObj, handleFn);
            return;
        }
    }
    MC_WARN("MovePlayer handler not found — packet drop unavailable");
}

// ---- installation ------------------------------------------------------------
bool install_gamemode_hooks() {
    // NOTE (registry annotation): GameMode useItem / useItemOn / interact hooks
    // are REMOVED FOREVER — their ABI mismatches this build (garbage pos ->
    // crash on right-click; fixed twice). Keep them OUT when rebuilding.
    struct H { const char* n; void* det; void** orig; } hs[] = {
        {"GameMode_attack",            (void*)h_gm_attack,       (void**)&o_gm_attack},
        {"GameMode_startDestroyBlock", (void*)h_gm_startdestroy, (void**)&o_gm_startdestroy},
        {"GameMode_tick",              (void*)h_gm_tick,         (void**)&o_gm_tick},
        {"GameModeSub_attack",         (void*)h_gm_sub_attack,   (void**)&o_gm_sub_attack},
    };
    int ok = 0;
    for (auto& h : hs) {
        uintptr_t a = sig_get(h.n);
        if (!a) { MC_WARN("hook %s: address not resolved", h.n); continue; }
        if (MH_CreateHook((void*)a, h.det, h.orig) == MH_OK) {
            MH_EnableHook((void*)a);
            ok++;
            MC_LOG("hook %s installed @0x%llx", h.n, (unsigned long long)a);
        } else MC_ERR("hook %s MH_CreateHook failed", h.n);
    }
    MC_LOG("gamemode hooks installed: %d/%d", ok, (int)(sizeof(hs)/sizeof(hs[0])));
    install_packet_hook();
    return true;
}

} // namespace game
} // namespace mc
