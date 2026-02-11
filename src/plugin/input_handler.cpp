#include "input_handler.h"
#include "globals.h"
#include "overlay.h"
#include "shared/version.h"

#include <windowsx.h>

#include "include/cef_browser.h"
#include "include/internal/cef_types.h"

namespace InputHandler {

static CefRefPtr<CefBrowser> s_browser;

// Translate Windows virtual key code to CEF key event
static cef_key_event_t MakeKeyEvent(UINT msg, WPARAM wParam, LPARAM lParam) {
    cef_key_event_t event = {};
    event.windows_key_code = static_cast<int>(wParam);
    event.native_key_code  = static_cast<int>(lParam);
    event.is_system_key    = (msg == WM_SYSCHAR || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP);
    event.modifiers        = 0;

    if (GetKeyState(VK_SHIFT)   & 0x8000) event.modifiers |= EVENTFLAG_SHIFT_DOWN;
    if (GetKeyState(VK_CONTROL) & 0x8000) event.modifiers |= EVENTFLAG_CONTROL_DOWN;
    if (GetKeyState(VK_MENU)    & 0x8000) event.modifiers |= EVENTFLAG_ALT_DOWN;

    switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            event.type = KEYEVENT_RAWKEYDOWN;
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            event.type = KEYEVENT_KEYUP;
            break;
        case WM_CHAR:
        case WM_SYSCHAR:
            event.type = KEYEVENT_CHAR;
            break;
    }

    return event;
}

// Convert screen coordinates to overlay-relative coordinates
static void ScreenToOverlay(int screenX, int screenY, int& outX, int& outY) {
    float ox, oy;
    Overlay::GetOverlayPosition(ox, oy);
    outX = screenX - static_cast<int>(ox);
    outY = screenY - static_cast<int>(oy);
}

static UINT WndProcCallback(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (!s_browser || !Globals::OverlayVisible) return 0;

    auto host = s_browser->GetHost();
    if (!host) return 0;

    switch (uMsg) {
        case WM_MOUSEMOVE: {
            int x, y;
            ScreenToOverlay(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), x, y);

            CefMouseEvent mouseEvent;
            mouseEvent.x = x;
            mouseEvent.y = y;
            mouseEvent.modifiers = 0;
            host->SendMouseMoveEvent(mouseEvent, false);
            return 1; // Consumed
        }

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP: {
            int x, y;
            ScreenToOverlay(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), x, y);

            CefMouseEvent mouseEvent;
            mouseEvent.x = x;
            mouseEvent.y = y;
            mouseEvent.modifiers = 0;
            host->SendMouseClickEvent(mouseEvent, MBT_LEFT,
                                       uMsg == WM_LBUTTONUP, 1);
            return 1;
        }

        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP: {
            int x, y;
            ScreenToOverlay(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), x, y);

            CefMouseEvent mouseEvent;
            mouseEvent.x = x;
            mouseEvent.y = y;
            mouseEvent.modifiers = 0;
            host->SendMouseClickEvent(mouseEvent, MBT_RIGHT,
                                       uMsg == WM_RBUTTONUP, 1);
            return 1;
        }

        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP: {
            int x, y;
            ScreenToOverlay(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), x, y);

            CefMouseEvent mouseEvent;
            mouseEvent.x = x;
            mouseEvent.y = y;
            mouseEvent.modifiers = 0;
            host->SendMouseClickEvent(mouseEvent, MBT_MIDDLE,
                                       uMsg == WM_MBUTTONUP, 1);
            return 1;
        }

        case WM_MOUSEWHEEL: {
            int x, y;
            ScreenToOverlay(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), x, y);

            CefMouseEvent mouseEvent;
            mouseEvent.x = x;
            mouseEvent.y = y;
            mouseEvent.modifiers = 0;
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            host->SendMouseWheelEvent(mouseEvent, 0, delta);
            return 1;
        }

        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_CHAR:
        case WM_SYSCHAR: {
            cef_key_event_t keyEvent = MakeKeyEvent(uMsg, wParam, lParam);
            host->SendKeyEvent(keyEvent);
            return 1;
        }
    }

    return 0; // Not consumed — pass through to game
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
    s_browser = nullptr;
}

void SetBrowser(CefRefPtr<CefBrowser> browser) {
    s_browser = browser;
}

} // namespace InputHandler
