#include "cef_manager.h"
#include "globals.h"
#include "browser_app.h"
#include "browser_client.h"
#include "shared/version.h"

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_command_line.h"

#include <string>

namespace CefManager {

static CefRefPtr<BrowserApp>    s_app;
static CefRefPtr<BrowserClient> s_client;
static CefRefPtr<CefBrowser>    s_browser;
static bool                     s_initialized = false;

bool Initialize() {
    if (s_initialized) return true;

    const char* cefDir = Globals::GetCefDirectory();

    if (Globals::API) {
        Globals::API->Log(LOGL_INFO, ADDON_NAME,
            (std::string("CEF directory: ") + cefDir).c_str());
    }

    // Subprocess lives in the CEF subfolder
    std::string subprocessPath = std::string(cefDir) + "\\nexus_js_subprocess.exe";

    // Log file existence checks for debugging
    if (Globals::API) {
        auto fileExists = [](const std::string& path) -> bool {
            DWORD attr = GetFileAttributesA(path.c_str());
            return attr != INVALID_FILE_ATTRIBUTES;
        };

        const char* requiredFiles[] = {
            "\\libcef.dll",
            "\\nexus_js_subprocess.exe",
            "\\icudtl.dat",
            "\\v8_context_snapshot.bin",
            "\\chrome_elf.dll",
            "\\locales\\en-US.pak",
        };
        for (const char* file : requiredFiles) {
            std::string fullPath = std::string(cefDir) + file;
            Globals::API->Log(
                fileExists(fullPath) ? LOGL_INFO : LOGL_WARNING,
                ADDON_NAME,
                (std::string(fileExists(fullPath) ? "Found: " : "MISSING: ") + file).c_str());
        }

        // Check if libcef.dll is already loaded by GW2
        HMODULE existingCef = GetModuleHandleA("libcef.dll");
        if (existingCef) {
            char existingPath[MAX_PATH] = {};
            GetModuleFileNameA(existingCef, existingPath, MAX_PATH);
            Globals::API->Log(LOGL_WARNING, ADDON_NAME,
                (std::string("libcef.dll already loaded from: ") + existingPath).c_str());
        }
    }

    CefMainArgs mainArgs(Globals::HModule);

    s_app = new BrowserApp();

    CefSettings settings = {};
    settings.size = sizeof(CefSettings);
    settings.no_sandbox = true;
    settings.multi_threaded_message_loop = false;
    settings.windowless_rendering_enabled = true;

    CefString(&settings.browser_subprocess_path).FromString(subprocessPath);

    // Resources and locales in CEF subfolder
    std::string resourcesDir = std::string(cefDir);
    CefString(&settings.resources_dir_path).FromString(resourcesDir);

    std::string localesDir = std::string(cefDir) + "\\locales";
    CefString(&settings.locales_dir_path).FromString(localesDir);

    // Cache and log in CEF subfolder
    std::string cachePath = std::string(cefDir) + "\\cef_cache";
    CefString(&settings.cache_path).FromString(cachePath);

    std::string logPath = std::string(cefDir) + "\\cef_debug.log";
    CefString(&settings.log_file).FromString(logPath);
    settings.log_severity = LOGSEVERITY_INFO;

    // Ensure libcef.dll's transitive dependencies can be found in the subfolder
    SetDllDirectoryA(cefDir);

    if (!CefInitialize(mainArgs, settings, s_app, nullptr)) {
        if (Globals::API) {
            Globals::API->Log(LOGL_CRITICAL, ADDON_NAME, "CefInitialize failed!");
            Globals::API->Log(LOGL_CRITICAL, ADDON_NAME,
                (std::string("Check CEF log at: ") + logPath).c_str());
        }
        return false;
    }

    s_initialized = true;
    return true;
}

void DoMessageLoopWork() {
    if (s_initialized) {
        CefDoMessageLoopWork();
    }
}

bool CreateBrowser(const std::string& url, int width, int height) {
    if (!s_initialized) return false;

    s_client = new BrowserClient(width, height);

    CefWindowInfo windowInfo;
    windowInfo.SetAsWindowless(nullptr);

    CefBrowserSettings browserSettings = {};
    browserSettings.size = sizeof(CefBrowserSettings);
    browserSettings.windowless_frame_rate = 60;

    s_browser = CefBrowserHost::CreateBrowserSync(
        windowInfo, s_client, url, browserSettings, nullptr, nullptr);

    if (!s_browser) {
        if (Globals::API) {
            Globals::API->Log(LOGL_CRITICAL, ADDON_NAME, "CreateBrowserSync failed!");
        }
        return false;
    }

    return true;
}

void ResizeBrowser(int width, int height) {
    if (s_client) {
        s_client->SetSize(width, height);
    }
    if (s_browser && s_browser->GetHost()) {
        s_browser->GetHost()->WasResized();
    }
}

void Shutdown() {
    if (!s_initialized) return;

    // Close browser
    if (s_browser) {
        s_browser->GetHost()->CloseBrowser(true);
        s_browser = nullptr;
    }

    s_client = nullptr;
    s_app = nullptr;

    CefShutdown();
    s_initialized = false;
}

} // namespace CefManager
