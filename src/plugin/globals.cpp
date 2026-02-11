#include "globals.h"
#include <string>

namespace Globals {

HMODULE     HModule        = nullptr;
AddonAPI_t* API            = nullptr;
bool        IsLoaded       = false;
bool        OverlayVisible = false;

static std::string s_dllDir;
static std::string s_cefDir;

const char* GetDllDirectory() {
    if (s_dllDir.empty() && HModule) {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(HModule, path, MAX_PATH);
        std::string fullPath(path);
        size_t pos = fullPath.find_last_of("\\/");
        if (pos != std::string::npos) {
            s_dllDir = fullPath.substr(0, pos);
        }
    }
    return s_dllDir.c_str();
}

const char* GetCefDirectory() {
    if (s_cefDir.empty()) {
        s_cefDir = std::string(GetDllDirectory()) + "\\nexus_js_loader";
    }
    return s_cefDir.c_str();
}

} // namespace Globals
