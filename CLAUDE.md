# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Important Note

The project maintainers do not accept contributions that are primarily AI-generated. Keep any AI-assisted changes minimal and well-reasoned, or disclose AI involvement clearly.

## Build Commands

Prerequisites: CMake 3.25+, Python 3+, Ninja. Initialize submodules before first build:
```sh
git submodule update --init --recursive
```

**Linux (GCC, RelWithDebInfo — primary development target):**
```sh
cmake --preset linux-default-relwithdebinfo
cmake --build --preset linux-default-relwithdebinfo
```

Other Linux presets: `linux-default-debug`, `linux-clang-relwithdebinfo`, `linux-clang-debug`

**macOS:**
```sh
cmake --preset macos-default-relwithdebinfo
cmake --build --preset macos-default-relwithdebinfo
```

**Windows:**
```sh
cmake --preset windows-msvc-relwithdebinfo
cmake --build --preset windows-msvc-relwithdebinfo
```

**Running:**
```sh
build/{preset}/dusk /path/to/game.rvz
```
Supported disc formats: ISO (GCM), RVZ, WIA, WBFS, CISO, GCZ. Defaults to `game.iso` in CWD if no path given.

There are no unit tests; correctness is validated against the original game.

## Code Style

Formatting is enforced via `.clang-format` (run `clang-format -i` on changed files). Key rules:
- 4-space indentation, no tabs
- 100-column limit
- Pointer/reference alignment: left (`int* p`, not `int *p`)
- `Standard: C++03` style formatting (though the codebase uses C++20 features)
- `SortIncludes: true` — includes are sorted automatically

## Architecture

Dusk is a layered C/C++ application. From bottom to top:

**`libs/` — GameCube/Wii platform layer**
- `libs/JSystem/` — Nintendo's J-System library (file I/O, math, collections)
- `libs/PowerPC_EABI_Support/` — ABI support from the original toolchain
- `libs/TRK_MINNOW_DOLPHIN/` — CodeWarrior debugger stub
- `libs/freeverb/` — Audio reverb DSP
- `libs/dolphin/`, `libs/revolution/` — GC/Wii hardware abstraction

**`src/` — Reverse-engineered game code** (mirrors the original GC binary structure)
- `src/m_Do/` — Dolphin OS wrappers (threads, memory, math)
- `src/d/` — Core game logic (largest component; actors, collision, rendering, UI)
- `src/f_ap/`, `src/f_op/`, `src/f_pc/` — Frame, object, and process management
- `src/c/` — Shared utilities
- `src/SSystem/` — System-level services

**`src/dusk/` — Dusk-specific layer** (platform integration and modern features)
- `main.cpp` — Application entry point
- `config.cpp` / `settings.cpp` — Persistent user configuration
- `achievements.cpp` — RetroAchievements integration
- `discord.cpp` — Discord Rich Presence
- `frame_interpolation.cpp` — Frame rate enhancement
- `OSThread.cpp` — Threading abstraction over Aurora
- `stubs.cpp` — Unimplemented GC SDK stubs
- `audio/` — Audio backend
- `imgui/` — In-game overlay UI (ImGui)
- `http/` — HTTP client (updates, achievements)
- `ios/` — iOS-specific code

**`extern/aurora/`** — Cross-platform graphics/windowing renderer (git submodule, wraps Vulkan/OpenGL/Metal, SDL). This is the primary dependency for all platform abstraction.

**`platforms/`** — Platform-specific packaging (Android gradle, iOS plist, macOS bundle, Windows manifest/icon, Linux freedesktop)

**`include/`** — Headers mirroring the `src/` structure, plus `include/dusk/` for Dusk-layer headers and `include/dusk_pch.hpp` (precompiled header).

**`res/`** — Runtime resources copied into the build output directory.

Version strings are derived from git tags matching `v*.*.*` at configure time.
