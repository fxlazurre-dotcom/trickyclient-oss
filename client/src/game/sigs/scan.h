// ModCore — game layer: image info + AOB pattern scanning (was core.h/main.cpp)
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mc {

// module/image helpers (game exe base/size, captured at bootstrap)
uintptr_t image_base();
void     image_set(uintptr_t base, size_t size);   // dllmain bootstrap
size_t    image_size();

// parse "48 8B ?? ..." into bytes+mask, scan [base, base+size)
struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<bool>    mask; // true = fixed
    bool parse(const char* pat);
};
uintptr_t scan_one(const Pattern& p, const char* name, bool* ok);

} // namespace mc
