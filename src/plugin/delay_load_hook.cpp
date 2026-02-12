// Custom delay-load hook for libcef.dll
// Redirects loading to the nexus_js_loader/ subfolder so we use our own CEF
// instead of GW2's libcef.dll which may already be loaded in the process.

#include <windows.h>
#include <delayimp.h>
#include <string>
#include "globals.h"
#include "shared/version.h"

static FARPROC WINAPI delayHook(unsigned dliNotify, PDelayLoadInfo pdli) {
    if (dliNotify == dliNotePreLoadLibrary) {
        if (_stricmp(pdli->szDll, "libcef.dll") == 0) {
            std::string path = std::string(Globals::GetCefDirectory()) + "\\libcef.dll";
            HMODULE hmod = LoadLibraryExA(path.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);

            // Log what we actually loaded
            if (Globals::API) {
                if (hmod) {
                    char loadedPath[MAX_PATH] = {};
                    GetModuleFileNameA(hmod, loadedPath, MAX_PATH);
                    Globals::API->Log(LOGL_INFO, ADDON_NAME,
                        (std::string("Delay-load resolved libcef.dll to: ") + loadedPath).c_str());
                } else {
                    DWORD err = GetLastError();
                    Globals::API->Log(LOGL_CRITICAL, ADDON_NAME,
                        (std::string("Failed to load libcef.dll from: ") + path
                         + " (error " + std::to_string(err) + ")").c_str());
                }
            }

            return (FARPROC)hmod;
        }
    }
    return NULL;
}

const PfnDliHook __pfnDliNotifyHook2 = delayHook;
