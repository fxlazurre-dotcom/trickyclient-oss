// ModCore — module registry: the category list, the tick dispatcher (bus
// subscriber), enable/toggle, ArrayList names and ini persistence.
#pragma once
#include <mutex>
#include <string>
#include <vector>
#include "modules/module.h"

namespace mc {

std::mutex& modules_mtx();                 // guards Module fields while on_tick runs
Module* const* modules_all(int* n);        // the static table (stable order)
Module* mod_find(const char* name);
const char* const* modules_categories(int* n);   // GUI column order

void modules_init();                       // build + load ini + subscribe + workers
void modules_shutdown();                   // save ini (best effort)
void modules_toggle(Module* m);            // flip (toggle_state or enabled)
void modules_mark_dirty();                 // a setting changed -> save later
bool modules_hud_any();                    // any HUD element enabled
std::vector<std::string> modules_arraylist();     // enabled+listed names (row order)

// one Level::tick: dispatch to every enabled module (bus subscriber)
void modules_dispatch_tick(void* level);

} // namespace mc
