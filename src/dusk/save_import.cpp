#include "dusk/save_import.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_misc.h>
#include <fmt/format.h>

#include <algorithm>
#include <filesystem>

#include "dusk/main.h"
#include "dusk/settings.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <shlobj_core.h>
#include <windows.h>
#include <winreg.h>
#endif

// Compile-time mobile guard matching ImGuiEngine.hpp's IsMobile definition.
#if (defined(__APPLE__) && TARGET_OS_IOS && !TARGET_OS_MACCATALYST) || defined(__ANDROID__)
#define DUSK_PLATFORM_MOBILE 1
#else
#define DUSK_PLATFORM_MOBILE 0
#endif

namespace dusk::save_import {
namespace {

bool is_gci_folder_mode() {
    // cardFileType: 0 = CARD_RAWIMAGE, 1 = CARD_GCIFOLDER
    return getSettings().backend.cardFileType.getValue() == 1;
}

// ---------------------------------------------------------------------------
// Platform-specific: locate the Dolphin Emulator user directory
// ---------------------------------------------------------------------------

#if defined(_WIN32)

static std::filesystem::path dolphin_base_path() {
    // 1. Registry: HKCU\Software\Dolphin Emulator\UserConfigPath
    HKEY hkey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\Dolphin Emulator"), 0,
                     KEY_QUERY_VALUE, &hkey) == ERROR_SUCCESS) {
        wchar_t buf[MAX_PATH] = {};
        DWORD size = sizeof(buf);
        bool ok = RegQueryValueEx(hkey, TEXT("UserConfigPath"), nullptr, nullptr,
                                  reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS;
        RegCloseKey(hkey);
        if (ok && buf[0]) return std::filesystem::path(buf);
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

#elif defined(__ANDROID__)

static std::filesystem::path dolphin_base_path() {
    // Dolphin Android historically writes to external storage.
    // Try the most common locations in order.
    static const char* kCandidates[] = {
        // Modern Dolphin (scoped storage / getExternalFilesDir)
        "/sdcard/Android/data/org.dolphinemu.dolphinemu/files",
        // Legacy Dolphin (external storage root)
        "/sdcard/dolphin-emu",
        "/storage/emulated/0/dolphin-emu",
        "/storage/emulated/0/Android/data/org.dolphinemu.dolphinemu/files",
    };
    for (const char* c : kCandidates) {
        if (std::filesystem::exists(c)) return c;
    }
    return {};
}

#else // macOS + Linux

static std::filesystem::path dolphin_base_path() {
    // 1. SDL pref path covers modern Dolphin on both platforms.
    //    Linux:  ~/.local/share/dolphin-emu/
    //    macOS:  ~/Library/Application Support/dolphin-emu/ (older builds)
    char* p = SDL_GetPrefPath(nullptr, "dolphin-emu");
    if (p) {
        std::filesystem::path result(p);
        SDL_free(p);
        if (std::filesystem::exists(result / "GC")) return result;
    }

#if defined(__APPLE__)
    // macOS Dolphin stores data under ~/Library/Application Support/Dolphin/
    const char* home = getenv("HOME");
    if (home && home[0] == '/') {
        return std::filesystem::path(home) / "Library" / "Application Support" / "Dolphin";
    }
#else
    // Linux legacy: ~/.dolphin-emu/
    const char* home = getenv("HOME");
    if (home && home[0] == '/') {
        return std::filesystem::path(home) / ".dolphin-emu";
    }
#endif
    return {};
}

#endif // platform

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

static constexpr const char* kRegions[] = {"USA", "EUR", "JAP"};

static std::filesystem::path dolphin_card_path(bool gciFolder) {
    auto base = dolphin_base_path();
    if (base.empty()) return {};

    for (const char* region : kRegions) {
        std::filesystem::path p = gciFolder
            ? base / "GC" / region / "Card A"
            : base / "GC" / fmt::format("MemoryCardA.{}.raw", region);
        if (std::filesystem::exists(p)) return p;
    }
    return {};
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::filesystem::path saves_dir() {
#if !DUSK_PLATFORM_MOBILE
    // On desktop, a saves/ folder next to the executable takes priority so
    // users can keep saves with the game binary instead of navigating AppData.
    // On mobile, SDL_GetBasePath() returns a read-only bundle/APK path, so
    // we skip this check entirely.
    const char* base = SDL_GetBasePath();
    if (base) {
        std::filesystem::path local = std::filesystem::path(base) / "saves";
        SDL_free(const_cast<char*>(base));
        if (std::filesystem::exists(local)) return local;
    }
#endif
    return std::filesystem::path(dusk::ConfigPath);
}

void open_saves_dir() {
    auto dir = saves_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

#if defined(__ANDROID__)
    // Android 7+ blocks file:// URIs from implicit intents. We can't reliably
    // open a file manager to an arbitrary path, so this is a no-op; callers
    // should display the path text instead.
    (void)dir;

#elif defined(__APPLE__) && TARGET_OS_IOS && !TARGET_OS_MACCATALYST
    // On iOS, saves live inside the app sandbox. The user can browse them via
    // Files app → On My iPhone/iPad → Dusk. Opening "shareddocuments://"
    // launches Files app at its root so the user can navigate there.
    SDL_OpenURL("shareddocuments://");

#else
    // Windows / macOS / Linux: open the folder in Explorer / Finder / Nautilus.
    std::string s = dir.string();
    std::replace(s.begin(), s.end(), '\\', '/');
    // Windows paths like "C:/..." need three slashes: file:///C:/...
    std::string url = (!s.empty() && s[0] != '/') ? "file:///" + s : "file://" + s;
    SDL_OpenURL(url.c_str());
#endif
}

bool can_open_saves_dir() {
#if defined(__ANDROID__)
    return false;
#else
    return true;
#endif
}

std::filesystem::path detect_dolphin_saves() {
#if defined(__APPLE__) && TARGET_OS_IOS && !TARGET_OS_MACCATALYST
    // Dolphin does not run on iOS.
    return {};
#else
    return dolphin_card_path(is_gci_folder_mode());
#endif
}

ImportResult import_from_dolphin() {
#if defined(__APPLE__) && TARGET_OS_IOS && !TARGET_OS_MACCATALYST
    return ImportResult::NothingFound;
#else
    bool gciFolder = is_gci_folder_mode();
    auto src = dolphin_card_path(gciFolder);
    if (src.empty()) return ImportResult::NothingFound;

    auto dstBase = saves_dir();

    try {
        if (gciFolder) {
            // src  = .../GC/USA/Card A   (directory of .gci files)
            // dest = {savesDir}/USA/Card A
            auto region = src.parent_path().filename();
            auto dst = dstBase / region / "Card A";

            if (std::filesystem::exists(src) && std::filesystem::exists(dst) &&
                std::filesystem::equivalent(src, dst))
                return ImportResult::AlreadyCurrent;

            std::filesystem::create_directories(dst);
            bool copied = false;
            for (const auto& entry : std::filesystem::directory_iterator(src)) {
                if (entry.path().extension() == ".gci") {
                    std::filesystem::copy(entry.path(), dst / entry.path().filename(),
                        std::filesystem::copy_options::overwrite_existing);
                    copied = true;
                }
            }
            if (!copied) return ImportResult::NothingFound;
        } else {
            // src  = .../GC/MemoryCardA.USA.raw
            // dest = {savesDir}/MemoryCardA.USA.raw
            auto dst = dstBase / src.filename();

            if (std::filesystem::exists(dst) && std::filesystem::equivalent(src, dst))
                return ImportResult::AlreadyCurrent;

            std::filesystem::create_directories(dstBase);
            std::filesystem::copy(src, dst,
                std::filesystem::copy_options::overwrite_existing);
        }
        return ImportResult::Success;
    } catch (...) {
        return ImportResult::Error;
    }
#endif
}

} // namespace dusk::save_import
