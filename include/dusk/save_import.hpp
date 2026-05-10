#pragma once

#include <filesystem>

namespace dusk::save_import {

enum class ImportResult {
    Success,
    NothingFound,   // Dolphin saves not detected on this system
    AlreadyCurrent, // Source and destination are the same path
    Error,          // Copy failed
};

// Returns the directory where Dusk currently stores save files.
// Prefers {executableDir}/saves/ if it exists, otherwise falls back to ConfigPath.
std::filesystem::path saves_dir();

// Opens saves_dir() in the OS file manager (Explorer / Finder / Nautilus).
void open_saves_dir();

// Returns the detected Dolphin GCI folder (or raw file path) for Card A, or an
// empty path if Dolphin saves are not found on this system.
std::filesystem::path detect_dolphin_saves();

// Copies save files from Dolphin's detected location into saves_dir().
// Safe to call before CARDInit(); has no effect on the running card session.
ImportResult import_from_dolphin();

} // namespace dusk::save_import
