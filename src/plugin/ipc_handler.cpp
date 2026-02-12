#include "ipc_handler.h"
#include "in_process_browser.h"
#include "globals.h"
#include "shared/version.h"

#include "nlohmann/json.hpp"

#include <string>
#include <vector>
#include <mutex>
#include <array>
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

// ---- Browser reference for ExecuteJavaScript calls ----

static InProcessBrowser* s_browser = nullptr;

// ---- Event dispatch infrastructure ----
// EVENT_CONSUME is void(*)(void*) — a C function pointer that can't hold state.
// We use a template-generated trampoline table so each event subscription gets
// a unique function pointer that looks up its event name from a static table.

struct PendingEvent {
    std::string name;
    std::string jsonData;
};

static std::mutex                    s_eventMutex;
static std::vector<PendingEvent>     s_pendingEvents;

static constexpr int MAX_EVENT_SLOTS = 64;
static std::string s_eventSlotNames[MAX_EVENT_SLOTS];
static bool        s_eventSlotUsed[MAX_EVENT_SLOTS] = {};

template<int N>
static void EventTrampoline(void* /*aEventArgs*/) {
    std::lock_guard<std::mutex> lock(s_eventMutex);
    s_pendingEvents.push_back({s_eventSlotNames[N], ""});
}

template<int... Is>
static std::array<EVENT_CONSUME, sizeof...(Is)>
MakeTrampolineTable(std::integer_sequence<int, Is...>) {
    return {{ &EventTrampoline<Is>... }};
}

static auto s_trampolines = MakeTrampolineTable(
    std::make_integer_sequence<int, MAX_EVENT_SLOTS>{});

// Maps event name → trampoline slot index
static std::unordered_map<std::string, int> s_eventSlots;

// ---- Keybind dispatch infrastructure ----

struct PendingKeybind {
    std::string identifier;
    bool        isRelease;
};

static std::mutex                     s_keybindMutex;
static std::vector<PendingKeybind>    s_pendingKeybinds;
static std::unordered_map<std::string, INPUTBINDS_PROCESS> s_keybindCallbacks;

// ---- Helper: send async response to JS via ExecuteJavaScript ----

static void SendAsyncResponse(int requestId, bool success, const std::string& value) {
    if (!s_browser) return;

    // Escape the value string for embedding in JS
    json j;
    j["type"] = "response";
    j["requestId"] = requestId;
    j["success"] = success;

    // Try to parse value as JSON — if it parses, embed as object; otherwise as string
    try {
        j["value"] = json::parse(value);
    } catch (...) {
        j["value"] = value;
    }

    std::string code = "window.__nexus_dispatch(" + j.dump() + ");";
    s_browser->ExecuteJavaScript(code);
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

static bool HandleEventsSubscribe(const json& msg) {
    std::string eventName = msg.value("name", "");
    if (!eventName.empty()) {
        SubscribeEvent(eventName);
    }
    return true;
}

static bool HandleEventsUnsubscribe(const json& msg) {
    std::string eventName = msg.value("name", "");
    if (!eventName.empty()) {
        UnsubscribeEvent(eventName);
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

static bool HandleKeybindsRegister(const json& msg) {
    std::string identifier = msg.value("id", "");
    std::string defaultBind = msg.value("defaultBind", "");

    if (identifier.empty()) return false;

    auto callback = [](const char* aIdentifier, bool aIsRelease) {
        std::lock_guard<std::mutex> lock(s_keybindMutex);
        s_pendingKeybinds.push_back({aIdentifier, aIsRelease});
    };

    if (Globals::API) {
        Globals::API->InputBinds_RegisterWithString(
            identifier.c_str(), callback, defaultBind.c_str());
        s_keybindCallbacks[identifier] = callback;
    }
    return true;
}

static bool HandleKeybindsDeregister(const json& msg) {
    std::string identifier = msg.value("id", "");
    if (identifier.empty()) return false;

    if (Globals::API) {
        Globals::API->InputBinds_Deregister(identifier.c_str());
        s_keybindCallbacks.erase(identifier);
    }
    return true;
}

static bool HandleGameBinds(const json& msg, const std::string& action) {
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
        SendAsyncResponse(requestId, true, result ? "true" : "false");
    }
    return true;
}

static bool HandlePaths(const json& msg, const std::string& action) {
    int requestId = msg.value("requestId", 0);

    if (!Globals::API) {
        SendAsyncResponse(requestId, false, "API not available");
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

    SendAsyncResponse(requestId, true, result);
    return true;
}

static bool HandleDataLink(const json& msg, const std::string& action) {
    int requestId = msg.value("requestId", 0);

    if (!Globals::API) {
        SendAsyncResponse(requestId, false, "API not available");
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
            SendAsyncResponse(requestId, true, j.dump());
        } else {
            SendAsyncResponse(requestId, false, "MumbleLink not available");
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
            SendAsyncResponse(requestId, true, j.dump());
        } else {
            SendAsyncResponse(requestId, false, "NexusLink not available");
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

static bool HandleLocalization(const json& msg, const std::string& action) {
    if (!Globals::API) return true;

    if (action == "localization_translate") {
        int requestId = msg.value("requestId", 0);
        std::string id = msg.value("id", "");
        const char* result = Globals::API->Localization_Translate(id.c_str());
        SendAsyncResponse(requestId, true, result ? result : id);
    } else if (action == "localization_set") {
        std::string id   = msg.value("id", "");
        std::string lang = msg.value("lang", "");
        std::string text = msg.value("text", "");
        Globals::API->Localization_Set(id.c_str(), lang.c_str(), text.c_str());
    }
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

    // Route by action name
    if (action == "log")                   return HandleLog(msg);
    if (action == "alert")                 return HandleAlert(msg);
    if (action == "events_subscribe")      return HandleEventsSubscribe(msg);
    if (action == "events_unsubscribe")    return HandleEventsUnsubscribe(msg);
    if (action == "events_raise")          return HandleEventsRaise(msg);
    if (action == "keybinds_register")     return HandleKeybindsRegister(msg);
    if (action == "keybinds_deregister")   return HandleKeybindsDeregister(msg);

    if (action == "gamebinds_press" || action == "gamebinds_release" ||
        action == "gamebinds_invoke" || action == "gamebinds_isBound") {
        return HandleGameBinds(msg, action);
    }

    if (action == "paths_getGameDirectory" || action == "paths_getAddonDirectory" ||
        action == "paths_getCommonDirectory") {
        return HandlePaths(msg, action);
    }

    if (action == "datalink_getMumbleLink" || action == "datalink_getNexusLink") {
        return HandleDataLink(msg, action);
    }

    if (action == "quickaccess_add" || action == "quickaccess_remove" ||
        action == "quickaccess_notify") {
        return HandleQuickAccess(msg, action);
    }

    if (action == "localization_translate" || action == "localization_set") {
        return HandleLocalization(msg, action);
    }

    if (Globals::API) {
        Globals::API->Log(LOGL_DEBUG, ADDON_NAME,
            (std::string("Unhandled bridge action: ") + action).c_str());
    }
    return false;
}

void SetBrowser(InProcessBrowser* browser) {
    s_browser = browser;
}

void SubscribeEvent(const std::string& eventName) {
    if (!Globals::API) return;

    // Already subscribed?
    if (s_eventSlots.count(eventName)) return;

    // Allocate a trampoline slot
    int slot = -1;
    for (int i = 0; i < MAX_EVENT_SLOTS; ++i) {
        if (!s_eventSlotUsed[i]) {
            s_eventSlotUsed[i] = true;
            s_eventSlotNames[i] = eventName;
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        Globals::API->Log(LOGL_WARNING, ADDON_NAME,
            "Too many event subscriptions — max 64 reached.");
        return;
    }

    s_eventSlots[eventName] = slot;
    Globals::API->Events_Subscribe(eventName.c_str(), s_trampolines[slot]);

    Globals::API->Log(LOGL_DEBUG, ADDON_NAME,
        (std::string("Subscribed to event: ") + eventName).c_str());
}

void UnsubscribeEvent(const std::string& eventName) {
    if (!Globals::API) return;

    auto it = s_eventSlots.find(eventName);
    if (it != s_eventSlots.end()) {
        int slot = it->second;
        Globals::API->Events_Unsubscribe(eventName.c_str(), s_trampolines[slot]);
        s_eventSlotUsed[slot] = false;
        s_eventSlotNames[slot].clear();
        s_eventSlots.erase(it);
    }
}

void FlushPendingEvents() {
    if (!s_browser) return;

    // Flush events via ExecuteJavaScript
    {
        std::lock_guard<std::mutex> lock(s_eventMutex);
        for (const auto& ev : s_pendingEvents) {
            json j;
            j["type"] = "event";
            j["name"] = ev.name;
            if (!ev.jsonData.empty()) {
                try {
                    j["data"] = json::parse(ev.jsonData);
                } catch (...) {
                    j["data"] = ev.jsonData;
                }
            } else {
                j["data"] = nullptr;
            }
            std::string code = "window.__nexus_dispatch(" + j.dump() + ");";
            s_browser->ExecuteJavaScript(code);
        }
        s_pendingEvents.clear();
    }

    // Flush keybinds via ExecuteJavaScript
    {
        std::lock_guard<std::mutex> lock(s_keybindMutex);
        for (const auto& kb : s_pendingKeybinds) {
            json j;
            j["type"] = "keybind";
            j["id"] = kb.identifier;
            j["isRelease"] = kb.isRelease;
            std::string code = "window.__nexus_dispatch(" + j.dump() + ");";
            s_browser->ExecuteJavaScript(code);
        }
        s_pendingKeybinds.clear();
    }
}

void Cleanup() {
    // Unsubscribe all events
    if (Globals::API) {
        for (const auto& pair : s_eventSlots) {
            Globals::API->Events_Unsubscribe(pair.first.c_str(), s_trampolines[pair.second]);
        }
    }
    for (const auto& pair : s_eventSlots) {
        s_eventSlotUsed[pair.second] = false;
        s_eventSlotNames[pair.second].clear();
    }
    s_eventSlots.clear();

    // Deregister all keybinds
    if (Globals::API) {
        for (const auto& pair : s_keybindCallbacks) {
            Globals::API->InputBinds_Deregister(pair.first.c_str());
        }
    }
    s_keybindCallbacks.clear();

    {
        std::lock_guard<std::mutex> lock1(s_eventMutex);
        s_pendingEvents.clear();
    }
    {
        std::lock_guard<std::mutex> lock2(s_keybindMutex);
        s_pendingKeybinds.clear();
    }

    s_browser = nullptr;
}

} // namespace IpcHandler
