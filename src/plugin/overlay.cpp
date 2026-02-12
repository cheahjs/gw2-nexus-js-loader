#include "overlay.h"
#include "globals.h"
#include "cef_host_proxy.h"
#include "web_app_manager.h"
#include "shared/version.h"

#include "imgui.h"

namespace Overlay {

static float s_overlayX = 100.0f;
static float s_overlayY = 100.0f;

// URL input buffer for options panel
static char s_urlBuffer[2048] = "https://example.com";

void Render() {
    if (!Globals::OverlayVisible) return;

    void* textureHandle = CefHostProxy::GetTextureHandle();
    if (!textureHandle) return;

    int texW = CefHostProxy::GetWidth();
    int texH = CefHostProxy::GetHeight();
    if (texW <= 0 || texH <= 0) return;

    // Set ImGui context from Nexus
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(Globals::API->ImguiContext));

    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(texW), static_cast<float>(texH) + 20.0f), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("JS Loader##Overlay", &Globals::OverlayVisible, flags)) {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        s_overlayX = pos.x;
        s_overlayY = pos.y;

        ImGui::Image(textureHandle, ImVec2(static_cast<float>(texW), static_cast<float>(texH)));
    }
    ImGui::End();
}

void RenderOptions() {
    // Set ImGui context from Nexus
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(Globals::API->ImguiContext));

    ImGui::TextUnformatted("JS Loader Settings");
    ImGui::Separator();

    ImGui::Text("Overlay toggle: ALT+SHIFT+J");
    ImGui::Text("Status: %s", Globals::OverlayVisible ? "Visible" : "Hidden");
    ImGui::Text("Host: %s", CefHostProxy::IsReady() ? "Connected" : "Not connected");

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

} // namespace Overlay
