#pragma once

// Renders the CEF overlay as an ImGui window and the options panel.
namespace Overlay {

// Render the CEF overlay window. Call from RT_Render.
void Render();

// Render the options/settings panel. Call from RT_OptionsRender.
void RenderOptions();

// Get the current overlay window position (for coordinate transforms).
void GetOverlayPosition(float& x, float& y);

} // namespace Overlay
