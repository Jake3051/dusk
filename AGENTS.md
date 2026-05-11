# AGENTS.md

This file provides guidance to OpenCode when working with code in this repository.

## Important

The project maintainers do not accept contributions that are primarily AI-generated. Keep AI-assisted changes minimal and well-reasoned.

## Build

Prerequisites: CMake 3.25+, Python 3+, Ninja. Init submodules first:

```sh
git submodule update --init --recursive
```

**Primary dev target (Linux, GCC, RelWithDebInfo):**

```sh
cmake --preset linux-default-relwithdebinfo
cmake --build --preset linux-default-relwithdebinfo
```

Other presets: `linux-default-debug`, `linux-clang-relwithdebinfo`, `linux-clang-debug`, `macos-default-relwithdebinfo`, `windows-msvc-relwithdebinfo`. See `CMakePresets.json` for the full list.

**Run:** `build/{preset}/dusk /path/to/game.rvz` (supports ISO/GCM/RVZ/WIA/WBFS/CISO/GCZ; defaults to `game.iso` in CWD).

Nix dev shell: `nix develop` (flake.nix).

## Testing

There are no unit tests. Correctness is validated against the original game binary. Do not attempt to run `ctest`, `make test`, or similar.

## Linting & Formatting

- **C/C++ formatting:** `clang-format -i` (config in `.clang-format`). Key rules: 4-space indent, 100-column limit, left pointer/reference alignment (`int* p`), `SortIncludes: true`, C++03 formatting style.
- **Python:** flake8 (ignoring E203, E501 — see `.flake8`).
- No other lint/typecheck commands exist in CI or scripts.

## Architecture

Dusk is a reverse-engineered reimplementation of a GameCube/Wii game. Layered bottom-to-top:

- **`libs/`** — GameCube/Wii platform layer (JSystem, PowerPC ABI, TRK debugger, dolphin/revolution HW abstraction)
- **`src/`** — Reverse-engineered game code mirroring the original GC binary structure. `src/d/` is the largest component (actors, collision, rendering, UI).
- **`src/dusk/`** — Modern platform integration layer (entry point, config, audio, ImGui overlay, HTTP, achievements, Discord RPC, frame interpolation)
- **`include/`** — Headers mirroring `src/` structure. All game code uses include dirs `include`, `src`, `libs/JSystem/include`, `libs`, `extern/aurora/include/dolphin`, `extern`. The PCH is `include/dusk_pch.hpp`.
- **`extern/aurora/`** — Cross-platform renderer (git submodule; wraps Vulkan/OpenGL/Metal, SDL)
- **`platforms/`** — Platform packaging (Android, iOS, macOS, Windows, Linux)
- **`res/`** — Runtime resources copied to build output
- **`files.cmake`** — Authoritative source file list consumed by `CMakeLists.txt`
- **`tools/`** — dev utilities (currently `saves_to_states_json.py`)
- **`ci/`** — CI helper scripts (AppImage generation)

## Gotchas

- **Multi-char constants:** Multi-character constants like `'ABCD'` are implementation-defined. The code uses `#pragma` suppression for this. For >4-char literals, use the `MULTI_CHAR()` macro (GCC/Clang truncate to int otherwise).
- **`res/` is deployed:** The build copies `res/` into the output directory. If adding new files under `res/` you do not need to update `files.cmake` — they are copied by a POST_BUILD command.
- **Version from git:** Version strings derive from git tags `v*.*.*` at configure time. Building outside a git repo or without tags produces `UNKNOWN-VERSION`.
- **clangd config:** `.clangd` suppresses specific warnings for `.inc` files (undeclared vars), MSL C++ headers (forced `--std=c++98`), and `.pch` files (forced `--std=c++98`). Be careful when editing these file types.
- **Windows RC files:** `dusk.rc` and `dusk.manifest` are generated from `.in` templates in `platforms/windows/` at configure time. A PowerShell script generates the `.ico` from `res/icon.png`.
- **No `-Werror` by default:** Warnings are suppressed by default via `DUSK_BUILD_WARNINGS` option (off). CI builds ship with warnings off.
- **`files.cmake` must be updated:** When adding/removing source files, update `files.cmake` — not `CMakeLists.txt` directly.

## Code conventions

- Left pointer/reference alignment: `int* p`, `const char* s`, `void foo(int& x)`
- `#include` groups: JSystem/dolzel headers, external/system `<...>`, local `"..."`. clang-format sorts within groups.
- Game source files compile with `NDEBUG` in all build types (release-like optimizations).
- A subset of files (`GAME_DEBUG_FILES` in `CMakeLists.txt`) compile with `DEBUG=1` in debug builds — mostly Dusk-layer audio and SSem.
