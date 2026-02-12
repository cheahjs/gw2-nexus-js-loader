#include "input_handler.h"
#include "globals.h"
#include "overlay.h"
#include "web_app_manager.h"
#include "in_process_browser.h"
#include "shared/version.h"

#include <windows.h>
#include <windowsx.h>

namespace InputHandler {

// Build modifiers bitmask from current key state
static uint32_t GetModifiers() {
    uint32_t modifiers = 0;
    if (GetKeyState(VK_SHIFT)   & 0x8000) modifiers |= (1 << 1);  // EVENTFLAG_SHIFT_DOWN
    if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= (1 << 2);  // EVENTFLAG_CONTROL_DOWN
    if (GetKeyState(VK_MENU)    & 0x8000) modifiers |= (1 << 3);  // EVENTFLAG_ALT_DOWN
    return modifiers;
}

// Find which browser (main or DevTools) a mouse position targets.
// Returns the browser and content origin for coordinate conversion.
static InProcessBrowser* GetMouseTarget(int clientX, int clientY,
                                         float& contentX, float& contentY) {
    if (Overlay::ContentHitTest(clientX, clientY)) {
        Overlay::GetOverlayPosition(contentX, contentY);
        return WebAppManager::GetBrowser();
    }
    if (Overlay::DevToolsContentHitTest(clientX, clientY)) {
        Overlay::GetDevToolsPosition(contentX, contentY);
        return WebAppManager::GetDevToolsBrowser();
    }
    return nullptr;
}

// Mouse capture: while a button is held down, keep forwarding mouse events to the
// browser that received the button-down, even if the cursor leaves the content area.
// Without this, dragging scrollbars or selections breaks when the cursor moves outside.
static InProcessBrowser* s_capturedBrowser = nullptr;
static float s_captureOriginX = 0.0f;
static float s_captureOriginY = 0.0f;

// External drag: set when a button-down passes through (e.g. ImGui title bar drag).
// While active, mouse moves pass through too so ImGui can track the drag.
static bool s_externalDrag = false;

// Keyboard focus: set when clicking in a content area, cleared on click elsewhere.
// This ensures clicking the game (or title bar, other windows) unfocuses the overlay.
enum FocusTarget { FOCUS_NONE, FOCUS_OVERLAY, FOCUS_DEVTOOLS };
static FocusTarget s_focus = FOCUS_NONE;

static InProcessBrowser* GetKeyboardTarget() {
    switch (s_focus) {
        case FOCUS_OVERLAY:  return WebAppManager::GetBrowser();
        case FOCUS_DEVTOOLS: return WebAppManager::GetDevToolsBrowser();
        default:             return nullptr;
    }
}

static UINT WndProcCallback(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Nexus convention: return 0 = consumed, non-zero = pass through.
    if (!Globals::OverlayVisible && !WebAppManager::IsDevToolsOpen()) return uMsg;

    // Update keyboard focus on any mouse-button-down event.
    // Click in overlay content → focus overlay. Click in DevTools → focus DevTools.
    // Click anywhere else (game, title bar, other addon) → clear focus.
    if (uMsg == WM_LBUTTONDOWN || uMsg == WM_RBUTTONDOWN || uMsg == WM_MBUTTONDOWN) {
        int clickX = GET_X_LPARAM(lParam);
        int clickY = GET_Y_LPARAM(lParam);
        if (Overlay::ContentHitTest(clickX, clickY)) {
            s_focus = FOCUS_OVERLAY;
        } else if (Overlay::DevToolsContentHitTest(clickX, clickY)) {
            s_focus = FOCUS_DEVTOOLS;
        } else {
            s_focus = FOCUS_NONE;
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
            float cx, cy;
            InProcessBrowser* target = GetMouseTarget(clientX, clientY, cx, cy);
            if (!target) return uMsg;
            target->SendMouseMove(clientX - static_cast<int>(cx),
                                  clientY - static_cast<int>(cy), modifiers);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int clientX = GET_X_LPARAM(lParam);
            int clientY = GET_Y_LPARAM(lParam);
            float cx, cy;
            InProcessBrowser* target = GetMouseTarget(clientX, clientY, cx, cy);
            if (!target) { s_externalDrag = true; return uMsg; }
            target->SendMouseClick(clientX - static_cast<int>(cx),
                                   clientY - static_cast<int>(cy),
                                   modifiers, 0, false, 1);
            s_capturedBrowser = target;
            s_captureOriginX = cx;
            s_captureOriginY = cy;
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
            float cx, cy;
            InProcessBrowser* target = GetMouseTarget(clientX, clientY, cx, cy);
            if (!target) { s_externalDrag = true; return uMsg; }
            target->SendMouseClick(clientX - static_cast<int>(cx),
                                   clientY - static_cast<int>(cy),
                                   modifiers, 2, false, 1);
            s_capturedBrowser = target;
            s_captureOriginX = cx;
            s_captureOriginY = cy;
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
            float cx, cy;
            InProcessBrowser* target = GetMouseTarget(clientX, clientY, cx, cy);
            if (!target) { s_externalDrag = true; return uMsg; }
            target->SendMouseClick(clientX - static_cast<int>(cx),
                                   clientY - static_cast<int>(cy),
                                   modifiers, 1, false, 1);
            s_capturedBrowser = target;
            s_captureOriginX = cx;
            s_captureOriginY = cy;
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
            // WM_MOUSEWHEEL coordinates are in screen space — convert to client
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &pt);
            float cx, cy;
            InProcessBrowser* target = GetMouseTarget(pt.x, pt.y, cx, cy);
            if (!target) return uMsg;
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            target->SendMouseWheel(pt.x - static_cast<int>(cx),
                                   pt.y - static_cast<int>(cy),
                                   modifiers, 0, delta);
            return 0;
        }

        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP: {
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
