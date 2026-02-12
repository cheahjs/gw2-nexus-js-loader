#include "ipc_handler.h"
#include "globals.h"
#include "cef_host_proxy.h"
#include "shared/ipc_messages.h"
#include "shared/version.h"

#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>

namespace IpcHandler {

// ---- Event dispatch infrastructure ----

struct PendingEvent {
    std::string name;
    std::string jsonData;
};

static std::mutex                    s_eventMutex;
static std::vector<PendingEvent>     s_pendingEvents;
static std::unordered_map<std::string, EVENT_CONSUME> s_eventCallbacks;

// ---- Keybind dispatch infrastructure ----

struct PendingKeybind {
    std::string identifier;
    bool        isRelease;
};

static std::mutex                     s_keybindMutex;
static std::vector<PendingKeybind>    s_pendingKeybinds;
static std::unordered_map<std::string, INPUTBINDS_PROCESS> s_keybindCallbacks;

// ---- Helper: send async response over pipe ----

static void SendAsyncResponse(int requestId, bool success, const std::string& value) {
    CefHostProxy::SendApiResponse(requestId, success, value);
}

// ---- IPC message handlers ----

using Args = std::vector<PipeProtocol::PipeArg>;

static bool HandleLog(const Args& args) {
    if (args.size() < 3) return false;
    int level = args[0].intVal;
    std::string channel = args[1].strVal;
    std::string text = args[2].strVal;

    if (Globals::API) {
        Globals::API->Log(static_cast<ELogLevel>(level), channel.c_str(), text.c_str());
    }
    return true;
}

static bool HandleAlert(const Args& args) {
    if (args.size() < 1) return false;
    std::string text = args[0].strVal;

    if (Globals::API) {
        Globals::API->GUI_SendAlert(text.c_str());
    }
    return true;
}

static bool HandleEventsSubscribe(const Args& args) {
    if (args.size() < 1) return false;
    std::string eventName = args[0].strVal;
    SubscribeEvent(eventName);
    return true;
}

static bool HandleEventsUnsubscribe(const Args& args) {
    if (args.size() < 1) return false;
    std::string eventName = args[0].strVal;
    UnsubscribeEvent(eventName);
    return true;
}

static bool HandleEventsRaise(const Args& args) {
    if (args.size() < 1) return false;
    std::string eventName = args[0].strVal;
    if (Globals::API) {
        Globals::API->Events_RaiseNotification(eventName.c_str());
    }
    return true;
}

static bool HandleKeybindsRegister(const Args& args) {
    if (args.size() < 2) return false;
    std::string identifier = args[0].strVal;
    std::string defaultBind = args[1].strVal;

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

static bool HandleKeybindsDeregister(const Args& args) {
    if (args.size() < 1) return false;
    std::string identifier = args[0].strVal;

    if (Globals::API) {
        Globals::API->InputBinds_Deregister(identifier.c_str());
        s_keybindCallbacks.erase(identifier);
    }
    return true;
}

static bool HandleGameBinds(const Args& args, const std::string& msgName) {
    if (args.size() < 1) return false;
    int bind = args[0].intVal;

    if (!Globals::API) return true;

    if (msgName == IPC::GAMEBINDS_PRESS) {
        Globals::API->GameBinds_PressAsync(static_cast<EGameBinds>(bind));
    } else if (msgName == IPC::GAMEBINDS_RELEASE) {
        Globals::API->GameBinds_ReleaseAsync(static_cast<EGameBinds>(bind));
    } else if (msgName == IPC::GAMEBINDS_INVOKE) {
        int duration = (args.size() > 1) ? args[1].intVal : 0;
        Globals::API->GameBinds_InvokeAsync(static_cast<EGameBinds>(bind), duration);
    } else if (msgName == IPC::GAMEBINDS_ISBOUND) {
        int requestId = (args.size() > 1) ? args[1].intVal : 0;
        bool result = Globals::API->GameBinds_IsBound(static_cast<EGameBinds>(bind));
        SendAsyncResponse(requestId, true, result ? "true" : "false");
    }
    return true;
}

static bool HandlePaths(const Args& args, const std::string& msgName) {
    if (args.size() < 1) return false;
    int requestId = args[0].intVal;

    if (!Globals::API) {
        SendAsyncResponse(requestId, false, "API not available");
        return true;
    }

    std::string result;
    if (msgName == IPC::PATHS_GAME_DIR) {
        const char* p = Globals::API->Paths_GetGameDirectory();
        result = p ? p : "";
    } else if (msgName == IPC::PATHS_ADDON_DIR) {
        std::string name = (args.size() > 1) ? args[1].strVal : "";
        const char* p = Globals::API->Paths_GetAddonDirectory(name.empty() ? nullptr : name.c_str());
        result = p ? p : "";
    } else if (msgName == IPC::PATHS_COMMON_DIR) {
        const char* p = Globals::API->Paths_GetCommonDirectory();
        result = p ? p : "";
    }

    SendAsyncResponse(requestId, true, result);
    return true;
}

static bool HandleDataLink(const Args& args, const std::string& msgName) {
    if (args.size() < 1) return false;
    int requestId = args[0].intVal;

    if (!Globals::API) {
        SendAsyncResponse(requestId, false, "API not available");
        return true;
    }

    if (msgName == IPC::DATALINK_GET_MUMBLE) {
        void* mumble = Globals::API->DataLink_Get(DL_MUMBLE_LINK);
        if (mumble) {
            SendAsyncResponse(requestId, true, "{\"available\":true}");
        } else {
            SendAsyncResponse(requestId, true, "{\"available\":false}");
        }
    } else if (msgName == IPC::DATALINK_GET_NEXUS) {
        NexusLinkData_t* nexusLink = static_cast<NexusLinkData_t*>(
            Globals::API->DataLink_Get(DL_NEXUS_LINK));
        if (nexusLink) {
            char json[512];
            snprintf(json, sizeof(json),
                "{\"width\":%u,\"height\":%u,\"scaling\":%.2f,"
                "\"isMoving\":%s,\"isCameraMoving\":%s,\"isGameplay\":%s}",
                nexusLink->Width, nexusLink->Height, nexusLink->Scaling,
                nexusLink->IsMoving ? "true" : "false",
                nexusLink->IsCameraMoving ? "true" : "false",
                nexusLink->IsGameplay ? "true" : "false");
            SendAsyncResponse(requestId, true, json);
        } else {
            SendAsyncResponse(requestId, false, "NexusLink not available");
        }
    }
    return true;
}

static bool HandleQuickAccess(const Args& args, const std::string& msgName) {
    if (!Globals::API) return true;

    if (msgName == IPC::QA_ADD) {
        if (args.size() < 5) return false;
        std::string id      = args[0].strVal;
        std::string tex     = args[1].strVal;
        std::string texHov  = args[2].strVal;
        std::string keybind = args[3].strVal;
        std::string tooltip = args[4].strVal;
        Globals::API->QuickAccess_Add(
            id.c_str(), tex.c_str(), texHov.c_str(),
            keybind.c_str(), tooltip.c_str());
    } else if (msgName == IPC::QA_REMOVE) {
        if (args.size() < 1) return false;
        std::string id = args[0].strVal;
        Globals::API->QuickAccess_Remove(id.c_str());
    } else if (msgName == IPC::QA_NOTIFY) {
        if (args.size() < 1) return false;
        std::string id = args[0].strVal;
        Globals::API->QuickAccess_Notify(id.c_str());
    }
    return true;
}

static bool HandleLocalization(const Args& args, const std::string& msgName) {
    if (!Globals::API) return true;

    if (msgName == IPC::LOC_TRANSLATE) {
        if (args.size() < 2) return false;
        int requestId = args[0].intVal;
        std::string id = args[1].strVal;
        const char* result = Globals::API->Localization_Translate(id.c_str());
        SendAsyncResponse(requestId, true, result ? result : id);
    } else if (msgName == IPC::LOC_SET) {
        if (args.size() < 3) return false;
        std::string id   = args[0].strVal;
        std::string lang = args[1].strVal;
        std::string text = args[2].strVal;
        Globals::API->Localization_Set(id.c_str(), lang.c_str(), text.c_str());
    }
    return true;
}

// ---- Public interface ----

bool HandleApiRequest(const std::string& messageName,
                       const std::vector<PipeProtocol::PipeArg>& args) {
    if (messageName == IPC::LOG_MESSAGE)        return HandleLog(args);
    if (messageName == IPC::ALERT)              return HandleAlert(args);
    if (messageName == IPC::EVENTS_SUBSCRIBE)   return HandleEventsSubscribe(args);
    if (messageName == IPC::EVENTS_UNSUBSCRIBE) return HandleEventsUnsubscribe(args);
    if (messageName == IPC::EVENTS_RAISE)       return HandleEventsRaise(args);
    if (messageName == IPC::KEYBINDS_REGISTER)  return HandleKeybindsRegister(args);
    if (messageName == IPC::KEYBINDS_DEREGISTER) return HandleKeybindsDeregister(args);

    if (messageName == IPC::GAMEBINDS_PRESS ||
        messageName == IPC::GAMEBINDS_RELEASE ||
        messageName == IPC::GAMEBINDS_INVOKE ||
        messageName == IPC::GAMEBINDS_ISBOUND) {
        return HandleGameBinds(args, messageName);
    }

    if (messageName == IPC::PATHS_GAME_DIR ||
        messageName == IPC::PATHS_ADDON_DIR ||
        messageName == IPC::PATHS_COMMON_DIR) {
        return HandlePaths(args, messageName);
    }

    if (messageName == IPC::DATALINK_GET_MUMBLE ||
        messageName == IPC::DATALINK_GET_NEXUS) {
        return HandleDataLink(args, messageName);
    }

    if (messageName == IPC::QA_ADD ||
        messageName == IPC::QA_REMOVE ||
        messageName == IPC::QA_NOTIFY) {
        return HandleQuickAccess(args, messageName);
    }

    if (messageName == IPC::LOC_TRANSLATE ||
        messageName == IPC::LOC_SET) {
        return HandleLocalization(args, messageName);
    }

    return false; // Unhandled
}

void SubscribeEvent(const std::string& eventName) {
    if (!Globals::API) return;

    auto callback = [](void* aEventArgs) {
        (void)aEventArgs;
    };

    s_eventCallbacks[eventName] = callback;
    Globals::API->Events_Subscribe(eventName.c_str(), callback);

    if (Globals::API) {
        Globals::API->Log(LOGL_DEBUG, ADDON_NAME,
            (std::string("Subscribed to event: ") + eventName).c_str());
    }
}

void UnsubscribeEvent(const std::string& eventName) {
    if (!Globals::API) return;

    auto it = s_eventCallbacks.find(eventName);
    if (it != s_eventCallbacks.end()) {
        Globals::API->Events_Unsubscribe(eventName.c_str(), it->second);
        s_eventCallbacks.erase(it);
    }
}

void FlushPendingEvents() {
    // Flush events over pipe
    {
        std::lock_guard<std::mutex> lock(s_eventMutex);
        for (const auto& ev : s_pendingEvents) {
            CefHostProxy::SendEventDispatch(ev.name, ev.jsonData);
        }
        s_pendingEvents.clear();
    }

    // Flush keybinds over pipe
    {
        std::lock_guard<std::mutex> lock(s_keybindMutex);
        for (const auto& kb : s_pendingKeybinds) {
            CefHostProxy::SendKeybindInvoke(kb.identifier, kb.isRelease);
        }
        s_pendingKeybinds.clear();
    }
}

void Cleanup() {
    // Unsubscribe all events
    if (Globals::API) {
        for (const auto& pair : s_eventCallbacks) {
            Globals::API->Events_Unsubscribe(pair.first.c_str(), pair.second);
        }
    }
    s_eventCallbacks.clear();

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
}

} // namespace IpcHandler
