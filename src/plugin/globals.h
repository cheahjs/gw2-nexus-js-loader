#pragma once

#include <windows.h>
#include "Nexus.h"

namespace Globals {

extern HMODULE     HModule;        // DLL HMODULE (set in DllMain)
extern AddonAPI_t* API;            // Nexus API table (set in Load)
extern bool        IsLoaded;       // Whether the addon has been loaded
extern bool        OverlayVisible; // Whether the CEF overlay is visible/focused

// Helper: get the directory containing the DLL
const char* GetDllDirectory();

// Helper: get the CEF subfolder path ({DllDirectory}\nexus_js_loader)
const char* GetCefDirectory();

// Helper: get the CEF host exe path ({CefDirectory}\nexus_js_cef_host.exe)
const char* GetCefHostExePath();

} // namespace Globals
