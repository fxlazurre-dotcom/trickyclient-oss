#!/bin/bash
# Build TrickyClient-OSS.dll with mingw-w64 (static)
set -e
cd "$(dirname "$0")"
ROOT="$(dirname "$0")/deps"
CXX=x86_64-w64-mingw32-g++
SRC=src
IMGUI=$ROOT/imgui
MH=$ROOT/minhook

CXXFLAGS="-std=c++20 -O2 -shared -static -static-libgcc -static-libstdc++ \
 -I$SRC -I$IMGUI -I$IMGUI/backends -I$MH/include \
 -Wall -Wno-unused-parameter -Wno-format -fpermissive"

SOURCES="$SRC/dllmain.cpp $SRC/overlay.cpp \
 $SRC/core/state.cpp $SRC/core/hooks.cpp $SRC/core/input.cpp $SRC/core/threadbus.cpp \
 $SRC/game/sigs/scan.cpp $SRC/game/sigs/sigs.cpp \
 $SRC/game/sdk/actor/LocalPlayer.cpp $SRC/game/sdk/actor/ecs.cpp \
 $SRC/game/sdk/world/Level.cpp $SRC/game/sdk/client/ClientInstance.cpp \
 $SRC/game/sdk/client/gameinput.cpp $SRC/game/sdk/client/reconsole.cpp \
 $SRC/game/hooks/hooks.cpp $SRC/game/hooks/gamemode.cpp $SRC/game/hooks/level.cpp \
 $SRC/game/hooks/client.cpp $SRC/game/hooks/input.cpp $SRC/game/patches.cpp \
 $SRC/modules/registry.cpp \
 $SRC/modules/combat/killaura.cpp $SRC/modules/combat/autoclicker.cpp $SRC/modules/combat/aimassist.cpp \
 $SRC/modules/movement/speed.cpp $SRC/modules/movement/longjump.cpp $SRC/modules/movement/autosprint.cpp \
 $SRC/modules/misc/tools.cpp \
 $SRC/modules/hud/watermark.cpp $SRC/modules/hud/arraylist.cpp $SRC/modules/hud/info.cpp \
 $SRC/ui/theme.cpp $SRC/ui/widgets.cpp $SRC/ui/fonts.cpp \
 $SRC/ui/clickgui.cpp $SRC/ui/hud_render.cpp $SRC/ui/renderer/icons.cpp \
 $IMGUI/imgui.cpp $IMGUI/imgui_draw.cpp $IMGUI/imgui_tables.cpp $IMGUI/imgui_widgets.cpp \
 $IMGUI/backends/imgui_impl_dx12.cpp $IMGUI/backends/imgui_impl_win32.cpp \
 $MH/src/buffer.c $MH/src/hook.c $MH/src/trampoline.c \
 $MH/src/hde/hde64.c"

echo "[*] compiling..."
$CXX $CXXFLAGS $SOURCES -o TrickyClient-OSS.dll \
  -ld3d12 -ldxgi -ld3dcompiler -ldwmapi -luser32 -lgdi32 -lpsapi -lkernel32 -lwinmm -lole32 -lshell32
echo "[+] built: $(ls -la TrickyClient-OSS.dll)"
x86_64-w64-mingw32-objdump -p TrickyClient-OSS.dll | grep "DLL Name" || true
