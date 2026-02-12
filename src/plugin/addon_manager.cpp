#include "addon_manager.h"
#include "addon_instance.h"
#include "addon_scheme_handler.h"
#include "cef_loader.h"
#include "globals.h"
#include "shared/version.h"

#include "nlohmann/json.hpp"

#include <windows.h>
#include <fstream>

using json = nlohmann::json;

namespace AddonManager {

static std::map<std::string, std::shared_ptr<AddonInstance>> s_addons;
static constexpr DWORD BROWSER_CREATION_TIMEOUT_MS = 15000;

// Parse a manifest.json file into an AddonManifest. Returns false if invalid.
static bool ParseManifest(const std::string& addonDir, const std::string& addonId, AddonManifest& out) {
    std::string manifestPath = addonDir + "\\manifest.json";

    std::ifstream file(manifestPath);
    if (!file.is_open()) return false;

    json j;
    try {
        file >> j;
    } catch (const json::parse_error& e) {
        if (Globals::API) {
            Globals::API->Log(LOGL_WARNING, ADDON_NAME,
                (std::string("Failed to parse manifest for '") + addonId + "': " + e.what()).c_str());
        }
        return false;
    }

    // Validate required fields
    if (!j.contains("name") || !j["name"].is_string() ||
        !j.contains("version") || !j["version"].is_string() ||
        !j.contains("author") || !j["author"].is_string() ||
        !j.contains("description") || !j["description"].is_string() ||
        !j.contains("entry") || !j["entry"].is_string()) {
        if (Globals::API) {
            Globals::API->Log(LOGL_WARNING, ADDON_NAME,
                (std::string("Manifest missing required fields for '") + addonId + "'").c_str());
        }
        return false;
    }

    out.id          = addonId;
    out.name        = j["name"].get<std::string>();
    out.version     = j["version"].get<std::string>();
    out.author      = j["author"].get<std::string>();
    out.description = j["description"].get<std::string>();
    out.entry       = j["entry"].get<std::string>();
    out.basePath    = addonDir;

    return true;
}

void Initialize() {
    if (!Globals::API) return;
    if (!CefLoader::IsAvailable()) return;

    // Get the addon scan directory: <GW2>/addons/jsloader/
    const char* addonDir = Globals::API->Paths_GetAddonDirectory("jsloader");
    if (!addonDir) {
        Globals::API->Log(LOGL_WARNING, ADDON_NAME,
            "Could not get jsloader addon directory.");
        return;
    }

    std::string scanDir(addonDir);
    Globals::API->Log(LOGL_INFO, ADDON_NAME,
        (std::string("Scanning for addons in: ") + scanDir).c_str());

    // Enumerate subdirectories
    std::string searchPattern = scanDir + "\\*";
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPattern.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        Globals::API->Log(LOGL_INFO, ADDON_NAME,
            "No addons found (directory empty or does not exist).");
        return;
    }

    int addonCount = 0;
    do {
        // Skip . and ..
        if (findData.cFileName[0] == '.') continue;

        // Only process directories
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;

        std::string addonId(findData.cFileName);
        std::string addonPath = scanDir + "\\" + addonId;

        AddonManifest manifest;
        if (!ParseManifest(addonPath, addonId, manifest)) continue;

        // Register scheme handler for this addon
        AddonSchemeHandler::RegisterForAddon(manifest.id, manifest.basePath);

        // Create addon instance
        auto instance = std::make_shared<AddonInstance>(manifest);
        instance->CreateMainBrowser();
        s_addons[addonId] = instance;
        ++addonCount;

        Globals::API->Log(LOGL_INFO, ADDON_NAME,
            (std::string("Loaded addon: ") + manifest.name + " v" + manifest.version +
             " by " + manifest.author).c_str());

    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);

    char msg[128];
    snprintf(msg, sizeof(msg), "Addon scan complete. %d addon(s) loaded.", addonCount);
    Globals::API->Log(LOGL_INFO, ADDON_NAME, msg);
}

void Shutdown() {
    for (auto& [id, addon] : s_addons) {
        addon->Shutdown();
    }
    s_addons.clear();

    // Unregister all scheme handlers
    AddonSchemeHandler::UnregisterAll();
}

void FlushAllFrames() {
    for (auto& [id, addon] : s_addons) {
        addon->FlushFrames();
    }
}

void FlushAllPendingEvents() {
    for (auto& [id, addon] : s_addons) {
        addon->FlushPendingEvents();
    }
}

bool AnyReady() {
    for (auto& [id, addon] : s_addons) {
        if (addon->IsAnyBrowserReady()) return true;
    }
    return false;
}

bool CheckWatchdog() {
    if (s_addons.empty()) return false;

    bool allFailed = true;
    for (auto& [id, addon] : s_addons) {
        if (addon->GetState() == AddonState::Error) continue;

        if (addon->CheckBrowserHealth(BROWSER_CREATION_TIMEOUT_MS)) {
            allFailed = false;
        }
    }
    return allFailed;
}

const std::map<std::string, std::shared_ptr<AddonInstance>>& GetAddons() {
    return s_addons;
}

AddonInstance* GetAddon(const std::string& addonId) {
    auto it = s_addons.find(addonId);
    return (it != s_addons.end()) ? it->second.get() : nullptr;
}

} // namespace AddonManager
