// ModCore — game SDK: GameInputRedist instrumentation (moved verbatim from
// the monolith). Diagnostics + the vtable-slot clickers; the FUNCTION-body
// clicker (v7) + the phase machine live in game/hooks/input.cpp.
#include "game/sdk/SDK.hpp"
#include "game/hooks/input.h"        // gi_click_phase / gi_apply_click*
#include "core/log.h"
#include "core/state.h"
#include "core/input.h"
#include "core/hooks.h"

#include <windows.h>

namespace mc {
namespace game {
// ---- GameInput runtime probe (read-only): vtables of LIVE objects ----
void gameinput_probe() {
    HMODULE gi = GetModuleHandleW(L"GameInputRedist.dll");
    if (!gi) { MC_LOG("gameinput: GameInputRedist.dll not loaded yet"); return; }
    uint8_t* base = (uint8_t*)gi;
    auto* dos = (IMAGE_DOS_HEADER*)base;
    auto* nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
    if (dos->e_magic != 0x5A4D || nt->Signature != 0x4550) { MC_WARN("gameinput: bad PE"); return; }
    auto* sec = IMAGE_FIRST_SECTION(nt);
    const IMAGE_SECTION_HEADER* rdataS = nullptr; const IMAGE_SECTION_HEADER* dataS = nullptr;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (!memcmp(sec->Name, ".rdata", 6)) rdataS = sec;
        if (!memcmp(sec->Name, ".data", 5) && sec->Name[5] != 'T') dataS = sec;
    }
    if (!rdataS || !dataS) { MC_WARN("gameinput: sections not found"); return; }
    uintptr_t rd_lo = (uintptr_t)base + rdataS->VirtualAddress;
    uintptr_t rd_hi = rd_lo + rdataS->Misc.VirtualSize;
    uintptr_t text_lo = (uintptr_t)base + nt->OptionalHeader.BaseOfCode;
    uintptr_t text_hi = text_lo + nt->OptionalHeader.SizeOfCode;
    MC_LOG("gameinput: base=%p text=[%p..%p] rdata=[%p..%p]",
           (void*)base, (void*)text_lo, (void*)text_hi, (void*)rd_lo, (void*)rd_hi);
    uintptr_t d_lo = (uintptr_t)base + dataS->VirtualAddress;
    uintptr_t d_hi = d_lo + dataS->Misc.VirtualSize;
    static uintptr_t s_vts[512]; static uintptr_t s_inst[512];
    int nv = 0;
    for (uintptr_t d = d_lo; d + 8 <= d_hi && nv < 512; d += 8) {
        uintptr_t q = *(uintptr_t*)d;
        if (q < 0x10000 || (q & 7)) continue;
        if (q >= rd_lo && q < rd_hi) continue;
        if (!sdk::readable_ptr((void*)q, 8)) continue;
        uintptr_t vt = *(uintptr_t*)q;
        if (vt < rd_lo || vt >= rd_hi) continue;
        bool dup = false;
        for (int k = 0; k < nv; ++k) if (s_vts[k] == vt) { dup = true; break; }
        if (!dup) { s_vts[nv] = vt; s_inst[nv] = q; ++nv; }
    }
    int logged = 0;
    for (int k = 0; k < nv && logged < 12; ++k) {
        uintptr_t pnt = s_vts[k];
        int run = 0;
        while (pnt + (size_t)run*8 + 8 <= rd_hi) {
            uintptr_t q = *(uintptr_t*)(pnt + (size_t)run*8);
            if (q >= text_lo && q < text_hi) ++run; else break;
        }
        if (run < 12) continue;
        ++logged;
        MC_LOG("gameinput: LIVE vtable @rva 0x%llx inst=%p slots>=%d [0..7]: 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx",
               (unsigned long long)(pnt - (uintptr_t)base), (void*)s_inst[k], run,
               (unsigned long long)(*(uintptr_t*)pnt - (uintptr_t)base),
               (unsigned long long)(*(uintptr_t*)(pnt+8) - (uintptr_t)base),
               (unsigned long long)(*(uintptr_t*)(pnt+16) - (uintptr_t)base),
               (unsigned long long)(*(uintptr_t*)(pnt+24) - (uintptr_t)base),
               (unsigned long long)(*(uintptr_t*)(pnt+32) - (uintptr_t)base),
               (unsigned long long)(*(uintptr_t*)(pnt+40) - (uintptr_t)base),
               (unsigned long long)(*(uintptr_t*)(pnt+48) - (uintptr_t)base),
               (unsigned long long)(*(uintptr_t*)(pnt+56) - (uintptr_t)base));
    }
    MC_LOG("gameinput: probe done, %d objects, %d live vtables", nv, logged);
}

// ---- GameInput instrumentation: the STABLE hot vtable (rva 0x95cb0) ----
typedef uint64_t (*t_gi8)(void*, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static bool gi_scan_and_inject(const char* tag, void* s, void* args[7], bool pre);
static void gi_chase_factory_out(void** c_slot);
static t_gi8 o_gi5 = nullptr, o_gi6 = nullptr, o_gi8s = nullptr;
static volatile LONG g_gi5n = 0, g_gi6n = 0, g_gi8n = 0;
static void gi_dump_reading(const char* tag, void* buf) {
    if (!buf || (uintptr_t)buf < 0x10000) return;
    if (!sdk::readable_ptr(buf, 0x50)) return;
    uint64_t* q = (uint64_t*)buf;
    MC_LOG("%s lmb edge: %016llx %016llx %016llx %016llx %016llx %016llx %016llx %016llx %016llx %016llx",
           tag, (unsigned long long)q[0], (unsigned long long)q[1], (unsigned long long)q[2],
           (unsigned long long)q[3], (unsigned long long)q[4], (unsigned long long)q[5],
           (unsigned long long)q[6], (unsigned long long)q[7], (unsigned long long)q[8],
           (unsigned long long)q[9]);
}
static void gi_dump_call(const char* slot, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t ret) {
    char tag[96];
    snprintf(tag, sizeof(tag), "%s OUT c=%llx", slot, (unsigned long long)c);
    // FULL dump of the OUT buffer (arg c): the reading lands here per the
    // call graph — 54 qwords so the buttons offset is visible wherever it is
    if (c > 0x10000 && sdk::readable_ptr((void*)c, 0x1B0)) {
        uint64_t* q = (uint64_t*)c;
        for (int row = 0; row < 6; ++row) {
            char r[128];
            snprintf(r, sizeof(r), "%s +%02x0:", tag, row);
            MC_LOG("%s %016llx %016llx %016llx %016llx %016llx %016llx %016llx %016llx %016llx",
                   r,
                   (unsigned long long)q[row*9+0], (unsigned long long)q[row*9+1],
                   (unsigned long long)q[row*9+2], (unsigned long long)q[row*9+3],
                   (unsigned long long)q[row*9+4], (unsigned long long)q[row*9+5],
                   (unsigned long long)q[row*9+6], (unsigned long long)q[row*9+7],
                   (unsigned long long)q[row*9+8]);
        }
    }
}
static uint64_t h_gi5(void* s, uint64_t a, uint64_t b, uint64_t c, uint64_t d2, uint64_t e2, uint64_t f2, uint64_t g2) {
    { void* B[7] = {(void*)a, (void*)b, (void*)c, (void*)d2, (void*)e2, (void*)f2, (void*)g2}; gi_scan_and_inject("cb0[5]", s, B, false); }
    long k = InterlockedIncrement(&g_gi5n);
    { void* B[7] = {(void*)a, (void*)b, (void*)c, (void*)d2, (void*)e2, (void*)f2, (void*)g2}; gi_scan_and_inject("cb0[5]-pre", s, B, true); }
    uint64_t r = o_gi5(s, a, b, c, d2, e2, f2, g2);
    static int s_prev_lmb = -1;
    int lmb = real_key_down(VK_LBUTTON) ? 1 : 0;
    if (lmb != s_prev_lmb) { s_prev_lmb = lmb; gi_dump_call(lmb ? "gi5 LMB-DOWN" : "gi5 LMB-UP  ", a, b, c, d2, r); }
    if (k <= 12 || (k % 2000) == 0) {
        void* ra = __builtin_return_address(0);
        MC_LOG("gameinput vt[5] #%ld ra=%llx a=%llx b=%llx c=%llx d=%llx", k,
               (unsigned long long)ra, a, b, c, d2);
    }
    return r;
}
static uint64_t h_gi6(void* s, uint64_t a, uint64_t b, uint64_t c, uint64_t d2, uint64_t e2, uint64_t f2, uint64_t g2) {
    { void* B[7] = {(void*)a, (void*)b, (void*)c, (void*)d2, (void*)e2, (void*)f2, (void*)g2}; gi_scan_and_inject("cb0[6]", s, B, false); }
    long k = InterlockedIncrement(&g_gi6n);
    { void* B[7] = {(void*)a, (void*)b, (void*)c, (void*)d2, (void*)e2, (void*)f2, (void*)g2}; gi_scan_and_inject("cb0[6]-pre", s, B, true); }
    uint64_t r = o_gi6(s, a, b, c, d2, e2, f2, g2);
    static int s_prev_lmb = -1;
    int lmb = real_key_down(VK_LBUTTON) ? 1 : 0;
    if (lmb != s_prev_lmb) { s_prev_lmb = lmb; gi_dump_call(lmb ? "gi6 LMB-DOWN" : "gi6 LMB-UP  ", a, b, c, d2, r); }
    return r;
}
static uint64_t h_gi8s(void* s, uint64_t a, uint64_t b, uint64_t c, uint64_t d2, uint64_t e2, uint64_t f2, uint64_t g2) {
    { void* B[7] = {(void*)a, (void*)b, (void*)c, (void*)d2, (void*)e2, (void*)f2, (void*)g2}; gi_scan_and_inject("cb0[8]", s, B, false); }
    long k = InterlockedIncrement(&g_gi8n);
    { void* B[7] = {(void*)a, (void*)b, (void*)c, (void*)d2, (void*)e2, (void*)f2, (void*)g2}; gi_scan_and_inject("cb0[8]-pre", s, B, true); }
    uint64_t r = o_gi8s(s, a, b, c, d2, e2, f2, g2);
    static int s_prev_lmb = -1;
    int lmb = real_key_down(VK_LBUTTON) ? 1 : 0;
    if (lmb != s_prev_lmb) { s_prev_lmb = lmb; gi_dump_call(lmb ? "gi8 LMB-DOWN" : "gi8 LMB-UP  ", a, b, c, d2, r); }
    return r;
}
// ---- generic probe trampolines for cb0/cbB slots (which getters does the
// game ACTUALLY call? log rate + args + return address, then call original) ----
static t_gi8 o_gip[2][12];
#define GI_PROBE(V, N) \
static uint64_t h_gip##V##_##N(void* s, uint64_t a, uint64_t b, uint64_t c, uint64_t d2, uint64_t e2, uint64_t f2, uint64_t g2) { \
    static volatile LONG nn = 0; long k = InterlockedIncrement(&nn); \
    uint64_t r = o_gip[V][N](s, a, b, c, d2, e2, f2, g2); \
    if (k <= 2 || (k % 2000) == 0) { \
        void* ra = __builtin_return_address(0); \
        MC_LOG("gameinput probe%d[%d] #%ld ra=%llx a=%llx b=%llx c=%llx d=%llx r=%llx", V, N, k, (unsigned long long)ra, a, b, c, d2, r); \
    } \
    return r; \
}
GI_PROBE(0,3) GI_PROBE(0,4) GI_PROBE(0,7) GI_PROBE(0,10) GI_PROBE(0,11)
GI_PROBE(1,3) GI_PROBE(1,4) GI_PROBE(1,5) GI_PROBE(1,6) GI_PROBE(1,7) GI_PROBE(1,8) GI_PROBE(1,10) GI_PROBE(1,11)
static void* gip_det(int v, int s2) {
    if (v == 0) { switch (s2) { case 3: return (void*)h_gip0_3; case 4: return (void*)h_gip0_4; case 7: return (void*)h_gip0_7; case 10: return (void*)h_gip0_10; case 11: return (void*)h_gip0_11; } }
    else { switch (s2) { case 3: return (void*)h_gip1_3; case 4: return (void*)h_gip1_4; case 5: return (void*)h_gip1_5; case 6: return (void*)h_gip1_6; case 7: return (void*)h_gip1_7; case 8: return (void*)h_gip1_8; case 10: return (void*)h_gip1_10; case 11: return (void*)h_gip1_11; } }
    return nullptr;
}

// ===================== CLICKER v4: "answer the question" =====================
// Static RE of GameInputRedist.dll (fereading.cpp + FeReadingTearoff.h):
//   the reading object handed to the game has THREE interfaces:
//     vtA rva 0x95cb0, vtB rva 0x95c10 (thunks, this-8), vtC rva 0x95b48
//   vtA[9] @0x3b360 = GetMouseState(this, size, GameInputMouseState* out):
//     memcpy(out, this+0x108, size) — { u64 buttons, posX, posY, wheelX, wheelY }
//   vtB[9] @0x3b420 = sub rcx,8; jmp 0x3b360  (same getter, interface B)
// The game calls this getter every poll. We call the original, then apply our
// LMB phase to buttons bit0 (GameInputMouseLeft). The position passes through
// untouched -> camera drift is impossible and clicks work WHILE MOVING.
// No IPC races, no buffer guessing: the game asks "is LMB down?", we answer.
static t_gi8 o_gi9 = nullptr, o_gi9b = nullptr;
static volatile LONG g_gi9n = 0;
// (gi_apply_click lives in game/hooks/input.cpp now)
static t_gi8 o_gi14 = nullptr, o_gi14b = nullptr;
static volatile LONG g_gi14n = 0;
// (gi_apply_click14 lives in game/hooks/input.cpp too)
static uint64_t h_gi14(void* s, uint64_t a, uint64_t b, uint64_t c, uint64_t d2, uint64_t e2, uint64_t f2, uint64_t g2) {
    uint64_t r = o_gi14(s, a, b, c, d2, e2, f2, g2);
    gi_apply_click14(b);
    return r;
}
static uint64_t h_gi14b(void* s, uint64_t a, uint64_t b, uint64_t c, uint64_t d2, uint64_t e2, uint64_t f2, uint64_t g2) {
    uint64_t r = o_gi14b(s, a, b, c, d2, e2, f2, g2);
    gi_apply_click14(b);
    return r;
}
static uint64_t h_gi9(void* s, uint64_t a, uint64_t b, uint64_t c, uint64_t d2, uint64_t e2, uint64_t f2, uint64_t g2) {
    uint64_t r = o_gi9(s, a, b, c, d2, e2, f2, g2);
    gi_apply_click(b);
    return r;
}
static uint64_t h_gi9b(void* s, uint64_t a, uint64_t b, uint64_t c, uint64_t d2, uint64_t e2, uint64_t f2, uint64_t g2) {
    uint64_t r = o_gi9b(s, a, b, c, d2, e2, f2, g2);
    gi_apply_click(b);
    return r;
}
// legacy trampoline helper — logging only now (ALL blind writes removed:
// the calibration crashed the game by locking onto wine statics)
static bool gi_scan_and_inject(const char* tag, void* s, void* args[7], bool pre) {
    (void)tag; (void)s; (void)args; (void)pre;
    return false;
}

// ---- dynamic vtable chase: sv4 is a FACTORY (c = slot receiving the inter-
// face object). After the original call, *c points at the real per-frame
// interface the game uses -> patch its vtable slots automatically.
static t_gi8 o_gid[2][6];
static void* g_gid_vts[2] = {nullptr, nullptr};
#define GI_DYN_HANDLER(I, N) \
static uint64_t h_gid##I##_##N(void* s, uint64_t a, uint64_t b, uint64_t c, uint64_t d2, uint64_t e2, uint64_t f2, uint64_t g2) { \
    { void* B[7] = {(void*)a, (void*)b, (void*)c, (void*)d2, (void*)e2, (void*)f2, (void*)g2}; gi_scan_and_inject("dyn" #I "[" #N "]-pre", s, B, true); } \
    uint64_t r = o_gid[I][N - 3](s, a, b, c, d2, e2, f2, g2); \
    { void* B2[7] = {(void*)a, (void*)b, (void*)c, (void*)d2, (void*)e2, (void*)f2, (void*)g2}; gi_scan_and_inject("dyn" #I "[" #N "]", s, B2, false); } \
    static volatile LONG nn = 0; long k = InterlockedIncrement(&nn); \
    if (k <= 6) MC_LOG("gameinput dyn%d[%d] #%ld: a=%llx b=%llx c=%llx d=%llx", I, N, k, a, b, c, d2); \
    return r; \
}
GI_DYN_HANDLER(0,3) GI_DYN_HANDLER(0,4) GI_DYN_HANDLER(0,5) GI_DYN_HANDLER(0,6) GI_DYN_HANDLER(0,7) GI_DYN_HANDLER(0,8)
GI_DYN_HANDLER(1,3) GI_DYN_HANDLER(1,4) GI_DYN_HANDLER(1,5) GI_DYN_HANDLER(1,6) GI_DYN_HANDLER(1,7) GI_DYN_HANDLER(1,8)
static void gi_chase_factory_out(void** c_slot) {
    if (!c_slot || !sdk::readable_ptr(c_slot, 8)) return;
    void* obj = *c_slot;
    if (!obj || (uintptr_t)obj < 0x10000 || !sdk::readable_ptr(obj, 9*8)) return;
    void** vt = *(void***)obj;
    if (!sdk::readable_ptr(vt, 9*8)) return;
    // must live in GameInputRedist's image to be one of ours
    HMODULE gi = GetModuleHandleW(L"GameInputRedist.dll");
    if (!gi) return;
    uint8_t* gib = (uint8_t*)gi;
    if ((uint8_t*)vt < gib || (uint8_t*)vt >= gib + 0x170000) return;
    if (vt_already_patched(vt)) return;     // patched by anyone already
    for (int i = 0; i < 2; ++i) {
        if (g_gid_vts[i] == vt) return;      // already patched
        if (g_gid_vts[i] == nullptr) {
            g_gid_vts[i] = vt;
            void* det[6] = {(void*)h_gid0_3,(void*)h_gid0_4,(void*)h_gid0_5,(void*)h_gid0_6,(void*)h_gid0_7,(void*)h_gid0_8};
            if (i == 1) { void* d2[6] = {(void*)h_gid1_3,(void*)h_gid1_4,(void*)h_gid1_5,(void*)h_gid1_6,(void*)h_gid1_7,(void*)h_gid1_8};
                          for (int q = 0; q < 6; ++q) det[q] = d2[q]; }
            for (int s2 = 3; s2 <= 8; ++s2) {
                void* orig = nullptr;
                patch_slot_vt(&vt[s2], det[s2 - 3], &orig);
                o_gid[i][s2 - 3] = (t_gi8)orig;
            }
            MC_LOG("gameinput: CHASED factory out -> obj=%p vt rva 0x%llx, slots 3..8 instrumented",
                   obj, (unsigned long long)((uint8_t*)vt - gib));
            return;
        }
    }
}

// generic singleton-slot trampolines: log args + LMB-edge reading dumps
static t_gi8 o_gis[8];
#define GI_SLOT_HANDLER(N) \
static uint64_t h_gis##N(void* s, uint64_t a, uint64_t b, uint64_t c, uint64_t d2, uint64_t e2, uint64_t f2, uint64_t g2) { \
    static volatile LONG n = 0; \
    long k = InterlockedIncrement(&n); \
    { void* B[7] = {(void*)a, (void*)b, (void*)c, (void*)d2, (void*)e2, (void*)f2, (void*)g2}; gi_scan_and_inject("sv" #N "-pre", s, B, true); } \
    uint64_t r = o_gis[N - 3](s, a, b, c, d2, e2, f2, g2); \
    { void* B2[7] = {(void*)a, (void*)b, (void*)c, (void*)d2, (void*)e2, (void*)f2, (void*)g2}; gi_scan_and_inject("sv" #N, s, B2, false); } \
    if (N == 4) { \
        gi_chase_factory_out((void**)c); \
        static volatile LONG s_nvt = 0; long kv = InterlockedIncrement(&s_nvt); \
        if (sdk::readable_ptr((void*)c, 8) && *(uint64_t*)c) { \
            void* obj = (void*)*(uint64_t*)c; \
            if (sdk::readable_ptr(obj, 8) && kv <= 8 || (kv % 200) == 0) { \
                HMODULE gi_ = GetModuleHandleW(L"GameInputRedist.dll"); \
                uint64_t vt_ = sdk::readable_ptr(obj, 8) ? *(uint64_t*)obj : 0; \
                uint64_t gbase = (uint64_t)gi_; \
                MC_LOG("sv4 out obj=%llx vt_rva=%s%llx kind=%x seq=%llx", \
                       (unsigned long long)obj, (vt_ >= gbase && gbase) ? "+" : "!", \
                       (unsigned long long)(gbase ? vt_ - gbase : vt_), \
                       sdk::readable_ptr((char*)obj + 0x40, 4) ? *(unsigned int*)((char*)obj + 0x40) : 0, \
                       sdk::readable_ptr((char*)obj + 0x38, 8) ? *(unsigned long long*)((char*)obj + 0x38) : 0); \
            } \
        } \
        void* ra = __builtin_return_address(0); \
        if (k <= 3 || (k % 1000) == 0) MC_LOG("gameinput sv[%d] #%ld ra=%llx ret=%llx out_slot=%llx *out=%llx", N, k, (unsigned long long)ra, (unsigned long long)r, (unsigned long long)c, (sdk::readable_ptr((void*)c,8) ? (unsigned long long)*(uint64_t*)c : 0ull)); \
    } \
    if (k <= 6) { \
        MC_LOG("gameinput sv[%d] #%ld: a=%llx b=%llx c=%llx d=%llx e=%llx f=%llx g=%llx ret=%llx", N, k, a, b, c, d2, e2, f2, g2, r); \
        void* P[5] = {(void*)b, (void*)c, (void*)d2, (void*)e2, (void*)f2}; \
        const char* NM[5] = {"b", "c", "d", "e", "f"}; \
        for (int pi = 0; pi < 5; ++pi) { \
            void* pb = P[pi]; \
            if (!pb || (uintptr_t)pb < 0x10000 || !sdk::readable_ptr(pb, 32)) continue; \
            uint64_t* pq = (uint64_t*)pb; \
            MC_LOG("  sv%d %s: %016llx %016llx %016llx %016llx", N, NM[pi], \
                   (unsigned long long)pq[0], (unsigned long long)pq[1], \
                   (unsigned long long)pq[2], (unsigned long long)pq[3]); \
        } \
    } \
    static int s_prev_lmb = -1; \
    int lmb = real_key_down(VK_LBUTTON) ? 1 : 0; \
    if (lmb != s_prev_lmb) { s_prev_lmb = lmb; gi_dump_call(lmb ? "sv" #N " LMB-DOWN" : "sv" #N " LMB-UP  ", a, b, c, d2, r); } \
    return r; \
}
GI_SLOT_HANDLER(3) GI_SLOT_HANDLER(4) GI_SLOT_HANDLER(5) GI_SLOT_HANDLER(6)
GI_SLOT_HANDLER(7) GI_SLOT_HANDLER(8) GI_SLOT_HANDLER(9) GI_SLOT_HANDLER(10)

static t_gi8 o_gih[4][6];
#define GI_HEAP_HANDLER(V, N) \
static uint64_t h_gih##V##_##N(void* s, uint64_t a, uint64_t b, uint64_t c, uint64_t d2, uint64_t e2, uint64_t f2, uint64_t g2) { \
    { void* B[7] = {(void*)a, (void*)b, (void*)c, (void*)d2, (void*)e2, (void*)f2, (void*)g2}; gi_scan_and_inject("heap" #V "[" #N "]-pre", s, B, true); } \
    uint64_t r = o_gih[V][N - 3](s, a, b, c, d2, e2, f2, g2); \
    { void* B2[7] = {(void*)a, (void*)b, (void*)c, (void*)d2, (void*)e2, (void*)f2, (void*)g2}; gi_scan_and_inject("heap" #V "[" #N "]", s, B2, false); } \
    static volatile LONG nn = 0; long k = InterlockedIncrement(&nn); \
    if (k <= 4) MC_LOG("gameinput heap%d[%d] #%ld: a=%llx b=%llx c=%llx d=%llx", V, N, k, a, b, c, d2); \
    return r; \
}
GI_HEAP_HANDLER(0,3) GI_HEAP_HANDLER(0,4) GI_HEAP_HANDLER(0,5) GI_HEAP_HANDLER(0,6) GI_HEAP_HANDLER(0,7) GI_HEAP_HANDLER(0,8)
GI_HEAP_HANDLER(1,3) GI_HEAP_HANDLER(1,4) GI_HEAP_HANDLER(1,5) GI_HEAP_HANDLER(1,6) GI_HEAP_HANDLER(1,7) GI_HEAP_HANDLER(1,8)
GI_HEAP_HANDLER(2,3) GI_HEAP_HANDLER(2,4) GI_HEAP_HANDLER(2,5) GI_HEAP_HANDLER(2,6) GI_HEAP_HANDLER(2,7) GI_HEAP_HANDLER(2,8)
GI_HEAP_HANDLER(3,3) GI_HEAP_HANDLER(3,4) GI_HEAP_HANDLER(3,5) GI_HEAP_HANDLER(3,6) GI_HEAP_HANDLER(3,7) GI_HEAP_HANDLER(3,8)

void gameinput_instrument() {
    HMODULE gi = GetModuleHandleW(L"GameInputRedist.dll");
    if (!gi) { MC_WARN("gameinput instrument: module gone"); return; }
    void** vt = (void**)((uint8_t*)gi + 0x95cb0);
    if (sdk::readable_ptr(vt, 10*8)) {
        if (!vt_already_patched(vt)) {
        patch_slot_vt(&vt[5], (void*)h_gi5,  (void**)&o_gi5);
        patch_slot_vt(&vt[6], (void*)h_gi6,  (void**)&o_gi6);
        patch_slot_vt(&vt[8], (void*)h_gi8s, (void**)&o_gi8s);
        MC_LOG("gameinput: STABLE vtable 0x95cb0=%p slots 5/6/8 instrumented", (void*)vt);
        }
        // THE CLICKER: slot 9 = GetMouseState. Always patch even when the
        // registry knows the table (the dyn chase only patches 3..8).
        if (!o_gi9) {
            patch_slot_vt(&vt[9], (void*)h_gi9, (void**)&o_gi9);
            MC_LOG("gameinput: CLICKER armed on 0x95cb0[9] orig rva 0x%llx",
                   (unsigned long long)((uint8_t*)o_gi9 - (uint8_t*)gi));
        }
        // full-table probe: cover every slot we don't already own
        for (int s2 = 3; s2 <= 11; ++s2) {
            if (s2 == 5 || s2 == 6 || s2 == 8 || s2 == 9) continue;
            void* orig = nullptr; void* det = gip_det(0, s2);
            if (det) { patch_slot_vt(&vt[s2], det, &orig); o_gip[0][s2] = (t_gi8)orig; }
        }
        MC_LOG("gameinput: 0x95cb0 probe slots armed; orig rvas: 5=0x%llx 6=0x%llx 8=0x%llx 9=0x%llx",
               (unsigned long long)((uint8_t*)o_gi5 - (uint8_t*)gi),
               (unsigned long long)((uint8_t*)o_gi6 - (uint8_t*)gi),
               (unsigned long long)((uint8_t*)o_gi8s - (uint8_t*)gi),
               (unsigned long long)((uint8_t*)o_gi9 - (uint8_t*)gi));
    }
    // interface B of the SAME reading object: 0x95c10[9] = thunk to GetMouseState
    // CLICKER v7: restore the ORIGINAL pointers in slot 14 (vtable patching
    // loses to the CFG dispatch) and MinHook the getter body instead.
    if (sdk::readable_ptr(vt, 15*8)) {
        if (!o_gi14) { void* orig = nullptr; patch_slot_vt(&vt[14], (void*)h_gi14, &orig); o_gi14 = (t_gi8)orig; }
        if (o_gi14 && vt[14] == (void*)h_gi14) {
            DWORD op; VirtualProtect(&vt[14], 8, PAGE_EXECUTE_READWRITE, &op);
            vt[14] = (void*)o_gi14;                    // restore original pointer
            VirtualProtect(&vt[14], 8, op, &op);
        }
        // NOTE: the MinHook itself is installed later from install_hooks():
        // MH_CreateHook freezes all threads — doing it during early boot
        // (this runs at ~0s) deadlocks the game.
        MC_LOG("gameinput: v7 slot14 restored to 0x%llx (fn hook pending)",
               (unsigned long long)(uintptr_t)vt[14]);
    }
    void** vtB = (void**)((uint8_t*)gi + 0x95c10);
    if (sdk::readable_ptr(vtB, 12*8)) {
        if (!o_gi9b) {
            patch_slot_vt(&vtB[9], (void*)h_gi9b, (void**)&o_gi9b);
            MC_LOG("gameinput: CLICKER armed on 0x95c10[9] orig rva 0x%llx",
                   (unsigned long long)((uint8_t*)o_gi9b - (uint8_t*)gi));
        }
        for (int s2 = 3; s2 <= 11; ++s2) {
            if (s2 == 9) continue;
            void* orig = nullptr; void* det = gip_det(1, s2);
            if (det) { patch_slot_vt(&vtB[s2], det, &orig); o_gip[1][s2] = (t_gi8)orig; }
        }
        MC_LOG("gameinput: 0x95c10 probe slots armed");
    }
    if (sdk::readable_ptr(vtB, 15*8)) {
        if (!o_gi14b) { void* orig = nullptr; patch_slot_vt(&vtB[14], (void*)h_gi14b, &orig); o_gi14b = (t_gi8)orig; }
        if (o_gi14b && vtB[14] == (void*)h_gi14b) {
            DWORD op; VirtualProtect(&vtB[14], 8, PAGE_EXECUTE_READWRITE, &op);
            vtB[14] = (void*)o_gi14b;
            VirtualProtect(&vtB[14], 8, op, &op);
        }
    }
    // broad sweep: the LIVE singleton (rva 0x95ed8, instance in .data) —
    // patch slots 3..10 with generic arg/edge dumpers; the real reading
    // getter will show itself by its args on the next physical LMB edge
    void** svt = (void**)((uint8_t*)gi + 0x95ed8);
    if (sdk::readable_ptr(svt, 11*8) && !vt_already_patched(svt)) {
        for (int s = 3; s <= 10; ++s) {
            void* orig = nullptr;
            // one shared handler per slot via index (tag logs the slot)
            void* det = nullptr;
            switch (s) {
                case 3: det = (void*)h_gis3; break;
                case 4: det = (void*)h_gis4; break;
                case 5: det = (void*)h_gis5; break;
                case 6: det = (void*)h_gis6; break;
                case 7: det = (void*)h_gis7; break;
                case 8: det = (void*)h_gis8; break;
                case 9: det = (void*)h_gis9; break;
                case 10: det = (void*)h_gis10; break;
            }
            patch_slot_vt(&svt[s], det, &orig);
            o_gis[s - 3] = (t_gi8)orig;
        }
        MC_LOG("gameinput: SINGLETON vtable 0x95ed8=%p slots 3..10 instrumented", (void*)svt);
    }
    static const unsigned heap_rvas[4] = { 0x97998, 0x97ca0, 0x990f0, 0x95898 };
    for (int v = 0; v < 4; ++v) {
        void** hvt = (void**)((uint8_t*)gi + heap_rvas[v]);
        if (!sdk::readable_ptr(hvt, 9*8)) continue;
        if (vt_already_patched(hvt)) continue;
        for (int s2 = 3; s2 <= 8; ++s2) {
            void* orig = nullptr;
            void* det = nullptr;
            switch (v * 10 + s2) {
                case  3: det = (void*)h_gih0_3; break;  case  4: det = (void*)h_gih0_4; break;
                case  5: det = (void*)h_gih0_5; break;  case  6: det = (void*)h_gih0_6; break;
                case  7: det = (void*)h_gih0_7; break;  case  8: det = (void*)h_gih0_8; break;
                case 13: det = (void*)h_gih1_3; break;  case 14: det = (void*)h_gih1_4; break;
                case 15: det = (void*)h_gih1_5; break;  case 16: det = (void*)h_gih1_6; break;
                case 17: det = (void*)h_gih1_7; break;  case 18: det = (void*)h_gih1_8; break;
                case 23: det = (void*)h_gih2_3; break;  case 24: det = (void*)h_gih2_4; break;
                case 25: det = (void*)h_gih2_5; break;  case 26: det = (void*)h_gih2_6; break;
                case 27: det = (void*)h_gih2_7; break;  case 28: det = (void*)h_gih2_8; break;
                case 33: det = (void*)h_gih3_3; break;  case 34: det = (void*)h_gih3_4; break;
                case 35: det = (void*)h_gih3_5; break;  case 36: det = (void*)h_gih3_6; break;
                case 37: det = (void*)h_gih3_7; break;  case 38: det = (void*)h_gih3_8; break;
            }
            patch_slot_vt(&hvt[s2], det, &orig);
            o_gih[v][s2 - 3] = (t_gi8)orig;
        }
        MC_LOG("gameinput: HEAP vtable 0x%x slots 3..8 instrumented", heap_rvas[v]);
    }
}

// ---- GDK reading freeze (moved from overlay.cpp; DORMANT — never called) ----
// The Store build reads mouse/keyboard through the Xbox GameInput runtime.
// LESSONS: failing the reads (E_FAIL) wedged input permanently AND returning
// FALSE from the state getters also poisoned the game. The safe freeze is a
// VALID but EMPTY state (GetMouseState zeroes 24 bytes, GetKeyboardState 12).
// The game-side MouseDevice/Keyboard::feed hooks own the whole freeze today.
static void patch_slot_plain(void** slot, void* detour, void** orig) {
    DWORD op;
    VirtualProtect(slot, 8, PAGE_READWRITE, &op);
    *orig = *slot;
    *slot = detour;
    VirtualProtect(slot, 8, op, &op);
}
// ---- GameInput (GDK) freeze ----
// The Store build reads mouse/keyboard through the Xbox GameInput runtime
// (gameinput.dll; GameInputCreate / GameInputInitialize confirmed in the dump
// strings). GameInput bypasses WM messages and GetAsyncKeyState entirely.
// LESSONS: failing the reads (E_FAIL) wedged input permanently AND returning
// FALSE from the state getters also poisoned the game (it treats a reading
// without mouse/keyboard state as the device going away). The safe freeze is
// to return a VALID but EMPTY state: GetMouseState zeroes the 24-byte
// GameInputMouseState (no buttons, no deltas), GetKeyboardState zeroes
// timestamp+keyCount (no keys). To the game it looks exactly like a player
// not touching anything. Unfreeze is instant.
typedef bool (__stdcall *t_GIR_state)(void*, void*);
static t_GIR_state o_GIR_mouse = nullptr, o_GIR_keyboard = nullptr;
static volatile long g_gir_mouse_calls = 0, g_gir_kb_calls = 0;
static bool __stdcall h_GIR_mouse(void* self, void* out) {
    long n = InterlockedIncrement(&g_gir_mouse_calls);
    if (n <= 3) MC_LOG("gameinput: GetMouseState call #%ld (frozen=%d)", n, (int)input_frozen());
    if (input_frozen()) {
        if (out) memset(out, 0, 24); // GameInputMouseState: ts/buttons/pos/wheels
        return true;
    }
    return o_GIR_mouse(self, out);
}
static bool __stdcall h_GIR_keyboard(void* self, void* out) {
    long n = InterlockedIncrement(&g_gir_kb_calls);
    if (n <= 3) MC_LOG("gameinput: GetKeyboardState call #%ld (frozen=%d)", n, (int)input_frozen());
    if (input_frozen()) {
        if (out) memset(out, 0, 12); // timestamp + keyCount = 0 (no keys pressed)
        return true;
    }
    return o_GIR_keyboard(self, out);
}

void install_gameinput_freeze() {
    for (int attempt = 0; attempt < 30; ++attempt) {
        HMODULE gi = GetModuleHandleW(L"gameinput.dll");
        if (!gi) gi = LoadLibraryW(L"gameinput.dll");
        if (gi) {
            typedef long (__stdcall *t_create)(void**);
            t_create create = (t_create)(void*)GetProcAddress(gi, "GameInputCreate");
            if (!create) { MC_WARN("gameinput: loaded but no GameInputCreate export"); return; }
            void* inst = nullptr;
            if (FAILED(create(&inst)) || !inst) { MC_WARN("gameinput: GameInputCreate failed"); return; }
            void** ivt = *(void***)inst;
            typedef long (__stdcall *t_cur)(void*, uint32_t, void**);
            typedef void  (__stdcall *t_rel)(void*);
            t_cur get_cur = (t_cur)ivt[3];   // IGameInput::GetCurrentInput
            t_rel  rel    = (t_rel)ivt[2];   // IUnknown::Release
            // readings appear once devices are registered; poll for one
            for (int i = 0; i < 60; ++i) {
                void* reading = nullptr;
                if (SUCCEEDED(get_cur(inst, 0x7FFFFFFF /*GameInputKindAny*/, &reading)) && reading) {
                    void** rvt = *(void***)reading;
                    void* orig_mouse = rvt[9], *orig_kb = rvt[8];
                    rel(reading); // release before patching (slot 2 stays untouched)
                    patch_slot_plain(&rvt[8], (void*)h_GIR_keyboard, (void**)&o_GIR_keyboard);
                    patch_slot_plain(&rvt[9], (void*)h_GIR_mouse,    (void**)&o_GIR_mouse);
                    MC_LOG("gameinput: reading vtable %p patched (kb %p -> %p, mouse %p -> %p)",
                           (void*)rvt, orig_kb, (void*)o_GIR_keyboard, orig_mouse, (void*)o_GIR_mouse);
                    return;
                }
                Sleep(500);
            }
            MC_WARN("gameinput: no readings arrived, freeze not installed");
            return;
        }
        Sleep(1000); // runtime may load later than us; retry up to ~30 s
    }
    MC_LOG("gameinput: runtime not present (freeze falls back to WM_INPUT + key hooks)");
}
} // namespace game
} // namespace mc
