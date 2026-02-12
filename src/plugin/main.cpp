#include <windows.h>
#include "Nexus.h"
#include "shared/version.h"
#include "plugin/globals.h"
#include "plugin/cef_host_proxy.h"
#include "plugin/overlay.h"
#include "plugin/input_handler.h"
#include "plugin/web_app_manager.h"

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

    aAPI->Log(LOGL_INFO, ADDON_NAME, "Loading JS Loader...");

    // Initialize CEF host proxy (launches out-of-process CEF host)
    if (!CefHostProxy::Initialize()) {
        aAPI->Log(LOGL_CRITICAL, ADDON_NAME, "Failed to initialize CEF host proxy!");
        return;
    }

    // Register render callbacks
    aAPI->GUI_Register(RT_PreRender, OnPreRender);
    aAPI->GUI_Register(RT_Render, OnRender);
    aAPI->GUI_Register(RT_OptionsRender, OnOptionsRender);

    // Register input handler
    InputHandler::Initialize();

    // Register toggle keybind
    aAPI->InputBinds_RegisterWithString(
        "KB_JSLOADER_TOGGLE",
        OnToggleOverlay,
        "ALT+SHIFT+J"
    );

    // Subscribe to window resize events
    WebAppManager::Initialize();

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

    // Shutdown web apps
    WebAppManager::Shutdown();

    // Shutdown CEF host proxy (sends SHUTDOWN, waits, terminates)
    CefHostProxy::Shutdown();

    Globals::API->Log(LOGL_INFO, ADDON_NAME, "JS Loader unloaded.");
    Globals::API = nullptr;
}

void OnPreRender() {
    CefHostProxy::Tick();
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
