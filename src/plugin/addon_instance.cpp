#include "addon_instance.h"
#include "addon_manager.h"
#include "globals.h"
#include "shared/version.h"

#include "nlohmann/json.hpp"

using json = nlohmann::json;

// ---- Global trampoline table for event dispatch ----
// Nexus Events_Subscribe requires a plain C function pointer void(*)(void*).
// We use a template-generated trampoline table shared across all addons.
// Each slot stores {addonId, eventName} so the trampoline can route to the
// correct addon's pending event queue.

static constexpr int GLOBAL_MAX_EVENT_SLOTS = 256;

struct TrampolineSlot {
    std::string addonId;
    std::string eventName;
    bool used = false;
};

static TrampolineSlot s_globalSlots[GLOBAL_MAX_EVENT_SLOTS];
static std::mutex s_globalSlotMutex;

template<int N>
static void GlobalEventTrampoline(void* /*aEventArgs*/) {
    std::string addonId;
    std::string eventName;
    {
        std::lock_guard<std::mutex> lock(s_globalSlotMutex);
        auto& slot = s_globalSlots[N];
        if (!slot.used) return;
        addonId = slot.addonId;
        eventName = slot.eventName;
    }
    auto* addon = AddonManager::GetAddon(addonId);
    if (!addon) return;
    addon->QueueEvent(eventName);
}

template<int... Is>
static std::array<EVENT_CONSUME, sizeof...(Is)>
MakeGlobalTrampolineTable(std::integer_sequence<int, Is...>) {
    return {{ &GlobalEventTrampoline<Is>... }};
}

static auto s_globalTrampolines = MakeGlobalTrampolineTable(
    std::make_integer_sequence<int, GLOBAL_MAX_EVENT_SLOTS>{});

static int AllocateGlobalSlot(const std::string& addonId, const std::string& eventName) {
    for (int i = 0; i < GLOBAL_MAX_EVENT_SLOTS; ++i) {
        if (!s_globalSlots[i].used) {
            s_globalSlots[i].used = true;
            s_globalSlots[i].addonId = addonId;
            s_globalSlots[i].eventName = eventName;
            return i;
        }
    }
    return -1;
}

static void FreeGlobalSlot(int slot) {
    if (slot >= 0 && slot < GLOBAL_MAX_EVENT_SLOTS) {
        s_globalSlots[slot].used = false;
        s_globalSlots[slot].addonId.clear();
        s_globalSlots[slot].eventName.clear();
    }
}

// ---- AddonInstance implementation ----

AddonInstance::AddonInstance(const AddonManifest& manifest)
    : m_manifest(manifest)
    , m_state(AddonState::Discovered) {
}

AddonInstance::~AddonInstance() {
    Shutdown();
}

void AddonInstance::CreateMainBrowser() {
    m_state = AddonState::Loading;

    auto browser = CefRefPtr<InProcessBrowser>(new InProcessBrowser());
    browser->SetAddonId(m_manifest.id);
    browser->SetWindowId("main");

    std::string url = "https://" + m_manifest.id + ".jsloader.local/" + m_manifest.entry;

    WindowInfo window;
    window.windowId = "main";
    window.title = m_manifest.name;
    window.width = 800;
    window.height = 600;
    window.browser = browser;

    if (browser->Create(url, window.width, window.height)) {
        m_windows["main"] = std::move(window);
    } else {
        m_state = AddonState::Error;
        if (Globals::API) {
            Globals::API->Log(LOGL_CRITICAL, ADDON_NAME,
                (std::string("Failed to create browser for addon '") + m_manifest.id + "'").c_str());
        }
    }
}

bool AddonInstance::CreateAddonWindow(const std::string& windowId, const std::string& url,
                                  int width, int height, const std::string& title) {
    if (m_windows.count(windowId)) return false; // already exists

    auto browser = CefRefPtr<InProcessBrowser>(new InProcessBrowser());
    browser->SetAddonId(m_manifest.id);
    browser->SetWindowId(windowId);

    // Resolve relative URLs against addon's base URL
    std::string fullUrl = url;
    if (url.find("://") == std::string::npos) {
        fullUrl = "https://" + m_manifest.id + ".jsloader.local/" + url;
    }

    WindowInfo window;
    window.windowId = windowId;
    window.title = title.empty() ? m_manifest.name : title;
    window.width = (width > 0) ? width : 800;
    window.height = (height > 0) ? height : 600;
    window.browser = browser;

    if (browser->Create(fullUrl, window.width, window.height)) {
        m_windows[windowId] = std::move(window);
        return true;
    }
    return false;
}

void AddonInstance::CloseWindow(const std::string& windowId) {
    if (windowId == "main") return; // don't allow closing the main window via JS

    auto it = m_windows.find(windowId);
    if (it != m_windows.end()) {
        if (it->second.browser) {
            it->second.browser->Close();
        }
        m_windows.erase(it);
    }
}

void AddonInstance::UpdateWindow(const std::string& windowId, const std::string& title,
                                  int width, int height, bool visible) {
    auto* w = GetWindow(windowId);
    if (!w) return;

    if (!title.empty()) w->title = title;
    if (width > 0 && height > 0) {
        w->width = width;
        w->height = height;
    }
    w->visible = visible;
}

void AddonInstance::SetInputPassthrough(const std::string& windowId, bool enabled) {
    auto* w = GetWindow(windowId);
    if (w) w->inputPassthrough = enabled;
}

WindowInfo* AddonInstance::GetWindow(const std::string& windowId) {
    auto it = m_windows.find(windowId);
    return (it != m_windows.end()) ? &it->second : nullptr;
}

void AddonInstance::FlushFrames() {
    for (auto& [id, window] : m_windows) {
        if (window.browser) {
            window.browser->FlushFrame();
        }
    }
    if (m_devTools) m_devTools->FlushFrame();
}

void AddonInstance::FlushPendingEvents() {
    // Find the main browser to dispatch events to
    auto* mainWindow = GetWindow("main");
    if (!mainWindow || !mainWindow->browser || !mainWindow->browser->IsReady()) return;

    // Flush events
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        for (const auto& ev : m_pendingEvents) {
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
            mainWindow->browser->ExecuteJavaScript(code);
        }
        m_pendingEvents.clear();
    }

    // Flush keybinds
    {
        std::lock_guard<std::mutex> lock(m_keybindMutex);
        for (const auto& kb : m_pendingKeybinds) {
            json j;
            j["type"] = "keybind";
            j["id"] = kb.identifier;
            j["isRelease"] = kb.isRelease;
            std::string code = "window.__nexus_dispatch(" + j.dump() + ");";
            mainWindow->browser->ExecuteJavaScript(code);
        }
        m_pendingKeybinds.clear();
    }
}

void AddonInstance::Shutdown() {
    // Close DevTools
    CloseDevTools();

    // Unsubscribe all events
    if (Globals::API) {
        for (const auto& [eventName, slot] : m_eventSlots) {
            Globals::API->Events_Unsubscribe(eventName.c_str(), s_globalTrampolines[slot]);
            FreeGlobalSlot(slot);
        }
    }
    m_eventSlots.clear();

    // Deregister all keybinds (use prefixed ID as registered with Nexus)
    if (Globals::API) {
        for (const auto& id : m_registeredKeybinds) {
            std::string fullId = "JSLOADER_" + m_manifest.id + "_" + id;
            Globals::API->InputBinds_Deregister(fullId.c_str());
        }
    }
    m_registeredKeybinds.clear();

    // Clear pending queues
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        m_pendingEvents.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_keybindMutex);
        m_pendingKeybinds.clear();
    }

    // Close all browsers
    for (auto& [id, window] : m_windows) {
        if (window.browser) {
            window.browser->Close();
        }
    }
    m_windows.clear();

    m_state = AddonState::Unloaded;
}

bool AddonInstance::IsAnyBrowserReady() const {
    for (const auto& [id, window] : m_windows) {
        if (window.browser && window.browser->IsReady()) return true;
    }
    return false;
}

bool AddonInstance::CheckBrowserHealth(DWORD timeoutMs) {
    if (m_state == AddonState::Error) return false;

    for (auto& [id, window] : m_windows) {
        if (!window.browser) continue;
        if (window.browser->IsReady()) {
            if (m_state == AddonState::Loading) {
                m_state = AddonState::Running;
            }
            continue;
        }

        if (window.browser->HasCreationFailed()) {
            m_state = AddonState::Error;
            if (Globals::API) {
                Globals::API->Log(LOGL_CRITICAL, ADDON_NAME,
                    (std::string("Browser creation failed for addon '") +
                     m_manifest.id + "' window '" + id + "'").c_str());
            }
            return false;
        }

        DWORD elapsed = GetTickCount() - window.browser->GetCreationRequestTick();
        if (elapsed > timeoutMs) {
            m_state = AddonState::Error;
            if (Globals::API) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                    "Browser creation timed out for addon '%s' window '%s' (%lu ms).",
                    m_manifest.id.c_str(), id.c_str(), elapsed);
                Globals::API->Log(LOGL_CRITICAL, ADDON_NAME, msg);
            }
            return false;
        }
    }
    return true;
}

void AddonInstance::OpenDevTools() {
    auto* mainWindow = GetWindow("main");
    if (!mainWindow || !mainWindow->browser || !mainWindow->browser->IsReady()) return;
    if (m_devTools) return; // already open

    m_devTools = new InProcessBrowser();

    CefWindowInfo windowInfo;
    windowInfo.SetAsWindowless(0);

    CefBrowserSettings settings;
    settings.windowless_frame_rate = 30;

    mainWindow->browser->GetBrowser()->GetHost()->ShowDevTools(
        windowInfo, CefRefPtr<CefClient>(m_devTools.get()), settings, CefPoint());

    if (Globals::API) {
        Globals::API->Log(LOGL_INFO, ADDON_NAME,
            (std::string("DevTools opened for addon '") + m_manifest.id + "'").c_str());
    }
}

void AddonInstance::CloseDevTools() {
    if (m_devTools) {
        auto* mainWindow = GetWindow("main");
        if (mainWindow && mainWindow->browser && mainWindow->browser->GetBrowser()) {
            mainWindow->browser->GetBrowser()->GetHost()->CloseDevTools();
        }
        m_devTools = nullptr;
    }
}

bool AddonInstance::IsDevToolsOpen() const {
    return m_devTools && m_devTools->IsReady();
}

InProcessBrowser* AddonInstance::GetDevToolsBrowser() {
    return m_devTools.get();
}

// ---- Per-addon IPC state ----

void AddonInstance::SubscribeEvent(const std::string& eventName) {
    if (!Globals::API) return;
    if (m_eventSlots.count(eventName)) return; // already subscribed

    std::lock_guard<std::mutex> lock(s_globalSlotMutex);
    int slot = AllocateGlobalSlot(m_manifest.id, eventName);
    if (slot < 0) {
        if (Globals::API) {
            Globals::API->Log(LOGL_WARNING, ADDON_NAME,
                "Too many event subscriptions — global max reached.");
        }
        return;
    }

    m_eventSlots[eventName] = slot;
    Globals::API->Events_Subscribe(eventName.c_str(), s_globalTrampolines[slot]);

    Globals::API->Log(LOGL_DEBUG, ADDON_NAME,
        (std::string("Addon '") + m_manifest.id + "' subscribed to event: " + eventName).c_str());
}

void AddonInstance::UnsubscribeEvent(const std::string& eventName) {
    if (!Globals::API) return;

    auto it = m_eventSlots.find(eventName);
    if (it != m_eventSlots.end()) {
        int slot = it->second;
        Globals::API->Events_Unsubscribe(eventName.c_str(), s_globalTrampolines[slot]);
        std::lock_guard<std::mutex> lock(s_globalSlotMutex);
        FreeGlobalSlot(slot);
        m_eventSlots.erase(it);
    }
}

// Global keybind callback: parses the addon ID from the prefixed identifier
// to route to the correct addon. This is a non-capturing function that can
// be used as a C function pointer (INPUTBINDS_PROCESS).
static void GlobalKeybindCallback(const char* aIdentifier, bool aIsRelease) {
    // Identifier format: "JSLOADER_<addonId>_<keybindId>"
    std::string id(aIdentifier);
    const std::string prefix = "JSLOADER_";
    if (id.substr(0, prefix.size()) != prefix) return;

    std::string rest = id.substr(prefix.size());
    auto sep = rest.find('_');
    if (sep == std::string::npos) return;

    std::string addonId = rest.substr(0, sep);
    std::string keybindId = rest.substr(sep + 1);

    auto* addon = AddonManager::GetAddon(addonId);
    if (!addon) return;
    addon->QueueKeybind(keybindId, aIsRelease);
}

void AddonInstance::RegisterKeybind(const std::string& identifier, const std::string& defaultBind) {
    if (!Globals::API || identifier.empty()) return;

    // Prefix keybind ID with addon ID to avoid collisions
    std::string fullId = "JSLOADER_" + m_manifest.id + "_" + identifier;

    Globals::API->InputBinds_RegisterWithString(
        fullId.c_str(), GlobalKeybindCallback, defaultBind.c_str());
    m_registeredKeybinds.insert(identifier);
}

void AddonInstance::DeregisterKeybind(const std::string& identifier) {
    if (!Globals::API || identifier.empty()) return;

    std::string fullId = "JSLOADER_" + m_manifest.id + "_" + identifier;
    Globals::API->InputBinds_Deregister(fullId.c_str());
    m_registeredKeybinds.erase(identifier);
}

void AddonInstance::QueueEvent(const std::string& name, const std::string& jsonData) {
    std::lock_guard<std::mutex> lock(m_eventMutex);
    m_pendingEvents.push_back({name, jsonData});
}

void AddonInstance::QueueKeybind(const std::string& identifier, bool isRelease) {
    std::lock_guard<std::mutex> lock(m_keybindMutex);
    m_pendingKeybinds.push_back({identifier, isRelease});
}

void AddonInstance::SendAsyncResponse(InProcessBrowser* browser, int requestId,
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
