#pragma once

class InProcessBrowser;

// Renders the CEF overlay as an ImGui window and the options panel.
namespace Overlay {

// Render the CEF overlay window. Call from RT_Render.
void Render();

// Render the DevTools window (if open). Call from RT_Render after Render().
void RenderDevTools();

// Render the options/settings panel. Call from RT_OptionsRender.
void RenderOptions();

// Get the content area position (where the CEF texture starts).
void GetOverlayPosition(float& x, float& y);

// Whether the overlay window had ImGui focus last frame.
bool HasFocus();

// Hit-test a point (in client coordinates) against the full overlay window
// bounds (including title bar).
bool HitTest(int clientX, int clientY);

// Hit-test a point against the content area only (where the CEF texture is).
bool ContentHitTest(int clientX, int clientY);

// DevTools window — same hit-testing and position queries.
void GetDevToolsPosition(float& x, float& y);
bool DevToolsHasFocus();
bool DevToolsContentHitTest(int clientX, int clientY);

} // namespace Overlay
