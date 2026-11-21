// ModCore — game SDK: GameInputRedist diagnostics + instrumentation
#pragma once
namespace mc {
namespace game {
void gameinput_probe();        // read-only: vtables of LIVE objects -> log
void gameinput_instrument();    // patch the known proxy vtables (probe + slot clickers)
void install_gameinput_freeze();// DORMANT: never called (kept from the monolith)
}
}
