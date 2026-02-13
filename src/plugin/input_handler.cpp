#include "input_handler.h"
#include "globals.h"
#include "overlay.h"
#include "addon_manager.h"
#include "addon_instance.h"
#include "in_process_browser.h"
#include "shared/version.h"

#include <windows.h>
#include <windowsx.h>

namespace InputHandler {

// Check if input at the given local coordinates should pass through based on
// the window's alpha threshold and the rendered pixel alpha at that position.
static bool ShouldPassThrough(const WindowInfo* window, int localX, int localY) {
    if (!window) return false;
    int threshold = window->alphaThreshold;
    if (threshold == 0) return false;
    if (threshold >= 256) return true;
    if (!window->browser) return false;
    uint8_t alpha = window->browser->GetPixelAlpha(localX, localY);
    return alpha < threshold;
}

// Build modifiers bitmask from current key state
static uint32_t GetModifiers() {
    uint32_t modifiers = 0;
    if (GetKeyState(VK_SHIFT)   & 0x8000) modifiers |= (1 << 1);  // EVENTFLAG_SHIFT_DOWN
    if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= (1 << 2);  // EVENTFLAG_CONTROL_DOWN
    if (GetKeyState(VK_MENU)    & 0x8000) modifiers |= (1 << 3);  // EVENTFLAG_ALT_DOWN
    return modifiers;
}

// Mouse capture: while a button is held down, keep forwarding mouse events to the
// browser that received the button-down, even if the cursor leaves the content area.
static InProcessBrowser* s_capturedBrowser = nullptr;
static float s_captureOriginX = 0.0f;
static float s_captureOriginY = 0.0f;

// External drag: set when a button-down passes through (e.g. ImGui title bar drag).
// While active, mouse moves pass through too so ImGui can track the drag.
static bool s_externalDrag = false;

// Keyboard focus: set on click based on which content area was clicked.
// The focused window receives keyboard events.
static AddonInstance* s_focusAddon = nullptr;
static WindowInfo*    s_focusWindow = nullptr;
// Special: focused DevTools browser (when DevTools content was clicked)
static InProcessBrowser* s_focusDevTools = nullptr;

static InProcessBrowser* GetKeyboardTarget() {
    if (s_focusDevTools) return s_focusDevTools;
    if (s_focusWindow && s_focusWindow->browser) {
        return s_focusWindow->browser.get();
    }
    return nullptr;
}

static UINT WndProcCallback(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Nexus convention: return 0 = consumed, non-zero = pass through.
    if (!Globals::OverlayVisible) return uMsg;

    // Update keyboard focus on any mouse-button-down event.
    if (uMsg == WM_LBUTTONDOWN || uMsg == WM_RBUTTONDOWN || uMsg == WM_MBUTTONDOWN) {
        int clickX = GET_X_LPARAM(lParam);
        int clickY = GET_Y_LPARAM(lParam);
        auto hit = Overlay::HitTestAll(clickX, clickY);
        if (hit.isContentArea && hit.window) {
            s_focusAddon = hit.addon;
            s_focusWindow = hit.window;
            s_focusDevTools = nullptr;
        } else if (hit.isContentArea && !hit.window && hit.addon) {
            // DevTools content area
            s_focusAddon = hit.addon;
            s_focusWindow = nullptr;
            s_focusDevTools = hit.addon->GetDevToolsBrowser();
        } else {
            s_focusAddon = nullptr;
            s_focusWindow = nullptr;
            s_focusDevTools = nullptr;
        }
    }

    uint32_t modifiers = GetModifiers();

    switch (uMsg) {
        case WM_MOUSEMOVE: {
            int clientX = GET_X_LPARAM(lParam);
            int clientY = GET_Y_LPARAM(lParam);
            if (s_capturedBrowser) {
                s_capturedBrowser->SendMouseMove(
                    clientX - static_cast<int>(s_captureOriginX),
                    clientY - static_cast<int>(s_captureOriginY), modifiers);
                return 0;
            }
            if (s_externalDrag) return uMsg;

            auto hit = Overlay::HitTestAll(clientX, clientY);
            if (!hit.isContentArea) return uMsg;

            // Check input passthrough
            if (ShouldPassThrough(hit.window, hit.localX, hit.localY)) return uMsg;

            InProcessBrowser* target = nullptr;
            if (hit.window && hit.window->browser) {
                target = hit.window->browser.get();
            } else if (hit.addon) {
                target = hit.addon->GetDevToolsBrowser();
            }
            if (!target) return uMsg;

            target->SendMouseMove(hit.localX, hit.localY, modifiers);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int clientX = GET_X_LPARAM(lParam);
            int clientY = GET_Y_LPARAM(lParam);
            auto hit = Overlay::HitTestAll(clientX, clientY);
            if (!hit.isContentArea) { s_externalDrag = true; return uMsg; }

            // Check input passthrough
            if (ShouldPassThrough(hit.window, hit.localX, hit.localY)) { s_externalDrag = true; return uMsg; }

            InProcessBrowser* target = nullptr;
            float originX = 0, originY = 0;
            if (hit.window && hit.window->browser) {
                target = hit.window->browser.get();
                originX = hit.window->contentX;
                originY = hit.window->contentY;
            } else if (hit.addon) {
                target = hit.addon->GetDevToolsBrowser();
                // DevTools uses s_dtX/s_dtY but we have localX/localY from HitTestAll
            }
            if (!target) { s_externalDrag = true; return uMsg; }

            target->SendMouseClick(hit.localX, hit.localY, modifiers, 0, false, 1);
            s_capturedBrowser = target;
            s_captureOriginX = static_cast<float>(clientX - hit.localX);
            s_captureOriginY = static_cast<float>(clientY - hit.localY);
            return 0;
        }
        case WM_LBUTTONUP: {
            if (!s_capturedBrowser) { s_externalDrag = false; return uMsg; }
            int clientX = GET_X_LPARAM(lParam);
            int clientY = GET_Y_LPARAM(lParam);
            s_capturedBrowser->SendMouseClick(
                clientX - static_cast<int>(s_captureOriginX),
                clientY - static_cast<int>(s_captureOriginY),
                modifiers, 0, true, 1);
            s_capturedBrowser = nullptr;
            return 0;
        }

        case WM_RBUTTONDOWN: {
            int clientX = GET_X_LPARAM(lParam);
            int clientY = GET_Y_LPARAM(lParam);
            auto hit = Overlay::HitTestAll(clientX, clientY);
            if (!hit.isContentArea) { s_externalDrag = true; return uMsg; }
            if (ShouldPassThrough(hit.window, hit.localX, hit.localY)) { s_externalDrag = true; return uMsg; }

            InProcessBrowser* target = nullptr;
            if (hit.window && hit.window->browser) {
                target = hit.window->browser.get();
            } else if (hit.addon) {
                target = hit.addon->GetDevToolsBrowser();
            }
            if (!target) { s_externalDrag = true; return uMsg; }

            target->SendMouseClick(hit.localX, hit.localY, modifiers, 2, false, 1);
            s_capturedBrowser = target;
            s_captureOriginX = static_cast<float>(clientX - hit.localX);
            s_captureOriginY = static_cast<float>(clientY - hit.localY);
            return 0;
        }
        case WM_RBUTTONUP: {
            if (!s_capturedBrowser) { s_externalDrag = false; return uMsg; }
            int clientX = GET_X_LPARAM(lParam);
            int clientY = GET_Y_LPARAM(lParam);
            s_capturedBrowser->SendMouseClick(
                clientX - static_cast<int>(s_captureOriginX),
                clientY - static_cast<int>(s_captureOriginY),
                modifiers, 2, true, 1);
            s_capturedBrowser = nullptr;
            return 0;
        }

        case WM_MBUTTONDOWN: {
            int clientX = GET_X_LPARAM(lParam);
            int clientY = GET_Y_LPARAM(lParam);
            auto hit = Overlay::HitTestAll(clientX, clientY);
            if (!hit.isContentArea) { s_externalDrag = true; return uMsg; }
            if (ShouldPassThrough(hit.window, hit.localX, hit.localY)) { s_externalDrag = true; return uMsg; }

            InProcessBrowser* target = nullptr;
            if (hit.window && hit.window->browser) {
                target = hit.window->browser.get();
            } else if (hit.addon) {
                target = hit.addon->GetDevToolsBrowser();
            }
            if (!target) { s_externalDrag = true; return uMsg; }

            target->SendMouseClick(hit.localX, hit.localY, modifiers, 1, false, 1);
            s_capturedBrowser = target;
            s_captureOriginX = static_cast<float>(clientX - hit.localX);
            s_captureOriginY = static_cast<float>(clientY - hit.localY);
            return 0;
        }
        case WM_MBUTTONUP: {
            if (!s_capturedBrowser) { s_externalDrag = false; return uMsg; }
            int clientX = GET_X_LPARAM(lParam);
            int clientY = GET_Y_LPARAM(lParam);
            s_capturedBrowser->SendMouseClick(
                clientX - static_cast<int>(s_captureOriginX),
                clientY - static_cast<int>(s_captureOriginY),
                modifiers, 1, true, 1);
            s_capturedBrowser = nullptr;
            return 0;
        }

        case WM_MOUSEWHEEL: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &pt);
            auto hit = Overlay::HitTestAll(pt.x, pt.y);
            if (!hit.isContentArea) return uMsg;
            if (ShouldPassThrough(hit.window, hit.localX, hit.localY)) return uMsg;

            InProcessBrowser* target = nullptr;
            if (hit.window && hit.window->browser) {
                target = hit.window->browser.get();
            } else if (hit.addon) {
                target = hit.addon->GetDevToolsBrowser();
            }
            if (!target) return uMsg;

            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            target->SendMouseWheel(hit.localX, hit.localY, modifiers, 0, delta);
            return 0;
        }

        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP: {
            // Check if focused window has input passthrough
            if (s_focusWindow && s_focusWindow->alphaThreshold >= 256) return uMsg;

            InProcessBrowser* target = GetKeyboardTarget();
            if (!target) return uMsg;
            uint32_t type;
            switch (uMsg) {
                case WM_KEYDOWN:
                case WM_SYSKEYDOWN:
                    type = 0; // KEYEVENT_RAWKEYDOWN
                    break;
                case WM_KEYUP:
                case WM_SYSKEYUP:
                    type = 2; // KEYEVENT_KEYUP
                    break;
                default:
                    type = 0;
                    break;
            }
            bool isSys = (uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSKEYUP);
            target->SendKeyEvent(type, modifiers,
                                  static_cast<int>(wParam),
                                  static_cast<int>(lParam),
                                  isSys, 0);
            return 0;
        }

        case WM_CHAR:
        case WM_SYSCHAR: {
            if (s_focusWindow && s_focusWindow->alphaThreshold >= 256) return uMsg;

            InProcessBrowser* target = GetKeyboardTarget();
            if (!target) return uMsg;
            bool isSys = (uMsg == WM_SYSCHAR);
            target->SendKeyEvent(3, // KEYEVENT_CHAR
                                  modifiers,
                                  static_cast<int>(wParam),
                                  static_cast<int>(lParam),
                                  isSys,
                                  static_cast<uint16_t>(wParam));
            return 0;
        }
    }

    return uMsg; // Not consumed — pass through to game
}

void Initialize() {
    if (Globals::API) {
        Globals::API->WndProc_Register(WndProcCallback);
    }
}

void Shutdown() {
    if (Globals::API) {
        Globals::API->WndProc_Deregister(WndProcCallback);
    }
}

} // namespace InputHandler
