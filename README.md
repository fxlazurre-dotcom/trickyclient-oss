# TrickyClient OSS

An open-source ImGui client base (mod-loader framework) for **Minecraft Bedrock 1.26.52.3** (Windows x64).
Signature database + function hooks + nursultan-style in-game GUI with a two-color theme system (INSERT) + working modules + HUD.

> ⚠️ **Development platform note**
> This client was developed and built **on Linux** (Arch, mingw-w64). The game was run
> through **Wine/Proton** (bedrock-on-linux, GDK-Proton) and injected with the stock
> bedrock-on-linux injector. Everything is platform-neutral Win32 + D3D12 + ImGui +
> MinHook, so native **Windows** should work as-is — but the load/injection chain has
> not been tested there and may need rework for a Windows launcher/injector.

## Features

### Modules (all live-tested in 1.26.52.3)

- **AutoClicker** — CPS + jitter. Hooks the GameInput mouse-state getter
  (`GameInputRedist!0x3b940`) with MinHook and applies the CPS phase to
  `GameInputMouseLeft` in the button bitmask after the original read: real,
  in-process clicks that keep working while moving
- **KillAura** — hits every entity inside the radius slider. Actor enumeration via
  `Level::getRuntimeActorList`, positions through the Actor-ECS StateVector
  component, attacks through the real `GameModeSub` derived-vtable guard
  (`vt[15]`, rva `0x25a0570`); the GameMode context is auto-found at
  `player+0xAA0`. Self-filters: pointer, vtable class, ECS component,
  minimum distance
- **AimAssist** — yaw-only magnetism (v8.6). Target scan in the `Level::tick`
  hook (chest aim point, sanity-gated, smoothed, sticky target), and the pull
  rides the mouse-getter hook: after the original memcpy our own accumulator
  `A` is added on top of the out-copy, so the game's input delta is always
  `hw_delta + rate` — real mouse motion passes through untouched and nothing
  ever snaps back. Sliders: range / speed (rate cap) / strength (pull
  hardness). Full stop inside 0.4 of the target's hitbox angular size with a
  soft fade (no dead-center lock)
- **AutoSprint** — injects sprint-key taps while walking
- **Speed / LongJump** — example velocity-write modules
- **BlockESP** — ore wallhack (coal / iron / copper / gold / redstone / lapis /
  diamond / emerald / nether, deepslate variants included). Blocks come from a
  traffic-latched `BlockSource` view (deep-validated against the player's own
  chunk + feet column through the original `getBlock` trampoline); ores are
  classified by block NAME with a per-type pointer cache, so id renumbering
  between point releases cannot break it. Scanning is a column cache: each XZ
  column is read once full-Y and then frozen — near columns (<=20 blocks)
  refresh every 45 s to catch mining, far ones cost nothing until you walk up.
  Rendering uses the engine frame transform captured by the
  `ScreenView::setupAndRender` hook (view matrix + camera pos + fov): one
  center transform per box with shared-subexpression corners (~10x cheaper
  than 8 full matrix multiplies), subpixel and behind-camera skips, and a
  diagonal-cross LOD for tiny boxes

### HUD

Always-on overlay elements (in-world, in the game's main menu, and behind the
GUI), toggled in the **HUD** category — a 1:1 port of the nursultan-style
canvas HUD (logical-px geometry at gui scale 2, Montserrat SemiBold):
- **Watermark** — rounded panel, gradient client name (accent -> lightened
  accent), live `N fps` suffix
- **ArrayList** — enabled modules, width-sorted, stepped rounded background,
  animated sine-flow color wave across the rows (accent1 -> accent2), gradient
  side line with glow. Renders nothing while no module is on
- **Info** — `XYZ` / `CPS` text lines (accent label + white value + shadow),
  bottom-left by default; `--` placeholders until real telemetry arrives
- Every element is **draggable** while the GUI is open (dashed outline hint),
  with per-element size and opacity sliders

### GUI & tooling

- **In-game GUI** (INSERT): the nursultan-style ClickGUI — centered category
  columns (Combat / Movement / Misc / HUD) with gradient-filled module rows,
  press-flash, hover scaleX/opacity animations, inline expandable settings
  (checkboxes, sliders, enum chips), a module search bar, and a two-color
  gradient picker (SV square + hue strip + hex) that drives the whole theme:
  GUI accents, badges, HUD gradients and the ArrayList wave all derive from
  the two theme colors. Open/close scale+fade animation. Misc keeps the debug
  toolset (log filter/depth, copy-log, signature + log-tail viewer).
  ImGui + DX12 overlay; the D3D12 command queue is picked by temporal
  correlation with Present
- **RE console**: commands via `runtime/re.cmd` (memory dump, vtables,
  strings, snapshots/diffs), output to the log — the tool that produced most
  of the offsets above
- **AOB signature database** (`signatures/signatures.toml`, 80 entries,
  consumed through the GENERATED `client/src/game/sigs/sigs_gen.h`; all 80
  re-verified on a clean-process memory dump, 2026-11-21),
  every one validated with `sigtool check` (exactly one match, verified
  RVAs): full GameMode virtual set, the Level family, Actor-ECS accessors,
  Player/camera turn chain (`Player::applyTurnDelta` = player `vt[19]`,
  the CameraSetPlayerRot handler), GameInput ring internals, anticheat
  choke points
- **Diagnostic log** (`~/mc-modloader/runtime/mcmod.log` through Wine `Z:`)
- Double-injection guard (named mutex)

### Hooks (MinHook)

`GameMode::attack / startDestroyBlock / tick`, `GameModeSub::attack` guard,
`Level::tick`, `ClientInstance::onPlayerLoaded / grabCursor`,
`MouseDevice::feed`, `Keyboard::feed`, and the GameInput mouse-state getter
body (clicker + aim channel). The `useItem / useItemOn / interact` slots are
intentionally NOT hooked — their ABI mismatches this build (right-click
crash).

## Building

```bash
cd client
./build.sh        # -> TrickyClient-OSS.dll (mingw-w64, statically linked)
```

Vendored dependencies: `vendor/imgui` (v1.91.5, MIT), `vendor/minhook` (BSD).
A custom `imconfig.h` sits in `client/src/` (ImGui asserts go to the log instead of
dialog boxes).

## Injection

- **Linux/Wine (as developed)**: `client/enable_inject.sh` wires the DLL into the
  bedrock-on-linux injector (backs up settings.json), or manually:
  `wine injector.exe 'Z:\<path>\TrickyClient-OSS.dll' Minecraft.Windows.exe`
- **Windows**: any LoadLibrary-based injector into `Minecraft.Windows.exe`

## Architecture

Layered with one-way dependencies — `ui/ -> modules/ -> game/ -> core/`;
nothing below knows about the layers above:

- **core/** — logging, shared client state (theme, menu, input freeze) and the
  **thread bus**: the channels between the game thread, the input hooks and
  the render thread (telemetry, aim pull, clicker config, CPS ring, level-tick
  subscribers). Telemetry can no longer depend on which module happens to be
  enabled — that bug class is structurally gone
- **game/** — pattern scanning + signature resolution, the SDK (offsets marked
  `VERIFIED` / `GUESS`, local-player probe, Actor-ECS access, Level,
  ClientInstance, GameInput diagnostics, the RE console), hook bodies and the
  dormant anticheat byte patches
- **modules/** — one file per module implementing a tiny interface (`Module`
  with generic `Setting` rows + `on_tick` / `on_hud` callbacks). The registry
  drives the GUI columns and the HUD ArrayList, so a new module is one file
  plus one line — no GUI or state edits
- **ui/** — the nursultan-style ClickGUI (renders ANY module generically from
  its settings) and the HUD; HUD modules draw through an opaque render
  context, so the modules layer never includes ImGui

Module settings, theme colors and HUD element positions persist to
`tricky.ini` (settings only — module enable states are deliberately not
restored, modules must not silently re-arm after a restart).

## Layout

```
client/
├── src/
│   ├── dllmain.cpp     # init order: image -> signatures -> modules -> hooks -> overlay
│   ├── overlay.cpp     # ImGui/DX12 overlay: Present hook, frame, cursor management
│   ├── core/           # log, state, input, hook plumbing, thread bus
│   ├── game/
│   │   ├── sigs/       # scan + resolve; sigs_gen.h is GENERATED from signatures.toml
│   │   ├── sdk/        # VERIFIED/GUESS-marked game wrappers (actor, world, client)
│   │   ├── hooks/      # hook bodies: GameMode, Level, ClientInstance, input pipeline
│   │   └── patches.*   # anticheat byte patches (dormant)
│   ├── modules/        # combat/ movement/ misc/ hud/ + module.h + registry
│   ├── ui/             # theme, widgets, fonts, ClickGUI, HUD render, icons
│   └── imconfig.h      # ImGui asserts -> log file (vendored imgui has none)
├── build.sh / enable_inject.sh
└── vendor/             # imgui, minhook
signatures/
├── signatures.toml     # pattern + rel/read, verified on 1.26.52.3
└── *.json              # inventory, vtable slot maps, evidence dumps
```

## Update workflow for a new game version

1. Dump the module image from memory (the exe is MSIXVC-encrypted on disk)
2. `sigtool check signatures/signatures.toml <new_dump> --mapped`
3. Regenerate broken signatures (`sigtool suggest`) and rebuild
   `client/src/game/sigs/sigs_gen.h`
4. Rebuild the client

## Subsystem status

| Subsystem | Status |
|---|---|
| Signatures (80) | ✅ 80/80 on 1.26.52.3 (re-verified on a clean dump) |
| GUI / overlay | ✅ stable (targets released on the Present thread — resize-safe) |
| HUD (watermark + info) | ✅ working |
| AutoClicker | ✅ working (in-process, motion-safe) |
| KillAura | ✅ working |
| AimAssist | ✅ working (yaw magnetism, mouse-through) |
| Speed / LongJump | ✅ working (velocity writes) |
| Swing animation | 🔬 open (game's own call invoked, not visible yet) |
| Client anticheat | 🔬 research (MovePlayerPacket drop; system stubs corrupt state) |

## Credits

- [BedrockDynamicSDK](https://github.com/gamelist1990/BedrockDynamicSDK) — signature
  seeds and the Actor-ECS layout for 1.26.52.3
- [LeviLamina](https://github.com/LiteLDev/LeviLamina) — header reference
- [Dear ImGui](https://github.com/ocornut/imgui), [MinHook](https://github.com/TsudaKageyu/minhook)

## Disclaimer

Single-player / private-world modding only. Not for bypassing server-side
anticheat online. Minecraft is a trademark of Mojang/Microsoft; this project is
not affiliated.
