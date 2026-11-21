// ModCore — game SDK: the RE console (poll runtime/re.cmd, execute, log).
// commands (one per line): "q ADDR N" dump N qwords | "mod ADDR" resolve module
// | "vt ADDR" dump vtable | "byte ADDR N" dump bytes | "str ADDR" read string
// Started by the GameInput worker once instrumentation is done.
#include "game/sdk/SDK.hpp"
#include "game/sdk/client/reconsole.h"
#include "core/log.h"
#include <windows.h>
#include <cstring>
#include <cstdio>

namespace mc {
namespace game {
// ---- RE console: poll runtime/re.cmd, execute, log to mcmod.log -------
// commands (one per line): "q ADDR N" dump N qwords | "mod ADDR" resolve module
// | "vt ADDR" dump vtable | "byte ADDR N" dump bytes | "str ADDR" read string
static void re_exec_line(const char* line) {
    char cmd[16] = {0}; unsigned long long a = 0; int n = 8;
    if (sscanf(line, "%15s %llx %d", cmd, &a, (int*)&n) < 2 || !a) return;
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
    } else if (!strcmp(cmd, "vt")) {
        if (a < 0x10000 || !sdk::readable_ptr((void*)a, 8)) { MC_LOG("[re] vt %llx UNREADABLE", a); return; }
        void** v = *(void***)a;
        if (!v || !sdk::readable_ptr(v, 16*8)) { MC_LOG("[re] vt %llx: bad vtable %p", a, v); return; }
        MC_LOG("[re] vt obj=%llx vtable=%llx", a, (unsigned long long)v);
        for (int i = 0; i < 16; ++i)
            MC_LOG("[re] vt [%2d] = %llx", i, (unsigned long long)v[i]);
    } else if (!strcmp(cmd, "str")) {
        if (a < 0x10000 || !sdk::readable_ptr((void*)a, 8)) { MC_LOG("[re] str %llx UNREADABLE", a); return; }
        MC_LOG("[re] str %llx: %s", a, (const char*)a);
    }
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
                re_exec_line(ln);
            }
        }
        CloseHandle(f);
    }
}
} // namespace game
} // namespace mc
