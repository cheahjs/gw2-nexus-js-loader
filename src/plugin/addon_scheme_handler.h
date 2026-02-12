#pragma once

#include <string>

// Serves local addon files via HTTPS scheme with synthetic domains.
// URL pattern: https://<addon-id>.jsloader.local/<path>
namespace AddonSchemeHandler {

// Register a scheme handler factory for a specific addon.
// The factory resolves URL paths to local files under basePath.
void RegisterForAddon(const std::string& addonId, const std::string& basePath);

// Unregister all scheme handler factories.
void UnregisterAll();

} // namespace AddonSchemeHandler
