#pragma once

#include <string>

class AddonInstance;
struct WindowInfo;
class InProcessBrowser;

// Renders all addon windows as ImGui windows and the options panel.
namespace Overlay {

// Hit test result: identifies which addon window (if any) is under a point.
struct HitTestResult {
    AddonInstance* addon = nullptr;
    WindowInfo* window = nullptr;
    bool isContentArea = false;
    int localX = 0, localY = 0;
};

// Render all addon windows. Call from RT_Render.
void Render();

// Render the options/settings panel. Call from RT_OptionsRender.
void RenderOptions();

// Hit-test a point (in client coordinates) against all addon windows.
// Returns the topmost window under the cursor with local coordinates.
HitTestResult HitTestAll(int clientX, int clientY);

// Get the currently focused window (if any).
// Returns the addon/window pair that had ImGui focus last frame.
struct FocusResult {
    AddonInstance* addon = nullptr;
    WindowInfo* window = nullptr;
};
FocusResult GetFocusedWindow();

} // namespace Overlay
