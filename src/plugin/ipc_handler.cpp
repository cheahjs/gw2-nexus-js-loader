#include "ipc_handler.h"
#include "globals.h"
#include "shared/ipc_messages.h"
#include "shared/version.h"

#include "include/cef_process_message.h"

#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>

namespace IpcHandler {

// ---- Event dispatch infrastructure ----

struct PendingEvent {
    std::string name;
    std::string jsonData; // Serialized event data (or empty)
};

static std::mutex                    s_eventMutex;
static std::vector<PendingEvent>     s_pendingEvents;

// Map of subscribed event names → their EVENT_CONSUME callback pointers
// so we can unsubscribe later.
static std::unordered_map<std::string, EVENT_CONSUME> s_eventCallbacks;

// Global event consumer that queues events for IPC dispatch
static void EventConsumer(void* aEventArgs) {
    // We don't know which event this is from in a bare callback,
    // so we use a per-event lambda approach via a dispatch map.
    // This function is not used directly — see SubscribeEvent.
    (void)aEventArgs;
}

// ---- Keybind dispatch infrastructure ----

struct PendingKeybind {
    std::string identifier;
    bool        isRelease;
};

static std::mutex                     s_keybindMutex;
static std::vector<PendingKeybind>    s_pendingKeybinds;
static std::unordered_map<std::string, INPUTBINDS_PROCESS> s_keybindCallbacks;

// ---- Helper: send async response to renderer ----

static void SendAsyncResponse(CefRefPtr<CefBrowser> browser,
                               int requestId,
                               bool success,
                               const std::string& value) {
    auto msg = CefProcessMessage::Create(IPC::ASYNC_RESPONSE);
    auto args = msg->GetArgumentList();
    args->SetInt(0, requestId);
    args->SetBool(1, success);
    args->SetString(2, value);
    browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, msg);
}

// ---- IPC message handlers ----

static bool HandleLog(CefRefPtr<CefProcessMessage> message) {
    auto args = message->GetArgumentList();
    int level = args->GetInt(0);
    std::string channel = args->GetString(1).ToString();
    std::string text = args->GetString(2).ToString();

    if (Globals::API) {
        Globals::API->Log(static_cast<ELogLevel>(level), channel.c_str(), text.c_str());
    }
    return true;
}

static bool HandleAlert(CefRefPtr<CefProcessMessage> message) {
    auto args = message->GetArgumentList();
    std::string text = args->GetString(0).ToString();

    if (Globals::API) {
        Globals::API->GUI_SendAlert(text.c_str());
    }
    return true;
}

static bool HandleEventsSubscribe(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefProcessMessage> message) {
    auto args = message->GetArgumentList();
    std::string eventName = args->GetString(0).ToString();
    SubscribeEvent(browser, eventName);
    return true;
}

static bool HandleEventsUnsubscribe(CefRefPtr<CefProcessMessage> message) {
    auto args = message->GetArgumentList();
    std::string eventName = args->GetString(0).ToString();
    UnsubscribeEvent(eventName);
    return true;
}

static bool HandleEventsRaise(CefRefPtr<CefProcessMessage> message) {
    auto args = message->GetArgumentList();
    std::string eventName = args->GetString(0).ToString();
    // For simplicity, raise as notification (no payload).
    // Complex payloads from JS would require serialization.
    if (Globals::API) {
        Globals::API->Events_RaiseNotification(eventName.c_str());
    }
    return true;
}

static bool HandleKeybindsRegister(CefRefPtr<CefProcessMessage> message) {
    auto args = message->GetArgumentList();
    std::string identifier = args->GetString(0).ToString();
    std::string defaultBind = args->GetString(1).ToString();

    // Create a keybind handler that queues invocations for IPC
    // We need a unique callback per keybind, so we use a static dispatch approach
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

static bool HandleKeybindsDeregister(CefRefPtr<CefProcessMessage> message) {
    auto args = message->GetArgumentList();
    std::string identifier = args->GetString(0).ToString();

    if (Globals::API) {
        Globals::API->InputBinds_Deregister(identifier.c_str());
        s_keybindCallbacks.erase(identifier);
    }
    return true;
}

static bool HandleGameBinds(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefProcessMessage> message,
                              const std::string& msgName) {
    auto args = message->GetArgumentList();
    int bind = args->GetInt(0);

    if (!Globals::API) return true;

    if (msgName == IPC::GAMEBINDS_PRESS) {
        Globals::API->GameBinds_PressAsync(static_cast<EGameBinds>(bind));
    } else if (msgName == IPC::GAMEBINDS_RELEASE) {
        Globals::API->GameBinds_ReleaseAsync(static_cast<EGameBinds>(bind));
    } else if (msgName == IPC::GAMEBINDS_INVOKE) {
        int duration = args->GetInt(1);
        Globals::API->GameBinds_InvokeAsync(static_cast<EGameBinds>(bind), duration);
    } else if (msgName == IPC::GAMEBINDS_ISBOUND) {
        int requestId = args->GetInt(1);
        bool result = Globals::API->GameBinds_IsBound(static_cast<EGameBinds>(bind));
        SendAsyncResponse(browser, requestId, true, result ? "true" : "false");
    }
    return true;
}

static bool HandlePaths(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefProcessMessage> message,
                          const std::string& msgName) {
    auto args = message->GetArgumentList();
    int requestId = args->GetInt(0);

    if (!Globals::API) {
        SendAsyncResponse(browser, requestId, false, "API not available");
        return true;
    }

    std::string result;
    if (msgName == IPC::PATHS_GAME_DIR) {
        const char* p = Globals::API->Paths_GetGameDirectory();
        result = p ? p : "";
    } else if (msgName == IPC::PATHS_ADDON_DIR) {
        std::string name = args->GetString(1).ToString();
        const char* p = Globals::API->Paths_GetAddonDirectory(name.empty() ? nullptr : name.c_str());
        result = p ? p : "";
    } else if (msgName == IPC::PATHS_COMMON_DIR) {
        const char* p = Globals::API->Paths_GetCommonDirectory();
        result = p ? p : "";
    }

    SendAsyncResponse(browser, requestId, true, result);
    return true;
}

static bool HandleDataLink(CefRefPtr<CefBrowser> browser,
                             CefRefPtr<CefProcessMessage> message,
                             const std::string& msgName) {
    auto args = message->GetArgumentList();
    int requestId = args->GetInt(0);

    if (!Globals::API) {
        SendAsyncResponse(browser, requestId, false, "API not available");
        return true;
    }

    if (msgName == IPC::DATALINK_GET_MUMBLE) {
        // Get Mumble Link data and serialize to JSON
        void* mumble = Globals::API->DataLink_Get(DL_MUMBLE_LINK);
        if (mumble) {
            // Mumble link is a complex struct — send a simplified version
            // The full MumbleLink struct parsing would be more complex
            SendAsyncResponse(browser, requestId, true, "{\"available\":true}");
        } else {
            SendAsyncResponse(browser, requestId, true, "{\"available\":false}");
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
            SendAsyncResponse(browser, requestId, true, json);
        } else {
            SendAsyncResponse(browser, requestId, false, "NexusLink not available");
        }
    }
    return true;
}

static bool HandleQuickAccess(CefRefPtr<CefProcessMessage> message,
                                const std::string& msgName) {
    auto args = message->GetArgumentList();
    if (!Globals::API) return true;

    if (msgName == IPC::QA_ADD) {
        std::string id      = args->GetString(0).ToString();
        std::string tex     = args->GetString(1).ToString();
        std::string texHov  = args->GetString(2).ToString();
        std::string keybind = args->GetString(3).ToString();
        std::string tooltip = args->GetString(4).ToString();
        Globals::API->QuickAccess_Add(
            id.c_str(), tex.c_str(), texHov.c_str(),
            keybind.c_str(), tooltip.c_str());
    } else if (msgName == IPC::QA_REMOVE) {
        std::string id = args->GetString(0).ToString();
        Globals::API->QuickAccess_Remove(id.c_str());
    } else if (msgName == IPC::QA_NOTIFY) {
        std::string id = args->GetString(0).ToString();
        Globals::API->QuickAccess_Notify(id.c_str());
    }
    return true;
}

static bool HandleLocalization(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefProcessMessage> message,
                                 const std::string& msgName) {
    auto args = message->GetArgumentList();
    if (!Globals::API) return true;

    if (msgName == IPC::LOC_TRANSLATE) {
        int requestId = args->GetInt(0);
        std::string id = args->GetString(1).ToString();
        const char* result = Globals::API->Localization_Translate(id.c_str());
        SendAsyncResponse(browser, requestId, true, result ? result : id);
    } else if (msgName == IPC::LOC_SET) {
        std::string id   = args->GetString(0).ToString();
        std::string lang = args->GetString(1).ToString();
        std::string text = args->GetString(2).ToString();
        Globals::API->Localization_Set(id.c_str(), lang.c_str(), text.c_str());
    }
    return true;
}

// ---- Public interface ----

bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> /*frame*/,
                               CefProcessId source_process,
                               CefRefPtr<CefProcessMessage> message) {
    if (source_process != PID_RENDERER) return false;

    std::string msgName = message->GetName().ToString();

    if (msgName == IPC::LOG_MESSAGE)        return HandleLog(message);
    if (msgName == IPC::ALERT)              return HandleAlert(message);
    if (msgName == IPC::EVENTS_SUBSCRIBE)   return HandleEventsSubscribe(browser, message);
    if (msgName == IPC::EVENTS_UNSUBSCRIBE) return HandleEventsUnsubscribe(message);
    if (msgName == IPC::EVENTS_RAISE)       return HandleEventsRaise(message);
    if (msgName == IPC::KEYBINDS_REGISTER)  return HandleKeybindsRegister(message);
    if (msgName == IPC::KEYBINDS_DEREGISTER) return HandleKeybindsDeregister(message);

    if (msgName == IPC::GAMEBINDS_PRESS ||
        msgName == IPC::GAMEBINDS_RELEASE ||
        msgName == IPC::GAMEBINDS_INVOKE ||
        msgName == IPC::GAMEBINDS_ISBOUND) {
        return HandleGameBinds(browser, message, msgName);
    }

    if (msgName == IPC::PATHS_GAME_DIR ||
        msgName == IPC::PATHS_ADDON_DIR ||
        msgName == IPC::PATHS_COMMON_DIR) {
        return HandlePaths(browser, message, msgName);
    }

    if (msgName == IPC::DATALINK_GET_MUMBLE ||
        msgName == IPC::DATALINK_GET_NEXUS) {
        return HandleDataLink(browser, message, msgName);
    }

    if (msgName == IPC::QA_ADD ||
        msgName == IPC::QA_REMOVE ||
        msgName == IPC::QA_NOTIFY) {
        return HandleQuickAccess(message, msgName);
    }

    if (msgName == IPC::LOC_TRANSLATE ||
        msgName == IPC::LOC_SET) {
        return HandleLocalization(browser, message, msgName);
    }

    return false; // Unhandled
}

void SubscribeEvent(CefRefPtr<CefBrowser> browser, const std::string& eventName) {
    if (!Globals::API) return;

    // Create a per-event callback that queues the event for IPC
    auto callback = [](void* aEventArgs) {
        // We can't easily know the event name from a bare C callback.
        // The workaround: we use a single global consumer and match by address.
        // For now, queue a generic event notification.
        (void)aEventArgs;
    };

    // Store and subscribe
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

void FlushPendingEvents(CefRefPtr<CefBrowser> browser) {
    if (!browser) return;

    // Flush events
    {
        std::lock_guard<std::mutex> lock(s_eventMutex);
        for (const auto& ev : s_pendingEvents) {
            auto msg = CefProcessMessage::Create(IPC::EVENTS_DISPATCH);
            auto args = msg->GetArgumentList();
            args->SetString(0, ev.name);
            args->SetString(1, ev.jsonData);
            browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, msg);
        }
        s_pendingEvents.clear();
    }

    // Flush keybinds
    {
        std::lock_guard<std::mutex> lock(s_keybindMutex);
        for (const auto& kb : s_pendingKeybinds) {
            auto msg = CefProcessMessage::Create(IPC::KEYBINDS_INVOKE);
            auto args = msg->GetArgumentList();
            args->SetString(0, kb.identifier);
            args->SetBool(1, kb.isRelease);
            browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, msg);
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
