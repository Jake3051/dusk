#!/usr/bin/env python3
"""
Apply out-of-tree patches to the aurora submodule at build time.

Run from the repository root before cmake configuration.
"""

import sys
from pathlib import Path

ROOT = Path(__file__).parent.parent
CARD = ROOT / "extern/aurora/lib/dolphin/card.cpp"


def apply(path: Path, old: str, new: str, description: str) -> None:
    text = path.read_text()
    if new in text:
        print(f"[patch-aurora] already applied: {description}")
        return
    if old not in text:
        print(f"[patch-aurora] ERROR: anchor not found for: {description}", file=sys.stderr)
        sys.exit(1)
    path.write_text(text.replace(old, new, 1))
    print(f"[patch-aurora] applied: {description}")


# ---------------------------------------------------------------------------
# card.cpp: prefer {exeDir}/saves/ over AppData in CARDInit
# ---------------------------------------------------------------------------
apply(
    CARD,
    '#include "../card/CardGciFolder.hpp"',
    '#include "../card/CardGciFolder.hpp"\n#include <SDL3/SDL_filesystem.h>',
    "card.cpp: add SDL_filesystem include",
)

apply(
    CARD,
    "  std::string cardWorkingDir;\n"
    "  if (aurora::g_config.configPath != nullptr)\n"
    "    cardWorkingDir = aurora::g_config.configPath;\n"
    "  else\n"
    "    cardWorkingDir = std::filesystem::current_path().string();",
    "  std::string cardWorkingDir;\n"
    "  {\n"
    "    const char* basePath = SDL_GetBasePath();\n"
    "    if (basePath != nullptr) {\n"
    '      std::filesystem::path localSaves = std::filesystem::path(basePath) / "saves";\n'
    "      SDL_free((void*)basePath);\n"
    "      if (std::filesystem::exists(localSaves)) {\n"
    "        cardWorkingDir = localSaves.string();\n"
    '        Log.info("Using local saves folder: {}", cardWorkingDir);\n'
    "      }\n"
    "    }\n"
    "    if (cardWorkingDir.empty()) {\n"
    "      if (aurora::g_config.configPath != nullptr)\n"
    "        cardWorkingDir = aurora::g_config.configPath;\n"
    "      else\n"
    "        cardWorkingDir = std::filesystem::current_path().string();\n"
    "    }\n"
    "  }",
    "card.cpp: prefer local saves/ folder in CARDInit",
)
