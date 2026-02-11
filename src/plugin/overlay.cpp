#include "overlay.h"
#include "globals.h"
#include "cef_manager.h"
#include "web_app_manager.h"
#include "osr_render_handler.h"
#include "shared/version.h"

// ImGui is provided by Nexus via the ImguiContext pointer.
// We must set the ImGui context before calling any ImGui functions.
// The imgui headers should be placed in the project or included from a dependency.
#include "imgui.h"

namespace Overlay {

static float s_overlayX = 100.0f;
static float s_overlayY = 100.0f;

// URL input buffer for options panel
static char s_urlBuffer[2048] = "https://example.com";

void Render() {
    if (!Globals::OverlayVisible) return;

    auto* renderHandler = WebAppManager::GetRenderHandler();
    if (!renderHandler) return;

    void* textureHandle = renderHandler->GetTextureHandle();
    if (!textureHandle) return;

    int texW = renderHandler->GetWidth();
    int texH = renderHandler->GetHeight();

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
