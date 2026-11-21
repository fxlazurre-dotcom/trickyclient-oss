// ModCore — game SDK: the RE console v2 (poll runtime/re.cmd, execute, log).
// Commands (one per line, all addresses hex):
//   q ADDR [N]     dump N qwords (<=64)
//   f ADDR [N]     dump N floats
//   vt ADDR        dump the object's vtable (16 slots)
//   str ADDR       read a C string
//   mod ADDR       which module owns the address
//   dis ADDR [N]   walk N instructions with HDE (dynamic disasm)
//   stop           suspend every other thread (world frozen — console alive)
//   go             resume the frozen threads
//   blk X Y Z      getBlock probe on the found BlockSource + type name
//   hit            dump the Level's HitResult (crosshair target, +0x1E8)
#include "game/sdk/SDK.hpp"
#include "game/sdk/client/reconsole.h"
#include "game/sdk/actor/LocalPlayer.hpp"
#include "game/sdk/world/BlockSource.hpp"
#include "core/log.h"
#include "hde64.h"

#include <windows.h>
#include <tlhelp32.h>
#include <cstring>
#include <cstdio>

namespace mc {
namespace game {

static HANDLE g_frozen[512];
static DWORD  g_n_frozen = 0;

static void cmd_stop() {
    if (g_n_frozen) { MC_LOG("[re] already stopped (%lu threads)", g_n_frozen); return; }
    DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) { MC_LOG("[re] stop: snapshot failed"); return; }
    THREADENTRY32 te{ sizeof(te) };
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
            if (g_n_frozen >= 512) break;
            if (HANDLE t = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID)) {
                if (SuspendThread(t) != (DWORD)-1) g_frozen[g_n_frozen++] = t;
                else CloseHandle(t);
            }
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    MC_LOG("[re] STOP: %lu threads frozen (go to resume)", g_n_frozen);
}

static void cmd_go() {
    for (DWORD i = 0; i < g_n_frozen; ++i) { ResumeThread(g_frozen[i]); CloseHandle(g_frozen[i]); }
    MC_LOG("[re] GO: %lu threads resumed", g_n_frozen);
    g_n_frozen = 0;
}

static void cmd_dis(uintptr_t a, int n) {
    HMODULE exe = GetModuleHandleW(nullptr);
    for (int i = 0; i < n; ++i) {
        if (!sdk::readable_ptr((void*)a, 16)) { MC_LOG("[re] dis 0x%llx UNREADABLE", (unsigned long long)a); return; }
        uint8_t buf[32] = {};
        memcpy(buf, (void*)a, 16);
        hde64s ins;
        unsigned len = hde64_disasm(buf, &ins);
        if (!len) { MC_LOG("[re] dis 0x%llx: bad insn %02x", (unsigned long long)a, buf[0]); return; }
        char line[128];
        int off = snprintf(line, sizeof line, "rva 0x%-8llx (%2u)", (unsigned long long)(a - (uintptr_t)exe), len);
        for (unsigned k = 0; k < len && k < 11; ++k) off += snprintf(line + off, sizeof(line) - off, "%02x ", buf[k]);
        MC_LOG("[dis] %s", line);
        a += len;
    }
}

static void cmd_blk(long long x, long long y, long long z) {
    void* bs = sdk::block_source_find(sdk::last_level());
    if (!bs) { MC_LOG("[blk] no BlockSource yet"); return; }
    void* blk = sdk::block_source_get(bs, (int)x, (int)y, (int)z);
    if (!blk) { MC_LOG("[blk] (%lld,%lld,%lld) -> null", x, y, z); return; }
    void* t = sdk::block_type_of(blk);
    MC_LOG("[blk] (%lld,%lld,%lld) block=%p type=%p name=\"%s\"", x, y, z, blk, t,
           t ? sdk::block_type_name(t) : "?");
}

static void cmd_hit() {
    void* level = sdk::last_level();
    if (!level || !sdk::readable_ptr((uint8_t*)level, 0x200)) { MC_LOG("[hit] no level"); return; }
    void* hr = *(void**)((uint8_t*)level + 0x1E8);   // Level_getHitResult (VERIFIED)
    if (!hr || !sdk::readable_ptr((uint8_t*)hr, 64)) { MC_LOG("[hit] level+0x1E8 -> %p unreadable", hr); return; }
    uint64_t* q = (uint64_t*)hr;
    MC_LOG("[hit] HitResult=%p", hr);
    for (int row = 0; row < 3; ++row)
        MC_LOG("[hit] +%02x0: %016llx %016llx %016llx %016llx",
               row, (unsigned long long)q[row*4+0], (unsigned long long)q[row*4+1],
               (unsigned long long)q[row*4+2], (unsigned long long)q[row*4+3]);
    // BDS layout: type@24, face@28, block@32 (block coords), pos@44 (Vec3)
    int32_t* i32 = (int32_t*)hr;
    float* f32 = (float*)hr;
    MC_LOG("[hit] type=%d face=%d block=(%d,%d,%d) pos=(%.2f,%.2f,%.2f)",
           i32[6], i32[7], i32[8], i32[9], i32[10], f32[11], f32[12], f32[13]);
}

static void re_exec_line(const char* line) {
    char cmd[16] = {0}; unsigned long long a = 0; int n = 8; (void)n;
    if (sscanf(line, "%15s %llx %d", cmd, &a, &n) < 2 || !a) {
        // commands without an address
        if (sscanf(line, "%15s", cmd) == 1) {
            if (!strcmp(cmd, "stop")) { cmd_stop(); return; }
            if (!strcmp(cmd, "go"))   { cmd_go(); return; }
            if (!strcmp(cmd, "hit"))  { cmd_hit(); return; }
        }
        return;
    }
    if (!strcmp(cmd, "mod")) {
        HMODULE hm = nullptr;
        BOOL ok = GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                     GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                     (LPCSTR)a, &hm);
        char pth[MAX_PATH] = {0};
        if (ok && hm) GetModuleFileNameA(hm, pth, MAX_PATH);
        MC_LOG("[re] mod %llx -> %s base=%llx", a, (ok && hm) ? pth : "(no module)",
               (unsigned long long)hm);
    } else if (!strcmp(cmd, "q")) {
        if (a < 0x10000 || !sdk::readable_ptr((void*)a, (size_t)(n > 64 ? 64 : n) * 8)) {
            MC_LOG("[re] q %llx UNREADABLE", a); return;
        }
        uint64_t* q = (uint64_t*)a;
        int mx = n > 64 ? 64 : n;
        for (int i = 0; i < mx; ++i)
            MC_LOG("[re] q %llx +%03x = %016llx", a, i * 8, (unsigned long long)q[i]);
    } else if (!strcmp(cmd, "f")) {
        if (a < 0x10000 || !sdk::readable_ptr((void*)a, (size_t)(n > 64 ? 64 : n) * 4)) {
            MC_LOG("[re] f %llx UNREADABLE", a); return;
        }
        float* f = (float*)a;
        int mx = n > 64 ? 64 : n;
        for (int i = 0; i < mx; i += 4)
            MC_LOG("[re] f %llx +%02x = %.3f %.3f %.3f %.3f", a, i * 4,
                   f[i], i + 1 < mx ? f[i+1] : 0, i + 2 < mx ? f[i+2] : 0, i + 3 < mx ? f[i+3] : 0);
    } else if (!strcmp(cmd, "vt")) {
        if (a < 0x10000 || !sdk::readable_ptr((void*)a, 8)) { MC_LOG("[re] vt %llx UNREADABLE", a); return; }
        void** v = *(void***)a;
        if (!v || !sdk::readable_ptr(v, 16*8)) { MC_LOG("[re] vt %llx: bad vtable %p", a, v); return; }
        HMODULE exe = GetModuleHandleW(nullptr);
        MC_LOG("[re] vt obj=%llx vtable=%llx (rva 0x%llx)", a, (unsigned long long)(uintptr_t)v,
               (unsigned long long)((uintptr_t)v - (uintptr_t)exe));
        for (int i = 0; i < 16; ++i)
            MC_LOG("[re] vt [%2d] = %llx (rva 0x%llx)", i, (unsigned long long)(uintptr_t)v[i],
                   (unsigned long long)((uintptr_t)v[i] - (uintptr_t)exe));
    } else if (!strcmp(cmd, "str")) {
        if (a < 0x10000 || !sdk::readable_ptr((void*)a, 8)) { MC_LOG("[re] str %llx UNREADABLE", a); return; }
        MC_LOG("[re] str %llx: %s", a, (const char*)a);
    } else if (!strcmp(cmd, "dis")) {
        cmd_dis((uintptr_t)a, n < 1 ? 8 : n);
    }
}

// blk takes three ints — parse it separately before the generic path
static void re_exec_line2(const char* line) {
    char cmd[16] = {0};
    if (sscanf(line, "%15s", cmd) != 1) return;
    if (!strcmp(cmd, "blk")) {
        long long x = 0, y = 0, z = 0;
        unsigned long long bs = 0;
        if (sscanf(line, "%*s %llx %lld %lld %lld", &bs, &x, &y, &z) == 4) {
            void* blk = sdk::block_source_get((void*)bs, (int)x, (int)y, (int)z);
            void* ty = blk ? sdk::block_type_of(blk) : nullptr;
            MC_LOG("[blk] bs=%llx (%lld,%lld,%lld) block=%p type=%p name=\"%s\"",
                   bs, x, y, z, blk, ty, ty ? sdk::block_type_name(ty) : "?");
        } else if (sscanf(line, "%*s %lld %lld %lld", &x, &y, &z) == 3)
            cmd_blk(x, y, z);
        else
            MC_LOG("[blk] usage: blk X Y Z | blk BSHEX X Y Z");
        return;
    }
    re_exec_line(line);
}

void re_console_thread() {
    for (;;) {
        Sleep(250);
        HANDLE f = CreateFileW(L"Z:\\home\\florixx\\mc-modloader\\runtime\\re.cmd",
                               GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) continue;
        char buf[8192]; DWORD rd = 0;
        LARGE_INTEGER sz{};
        GetFileSizeEx(f, &sz);
        if (sz.QuadPart > 0 && ReadFile(f, buf, sizeof(buf) - 1, &rd, nullptr) && rd > 0) {
            buf[rd] = 0;
            SetFilePointer(f, 0, nullptr, FILE_BEGIN); SetEndOfFile(f);   // truncate
            char* ctx = nullptr;
            for (char* ln = strtok_r(buf, "\r\n", &ctx); ln; ln = strtok_r(nullptr, "\r\n", &ctx)) {
                MC_LOG("[re] >>> %s", ln);
                re_exec_line2(ln);
            }
        }
        CloseHandle(f);
    }
}

} // namespace game
} // namespace mc
