#pragma once

#include <string>
#include <vector>

// Manages web app lifecycle: loading/unloading browsers, scanning addon directories.
namespace WebAppManager {

// Initialize the manager: create default browser, subscribe to events.
void Initialize();

// Shutdown: close all browsers, unsubscribe events.
void Shutdown();

// Load a URL in the primary browser.
void LoadUrl(const std::string& url);

// Reload the current page.
void Reload();

// Get list of loaded web app URLs.
const std::vector<std::string>& GetLoadedApps();

} // namespace WebAppManager
