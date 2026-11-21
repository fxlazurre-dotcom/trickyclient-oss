// ModCore — game SDK: BlockSource implementation.
// REAL ARCHITECTURE (established live via the capture hook, 2026-11-21):
// the game keeps a persistent Region at ClientInstance+0x248 and constructs
// SHORT-LIVED BlockSource views over it (many per frame — on stacks of the
// querying threads, some persistent on the heap). There is no long-lived
// "the BlockSource" field to find: we latch the latest HEAP view the game
// itself queries through, revalidate it every tick and re-latch on traffic.
// Real class vtable rva 0xe8554e0; world bounds: MAX at +0x38, MIN at +0x3A.
#include "game/sdk/world/BlockSource.hpp"
#include "game/sdk/SDK.hpp"
#include "game/sdk/actor/LocalPlayer.hpp"
#include "game/sigs/sigs.h"
#include "game/sigs/scan.h"
#include "core/log.h"
#include "game/sdk/actor/components.h"
#include <cmath>

#include <MinHook.h>
#include <windows.h>
#include <cstring>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <string>
#include <unordered_map>

namespace mc {
namespace sdk {

typedef void* (*t_get_block)(void*, const int32_t*);

static std::mutex g_bs_mtx;
// BlockSource views are EPHEMERAL by design (the game builds them over the
// persistent Region at ci+0x248 and drops them): we COPY the latched view
// into our own storage — the copied fields point at the persistent region,
// so the copy stays valid for the whole world session.
static unsigned char __attribute__((aligned(16))) g_bs_storage[0x80];
static void* g_bs = nullptr;            // == g_bs_storage when latched
static unsigned long long g_bs_time = 0;
// getBlock is MILLIONS-of-calls-per-frame hot (chunk tessellation!): after a
// heap view is latched the hook body is a single relaxed atomic load.
static std::atomic<bool> g_capture_done{false};
static void capture_rearm() { g_capture_done.store(false, std::memory_order_relaxed); }
static void** g_bs_vt_expected = nullptr;   // vtable of the real class (1st heap hit)

// DEEP validation (defined after o_gb_cap — it must call the ORIGINAL
// getBlock through the trampoline, NOT through our own detour)
static bool view_serves_player_blocks(void* view, void** vt);

void block_source_invalidate() {
    {
        std::lock_guard<std::mutex> g(g_bs_mtx);
        if (g_bs) MC_LOG("blockesp: BlockSource cache invalidated (world reload)");
        g_bs = nullptr;
    }
    capture_rearm();   // re-latch from the next game-side getBlock traffic
}

void* block_source_find(void* /*level_or_null*/) {
    std::lock_guard<std::mutex> g(g_bs_mtx);
    if (!g_bs) return nullptr;               // the capture hook re-latches shortly
    // the copy stays valid for the whole world session (it points at the
    // persistent region): only re-verify it periodically, and re-latch from
    // traffic ONLY when it stops serving the player real blocks. The old
    // 1.5 s blind rearm let worker views overwrite a GOOD copy — that churn
    // is what killed every pass.
    unsigned long long now = GetTickCount64();
    if (now - g_bs_time > 3000) {
        if (!view_serves_player_blocks(g_bs, *(void***)g_bs)) {
            g_bs = nullptr;
            capture_rearm();                 // wait for a fresh validated view
            return nullptr;
        }
        g_bs_time = now;                     // still good — keep serving it
    }
    return g_bs;
}

void block_source_latch(void* candidate) {   // called from the capture hook
    if (!candidate || !readable_ptr((uint8_t*)candidate, 0x80)) return;
    void** vt = *(void***)candidate;
    if (!readable_ptr(vt, 42 * 8)) return;
    // same-content refresh (fast path, no deep calls)
    {
        std::lock_guard<std::mutex> g(g_bs_mtx);
        if (g_bs && !memcmp(g_bs_storage, candidate, 0x80)) {
            g_bs_time = GetTickCount64();        // same view: just refresh age
            return;
        }
        if (g_bs && view_serves_player_blocks(g_bs, *(void***)g_bs)) {
            g_bs_time = GetTickCount64();        // hold a GOOD view: do NOT let
            return;                              // worker traffic overwrite it
        }
    }
    // deep-validate the candidate (chunk resolve + getBlock/getElement agree
    // on the player's feet) — snapshot/worker views fail here
    if (!view_serves_player_blocks(candidate, vt)) return;
    static unsigned long long s_last_log = 0;    // throttle "adopted" logs
    bool do_log = GetTickCount64() - s_last_log > 5000;
    {
        std::lock_guard<std::mutex> g(g_bs_mtx);
        if (!g_bs_vt_expected) g_bs_vt_expected = vt;
        memcpy(g_bs_storage, candidate, 0x80);   // adopt the view BY VALUE
        g_bs = g_bs_storage;
        g_bs_time = GetTickCount64();
    }
    g_capture_done.store(true, std::memory_order_relaxed);   // hot path becomes free
    if (do_log) {
        s_last_log = GetTickCount64();
        HMODULE exe = GetModuleHandleW(nullptr);
        int lo = (int)*(int16_t*)(g_bs_storage + 0x3A);   // MIN (VERIFIED live)
        int hi = (int)*(int16_t*)(g_bs_storage + 0x38);   // MAX (VERIFIED live)
        MC_LOG("blockesp: BlockSource adopted %p (vt rva 0x%llx, world y [%d..%d])",
               candidate, (unsigned long long)((uintptr_t)vt - (uintptr_t)exe), lo, hi);
    }
}

void* block_source_chunk(void* bs, int chunk_x, int chunk_z) {
    if (!bs || !readable_ptr((uint8_t*)bs, 0x40)) return nullptr;
    void** vt = *(void***)bs;
    if (!readable_ptr(vt, 42 * 8)) return nullptr;
    typedef void* (*t_get_chunk)(void*, const int32_t*);   // ChunkPos const& {x, z}
    const int32_t cp[2] = { chunk_x, chunk_z };
    void* chunk = ((t_get_chunk)vt[41])(bs, cp);
    return (chunk && readable_ptr((uint8_t*)chunk, 0x150)) ? chunk : nullptr;
}

void* block_source_get(void* bs, int x, int y, int z) {
    // HOT PATH (the scanner calls this tens of thousands of times per tick):
    // resolve the function ONCE, no VirtualQuery on our own static storage
    static t_get_block fn = (t_get_block)sig_get("BlockSource_getBlock");
    if (!fn || !bs) return nullptr;
    const int32_t pos[3] = { x, y, z };
    return fn(bs, pos);
}

int block_source_min_y(void* bs) {
    if (!bs || !readable_ptr((uint8_t*)bs, 0x40)) return -64;
    return (int)*(int16_t*)((uint8_t*)bs + 0x3A);
}
int block_source_max_y(void* bs) {
    if (!bs || !readable_ptr((uint8_t*)bs, 0x40)) return 320;
    return (int)*(int16_t*)((uint8_t*)bs + 0x38);
}

// ---- Block -> BlockType -> name --------------------------------------------
void* block_type_of(void* block) {
    if (!block || !readable_ptr((uint8_t*)block, 0x70)) return nullptr;
    void* t = *(void**)((uint8_t*)block + 0x68);      // BDS candidate offset
    return readable_ptr((uint8_t*)t, 0x110) ? t : nullptr;
}

static bool name_chars_ok(const char* s, size_t n) {
    if (n < 2 || n > 63) return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == ':' || c == '.';
        if (!ok) return false;
    }
    return true;
}

static std::mutex g_names_mtx;
static std::unordered_map<void*, std::string> g_names;
static long g_logged = 0;

const char* block_type_name(void* type) {
    {
        std::lock_guard<std::mutex> g(g_names_mtx);
        auto it = g_names.find(type);
        if (it != g_names.end()) return it->second.c_str();
    }
    std::string result;
    uint8_t* t = (uint8_t*)type;
    // (a) plain pointer to a C string
    char* p = *(char**)(t + 0xE8);
    if (readable_ptr(p, 64)) {              // full span: strnlen walks up to 64
        size_t n = strnlen(p, 64);
        if (name_chars_ok(p, n)) result = p;
    }
    // (b)/(c) MSVC std::string at +0xE8: { char buf[16] | char* heap; size; cap }
    if (result.empty()) {
        char* buf = (char*)(t + 0xE8);
        uint64_t size = *(uint64_t*)(t + 0xE8 + 16);
        uint64_t cap  = *(uint64_t*)(t + 0xE8 + 24);
        if (size <= 15) {                                  // SSO
            if (cap == 15 && name_chars_ok(buf, (size_t)size)) result = buf;
        } else if (size < 64 && cap >= size && cap < 128) { // heap
            char* heap = *(char**)buf;
            if (readable_ptr(heap, size) && name_chars_ok(heap, (size_t)size)) result = heap;
        }
    }
    const char* ret;
    long k;
    {
        std::lock_guard<std::mutex> g(g_names_mtx);
        auto it = g_names.find(type);
        if (it != g_names.end()) return it->second.c_str();   // another thread won
        ret = g_names.emplace(type, std::move(result)).first->second.c_str();
        k = ++g_logged;
    }
    if (k <= 12)
        MC_LOG("blockesp: type %p name \"%s\"", type, ret);
    return ret;
}

static bool name_has(const char* n, const char* what) {
    if (!n || !*n) return false;
    return strstr(n, what) != nullptr;
}

uint16_t block_legacy_id(void* block_or_type) {
    uint8_t* t = (uint8_t*)block_type_of(block_or_type);
    if (!t || !readable_ptr(t, 0x180)) return 0xFFFF;
    return *(uint16_t*)(t + 0x17E);       // BlockLegacy_mBlockId (reference client)
}

// classify a block by its legacy NAME: deepslate variants match their base
// ore automatically ("deepslate_gold_ore" contains "gold_ore"), so the list
// survives id renumbering between point releases.
static int classify_ore_name(const char* n) {
    if (!n || !*n) return -1;
    struct Frag { const char* f; int k; };
    static const Frag tbl[] = {
        { "coal_ore",       ORE_COAL },
        { "iron_ore",       ORE_IRON },
        { "copper_ore",     ORE_COPPER },
        { "gold_ore",       ORE_GOLD },
        { "redstone_ore",   ORE_REDSTONE },
        { "lapis_ore",      ORE_LAPIS },
        { "diamond_ore",    ORE_DIAMOND },
        { "emerald_ore",    ORE_EMERALD },
        { "quartz_ore",     ORE_NETHER },
        { "ancient_debris", ORE_NETHER },
    };
    for (auto& t : tbl)
        if (strstr(n, t.f)) return t.k;
    return -1;
}

int block_ore_kind(void* block_or_type) {
    // blocks are SINGLETONS: the kind is resolved by NAME once per unique
    // BlockLegacy and cached by pointer — the per-call cost is a hash lookup
    static std::unordered_map<void*, int> kind_cache;
    uint8_t* t = (uint8_t*)block_type_of(block_or_type);
    if (!t || !readable_ptr(t, 0x180)) return -1;
    auto it = kind_cache.find(t);
    if (it != kind_cache.end()) return it->second;
    const char* nm = block_type_name(t);
    int k = classify_ore_name(nm);
    kind_cache.emplace(t, k);
    if (k >= 0)
        MC_LOG("blockesp: ore type \"%s\" -> kind %d", nm ? nm : "?", k);
    return k;
}

// ---- capture hook: see every BlockSource the game queries blocks through ----
typedef void* (*t_gb_cap)(void*, const int32_t*);
static t_gb_cap o_gb_cap = nullptr;

static bool view_serves_player_blocks(void* view, void** vt) {
    // RE-ENTRANCY GUARD: this runs INSIDE the getBlock detour (from latch).
    // Calling block-source lookups here would re-enter the detour and recurse
    // forever — the 11:54 freeze. We call the ORIGINAL via the trampoline,
    // and the guard makes any stray re-entry a clean "false".
    static thread_local bool t_busy = false;
    if (t_busy || !o_gb_cap) return false;
    t_busy = true;
    struct BusyOff { bool& f; ~BusyOff() { f = false; } } off{ t_busy };

    void* player = local_player();
    if (!player || !readable_ptr((uint8_t*)player, 0x220))
        return false;                      // no live player -> nothing to validate against
    StateVectorC* sv = actor_statevec((uint8_t*)player);
    if (!sv) return false;
    int px = (int)floorf(sv->px), py = (int)floorf(sv->py), pz = (int)floorf(sv->pz);
    // 1) the player's chunk must resolve (boot stubs fail here)
    typedef void* (*t_gc)(void*, const int32_t*);
    const int32_t cp[2] = { px >> 4, pz >> 4 };
    void* chunk = ((t_gc)vt[41])(view, cp);
    if (!chunk || !readable_ptr((uint8_t*)chunk, 0x150)) return false;
    // 2) the column under the player's feet must answer through the game's
    //    own getBlock (ORIGINAL via trampoline — never our detour)
    for (int dy = 1; dy <= 3; dy++) {
        const int32_t q[3] = { px, py - dy, pz };
        void* blk = o_gb_cap(view, q);
        if (!blk) return false;            // null where the world HAS blocks
        uint16_t id = block_legacy_id(blk);
        if (id == 0xFFFF) return false;    // unreadable block header
        if (id != 0) return true;          // the solid block we stand on
    }
    return true;                           // all air — odd, but it answers
}
static void* g_seen[8];
static volatile LONG g_n_seen = 0;

static bool same_thread_stack(void* p, void* fb) {
    // wine stacks are ~1MB; a view on OUR calling thread's stack is temporary
    // by definition — only latch views living elsewhere (heap subsystems)
    uintptr_t a = (uintptr_t)p, f = (uintptr_t)fb;
    return a > f - 0x200000 && a < f + 0x10000;
}

static void* h_gb_cap(void* bs, const int32_t* pos) {
    void* r = o_gb_cap(bs, pos);
    if (g_capture_done.load(std::memory_order_relaxed)) return r;   // free fast path
    long n = g_n_seen;
    for (long i = 0; i < n && i < 8; ++i)
        if (g_seen[i] == bs) return r;
    if (n < 8 && InterlockedCompareExchange(&g_n_seen, n + 1, n) == n) {
        g_seen[n] = bs;
        void** vt = *(void***)bs;
        HMODULE exe = GetModuleHandleW(nullptr);
        int32_t ymax = (int)*(int16_t*)((uint8_t*)bs + 0x38);
        int32_t ymin = (int)*(int16_t*)((uint8_t*)bs + 0x3A);
        bool stack_view = same_thread_stack(bs, __builtin_frame_address(0));
        MC_LOG("blockcap: BS#%ld this=%p vt_rva=0x%llx q=(%d,%d,%d) -> %p y[%d..%d] %s",
               n + 1, bs, (unsigned long long)((uintptr_t)vt - (uintptr_t)exe),
               pos ? pos[0] : 0, pos ? pos[1] : 0, pos ? pos[2] : 0, r, ymin, ymax,
               stack_view ? "(stack view)" : "(HEAP)");
        (void)stack_view;
    }
    // latch the latest NON-STACK view as our working BlockSource
    if (!same_thread_stack(bs, __builtin_frame_address(0)))
        block_source_latch(bs);
    return r;
}

void install_block_capture() {
    uintptr_t a = sig_get("BlockSource_getBlock");
    if (!a) { MC_WARN("blockcap: sig missing"); return; }
    if (MH_CreateHook((void*)a, (void*)h_gb_cap, (void**)&o_gb_cap) == MH_OK) {
        MH_EnableHook((void*)a);
        MC_LOG("hook BlockSource_getBlock installed (capture mode)");
    } else MC_WARN("blockcap: MH_CreateHook failed");
}

} // namespace sdk
} // namespace mc
