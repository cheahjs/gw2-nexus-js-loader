#pragma once

#include "addon_manager.h"
#include "in_process_browser.h"

#include "include/cef_browser.h"

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

enum class AddonState { Discovered, Loading, Running, Error, Unloaded };

struct WindowInfo {
    std::string windowId;          // "main", "settings", etc.
    std::string title;
    int width = 800, height = 600;
    bool visible = true;
    int alphaThreshold = 0;         // 0=capture all, 1-255=alpha-based, 256=full passthrough
    CefRefPtr<InProcessBrowser> browser;

    // ImGui bounds (updated each frame by overlay)
    float windowX = 0, windowY = 0, windowW = 0, windowH = 0;
    float contentX = 0, contentY = 0;
    int contentW = 0, contentH = 0;
    bool hasFocus = false;
    bool contentHovered = false;
};

// Per-addon runtime state: owns manifest, windows, browsers, IPC state.
class AddonInstance {
public:
    explicit AddonInstance(const AddonManifest& manifest);
    ~AddonInstance();

    // Create the main browser window loading the addon entry point.
    void CreateMainBrowser();

    // Create an additional window. Returns true if created.
    // NOTE: Named CreateAddonWindow to avoid conflict with Win32 CreateWindow macro.
    bool CreateAddonWindow(const std::string& windowId, const std::string& url,
                           int width, int height, const std::string& title);

    // Close a window by ID.
    void CloseWindow(const std::string& windowId);

    // Update window properties (title, width, height, visible).
    void UpdateWindow(const std::string& windowId, const std::string& title,
                      int width, int height, bool visible);

    // Set input passthrough flag for a window.
    void SetInputPassthrough(const std::string& windowId, int alphaThreshold);

    // Get all windows.
    std::map<std::string, WindowInfo>& GetWindows() { return m_windows; }
    const std::map<std::string, WindowInfo>& GetWindows() const { return m_windows; }

    // Get a specific window.
    WindowInfo* GetWindow(const std::string& windowId);

    // Flush all browser frames (call from render thread).
    void FlushFrames();

    // Flush pending events/keybinds to the main browser.
    void FlushPendingEvents();

    // Shut down: close all browsers, clean up IPC state.
    void Shutdown();

    // State queries
    const AddonManifest& GetManifest() const { return m_manifest; }
    AddonState GetState() const { return m_state; }
    bool IsAnyBrowserReady() const;

    // Check browser health (timeout / crash detection). Returns true if healthy.
    bool CheckBrowserHealth(DWORD timeoutMs);

    // DevTools for main browser
    void OpenDevTools();
    void CloseDevTools();
    bool IsDevToolsOpen() const;
    InProcessBrowser* GetDevToolsBrowser();

    // ---- Per-addon IPC state ----

    // Event subscriptions
    void SubscribeEvent(const std::string& eventName);
    void UnsubscribeEvent(const std::string& eventName);

    // Keybind registrations
    void RegisterKeybind(const std::string& identifier, const std::string& defaultBind);
    void DeregisterKeybind(const std::string& identifier);

    // Send async response to specific browser
    void SendAsyncResponse(InProcessBrowser* browser, int requestId,
                           bool success, const std::string& value);

    const std::string& GetId() const { return m_manifest.id; }

    // Queue an event into the addon's pending events (thread-safe).
    // Called by global event trampolines.
    void QueueEvent(const std::string& name, const std::string& jsonData = "");

    // Queue a keybind into the addon's pending keybinds (thread-safe).
    void QueueKeybind(const std::string& identifier, bool isRelease);

private:
    AddonManifest m_manifest;
    AddonState    m_state = AddonState::Discovered;
    std::map<std::string, WindowInfo> m_windows;

    // DevTools
    CefRefPtr<InProcessBrowser> m_devTools;

    // Per-addon event dispatch
    struct PendingEvent {
        std::string name;
        std::string jsonData;
    };
    std::mutex                    m_eventMutex;
    std::vector<PendingEvent>     m_pendingEvents;

    static constexpr int MAX_EVENT_SLOTS = 64;
    std::string m_eventSlotNames[MAX_EVENT_SLOTS];
    bool        m_eventSlotUsed[MAX_EVENT_SLOTS] = {};
    std::unordered_map<std::string, int> m_eventSlots;

    // Per-addon keybind dispatch
    struct PendingKeybind {
        std::string identifier;
        bool        isRelease;
    };
    std::mutex                          m_keybindMutex;
    std::vector<PendingKeybind>         m_pendingKeybinds;
    std::unordered_set<std::string>     m_registeredKeybinds; // un-prefixed IDs
};
