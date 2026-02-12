#include "overlay.h"
#include "globals.h"
#include "addon_manager.h"
#include "addon_instance.h"
#include "in_process_browser.h"
#include "shared/version.h"

#include "imgui.h"

namespace Overlay {

// Tracks which addon/window had ImGui focus last frame
static AddonInstance* s_focusedAddon = nullptr;
static WindowInfo*    s_focusedWindow = nullptr;

// DevTools state per addon (tracked by addon ID)
static std::string s_devToolsAddonId;
static float s_dtX = 0.0f, s_dtY = 0.0f;
static int   s_dtW = 0, s_dtH = 0;
static bool  s_dtFocus = false;
static bool  s_dtHovered = false;

static void RenderDevToolsWindow(AddonInstance* addon) {
    if (!addon->IsDevToolsOpen()) return;

    InProcessBrowser* devTools = addon->GetDevToolsBrowser();
    if (!devTools) return;

    void* textureHandle = devTools->GetTextureHandle();
    if (!textureHandle) return;

    int texW = devTools->GetWidth();
    int texH = devTools->GetHeight();
    if (texW <= 0 || texH <= 0) return;

    std::string title = "DevTools [" + addon->GetManifest().name + "]##DevTools_" + addon->GetId();

    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(400, 300), ImVec2(4096, 4096));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar
                           | ImGuiWindowFlags_NoScrollWithMouse
                           | ImGuiWindowFlags_NoCollapse;

    bool visible = true;
    if (ImGui::Begin(title.c_str(), &visible, flags)) {
        s_dtFocus = ImGui::IsWindowFocused();

        ImVec2 pos = ImGui::GetCursorScreenPos();
        s_dtX = pos.x;
        s_dtY = pos.y;

        ImVec2 avail = ImGui::GetContentRegionAvail();
        int contentW = static_cast<int>(avail.x);
        int contentH = static_cast<int>(avail.y);
        if (contentW > 0 && contentH > 0) {
            s_dtW = contentW;
            s_dtH = contentH;
            if (contentW != texW || contentH != texH) {
                devTools->Resize(contentW, contentH);
            }
        } else {
            s_dtW = texW;
            s_dtH = texH;
        }

        ImGui::Image(textureHandle, ImVec2(static_cast<float>(s_dtW), static_cast<float>(s_dtH)));
        s_dtHovered = ImGui::IsItemHovered();

        if (s_dtFocus) {
            s_focusedAddon = addon;
            s_focusedWindow = nullptr; // DevTools is not a regular window
        }
    } else {
        s_dtFocus = false;
        s_dtW = 0;
        s_dtH = 0;
    }
    ImGui::End();

    if (!visible) {
        addon->CloseDevTools();
    }
}

void Render() {
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(Globals::API->ImguiContext));

    s_focusedAddon = nullptr;
    s_focusedWindow = nullptr;
    s_dtHovered = false;

    for (auto& [addonId, addon] : AddonManager::GetAddons()) {
        for (auto& [windowId, window] : addon->GetWindows()) {
            // Reset content hovered state
            window.contentHovered = false;

            if (!window.visible) continue;
            if (!Globals::OverlayVisible) continue;
            if (!window.browser || !window.browser->IsReady()) continue;

            void* textureHandle = window.browser->GetTextureHandle();
            if (!textureHandle) continue;

            int texW = window.browser->GetWidth();
            int texH = window.browser->GetHeight();
            if (texW <= 0 || texH <= 0) continue;

            // Unique ImGui ID: "title##addonId_windowId"
            std::string imguiId = window.title + "##" + addonId + "_" + windowId;

            ImGui::SetNextWindowSize(
                ImVec2(static_cast<float>(window.width), static_cast<float>(window.height) + 20.0f),
                ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSizeConstraints(ImVec2(320, 200), ImVec2(4096, 4096));

            ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar
                                   | ImGuiWindowFlags_NoScrollWithMouse
                                   | ImGuiWindowFlags_NoCollapse;

            if (ImGui::Begin(imguiId.c_str(), &window.visible, flags)) {
                // Track full window bounds for input hit testing
                ImVec2 wpos = ImGui::GetWindowPos();
                ImVec2 wsize = ImGui::GetWindowSize();
                window.windowX = wpos.x;
                window.windowY = wpos.y;
                window.windowW = wsize.x;
                window.windowH = wsize.y;
                window.hasFocus = ImGui::IsWindowFocused();

                // Track content area position
                ImVec2 pos = ImGui::GetCursorScreenPos();
                window.contentX = pos.x;
                window.contentY = pos.y;

                // Resize browser to match available content area
                ImVec2 avail = ImGui::GetContentRegionAvail();
                int contentW = static_cast<int>(avail.x);
                int contentH = static_cast<int>(avail.y);
                if (contentW > 0 && contentH > 0) {
                    window.contentW = contentW;
                    window.contentH = contentH;
                    if (contentW != texW || contentH != texH) {
                        window.browser->Resize(contentW, contentH);
                    }
                } else {
                    window.contentW = texW;
                    window.contentH = texH;
                }

                ImGui::Image(textureHandle,
                    ImVec2(static_cast<float>(window.contentW), static_cast<float>(window.contentH)));
                window.contentHovered = ImGui::IsItemHovered();

                if (window.hasFocus) {
                    s_focusedAddon = addon.get();
                    s_focusedWindow = &window;
                }
            } else {
                // Window is collapsed
                ImVec2 wpos = ImGui::GetWindowPos();
                ImVec2 wsize = ImGui::GetWindowSize();
                window.windowX = wpos.x;
                window.windowY = wpos.y;
                window.windowW = wsize.x;
                window.windowH = wsize.y;
                window.hasFocus = ImGui::IsWindowFocused();
                window.contentW = 0;
                window.contentH = 0;
            }
            ImGui::End();
        }

        // Render DevTools for this addon if open
        RenderDevToolsWindow(addon.get());
    }
}

void RenderOptions() {
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(Globals::API->ImguiContext));

    ImGui::TextUnformatted("JS Loader Settings");
    ImGui::Separator();

    ImGui::Text("Overlay toggle: ALT+SHIFT+L");
    ImGui::Text("Status: %s", Globals::OverlayVisible ? "Visible" : "Hidden");

    ImGui::Separator();

    const auto& addons = AddonManager::GetAddons();
    if (addons.empty()) {
        ImGui::TextDisabled("No addons loaded.");
        ImGui::TextWrapped("Place addon directories in the jsloader addon directory. "
                           "Each addon needs a manifest.json file.");
    } else {
        ImGui::Text("Addons (%d):", static_cast<int>(addons.size()));
        ImGui::Separator();

        for (const auto& [addonId, addon] : addons) {
            const auto& manifest = addon->GetManifest();
            AddonState state = addon->GetState();

            const char* stateStr = "Unknown";
            switch (state) {
                case AddonState::Discovered: stateStr = "Discovered"; break;
                case AddonState::Loading:    stateStr = "Loading"; break;
                case AddonState::Running:    stateStr = "Running"; break;
                case AddonState::Error:      stateStr = "Error"; break;
                case AddonState::Unloaded:   stateStr = "Unloaded"; break;
            }

            if (ImGui::TreeNode(addonId.c_str(), "%s v%s", manifest.name.c_str(), manifest.version.c_str())) {
                ImGui::Text("Author: %s", manifest.author.c_str());
                ImGui::Text("Description: %s", manifest.description.c_str());
                ImGui::Text("State: %s", stateStr);
                ImGui::Text("Entry: %s", manifest.entry.c_str());

                // Windows list
                const auto& windows = addon->GetWindows();
                ImGui::Text("Windows (%d):", static_cast<int>(windows.size()));
                for (const auto& [winId, window] : windows) {
                    ImGui::BulletText("%s: %s (%dx%d) %s%s",
                        winId.c_str(), window.title.c_str(),
                        window.contentW, window.contentH,
                        window.visible ? "visible" : "hidden",
                        window.inputPassthrough ? " [passthrough]" : "");
                }

                // Actions
                if (state == AddonState::Running) {
                    if (ImGui::Button(("DevTools##dt_" + addonId).c_str())) {
                        addon->OpenDevTools();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(("Reload##rl_" + addonId).c_str())) {
                        auto* mainWin = addon->GetWindow("main");
                        if (mainWin && mainWin->browser) {
                            mainWin->browser->Reload();
                        }
                    }
                }

                ImGui::TreePop();
            }
        }
    }
}

HitTestResult HitTestAll(int clientX, int clientY) {
    HitTestResult result;
    float x = static_cast<float>(clientX);
    float y = static_cast<float>(clientY);

    // Check DevTools windows first (they typically render on top)
    for (auto& [addonId, addon] : AddonManager::GetAddons()) {
        if (!addon->IsDevToolsOpen()) continue;
        // DevTools hit test uses the cached s_dt* values
        if (s_dtHovered && s_dtW > 0 && s_dtH > 0 &&
            x >= s_dtX && x < s_dtX + s_dtW &&
            y >= s_dtY && y < s_dtY + s_dtH) {
            result.addon = addon.get();
            result.window = nullptr; // DevTools isn't a WindowInfo
            result.isContentArea = true;
            result.localX = clientX - static_cast<int>(s_dtX);
            result.localY = clientY - static_cast<int>(s_dtY);
            return result;
        }
    }

    // Check addon windows — iterate in reverse so topmost ImGui windows are checked first.
    // ImGui renders back to front, so the last-rendered window that passes the hover check
    // is the topmost visible one.
    for (auto& [addonId, addon] : AddonManager::GetAddons()) {
        for (auto& [windowId, window] : addon->GetWindows()) {
            if (!window.visible || !window.browser || !window.browser->IsReady()) continue;

            // Content area hit test (uses ImGui's IsItemHovered which handles occlusion)
            if (window.contentHovered && window.contentW > 0 && window.contentH > 0 &&
                x >= window.contentX && x < window.contentX + window.contentW &&
                y >= window.contentY && y < window.contentY + window.contentH) {
                result.addon = addon.get();
                result.window = &window;
                result.isContentArea = true;
                result.localX = clientX - static_cast<int>(window.contentX);
                result.localY = clientY - static_cast<int>(window.contentY);
                return result;
            }

            // Full window bounds hit test (for title bar drags etc.)
            if (x >= window.windowX && x < window.windowX + window.windowW &&
                y >= window.windowY && y < window.windowY + window.windowH) {
                result.addon = addon.get();
                result.window = &window;
                result.isContentArea = false;
                return result;
            }
        }
    }

    return result; // No hit
}

FocusResult GetFocusedWindow() {
    return { s_focusedAddon, s_focusedWindow };
}

} // namespace Overlay
