#include "cef_loader.h"
#include "globals.h"
#include "shared/version.h"

#include <windows.h>
#include <delayimp.h>
#include <string>

// CEF API hash verification — we include the CEF header that defines the hash
// for the version we compiled against (CEF 103).
#include "include/cef_api_hash.h"
#include "include/cef_version.h"

// ---- Delay-load failure hook (must be at global scope for linker) ----
// Instead of crashing when libcef.dll isn't loaded, return nullptr gracefully.

static FARPROC WINAPI DelayLoadFailureHook(unsigned dliNotify, PDelayLoadInfo pdli) {
    (void)pdli;
    if (dliNotify == dliFailLoadLib) {
        // libcef.dll not found — this is expected if GW2 hasn't loaded CEF yet
        return nullptr;
    }
    if (dliNotify == dliFailGetProc) {
        return nullptr;
    }
    return nullptr;
}

// Register the delay-load failure hook with the MSVC delay-load helper.
// This symbol is looked up by the linker — must be extern "C" at global scope.
extern "C" const PfnDliHook __pfnDliFailureHook2 = reinterpret_cast<PfnDliHook>(DelayLoadFailureHook);

namespace CefLoader {

// Hash verification is one-time; CefInitialize check is per-call.
static bool s_hashChecked  = false;  // API hash has been verified
static bool s_hashValid    = false;  // API hash matched
static bool s_available    = false;  // CEF fully ready (hash + CefInitialize + UI thread)
static HMODULE s_hCef      = nullptr;

bool IsAvailable() {
    if (s_available) return true;
    return TryInitialize();
}

bool TryInitialize() {
    if (s_available) return true;

    // Permanent failure — API hash mismatch
    if (s_hashChecked && !s_hashValid) return false;

    // Check if libcef.dll is loaded in our process (by GW2)
    if (!s_hCef) {
        s_hCef = GetModuleHandleA("libcef.dll");
        if (!s_hCef) return false;
    }

    // Verify API hash (one-time check)
    if (!s_hashChecked) {
        // Log the path of the loaded libcef.dll
        char cefPath[MAX_PATH] = {};
        GetModuleFileNameA(s_hCef, cefPath, MAX_PATH);
        if (Globals::API) {
            Globals::API->Log(LOGL_INFO, ADDON_NAME,
                (std::string("Found libcef.dll: ") + cefPath).c_str());
        }

        // cef_api_hash(0) returns the platform hash as a string.
        // We use GetProcAddress directly to avoid triggering delay-load.
        typedef const char* (*cef_api_hash_fn)(int entry);
        cef_api_hash_fn fn = reinterpret_cast<cef_api_hash_fn>(
            GetProcAddress(s_hCef, "cef_api_hash"));

        if (!fn) {
            if (Globals::API) {
                Globals::API->Log(LOGL_CRITICAL, ADDON_NAME,
                    "libcef.dll does not export cef_api_hash — incompatible version.");
            }
            s_hashChecked = true;
            s_hashValid = false;
            return false;
        }

        const char* runtimeHash = fn(0); // 0 = CEF_API_HASH_PLATFORM
        const char* compiledHash = CEF_API_HASH_PLATFORM;

        if (!runtimeHash || strcmp(runtimeHash, compiledHash) != 0) {
            if (Globals::API) {
                char msg[512];
                snprintf(msg, sizeof(msg),
                    "CEF API hash mismatch! Runtime: %s, Compiled: %s. "
                    "GW2's CEF version differs from what this addon was built against.",
                    runtimeHash ? runtimeHash : "(null)", compiledHash);
                Globals::API->Log(LOGL_CRITICAL, ADDON_NAME, msg);
            }
            s_hashChecked = true;
            s_hashValid = false;
            return false;
        }

        if (Globals::API) {
            Globals::API->Log(LOGL_INFO, ADDON_NAME,
                "CEF API hash verified — compatible with GW2's libcef.dll.");
        }

        s_hashChecked = true;
        s_hashValid = true;
    }

    // API hash verified. We can't safely call cef_currently_on() to check if
    // CefInitialize has completed — that function accesses internal CEF thread
    // state that can hang under Wine if CEF is partially initialized.
    // Instead, we mark as available and let CreateBrowser (async) return false
    // if CefInitialize hasn't been called yet. The caller should retry.
    if (Globals::API) {
        Globals::API->Log(LOGL_INFO, ADDON_NAME,
            "CEF available — browser creation will be attempted from render thread.");
    }

    s_available = true;
    return true;
}

} // namespace CefLoader
