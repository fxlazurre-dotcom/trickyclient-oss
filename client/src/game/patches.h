// ModCore — game layer: AC byte patches (dormant)
#pragma once
namespace mc {
void patches_prepare();   // resolve the 6 patch targets from signatures
void set_ac_patches();     // apply/remove per ModuleState::ac_* toggles
}
