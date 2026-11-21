// ModCore overrides for Dear ImGui config: log asserts instead of dialog boxes
#pragma once
#ifndef IMGUI_DISABLE
#include "log.h"
#undef IM_ASSERT
#include <cstdlib>
#define IM_ASSERT(EXPR)                                                        \
    do {                                                                       \
        if (!(EXPR)) {                                                         \
            mc::Logger::inst().logf("IASSERT", "imgui assert failed: %s (file %s, line %d)", \
                                    #EXPR, __FILE__, __LINE__);                \
            abort();                                                           \
        }                                                                      \
    } while (0)
#endif
