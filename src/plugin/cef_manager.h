#pragma once

#include <string>

// Manages CEF lifecycle: initialization, browser creation, message loop, shutdown.
namespace CefManager {

// Initialize CEF. Must be called from Load() after API is set.
// Returns false on failure.
bool Initialize();

// Pump CEF message loop. Call from RT_PreRender.
void DoMessageLoopWork();

// Create a browser loading the given URL. Returns true on success.
bool CreateBrowser(const std::string& url, int width, int height);

// Close all browsers and shut down CEF. Call from Unload().
void Shutdown();

// Resize the active browser.
void ResizeBrowser(int width, int height);

} // namespace CefManager
