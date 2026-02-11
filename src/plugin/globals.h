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

} // namespace Globals
