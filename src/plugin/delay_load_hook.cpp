// Custom delay-load hook for libcef.dll
// Redirects loading to the nexus_js_loader/ subfolder so we use our own CEF
// instead of GW2's libcef.dll which may already be loaded in the process.

#include <windows.h>
#include <delayimp.h>
#include <string>
#include "globals.h"

static FARPROC WINAPI delayHook(unsigned dliNotify, PDelayLoadInfo pdli) {
    if (dliNotify == dliNotePreLoadLibrary) {
        if (_stricmp(pdli->szDll, "libcef.dll") == 0) {
            std::string path = std::string(Globals::GetCefDirectory()) + "\\libcef.dll";
            return (FARPROC)LoadLibraryExA(path.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        }
    }
    return NULL;
}

const PfnDliHook __pfnDliNotifyHook2 = delayHook;
