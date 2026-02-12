#include "web_app_manager.h"
#include "in_process_browser.h"
#include "ipc_handler.h"
#include "cef_loader.h"
#include "globals.h"
#include "shared/version.h"

#include "include/cef_browser.h"

#include <string>

namespace WebAppManager {

static CefRefPtr<InProcessBrowser> s_browser;
static std::vector<std::string> s_loadedApps;

static constexpr int DEFAULT_WIDTH  = 1280;
static constexpr int DEFAULT_HEIGHT = 720;

// Event callback for window resize
static void OnWindowResized(void* /*aEventArgs*/) {
    if (!Globals::API || !s_browser) return;

    NexusLinkData_t* nexusLink = static_cast<NexusLinkData_t*>(
        Globals::API->DataLink_Get(DL_NEXUS_LINK));
    if (nexusLink && nexusLink->Width > 0 && nexusLink->Height > 0) {
        s_browser->Resize(nexusLink->Width, nexusLink->Height);
    }
}

void Initialize() {
    if (!Globals::API) return;
    if (!CefLoader::IsAvailable()) {
        Globals::API->Log(LOGL_WARNING, ADDON_NAME,
            "CEF not available yet — browser creation deferred.");
        return;
    }

    // Create the in-process browser
    s_browser = new InProcessBrowser();

    // Set browser reference for IPC handler (event/keybind dispatch)
    IpcHandler::SetBrowser(s_browser.get());

    // Start with about:blank to minimize renderer-side processing.
    // A data: URL triggers site isolation and may cause GW2's CefHost.exe
    // (renderer subprocess) to spawn a new process with custom code that
    // crashes on our non-GW2 browser. about:blank is more likely to be
    // handled in a shared or spare renderer.
    std::string defaultUrl = "about:blank";

    if (s_browser->Create(defaultUrl, DEFAULT_WIDTH, DEFAULT_HEIGHT)) {
        s_loadedApps.push_back(defaultUrl);
    } else {
        Globals::API->Log(LOGL_CRITICAL, ADDON_NAME, "Failed to request browser creation.");
        IpcHandler::SetBrowser(nullptr);
        s_browser = nullptr;
    }

    // Subscribe to window resize events
    Globals::API->Events_Subscribe(EV_WINDOW_RESIZED, OnWindowResized);
}

void Shutdown() {
    if (Globals::API) {
        Globals::API->Events_Unsubscribe(EV_WINDOW_RESIZED, OnWindowResized);
    }

    IpcHandler::Cleanup();

    if (s_browser) {
        s_browser->Close();
        s_browser = nullptr;
    }

    s_loadedApps.clear();
}

void LoadUrl(const std::string& url) {
    if (!s_browser || !s_browser->IsReady()) {
        // Try to create browser if not ready
        if (!CefLoader::IsAvailable()) {
            if (Globals::API) {
                Globals::API->Log(LOGL_WARNING, ADDON_NAME, "CEF not available.");
            }
            return;
        }

        if (!s_browser) {
            s_browser = new InProcessBrowser();
            IpcHandler::SetBrowser(s_browser.get());
        }

        if (!s_browser->Create(url, DEFAULT_WIDTH, DEFAULT_HEIGHT)) {
            if (Globals::API) {
                Globals::API->Log(LOGL_WARNING, ADDON_NAME, "Failed to create browser for URL.");
            }
            return;
        }
        s_loadedApps.push_back(url);
    } else {
        // Navigate existing browser
        s_browser->Navigate(url);
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
    if (s_browser) {
        s_browser->Reload();
    }
}

const std::vector<std::string>& GetLoadedApps() {
    return s_loadedApps;
}

InProcessBrowser* GetBrowser() {
    return s_browser.get();
}

void* GetTextureHandle() {
    return s_browser ? s_browser->GetTextureHandle() : nullptr;
}

int GetWidth() {
    return s_browser ? s_browser->GetWidth() : 0;
}

int GetHeight() {
    return s_browser ? s_browser->GetHeight() : 0;
}

void FlushFrame() {
    if (s_browser) {
        s_browser->FlushFrame();
    }
}

bool IsReady() {
    return s_browser && s_browser->IsReady();
}

} // namespace WebAppManager
