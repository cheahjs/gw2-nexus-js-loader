#include "input_handler.h"
#include "globals.h"
#include "overlay.h"
#include "web_app_manager.h"
#include "in_process_browser.h"
#include "shared/version.h"

#include <windows.h>
#include <windowsx.h>

namespace InputHandler {

// Convert screen coordinates to overlay-relative coordinates
static void ScreenToOverlay(int screenX, int screenY, int& outX, int& outY) {
    float ox, oy;
    Overlay::GetOverlayPosition(ox, oy);
    outX = screenX - static_cast<int>(ox);
    outY = screenY - static_cast<int>(oy);
}

// Build modifiers bitmask from current key state
static uint32_t GetModifiers() {
    uint32_t modifiers = 0;
    if (GetKeyState(VK_SHIFT)   & 0x8000) modifiers |= (1 << 1);  // EVENTFLAG_SHIFT_DOWN
    if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= (1 << 2);  // EVENTFLAG_CONTROL_DOWN
    if (GetKeyState(VK_MENU)    & 0x8000) modifiers |= (1 << 3);  // EVENTFLAG_ALT_DOWN
    return modifiers;
}

static UINT WndProcCallback(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Nexus convention: return 0 = consumed, non-zero = pass through.
    if (!WebAppManager::IsReady() || !Globals::OverlayVisible) return uMsg;

    InProcessBrowser* browser = WebAppManager::GetBrowser();
    if (!browser) return uMsg;

    uint32_t modifiers = GetModifiers();

    switch (uMsg) {
        case WM_MOUSEMOVE: {
            int clientX = GET_X_LPARAM(lParam);
            int clientY = GET_Y_LPARAM(lParam);
            // Only consume mouse movement when overlay has focus
            if (!Overlay::HasFocus()) return uMsg;
            if (!Overlay::HitTest(clientX, clientY)) return uMsg;
            // Forward to CEF only if cursor is over the content area
            if (Overlay::ContentHitTest(clientX, clientY)) {
                int x, y;
                ScreenToOverlay(clientX, clientY, x, y);
                browser->SendMouseMove(x, y, modifiers);
            }
            return 0;
        }

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP: {
            int clientX = GET_X_LPARAM(lParam);
            int clientY = GET_Y_LPARAM(lParam);
            if (!Overlay::HitTest(clientX, clientY)) return uMsg;
            if (Overlay::ContentHitTest(clientX, clientY)) {
                int x, y;
                ScreenToOverlay(clientX, clientY, x, y);
                browser->SendMouseClick(x, y, modifiers, 0, // MBT_LEFT
                                         uMsg == WM_LBUTTONUP, 1);
            }
            return 0;
        }

        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP: {
            int clientX = GET_X_LPARAM(lParam);
            int clientY = GET_Y_LPARAM(lParam);
            if (!Overlay::HitTest(clientX, clientY)) return uMsg;
            if (Overlay::ContentHitTest(clientX, clientY)) {
                int x, y;
                ScreenToOverlay(clientX, clientY, x, y);
                browser->SendMouseClick(x, y, modifiers, 2, // MBT_RIGHT
                                         uMsg == WM_RBUTTONUP, 1);
            }
            return 0;
        }

        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP: {
            int clientX = GET_X_LPARAM(lParam);
            int clientY = GET_Y_LPARAM(lParam);
            if (!Overlay::HitTest(clientX, clientY)) return uMsg;
            if (Overlay::ContentHitTest(clientX, clientY)) {
                int x, y;
                ScreenToOverlay(clientX, clientY, x, y);
                browser->SendMouseClick(x, y, modifiers, 1, // MBT_MIDDLE
                                         uMsg == WM_MBUTTONUP, 1);
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            // WM_MOUSEWHEEL coordinates are in screen space — convert to client
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &pt);
            int clientX = pt.x;
            int clientY = pt.y;
            if (!Overlay::HitTest(clientX, clientY)) return uMsg;
            if (Overlay::ContentHitTest(clientX, clientY)) {
                int x, y;
                ScreenToOverlay(clientX, clientY, x, y);
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                browser->SendMouseWheel(x, y, modifiers, 0, delta);
            }
            return 0;
        }

        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP: {
            if (!Overlay::HasFocus()) return uMsg;
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
            browser->SendKeyEvent(type, modifiers,
                                   static_cast<int>(wParam),
                                   static_cast<int>(lParam),
                                   isSys, 0);
            return 0;
        }

        case WM_CHAR:
        case WM_SYSCHAR: {
            if (!Overlay::HasFocus()) return uMsg;
            bool isSys = (uMsg == WM_SYSCHAR);
            browser->SendKeyEvent(3, // KEYEVENT_CHAR
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
