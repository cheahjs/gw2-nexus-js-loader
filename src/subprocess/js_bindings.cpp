#include "js_bindings.h"
#include "ipc_client.h"
#include "shared/ipc_messages.h"

#include "include/cef_v8.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace JsBindings {

// ---- Callback storage for events and keybinds ----

struct EventSubscription {
    CefRefPtr<CefV8Context> context;
    CefRefPtr<CefV8Value>   callback;
};

static std::mutex s_callbackMutex;
static std::unordered_map<std::string, std::vector<EventSubscription>> s_eventCallbacks;
static std::unordered_map<std::string, EventSubscription>             s_keybindCallbacks;

// ---- V8 Handler: routes all nexus.* calls to IPC ----

class NexusV8Handler : public CefV8Handler {
public:
    bool Execute(const CefString& name,
                 CefRefPtr<CefV8Value> object,
                 const CefV8ValueList& arguments,
                 CefRefPtr<CefV8Value>& retval,
                 CefString& exception) override;

private:
    // Helper to create a JS Promise and send an async IPC request.
    // Returns the Promise object.
    CefRefPtr<CefV8Value> CreatePromiseAndSend(
        const std::string& ipcName,
        CefRefPtr<CefListValue> extraArgs,
        CefRefPtr<CefV8Context> context);

    IMPLEMENT_REFCOUNTING(NexusV8Handler);
};

CefRefPtr<CefV8Value> NexusV8Handler::CreatePromiseAndSend(
    const std::string& ipcName,
    CefRefPtr<CefListValue> extraArgs,
    CefRefPtr<CefV8Context> context)
{
    // Create a Promise using V8
    // We use the native Promise constructor via eval
    CefRefPtr<CefV8Value> global = context->GetGlobal();
    CefRefPtr<CefV8Value> promiseCtor = global->GetValue("Promise");

    // Create resolve/reject holders
    CefRefPtr<CefV8Value> resolveHolder;
    CefRefPtr<CefV8Value> rejectHolder;

    // We create the promise using a small inline approach:
    // Store resolve/reject in captured variables via a CefV8Handler callback
    class PromiseExecutor : public CefV8Handler {
    public:
        CefRefPtr<CefV8Value> resolve;
        CefRefPtr<CefV8Value> reject;

        bool Execute(const CefString& /*name*/,
                     CefRefPtr<CefV8Value> /*object*/,
                     const CefV8ValueList& arguments,
                     CefRefPtr<CefV8Value>& /*retval*/,
                     CefString& /*exception*/) override {
            if (arguments.size() >= 2) {
                resolve = arguments[0];
                reject  = arguments[1];
            }
            return true;
        }

        IMPLEMENT_REFCOUNTING(PromiseExecutor);
    };

    CefRefPtr<PromiseExecutor> executor = new PromiseExecutor();
    CefRefPtr<CefV8Value> executorFunc = CefV8Value::CreateFunction("executor", executor);

    CefV8ValueList promiseArgs;
    promiseArgs.push_back(executorFunc);

    CefRefPtr<CefV8Value> promise = promiseCtor->ExecuteFunction(nullptr, promiseArgs);

    // Send the IPC request with the resolve/reject functions
    IpcClient::SendAsyncRequest(ipcName, extraArgs, context,
                                 executor->resolve, executor->reject);

    return promise;
}

bool NexusV8Handler::Execute(const CefString& name,
                              CefRefPtr<CefV8Value> /*object*/,
                              const CefV8ValueList& arguments,
                              CefRefPtr<CefV8Value>& retval,
                              CefString& exception) {
    std::string funcName = name.ToString();
    CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();

    // ---- Logging ----
    if (funcName == "log_info" || funcName == "log_warning" ||
        funcName == "log_critical" || funcName == "log_debug" ||
        funcName == "log_trace") {
        if (arguments.size() < 2) {
            exception = "Expected (channel, message)";
            return true;
        }

        int level = 3; // INFO
        if (funcName == "log_critical") level = 1;
        else if (funcName == "log_warning") level = 2;
        else if (funcName == "log_info") level = 3;
        else if (funcName == "log_debug") level = 4;
        else if (funcName == "log_trace") level = 5;

        auto args = CefListValue::Create();
        args->SetInt(0, level);
        args->SetString(1, arguments[0]->GetStringValue());
        args->SetString(2, arguments[1]->GetStringValue());
        IpcClient::SendMessage(IPC::LOG_MESSAGE, args);
        return true;
    }

    // ---- Alert ----
    if (funcName == "alert") {
        if (arguments.size() < 1) {
            exception = "Expected (message)";
            return true;
        }
        auto args = CefListValue::Create();
        args->SetString(0, arguments[0]->GetStringValue());
        IpcClient::SendMessage(IPC::ALERT, args);
        return true;
    }

    // ---- Events ----
    if (funcName == "events_subscribe") {
        if (arguments.size() < 2 || !arguments[1]->IsFunction()) {
            exception = "Expected (name, callback)";
            return true;
        }
        std::string eventName = arguments[0]->GetStringValue().ToString();

        {
            std::lock_guard<std::mutex> lock(s_callbackMutex);
            s_eventCallbacks[eventName].push_back({ context, arguments[1] });
        }

        auto args = CefListValue::Create();
        args->SetString(0, eventName);
        IpcClient::SendMessage(IPC::EVENTS_SUBSCRIBE, args);
        return true;
    }

    if (funcName == "events_unsubscribe") {
        if (arguments.size() < 2 || !arguments[1]->IsFunction()) {
            exception = "Expected (name, callback)";
            return true;
        }
        std::string eventName = arguments[0]->GetStringValue().ToString();

        {
            std::lock_guard<std::mutex> lock(s_callbackMutex);
            auto it = s_eventCallbacks.find(eventName);
            if (it != s_eventCallbacks.end()) {
                auto& subs = it->second;
                subs.erase(
                    std::remove_if(subs.begin(), subs.end(),
                        [&](const EventSubscription& sub) {
                            return sub.callback->IsSame(arguments[1]);
                        }),
                    subs.end());
                if (subs.empty()) {
                    s_eventCallbacks.erase(it);
                    auto args = CefListValue::Create();
                    args->SetString(0, eventName);
                    IpcClient::SendMessage(IPC::EVENTS_UNSUBSCRIBE, args);
                }
            }
        }
        return true;
    }

    if (funcName == "events_raise") {
        if (arguments.size() < 1) {
            exception = "Expected (name[, data])";
            return true;
        }
        auto args = CefListValue::Create();
        args->SetString(0, arguments[0]->GetStringValue());
        IpcClient::SendMessage(IPC::EVENTS_RAISE, args);
        return true;
    }

    // ---- Keybinds ----
    if (funcName == "keybinds_register") {
        if (arguments.size() < 3 || !arguments[2]->IsFunction()) {
            exception = "Expected (id, defaultBind, callback)";
            return true;
        }
        std::string id = arguments[0]->GetStringValue().ToString();
        std::string defaultBind = arguments[1]->GetStringValue().ToString();

        {
            std::lock_guard<std::mutex> lock(s_callbackMutex);
            s_keybindCallbacks[id] = { context, arguments[2] };
        }

        auto args = CefListValue::Create();
        args->SetString(0, id);
        args->SetString(1, defaultBind);
        IpcClient::SendMessage(IPC::KEYBINDS_REGISTER, args);
        return true;
    }

    if (funcName == "keybinds_deregister") {
        if (arguments.size() < 1) {
            exception = "Expected (id)";
            return true;
        }
        std::string id = arguments[0]->GetStringValue().ToString();

        {
            std::lock_guard<std::mutex> lock(s_callbackMutex);
            s_keybindCallbacks.erase(id);
        }

        auto args = CefListValue::Create();
        args->SetString(0, id);
        IpcClient::SendMessage(IPC::KEYBINDS_DEREGISTER, args);
        return true;
    }

    // ---- Game Binds ----
    if (funcName == "gamebinds_press") {
        if (arguments.size() < 1) { exception = "Expected (bind)"; return true; }
        auto args = CefListValue::Create();
        args->SetInt(0, arguments[0]->GetIntValue());
        IpcClient::SendMessage(IPC::GAMEBINDS_PRESS, args);
        return true;
    }

    if (funcName == "gamebinds_release") {
        if (arguments.size() < 1) { exception = "Expected (bind)"; return true; }
        auto args = CefListValue::Create();
        args->SetInt(0, arguments[0]->GetIntValue());
        IpcClient::SendMessage(IPC::GAMEBINDS_RELEASE, args);
        return true;
    }

    if (funcName == "gamebinds_invoke") {
        if (arguments.size() < 2) { exception = "Expected (bind, durationMs)"; return true; }
        auto args = CefListValue::Create();
        args->SetInt(0, arguments[0]->GetIntValue());
        args->SetInt(1, arguments[1]->GetIntValue());
        IpcClient::SendMessage(IPC::GAMEBINDS_INVOKE, args);
        return true;
    }

    if (funcName == "gamebinds_isBound") {
        if (arguments.size() < 1) { exception = "Expected (bind)"; return true; }
        auto extraArgs = CefListValue::Create();
        extraArgs->SetInt(0, arguments[0]->GetIntValue());
        retval = CreatePromiseAndSend(IPC::GAMEBINDS_ISBOUND, extraArgs, context);
        return true;
    }

    // ---- DataLink ----
    if (funcName == "datalink_getMumbleLink") {
        retval = CreatePromiseAndSend(IPC::DATALINK_GET_MUMBLE, nullptr, context);
        return true;
    }

    if (funcName == "datalink_getNexusLink") {
        retval = CreatePromiseAndSend(IPC::DATALINK_GET_NEXUS, nullptr, context);
        return true;
    }

    // ---- Paths ----
    if (funcName == "paths_getGameDirectory") {
        retval = CreatePromiseAndSend(IPC::PATHS_GAME_DIR, nullptr, context);
        return true;
    }

    if (funcName == "paths_getAddonDirectory") {
        auto extraArgs = CefListValue::Create();
        if (arguments.size() > 0) {
            extraArgs->SetString(0, arguments[0]->GetStringValue());
        } else {
            extraArgs->SetString(0, "");
        }
        retval = CreatePromiseAndSend(IPC::PATHS_ADDON_DIR, extraArgs, context);
        return true;
    }

    if (funcName == "paths_getCommonDirectory") {
        retval = CreatePromiseAndSend(IPC::PATHS_COMMON_DIR, nullptr, context);
        return true;
    }

    // ---- Quick Access ----
    if (funcName == "quickaccess_add") {
        if (arguments.size() < 5) {
            exception = "Expected (id, texture, textureHover, keybind, tooltip)";
            return true;
        }
        auto args = CefListValue::Create();
        for (int i = 0; i < 5; ++i) {
            args->SetString(i, arguments[i]->GetStringValue());
        }
        IpcClient::SendMessage(IPC::QA_ADD, args);
        return true;
    }

    if (funcName == "quickaccess_remove") {
        if (arguments.size() < 1) { exception = "Expected (id)"; return true; }
        auto args = CefListValue::Create();
        args->SetString(0, arguments[0]->GetStringValue());
        IpcClient::SendMessage(IPC::QA_REMOVE, args);
        return true;
    }

    if (funcName == "quickaccess_notify") {
        if (arguments.size() < 1) { exception = "Expected (id)"; return true; }
        auto args = CefListValue::Create();
        args->SetString(0, arguments[0]->GetStringValue());
        IpcClient::SendMessage(IPC::QA_NOTIFY, args);
        return true;
    }

    // ---- Localization ----
    if (funcName == "localization_translate") {
        if (arguments.size() < 1) { exception = "Expected (id)"; return true; }
        auto extraArgs = CefListValue::Create();
        extraArgs->SetString(0, arguments[0]->GetStringValue());
        retval = CreatePromiseAndSend(IPC::LOC_TRANSLATE, extraArgs, context);
        return true;
    }

    if (funcName == "localization_set") {
        if (arguments.size() < 3) {
            exception = "Expected (id, lang, text)";
            return true;
        }
        auto args = CefListValue::Create();
        args->SetString(0, arguments[0]->GetStringValue());
        args->SetString(1, arguments[1]->GetStringValue());
        args->SetString(2, arguments[2]->GetStringValue());
        IpcClient::SendMessage(IPC::LOC_SET, args);
        return true;
    }

    exception = "Unknown function: " + funcName;
    return true;
}

// ---- V8 Extension Registration ----

void RegisterExtension() {
    // JavaScript source that creates the nexus.* namespace and bridges to native functions.
    // Each native function is registered as a separate V8 handler function,
    // then organized into the nexus namespace object.
    std::string extensionCode = R"(
        var nexus;
        if (!nexus) nexus = {};

        (function() {
            // Logging
            nexus.log = {
                info:     function(channel, message) { native function log_info();     return log_info(channel, message); },
                warning:  function(channel, message) { native function log_warning();  return log_warning(channel, message); },
                critical: function(channel, message) { native function log_critical(); return log_critical(channel, message); },
                debug:    function(channel, message) { native function log_debug();    return log_debug(channel, message); },
                trace:    function(channel, message) { native function log_trace();    return log_trace(channel, message); }
            };

            // Events
            nexus.events = {
                subscribe:   function(name, callback) { native function events_subscribe();   return events_subscribe(name, callback); },
                unsubscribe: function(name, callback) { native function events_unsubscribe(); return events_unsubscribe(name, callback); },
                raise:       function(name, data)     { native function events_raise();       return events_raise(name, data); }
            };

            // Keybinds
            nexus.keybinds = {
                register:   function(id, defaultBind, callback) { native function keybinds_register();   return keybinds_register(id, defaultBind, callback); },
                deregister: function(id)                        { native function keybinds_deregister(); return keybinds_deregister(id); }
            };

            // Game binds
            nexus.gamebinds = {
                press:   function(bind)            { native function gamebinds_press();   return gamebinds_press(bind); },
                release: function(bind)            { native function gamebinds_release(); return gamebinds_release(bind); },
                invoke:  function(bind, durationMs){ native function gamebinds_invoke();  return gamebinds_invoke(bind, durationMs); },
                isBound: function(bind)            { native function gamebinds_isBound(); return gamebinds_isBound(bind); }
            };

            // DataLink
            nexus.datalink = {
                getMumbleLink: function() { native function datalink_getMumbleLink(); return datalink_getMumbleLink(); },
                getNexusLink:  function() { native function datalink_getNexusLink();  return datalink_getNexusLink(); }
            };

            // Paths
            nexus.paths = {
                getGameDirectory:  function()     { native function paths_getGameDirectory();  return paths_getGameDirectory(); },
                getAddonDirectory: function(name) { native function paths_getAddonDirectory(); return paths_getAddonDirectory(name); },
                getCommonDirectory: function()    { native function paths_getCommonDirectory(); return paths_getCommonDirectory(); }
            };

            // Quick Access
            nexus.quickaccess = {
                add:    function(id, texture, textureHover, keybind, tooltip) { native function quickaccess_add();    return quickaccess_add(id, texture, textureHover, keybind, tooltip); },
                remove: function(id) { native function quickaccess_remove(); return quickaccess_remove(id); },
                notify: function(id) { native function quickaccess_notify(); return quickaccess_notify(id); }
            };

            // Localization
            nexus.localization = {
                translate: function(id)             { native function localization_translate(); return localization_translate(id); },
                set:       function(id, lang, text) { native function localization_set();      return localization_set(id, lang, text); }
            };

            // Alert
            nexus.alert = function(message) { native function alert(); return alert(message); };
        })();
    )";

    CefRefPtr<NexusV8Handler> handler = new NexusV8Handler();
    CefRegisterExtension("v8/nexus", extensionCode, handler);
}

// ---- Event/Keybind dispatch from browser process ----

bool HandleEventDispatch(CefRefPtr<CefProcessMessage> message) {
    auto args = message->GetArgumentList();
    std::string eventName = args->GetString(0).ToString();
    std::string jsonData  = args->GetString(1).ToString();

    std::lock_guard<std::mutex> lock(s_callbackMutex);
    auto it = s_eventCallbacks.find(eventName);
    if (it == s_eventCallbacks.end()) return true;

    for (auto& sub : it->second) {
        if (sub.context && sub.callback && sub.callback->IsFunction()) {
            sub.context->Enter();

            CefV8ValueList callArgs;
            if (!jsonData.empty()) {
                // Parse JSON string to V8 value
                CefRefPtr<CefV8Value> global = sub.context->GetGlobal();
                CefRefPtr<CefV8Value> jsonObj = global->GetValue("JSON");
                CefRefPtr<CefV8Value> parseFunc = jsonObj->GetValue("parse");

                CefV8ValueList parseArgs;
                parseArgs.push_back(CefV8Value::CreateString(jsonData));
                CefRefPtr<CefV8Value> parsed = parseFunc->ExecuteFunction(jsonObj, parseArgs);
                if (parsed) {
                    callArgs.push_back(parsed);
                }
            }

            sub.callback->ExecuteFunction(nullptr, callArgs);
            sub.context->Exit();
        }
    }

    return true;
}

bool HandleKeybindInvoke(CefRefPtr<CefProcessMessage> message) {
    auto args = message->GetArgumentList();
    std::string identifier = args->GetString(0).ToString();
    bool isRelease = args->GetBool(1);

    std::lock_guard<std::mutex> lock(s_callbackMutex);
    auto it = s_keybindCallbacks.find(identifier);
    if (it == s_keybindCallbacks.end()) return true;

    auto& sub = it->second;
    if (sub.context && sub.callback && sub.callback->IsFunction()) {
        sub.context->Enter();

        CefV8ValueList callArgs;
        callArgs.push_back(CefV8Value::CreateString(identifier));
        callArgs.push_back(CefV8Value::CreateBool(isRelease));
        sub.callback->ExecuteFunction(nullptr, callArgs);

        sub.context->Exit();
    }

    return true;
}

} // namespace JsBindings
