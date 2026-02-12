#include "overlay.h"
#include "globals.h"
#include "web_app_manager.h"
#include "in_process_browser.h"
#include "shared/version.h"

#include "imgui.h"

namespace Overlay {

// Content area position (where the CEF texture starts)
static float s_overlayX = 0.0f;
static float s_overlayY = 0.0f;

// Full window bounds (including title bar) — updated each frame in Render()
static float s_windowX = 0.0f;
static float s_windowY = 0.0f;
static float s_windowW = 0.0f;
static float s_windowH = 0.0f;

// Content area size — cached from WebAppManager
static int s_contentW = 0;
static int s_contentH = 0;

// Whether the overlay window had ImGui focus last frame
static bool s_hasFocus = false;

// Whether ImGui considers the content image hovered (accounts for window occlusion)
static bool s_contentHovered = false;

// DevTools window state
static float s_dtX = 0.0f;
static float s_dtY = 0.0f;
static int   s_dtW = 0;
static int   s_dtH = 0;
static bool  s_dtFocus = false;
static bool  s_dtHovered = false;

// URL input buffer for options panel
static char s_urlBuffer[2048] = "https://example.com";

void Render() {
    s_contentHovered = false;

    if (!Globals::OverlayVisible) {
        s_hasFocus = false;
        return;
    }

    void* textureHandle = WebAppManager::GetTextureHandle();
    if (!textureHandle) return;

    int texW = WebAppManager::GetWidth();
    int texH = WebAppManager::GetHeight();
    if (texW <= 0 || texH <= 0) return;

    // Set ImGui context from Nexus
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(Globals::API->ImguiContext));

    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(texW), static_cast<float>(texH) + 20.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(320, 200), ImVec2(4096, 4096));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("JS Loader##Overlay", &Globals::OverlayVisible, flags)) {
        // Track full window bounds for input hit testing
        ImVec2 wpos = ImGui::GetWindowPos();
        ImVec2 wsize = ImGui::GetWindowSize();
        s_windowX = wpos.x;
        s_windowY = wpos.y;
        s_windowW = wsize.x;
        s_windowH = wsize.y;
        s_hasFocus = ImGui::IsWindowFocused();

        // Track content area position
        ImVec2 pos = ImGui::GetCursorScreenPos();
        s_overlayX = pos.x;
        s_overlayY = pos.y;

        // Resize browser to match available content area
        ImVec2 avail = ImGui::GetContentRegionAvail();
        int contentW = static_cast<int>(avail.x);
        int contentH = static_cast<int>(avail.y);
        if (contentW > 0 && contentH > 0) {
            s_contentW = contentW;
            s_contentH = contentH;
            if (contentW != texW || contentH != texH) {
                WebAppManager::Resize(contentW, contentH);
            }
        } else {
            s_contentW = texW;
            s_contentH = texH;
        }

        ImGui::Image(textureHandle, ImVec2(static_cast<float>(s_contentW), static_cast<float>(s_contentH)));
        s_contentHovered = ImGui::IsItemHovered();
    } else {
        // Window is collapsed — update bounds but clear focus
        ImVec2 wpos = ImGui::GetWindowPos();
        ImVec2 wsize = ImGui::GetWindowSize();
        s_windowX = wpos.x;
        s_windowY = wpos.y;
        s_windowW = wsize.x;
        s_windowH = wsize.y;
        s_hasFocus = ImGui::IsWindowFocused();
        s_contentW = 0;
        s_contentH = 0;
    }
    ImGui::End();
}

void RenderOptions() {
    // Set ImGui context from Nexus
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(Globals::API->ImguiContext));

    ImGui::TextUnformatted("JS Loader Settings");
    ImGui::Separator();

    ImGui::Text("Overlay toggle: ALT+SHIFT+L");
    ImGui::Text("Status: %s", Globals::OverlayVisible ? "Visible" : "Hidden");
    ImGui::Text("Browser: %s", WebAppManager::IsReady() ? "Ready" : "Not ready");

    ImGui::Separator();
    ImGui::TextUnformatted("Web App URL:");
    ImGui::InputText("##url", s_urlBuffer, sizeof(s_urlBuffer));

    if (ImGui::Button("Load URL")) {
        WebAppManager::LoadUrl(s_urlBuffer);
    }

    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
        WebAppManager::Reload();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Developer Tools:");
    if (ImGui::Button("Open DevTools")) {
        WebAppManager::OpenDevTools();
    }

    ImGui::Separator();

    // Show active web apps
    const auto& apps = WebAppManager::GetLoadedApps();
    if (apps.empty()) {
        ImGui::TextDisabled("No web apps loaded.");
    } else {
        for (const auto& app : apps) {
            ImGui::BulletText("%s", app.c_str());
        }
    }
}

void GetOverlayPosition(float& x, float& y) {
    x = s_overlayX;
    y = s_overlayY;
}

bool HasFocus() {
    return s_hasFocus;
}

bool HitTest(int clientX, int clientY) {
    float x = static_cast<float>(clientX);
    float y = static_cast<float>(clientY);
    return x >= s_windowX && x < s_windowX + s_windowW
        && y >= s_windowY && y < s_windowY + s_windowH;
}

bool ContentHitTest(int clientX, int clientY) {
    // s_contentHovered is false when another ImGui window occludes the content
    if (!s_contentHovered) return false;
    if (s_contentW <= 0 || s_contentH <= 0) return false;
    float x = static_cast<float>(clientX);
    float y = static_cast<float>(clientY);
    return x >= s_overlayX && x < s_overlayX + s_contentW
        && y >= s_overlayY && y < s_overlayY + s_contentH;
}

// ---- DevTools window ----

void RenderDevTools() {
    s_dtHovered = false;

    if (!WebAppManager::IsDevToolsOpen()) {
        s_dtFocus = false;
        s_dtW = 0;
        s_dtH = 0;
        return;
    }

    InProcessBrowser* devTools = WebAppManager::GetDevToolsBrowser();
    if (!devTools) return;

    void* textureHandle = devTools->GetTextureHandle();
    if (!textureHandle) return;

    int texW = devTools->GetWidth();
    int texH = devTools->GetHeight();
    if (texW <= 0 || texH <= 0) return;

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(Globals::API->ImguiContext));

    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(400, 300), ImVec2(4096, 4096));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar
                           | ImGuiWindowFlags_NoScrollWithMouse
                           | ImGuiWindowFlags_NoCollapse;

    bool visible = true;
    if (ImGui::Begin("DevTools##DevToolsWindow", &visible, flags)) {
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

        ImGui::Image(textureHandle, ImVec2(static_cast<float>(s_dtW),
                                            static_cast<float>(s_dtH)));
        s_dtHovered = ImGui::IsItemHovered();
    } else {
        s_dtFocus = false;
        s_dtW = 0;
        s_dtH = 0;
    }
    ImGui::End();

    if (!visible) {
        WebAppManager::CloseDevTools();
    }
}

void GetDevToolsPosition(float& x, float& y) {
    x = s_dtX;
    y = s_dtY;
}

bool DevToolsHasFocus() {
    return s_dtFocus;
}

bool DevToolsContentHitTest(int clientX, int clientY) {
    if (!s_dtHovered) return false;
    if (s_dtW <= 0 || s_dtH <= 0) return false;
    float x = static_cast<float>(clientX);
    float y = static_cast<float>(clientY);
    return x >= s_dtX && x < s_dtX + s_dtW
        && y >= s_dtY && y < s_dtY + s_dtH;
}

} // namespace Overlay
