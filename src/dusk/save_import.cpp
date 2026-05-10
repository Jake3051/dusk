#include "dusk/save_import.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_misc.h>
#include <fmt/format.h>

#include <algorithm>
#include <filesystem>

#include "dusk/main.h"
#include "dusk/settings.h"

#if _WIN32
#define WIN32_LEAN_AND_MEAN
#include <shlobj_core.h>
#include <windows.h>
#include <winreg.h>
#endif

namespace dusk::save_import {
namespace {

bool is_gci_folder_mode() {
    // cardFileType: 0 = CARD_RAWIMAGE, 1 = CARD_GCIFOLDER
    return getSettings().backend.cardFileType.getValue() == 1;
}

#if _WIN32
static std::wstring wide_from_utf8(std::string_view utf8) {
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    std::wstring result(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), result.data(), len);
    return result;
}

static std::string utf8_from_wide(std::wstring_view wide) {
    if (wide.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
    std::string result(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), result.data(), len, nullptr, nullptr);
    return result;
}

static std::filesystem::path dolphin_base_path() {
    // 1. Check registry: HKCU\Software\Dolphin Emulator\UserConfigPath
    HKEY hkey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\Dolphin Emulator"), 0,
                     KEY_QUERY_VALUE, &hkey) == ERROR_SUCCESS) {
        wchar_t buf[MAX_PATH] = {};
        DWORD size = sizeof(buf);
        bool found = RegQueryValueEx(hkey, TEXT("UserConfigPath"), nullptr, nullptr,
                                     (LPBYTE)buf, &size) == ERROR_SUCCESS;
        RegCloseKey(hkey);
        if (found && buf[0]) return std::filesystem::path(buf);
    }

    // 2. My Documents\Dolphin Emulator
    PWSTR docs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs))) {
        std::filesystem::path p = std::filesystem::path(docs) / "Dolphin Emulator";
        CoTaskMemFree(docs);
        return p;
    }
    return {};
}
#else
static std::filesystem::path dolphin_base_path() {
    // 1. SDL pref path (covers modern Dolphin on Linux/macOS)
    char* p = SDL_GetPrefPath(nullptr, "dolphin-emu");
    if (p) {
        std::filesystem::path result(p);
        SDL_free(p);
        return result;
    }

    // 2. Legacy ~/.dolphin-emu or macOS ~/Library path
    const char* home = getenv("HOME");
    if (home && home[0] == '/') {
#ifdef __APPLE__
        return std::filesystem::path(home) / "Library" / "Application Support" / "Dolphin";
#else
        return std::filesystem::path(home) / ".dolphin-emu";
#endif
    }
    return {};
}
#endif

// All region codes Dolphin uses, in descending likelihood.
static constexpr const char* kRegions[] = {"USA", "EUR", "JAP"};

static std::filesystem::path dolphin_card_path(bool gciFolder) {
    auto base = dolphin_base_path();
    if (base.empty()) return {};

    for (const char* region : kRegions) {
        std::filesystem::path p;
        if (gciFolder) {
            p = base / "GC" / region / "Card A";
        } else {
            p = base / "GC" / fmt::format("MemoryCardA.{}.raw", region);
        }
        if (std::filesystem::exists(p)) return p;
    }
    return {};
}

} // namespace

std::filesystem::path saves_dir() {
    // Prefer a local saves/ folder next to the executable so users can keep
    // saves alongside the game binary without digging through AppData.
    const char* base = SDL_GetBasePath();
    if (base) {
        std::filesystem::path local = std::filesystem::path(base) / "saves";
        SDL_free((void*)base);
        if (std::filesystem::exists(local)) return local;
    }
    return std::filesystem::path(dusk::ConfigPath);
}

void open_saves_dir() {
    auto dir = saves_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    // Build a file:// URL with forward slashes (required on all platforms).
    std::string s = dir.string();
    std::replace(s.begin(), s.end(), '\\', '/');

    std::string url;
    if (!s.empty() && s[0] != '/') {
        // Windows absolute path: "C:/..." → "file:///C:/..."
        url = "file:///" + s;
    } else {
        url = "file://" + s;
    }

    SDL_OpenURL(url.c_str());
}

std::filesystem::path detect_dolphin_saves() {
    return dolphin_card_path(is_gci_folder_mode());
}

ImportResult import_from_dolphin() {
    bool gciFolder = is_gci_folder_mode();
    auto src = dolphin_card_path(gciFolder);
    if (src.empty()) return ImportResult::NothingFound;

    auto dstBase = saves_dir();

    try {
        if (gciFolder) {
            // src  = .../GC/USA/Card A          (directory of .gci files)
            // dest = {savesDir}/USA/Card A
            auto region = src.parent_path().filename(); // "USA" / "EUR" / "JAP"
            auto dst = dstBase / region / "Card A";

            if (std::filesystem::equivalent(src, dst)) return ImportResult::AlreadyCurrent;

            std::filesystem::create_directories(dst);
            for (const auto& entry : std::filesystem::directory_iterator(src)) {
                if (entry.path().extension() == ".gci") {
                    std::filesystem::copy(entry.path(), dst / entry.path().filename(),
                        std::filesystem::copy_options::overwrite_existing);
                }
            }
        } else {
            // src  = .../GC/MemoryCardA.USA.raw
            // dest = {savesDir}/MemoryCardA.USA.raw
            auto dst = dstBase / src.filename();

            if (std::filesystem::exists(dst) && std::filesystem::equivalent(src, dst))
                return ImportResult::AlreadyCurrent;

            std::filesystem::create_directories(dstBase);
            std::filesystem::copy(src, dst, std::filesystem::copy_options::overwrite_existing);
        }
        return ImportResult::Success;
    } catch (...) {
        return ImportResult::Error;
    }
}

} // namespace dusk::save_import
