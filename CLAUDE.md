# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

A Fallout 4 F4SE plugin (C++ DLL) that scans a live save's dynamically injected Leveled Items and exports them as RobCo Patcher `.ini` files. The mod lets players migrate away from script-injected leveled list mods (which bloat saves) to static RobCo Patcher configurations.

The plugin has two parts:
- **C++ DLL** (`src/LLMigrator.cpp`): Iterates `TESDataHandler`'s `TESLevItem` array, collecting `scriptAddedLists` entries, and writes `.ini`/`.csv` output to `Data/F4SE/Plugins/RobCo_Patcher/leveledList/<folder>/`.
- **Papyrus scripts** (`src/RobCoMigrator.psc`): Binds to the C++ native functions and drives the MCM UI buttons. `src/MCM.psc` is a stub for local compilation only.

## Build

**The DLL must be built on Windows** (MSVC, cross-compilation is not supported).

### Local build via Windows VM (preferred)

```bash
./build_on_virtual_machine.sh   # builds DLL on VM → build/Release/RobCoMigrator.dll
./build_mod.sh                  # compiles Papyrus + packages → RobCo Migrator.zip
# or both at once:
./build_all.sh
```

`build_on_virtual_machine.sh` SSHes to `Builder@192.168.122.204`, syncs `src/` and CMake files on every run, and syncs `lib/CommonLibF4` only on the first run. The DLL is fetched to `build/Release/` (so `build_mod.sh` finds it) and `build_output/`. Set the optional `MOD_DIR` variable in that script to auto-install into an MO2 mod folder after each build.

### CI (GitHub Actions)

Triggers on push/PR to `main`. Uses `windows-latest` with vcpkg and sccache.

### Manual Windows build

```
set VCPKG_ROOT=C:\vcpkg
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build --config Release
```

**Papyrus compilation** requires [Caprica](https://github.com/nikitalita/Caprica) on PATH:
```bash
cd src
caprica.sh RobCoMigrator.psc
```
`MCM.psc` is a stub; the real `MCM.pex` ships with Mod Configuration Menu.

**Full packaging** (DLL + scripts + MCM config → `RobCo Migrator.zip`):
```bash
./build_mod.sh
```

## Architecture

### C++ Layer (`src/`)
- `PCH.hpp`: Precompiled header — pulls in F4SE and `Common.hpp`.
- `Common.hpp`: Currently empty; reserved for shared helpers.
- `LLMigrator.hpp/.cpp`: All logic lives here under `namespace RobCoMigrator`.
  - `GetRobCoID()`: Converts a `TESForm*` to `PluginName|LocalID` format. ESL (light) plugins use 12-bit IDs (`0xFFF` mask); standard plugins use 24-bit (`0xFFFFFF` mask).
  - `GeneratePatch()`: Core scan — iterates all `TESLevItem` forms, collects `scriptAddedLists`, and writes output. Three export modes: `CREATE_NEW` (timestamped new file), `OVERWRITE` (backup old as `.old`), `MERGE` (append only new entries).
  - `GetInjectedLists()` + Papyrus `Revert()`: Hands raw form pointers to Papyrus, which calls `LeveledItem.Revert()` to purge dynamic injections from RAM.
  - `g_bSafeToRevert` / `g_bUserUnlockedRevert`: Two-step safety lock — patch must be generated before revert is permitted; user must also toggle the MCM checkbox.
- `main.cpp`: F4SE entry point. Registers the Papyrus namespace via `F4SE::GetPapyrusInterface()->Register()`.

### Papyrus Layer (`src/`)
- `RobCoMigrator.psc`: Declares native bindings and implements the MCM button logic. All functions are `global`.
- `MCM.psc`: Compilation stub only; not shipped.
- `config.json`: MCM layout (Mod Configuration Menu format).

### Library (`lib/CommonLibF4`)
Git submodule. Provides `RE::` and `F4SE::` namespaces (Fallout 4 reverse-engineered API). Configured with:
- `COMMONLIB_RUNTIME_OG/NG/AE = ON` (all non-VR runtimes)
- `COMMONLIB_OPTION_FMT = ON` (ships `{fmt}`)
- vcpkg also provides `spdlog` and `fmt` (see `vcpkg.json`)

### MCM config.json Format

`src/config.json` defines the Mod Configuration Menu layout. Key rules:

- **`text` elements**: plain text only by default. Add `"html": true` to enable `<b>`, `<i>`, `<u>`, `<font color='#RRGGBB'>`, and `<font size='N'>`. Use grey (`#AAAAAA`) for description/context text.
- **`section` elements**: always render HTML without needing `"html": true`. Use `<font size='20'><b>Header</b></font>` for sized section headers.
- Use `"type": "spacer"` (optionally with `"numLines": N`) for vertical spacing.
- **`switcher`** (ON/OFF toggle): use `"valueOptions": { "sourceType": "ModSettingBool" }` with an `"id"` of format `"keyName:SectionName"`. MCM persists the bool in `Data/MCM/Settings/<modName>.ini`. Read it from Papyrus via `MCM.GetModSettingBool(modName, "keyName:Section")` and write via `MCM.SetModSettingBool(...)`. No ESP required.
- Button `action` must specify `"type": "CallGlobalFunction"` with `"script"` (Papyrus script name) and `"function"` (global function name).
- `"id"` on a button allows enabling/disabling it from Papyrus via `MCM.SetEntryEnabled()`.

Supported types: `section`, `text`, `spacer`/`empty`, `button`, `toggle`/`switcher`, `slider`, `stepper`, `dropdown`, `hotkey`, `image`.

### Known Quirks
- `localtime_s` is MSVC-only; this code will not compile on GCC/Clang without a shim.
- `MCM.psc` stub exists only so Caprica can resolve the `MCM` dependency when compiling `RobCoMigrator.psc` locally. Do not modify it.
- The MSVC preprocessor bug in `lib/CommonLibF4/include/REX/W32/CORE.hpp` (`#if _INC_WINAPIFAMILY != 0` → `#if !defined(_INC_WINAPIFAMILY)`) is patched in CI automatically (see `build.yml`).
