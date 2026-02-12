#pragma once

#include <string>
#include <vector>

class InProcessBrowser;

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

// Get the InProcessBrowser instance (for input handler, overlay, etc.)
InProcessBrowser* GetBrowser();

// Get the current texture handle for overlay rendering.
void* GetTextureHandle();

// Get current browser dimensions.
int GetWidth();
int GetHeight();

// Resize the browser to the given dimensions.
void Resize(int width, int height);

// Apply buffered pixel data to D3D11 texture. Call from render thread.
void FlushFrame();

// Whether the browser is ready for use.
bool IsReady();

// DevTools — rendered offscreen into a second ImGui window.
void OpenDevTools();
void CloseDevTools();
bool IsDevToolsOpen();
InProcessBrowser* GetDevToolsBrowser();

} // namespace WebAppManager
