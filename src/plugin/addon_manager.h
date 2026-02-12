#pragma once

#include <string>
#include <map>
#include <memory>

class AddonInstance;

struct AddonManifest {
    std::string id;          // directory name
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::string entry;       // e.g. "index.html"
    std::string basePath;    // absolute filesystem path to addon dir
};

// Discovers addons from disk, owns their lifecycle, provides accessors.
namespace AddonManager {

// Scan addon directory, parse manifests, register scheme handlers, create browsers.
void Initialize();

// Shut down all addons, close browsers, unregister scheme handlers.
void Shutdown();

// Apply buffered pixel data for all addon browsers. Call from render thread.
void FlushAllFrames();

// Flush pending events/keybinds to JS for all addons. Call from OnPreRender.
void FlushAllPendingEvents();

// Whether at least one addon's browser is ready.
bool AnyReady();

// Check for browser creation timeouts / renderer crashes across all addons.
// Returns true if all addons have permanently failed (CEF should be disabled).
bool CheckWatchdog();

// Get all addon instances (for overlay rendering, hit testing, etc.)
const std::map<std::string, std::shared_ptr<AddonInstance>>& GetAddons();

// Look up a specific addon by ID.
AddonInstance* GetAddon(const std::string& addonId);

} // namespace AddonManager
