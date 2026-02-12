#include "web_app_manager.h"
#include "globals.h"
#include "cef_host_proxy.h"
#include "ipc_handler.h"
#include "shared/version.h"

#include <string>

namespace WebAppManager {

static std::vector<std::string> s_loadedApps;

static constexpr int DEFAULT_WIDTH  = 1280;
static constexpr int DEFAULT_HEIGHT = 720;

// Event callback for window resize
static void OnWindowResized(void* /*aEventArgs*/) {
    if (!Globals::API) return;

    NexusLinkData_t* nexusLink = static_cast<NexusLinkData_t*>(
        Globals::API->DataLink_Get(DL_NEXUS_LINK));
    if (nexusLink && nexusLink->Width > 0 && nexusLink->Height > 0) {
        CefHostProxy::ResizeBrowser(nexusLink->Width, nexusLink->Height);
    }
}

void Initialize() {
    if (!Globals::API) return;

    // Create default browser with a placeholder page
    std::string defaultUrl = "data:text/html,<html><body style='background:%23222;color:%23eee;font-family:sans-serif;display:flex;align-items:center;justify-content:center;height:100vh;margin:0'><div><h1>JS Loader</h1><p>Use the Options panel to load a web app URL.</p></div></body></html>";

    if (CefHostProxy::CreateBrowser(defaultUrl, DEFAULT_WIDTH, DEFAULT_HEIGHT)) {
        Globals::API->Log(LOGL_INFO, ADDON_NAME, "Default browser created.");
        s_loadedApps.push_back(defaultUrl);
    }

    // Subscribe to window resize events
    Globals::API->Events_Subscribe(EV_WINDOW_RESIZED, OnWindowResized);
}

void Shutdown() {
    if (Globals::API) {
        Globals::API->Events_Unsubscribe(EV_WINDOW_RESIZED, OnWindowResized);
    }

    IpcHandler::Cleanup();

    CefHostProxy::CloseBrowser();
    s_loadedApps.clear();
}

void LoadUrl(const std::string& url) {
    if (!CefHostProxy::IsReady()) {
        if (!CefHostProxy::CreateBrowser(url, DEFAULT_WIDTH, DEFAULT_HEIGHT)) {
            if (Globals::API) {
                Globals::API->Log(LOGL_WARNING, ADDON_NAME, "Failed to create browser for URL.");
            }
            return;
        }
        s_loadedApps.push_back(url);
    } else {
        // Navigate existing browser
        CefHostProxy::Navigate(url);
        if (!s_loadedApps.empty()) {
            s_loadedApps[0] = url;
        } else {
            s_loadedApps.push_back(url);
        }
    }

    if (Globals::API) {
        Globals::API->Log(LOGL_INFO, ADDON_NAME,
            (std::string("Loading URL: ") + url).c_str());
    }
}

void Reload() {
    CefHostProxy::Reload();
}

const std::vector<std::string>& GetLoadedApps() {
    return s_loadedApps;
}

} // namespace WebAppManager
