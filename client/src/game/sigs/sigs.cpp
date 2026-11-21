// ModCore — game layer: signature resolution (moved verbatim from the monolith)
#include "game/sigs/sigs.h"
#include "game/sigs/scan.h"
#include "core/log.h"
#include "sigs_gen.h"            // generated SIG_ENTRIES (see WORKFLOW.md)

#include <string>
#include <vector>

namespace mc {

struct SigRes { std::string name; uintptr_t addr; bool ok; };
static std::vector<SigRes> g_sigres;

uintptr_t sig_get(const char* name) {
    for (auto& s : g_sigres) if (s.name == name) return s.addr;
    return 0;
}

void resolve_all_signatures() {
    for (int i = 0; i < SIG_COUNT; ++i) {
        SigRes r{SIG_ENTRIES[i].name, 0, false};
        Pattern p;
        if (p.parse(SIG_ENTRIES[i].pattern)) {
            bool ok = false;
            r.addr = scan_one(p, r.name.c_str(), &ok);
            // body-anchored sigs: shift the match back to the function entry
            r.addr -= SIG_ENTRIES[i].delta;
            r.ok = ok;
        }
        g_sigres.push_back(r);
        MC_LOG("SIG %s %s rva=0x%llx", r.name.c_str(), r.ok ? "OK" : "FAIL",
               (unsigned long long)(r.ok ? r.addr - image_base() : 0));
    }
}

int         sig_count_()               { return (int)g_sigres.size(); }
const char* sig_name_(int i)           { return g_sigres[i].name.c_str(); }
uintptr_t   sig_addr_(int i)           { return g_sigres[i].addr; }
bool        sig_ok_(int i)             { return g_sigres[i].ok; }

} // namespace mc
