#include "ipc_handler.h"
#include "in_process_browser.h"
#include "addon_manager.h"
#include "addon_instance.h"
#include "globals.h"
#include "shared/version.h"

#include "nlohmann/json.hpp"

#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <cstring>

using json = nlohmann::json;

// Standard MumbleLink struct (Mumble positional audio protocol).
// Nexus maps this via DataLink_Get(DL_MUMBLE_LINK).
struct LinkedMem {
    uint32_t uiVersion;
    uint32_t uiTick;
    float    fAvatarPosition[3];
    float    fAvatarFront[3];
    float    fAvatarTop[3];
    wchar_t  name[256];
    float    fCameraPosition[3];
    float    fCameraFront[3];
    float    fCameraTop[3];
    wchar_t  identity[256];
    uint32_t context_len;
    unsigned char context[256];
    wchar_t  description[2048];
};

// Convert wchar_t string to UTF-8 std::string (Windows WideCharToMultiByte)
static std::string WcharToUtf8(const wchar_t* wstr, size_t maxLen) {
    size_t len = 0;
    while (len < maxLen && wstr[len] != L'\0') ++len;
    if (len == 0) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr, static_cast<int>(len), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, static_cast<int>(len), &result[0], size, nullptr, nullptr);
    return result;
}

namespace IpcHandler {

// ---- Helper: send async response to JS via ExecuteJavaScript ----

static void SendAsyncResponse(InProcessBrowser* browser, int requestId,
                               bool success, const std::string& value) {
    if (!browser) return;

    json j;
    j["type"] = "response";
    j["requestId"] = requestId;
    j["success"] = success;

    try {
        j["value"] = json::parse(value);
    } catch (...) {
        j["value"] = value;
    }

    std::string code = "window.__nexus_dispatch(" + j.dump() + ");";
    browser->ExecuteJavaScript(code);
}

// ---- JSON message handlers ----

static bool HandleLog(const json& msg) {
    int level = msg.value("level", 3);
    std::string channel = msg.value("channel", "");
    std::string text = msg.value("message", "");

    if (Globals::API) {
        Globals::API->Log(static_cast<ELogLevel>(level), channel.c_str(), text.c_str());
    }
    return true;
}

static bool HandleAlert(const json& msg) {
    std::string text = msg.value("message", "");
    if (Globals::API) {
        Globals::API->GUI_SendAlert(text.c_str());
    }
    return true;
}

static bool HandleEventsSubscribe(const json& msg, AddonInstance* addon) {
    std::string eventName = msg.value("name", "");
    if (!eventName.empty() && addon) {
        addon->SubscribeEvent(eventName);
    }
    return true;
}

static bool HandleEventsUnsubscribe(const json& msg, AddonInstance* addon) {
    std::string eventName = msg.value("name", "");
    if (!eventName.empty() && addon) {
        addon->UnsubscribeEvent(eventName);
    }
    return true;
}

static bool HandleEventsRaise(const json& msg) {
    std::string eventName = msg.value("name", "");
    if (!eventName.empty() && Globals::API) {
        Globals::API->Events_RaiseNotification(eventName.c_str());
    }
    return true;
}

static bool HandleKeybindsRegister(const json& msg, AddonInstance* addon) {
    std::string identifier = msg.value("id", "");
    std::string defaultBind = msg.value("defaultBind", "");
    if (!identifier.empty() && addon) {
        addon->RegisterKeybind(identifier, defaultBind);
    }
    return true;
}

static bool HandleKeybindsDeregister(const json& msg, AddonInstance* addon) {
    std::string identifier = msg.value("id", "");
    if (!identifier.empty() && addon) {
        addon->DeregisterKeybind(identifier);
    }
    return true;
}

static bool HandleGameBinds(const json& msg, const std::string& action,
                             InProcessBrowser* browser) {
    int bind = msg.value("bind", 0);

    if (!Globals::API) return true;

    if (action == "gamebinds_press") {
        Globals::API->GameBinds_PressAsync(static_cast<EGameBinds>(bind));
    } else if (action == "gamebinds_release") {
        Globals::API->GameBinds_ReleaseAsync(static_cast<EGameBinds>(bind));
    } else if (action == "gamebinds_invoke") {
        int duration = msg.value("durationMs", 0);
        Globals::API->GameBinds_InvokeAsync(static_cast<EGameBinds>(bind), duration);
    } else if (action == "gamebinds_isBound") {
        int requestId = msg.value("requestId", 0);
        bool result = Globals::API->GameBinds_IsBound(static_cast<EGameBinds>(bind));
        SendAsyncResponse(browser, requestId, true, result ? "true" : "false");
    }
    return true;
}

static bool HandlePaths(const json& msg, const std::string& action,
                         InProcessBrowser* browser) {
    int requestId = msg.value("requestId", 0);

    if (!Globals::API) {
        SendAsyncResponse(browser, requestId, false, "API not available");
        return true;
    }

    std::string result;
    if (action == "paths_getGameDirectory") {
        const char* p = Globals::API->Paths_GetGameDirectory();
        result = p ? p : "";
    } else if (action == "paths_getAddonDirectory") {
        std::string name = msg.value("name", "");
        const char* p = Globals::API->Paths_GetAddonDirectory(name.empty() ? nullptr : name.c_str());
        result = p ? p : "";
    } else if (action == "paths_getCommonDirectory") {
        const char* p = Globals::API->Paths_GetCommonDirectory();
        result = p ? p : "";
    }

    SendAsyncResponse(browser, requestId, true, result);
    return true;
}

static bool HandleDataLink(const json& msg, const std::string& action,
                            InProcessBrowser* browser) {
    int requestId = msg.value("requestId", 0);

    if (!Globals::API) {
        SendAsyncResponse(browser, requestId, false, "API not available");
        return true;
    }

    if (action == "datalink_getMumbleLink") {
        void* mumble = Globals::API->DataLink_Get(DL_MUMBLE_LINK);
        if (mumble) {
            LinkedMem* lm = static_cast<LinkedMem*>(mumble);
            json j;
            j["uiVersion"] = lm->uiVersion;
            j["uiTick"] = lm->uiTick;
            j["avatarPosition"] = { lm->fAvatarPosition[0], lm->fAvatarPosition[1], lm->fAvatarPosition[2] };
            j["avatarFront"] = { lm->fAvatarFront[0], lm->fAvatarFront[1], lm->fAvatarFront[2] };
            j["avatarTop"] = { lm->fAvatarTop[0], lm->fAvatarTop[1], lm->fAvatarTop[2] };
            j["name"] = WcharToUtf8(lm->name, 256);
            j["cameraPosition"] = { lm->fCameraPosition[0], lm->fCameraPosition[1], lm->fCameraPosition[2] };
            j["cameraFront"] = { lm->fCameraFront[0], lm->fCameraFront[1], lm->fCameraFront[2] };
            j["cameraTop"] = { lm->fCameraTop[0], lm->fCameraTop[1], lm->fCameraTop[2] };
            j["identity"] = WcharToUtf8(lm->identity, 256);
            j["contextLen"] = lm->context_len;
            SendAsyncResponse(browser, requestId, true, j.dump());
        } else {
            SendAsyncResponse(browser, requestId, false, "MumbleLink not available");
        }
    } else if (action == "datalink_getNexusLink") {
        NexusLinkData_t* nexusLink = static_cast<NexusLinkData_t*>(
            Globals::API->DataLink_Get(DL_NEXUS_LINK));
        if (nexusLink) {
            json j;
            j["width"] = nexusLink->Width;
            j["height"] = nexusLink->Height;
            j["scaling"] = nexusLink->Scaling;
            j["isMoving"] = nexusLink->IsMoving;
            j["isCameraMoving"] = nexusLink->IsCameraMoving;
            j["isGameplay"] = nexusLink->IsGameplay;
            SendAsyncResponse(browser, requestId, true, j.dump());
        } else {
            SendAsyncResponse(browser, requestId, false, "NexusLink not available");
        }
    }
    return true;
}

static bool HandleQuickAccess(const json& msg, const std::string& action) {
    if (!Globals::API) return true;

    if (action == "quickaccess_add") {
        std::string id      = msg.value("id", "");
        std::string tex     = msg.value("texture", "");
        std::string texHov  = msg.value("textureHover", "");
        std::string keybind = msg.value("keybind", "");
        std::string tooltip = msg.value("tooltip", "");
        Globals::API->QuickAccess_Add(
            id.c_str(), tex.c_str(), texHov.c_str(),
            keybind.c_str(), tooltip.c_str());
    } else if (action == "quickaccess_remove") {
        std::string id = msg.value("id", "");
        Globals::API->QuickAccess_Remove(id.c_str());
    } else if (action == "quickaccess_notify") {
        std::string id = msg.value("id", "");
        Globals::API->QuickAccess_Notify(id.c_str());
    }
    return true;
}

static bool HandleLocalization(const json& msg, const std::string& action,
                                InProcessBrowser* browser) {
    if (!Globals::API) return true;

    if (action == "localization_translate") {
        int requestId = msg.value("requestId", 0);
        std::string id = msg.value("id", "");
        const char* result = Globals::API->Localization_Translate(id.c_str());
        SendAsyncResponse(browser, requestId, true, result ? result : id);
    } else if (action == "localization_set") {
        std::string id   = msg.value("id", "");
        std::string lang = msg.value("lang", "");
        std::string text = msg.value("text", "");
        Globals::API->Localization_Set(id.c_str(), lang.c_str(), text.c_str());
    }
    return true;
}

// ---- Window management handlers ----

static bool HandleWindowsCreate(const json& msg, AddonInstance* addon,
                                 InProcessBrowser* browser) {
    int requestId = msg.value("requestId", 0);
    std::string windowId = msg.value("windowId", "");
    std::string url = msg.value("url", "");
    int width = msg.value("width", 800);
    int height = msg.value("height", 600);
    std::string title = msg.value("title", "");

    if (windowId.empty() || !addon) {
        SendAsyncResponse(browser, requestId, false, "Invalid windowId or addon");
        return true;
    }

    bool ok = addon->CreateAddonWindow(windowId, url, width, height, title);
    SendAsyncResponse(browser, requestId, ok, ok ? "created" : "failed");
    return true;
}

static bool HandleWindowsClose(const json& msg, AddonInstance* addon) {
    std::string windowId = msg.value("windowId", "");
    if (!windowId.empty() && addon) {
        addon->CloseWindow(windowId);
    }
    return true;
}

static bool HandleWindowsUpdate(const json& msg, AddonInstance* addon) {
    std::string windowId = msg.value("windowId", "");
    if (windowId.empty() || !addon) return true;

    std::string title = msg.value("title", "");
    int width = msg.value("width", 0);
    int height = msg.value("height", 0);
    bool visible = msg.value("visible", true);

    addon->UpdateWindow(windowId, title, width, height, visible);
    return true;
}

static bool HandleWindowsSetInputPassthrough(const json& msg, AddonInstance* addon) {
    std::string windowId = msg.value("windowId", "");
    bool enabled = msg.value("enabled", false);
    if (!windowId.empty() && addon) {
        addon->SetInputPassthrough(windowId, enabled);
    }
    return true;
}

static bool HandleWindowsList(const json& msg, AddonInstance* addon,
                               InProcessBrowser* browser) {
    int requestId = msg.value("requestId", 0);

    if (!addon) {
        SendAsyncResponse(browser, requestId, false, "Addon not found");
        return true;
    }

    json windowList = json::array();
    for (const auto& [id, window] : addon->GetWindows()) {
        json w;
        w["windowId"] = window.windowId;
        w["title"] = window.title;
        w["width"] = window.width;
        w["height"] = window.height;
        w["visible"] = window.visible;
        w["inputPassthrough"] = window.inputPassthrough;
        windowList.push_back(w);
    }

    SendAsyncResponse(browser, requestId, true, windowList.dump());
    return true;
}

// ---- Public interface ----

bool HandleBridgeMessage(const std::string& jsonStr, InProcessBrowser* browser) {
    json msg;
    try {
        msg = json::parse(jsonStr);
    } catch (const json::parse_error& e) {
        if (Globals::API) {
            Globals::API->Log(LOGL_WARNING, ADDON_NAME,
                (std::string("Bridge JSON parse error: ") + e.what()).c_str());
        }
        return false;
    }

    std::string action = msg.value("action", "");
    if (action.empty()) return false;

    // Extract addon/window identity from message
    std::string addonId = msg.value("__addonId", "");
    std::string windowId = msg.value("__windowId", "");

    // Fall back to browser's identity if not in message
    if (addonId.empty() && browser) {
        addonId = browser->GetAddonId();
    }
    if (windowId.empty() && browser) {
        windowId = browser->GetWindowId();
    }

    // Look up the addon instance
    AddonInstance* addon = AddonManager::GetAddon(addonId);

    // Route by action name
    if (action == "log")                   return HandleLog(msg);
    if (action == "alert")                 return HandleAlert(msg);
    if (action == "events_subscribe")      return HandleEventsSubscribe(msg, addon);
    if (action == "events_unsubscribe")    return HandleEventsUnsubscribe(msg, addon);
    if (action == "events_raise")          return HandleEventsRaise(msg);
    if (action == "keybinds_register")     return HandleKeybindsRegister(msg, addon);
    if (action == "keybinds_deregister")   return HandleKeybindsDeregister(msg, addon);

    if (action == "gamebinds_press" || action == "gamebinds_release" ||
        action == "gamebinds_invoke" || action == "gamebinds_isBound") {
        return HandleGameBinds(msg, action, browser);
    }

    if (action == "paths_getGameDirectory" || action == "paths_getAddonDirectory" ||
        action == "paths_getCommonDirectory") {
        return HandlePaths(msg, action, browser);
    }

    if (action == "datalink_getMumbleLink" || action == "datalink_getNexusLink") {
        return HandleDataLink(msg, action, browser);
    }

    if (action == "quickaccess_add" || action == "quickaccess_remove" ||
        action == "quickaccess_notify") {
        return HandleQuickAccess(msg, action);
    }

    if (action == "localization_translate" || action == "localization_set") {
        return HandleLocalization(msg, action, browser);
    }

    // Window management actions
    if (action == "windows_create")             return HandleWindowsCreate(msg, addon, browser);
    if (action == "windows_close")              return HandleWindowsClose(msg, addon);
    if (action == "windows_update")             return HandleWindowsUpdate(msg, addon);
    if (action == "windows_setInputPassthrough") return HandleWindowsSetInputPassthrough(msg, addon);
    if (action == "windows_list")               return HandleWindowsList(msg, addon, browser);

    if (Globals::API) {
        Globals::API->Log(LOGL_DEBUG, ADDON_NAME,
            (std::string("Unhandled bridge action: ") + action).c_str());
    }
    return false;
}

} // namespace IpcHandler
