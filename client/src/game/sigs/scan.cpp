// ModCore — game layer: pattern scanning (moved verbatim from the monolith)
#include "game/sigs/scan.h"

namespace mc {

// ---- image ----
static uintptr_t g_base = 0;
static size_t    g_size = 0;
uintptr_t image_base() { return g_base; }
size_t    image_size() { return g_size; }
void image_set(uintptr_t base, size_t size) { g_base = base; g_size = size; }

// ---- pattern scanning ----
bool Pattern::parse(const char* pat) {
    bytes.clear(); mask.clear();
    for (const char* s = pat; *s;) {
        while (*s == ' ') s++;
        if (!*s) break;
        char a = *s++;
        if (a == '?') {
            bytes.push_back(0); mask.push_back(false);
            if (*s == '?') s++;
            continue;
        }
        char b = *s++;
        auto hx = [](char c)->int {
            if (c >= '0' && c <= '9') return c-'0';
            if (c >= 'A' && c <= 'F') return c-'A'+10;
            if (c >= 'a' && c <= 'f') return c-'a'+10;
            return -1; };
        int hi = hx(a), lo = hx(b);
        if (hi < 0 || lo < 0) return false;
        bytes.push_back((uint8_t)((hi<<4)|lo)); mask.push_back(true);
    }
    return !bytes.empty();
}

uintptr_t scan_one(const Pattern& p, const char* name, bool* ok) {
    (void)name;
    *ok = false;
    const uint8_t* lo = (const uint8_t*)g_base;
    const uint8_t* hi = lo + g_size;
    const size_t n = p.bytes.size();
    const uint8_t first = p.bytes[0];
    bool firstFixed = p.mask[0];
    for (const uint8_t* q = lo; q + n <= hi; ++q) {
        if (firstFixed && q[0] != first) continue;
        size_t i = 1;
        for (; i < n; ++i)
            if (p.mask[i] && q[i] != p.bytes[i]) break;
        if (i == n) { *ok = true; return (uintptr_t)q; }
    }
    return 0;
}

} // namespace mc
