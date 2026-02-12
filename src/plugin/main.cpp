#include <windows.h>
#include "Nexus.h"
#include "shared/version.h"
#include "plugin/globals.h"
#include "plugin/cef_loader.h"
#include "plugin/overlay.h"
#include "plugin/input_handler.h"
#include "plugin/ipc_handler.h"
#include "plugin/web_app_manager.h"
#include "plugin/in_process_browser.h"
#include "imgui.h"

// Forward declarations for Nexus callbacks
void AddonLoad(AddonAPI_t* aAPI);
void AddonUnload();
void OnPreRender();
void OnRender();
void OnOptionsRender();

// Toggle overlay keybind handler
void OnToggleOverlay(const char* aIdentifier, bool aIsRelease);

// Addon definition — returned to Nexus via exported GetAddonDef()
static AddonDefinition_t s_addonDef = {};

// Whether we've completed deferred CEF initialization
static bool s_cefInitialized = false;

// Whether browser creation permanently failed (renderer crashed, etc.)
static bool s_cefFailed = false;

// Delay browser creation to give GW2 time to complete CefInitialize.
// Without this, calling CEF functions too early can hang under Wine.
static int  s_frameCount = 0;
static constexpr int CEF_INIT_DELAY_FRAMES = 300;  // ~5 seconds at 60fps
static constexpr int CEF_RETRY_INTERVAL    = 60;   // retry every ~1 second

// Watchdog: if browser isn't ready within this many ms, mark as failed.
// Prevents indefinite hang if CefHost.exe renderer subprocess crashes.
static constexpr DWORD BROWSER_CREATION_TIMEOUT_MS = 15000;  // 15 seconds

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    s_addonDef.Signature   = ADDON_SIGNATURE;
    s_addonDef.APIVersion  = NEXUS_API_VERSION;
    s_addonDef.Name        = ADDON_NAME;
    s_addonDef.Version     = { ADDON_VERSION_MAJOR, ADDON_VERSION_MINOR, ADDON_VERSION_BUILD, ADDON_VERSION_REV };
    s_addonDef.Author      = ADDON_AUTHOR;
    s_addonDef.Description = ADDON_DESCRIPTION;
    s_addonDef.Load        = AddonLoad;
    s_addonDef.Unload      = AddonUnload;
    s_addonDef.Flags       = AF_None;
    s_addonDef.Provider    = UP_None;
    s_addonDef.UpdateLink  = nullptr;
    return &s_addonDef;
}

void AddonLoad(AddonAPI_t* aAPI) {
    Globals::API = aAPI;

    aAPI->Log(LOGL_INFO, ADDON_NAME, "Loading JS Loader (in-process CEF)...");

    // Set ImGui context and allocators to match Nexus.
    // Our DLL compiles its own ImGui 1.80 (matching Nexus's version).
    // We must share the same context and memory allocator.
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(aAPI->ImguiContext));
    ImGui::SetAllocatorFunctions(
        reinterpret_cast<void*(*)(size_t, void*)>(aAPI->ImguiMalloc),
        reinterpret_cast<void(*)(void*, void*)>(aAPI->ImguiFree));

    // Register render callbacks (needed even before CEF is available for polling)
    aAPI->GUI_Register(RT_PreRender, OnPreRender);
    aAPI->GUI_Register(RT_Render, OnRender);
    aAPI->GUI_Register(RT_OptionsRender, OnOptionsRender);

    // Register input handler
    InputHandler::Initialize();

    // Register toggle keybind
    aAPI->InputBinds_RegisterWithString(
        "KB_JSLOADER_TOGGLE",
        OnToggleOverlay,
        "ALT+SHIFT+L"
    );

    // CEF browser creation is always deferred to OnPreRender.
    // At addon load time, CefInitialize may not have been called yet by GW2,
    // and we need to be on the CEF UI thread (which is the render thread).
    aAPI->Log(LOGL_INFO, ADDON_NAME,
        "Will create browser when CEF is ready (deferred to render thread).");

    Globals::IsLoaded = true;
    aAPI->Log(LOGL_INFO, ADDON_NAME, "JS Loader loaded successfully.");
}

void AddonUnload() {
    if (!Globals::API) return;

    Globals::API->Log(LOGL_INFO, ADDON_NAME, "Unloading JS Loader...");

    Globals::IsLoaded = false;

    // Deregister keybind
    Globals::API->InputBinds_Deregister("KB_JSLOADER_TOGGLE");

    // Deregister render callbacks
    Globals::API->GUI_Deregister(OnPreRender);
    Globals::API->GUI_Deregister(OnRender);
    Globals::API->GUI_Deregister(OnOptionsRender);

    // Shutdown input handler
    InputHandler::Shutdown();

    // Shutdown web apps and browser
    WebAppManager::Shutdown();

    Globals::API->Log(LOGL_INFO, ADDON_NAME, "JS Loader unloaded.");
    Globals::API = nullptr;
}

void OnPreRender() {
    static bool s_firstCall = true;
    if (s_firstCall) {
        s_firstCall = false;
        if (Globals::API) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                "First OnPreRender call. Render thread ID: %lu",
                GetCurrentThreadId());
            Globals::API->Log(LOGL_INFO, ADDON_NAME, msg);
        }
    }

    // Don't try CEF if it permanently failed
    if (s_cefFailed) return;

    // Deferred CEF initialization: wait for GW2 to fully initialize CEF,
    // then create browser from the render thread.
    if (!s_cefInitialized) {
        ++s_frameCount;

        // Wait for startup delay before touching any CEF functions.
        // Calling CEF functions before CefInitialize completes hangs under Wine.
        if (s_frameCount < CEF_INIT_DELAY_FRAMES) return;

        // Only retry periodically, not every frame
        if ((s_frameCount - CEF_INIT_DELAY_FRAMES) % CEF_RETRY_INTERVAL != 0) return;

        // Check if libcef.dll is loaded and API hash matches
        if (!CefLoader::IsAvailable()) return;

        // Try to create the browser (async). CreateBrowser will return false
        // if CefInitialize hasn't been called yet.
        if (Globals::API) {
            Globals::API->Log(LOGL_INFO, ADDON_NAME,
                "Attempting browser creation...");
        }

        WebAppManager::Initialize();
        s_cefInitialized = true;

        if (Globals::API) {
            Globals::API->Log(LOGL_INFO, ADDON_NAME,
                "Browser creation initiated from render thread.");
        }
        return;
    }

    // Watchdog: check if browser creation is taking too long.
    // If CefHost.exe renderer crashed, OnAfterCreated never fires and CEF
    // may block GW2's main thread. Detect this via timeout + failure flag.
    if (!WebAppManager::IsReady()) {
        auto* browser = WebAppManager::GetBrowser();
        if (browser && browser->HasCreationFailed()) {
            s_cefFailed = true;
            if (Globals::API) {
                Globals::API->Log(LOGL_CRITICAL, ADDON_NAME,
                    "Browser creation failed (renderer crashed). CEF disabled.");
            }
            WebAppManager::Shutdown();
            return;
        }

        if (browser) {
            DWORD elapsed = GetTickCount() - browser->GetCreationRequestTick();
            if (elapsed > BROWSER_CREATION_TIMEOUT_MS) {
                s_cefFailed = true;
                if (Globals::API) {
                    char msg[256];
                    snprintf(msg, sizeof(msg),
                        "Browser creation timed out after %lu ms. CEF disabled.",
                        elapsed);
                    Globals::API->Log(LOGL_CRITICAL, ADDON_NAME, msg);
                }
                WebAppManager::Shutdown();
                return;
            }
        }
    }

    // Apply any buffered CEF pixel data to the D3D11 texture.
    // This MUST happen in PreRender (before ImGui frame begins), not in
    // OnRender. Map/Unmap on the immediate context during an active ImGui
    // render pass corrupts D3D11 pipeline state and crashes the game.
    WebAppManager::FlushFrame();

    // Flush pending events/keybinds to JS
    IpcHandler::FlushPendingEvents();
}

void OnRender() {
    Overlay::Render();
}

void OnOptionsRender() {
    Overlay::RenderOptions();
}

void OnToggleOverlay(const char* /*aIdentifier*/, bool aIsRelease) {
    if (!aIsRelease) {
        Globals::OverlayVisible = !Globals::OverlayVisible;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID /*lpReserved*/) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        Globals::HModule = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}
