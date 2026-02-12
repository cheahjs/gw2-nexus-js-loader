#include "in_process_browser.h"
#include "nexus_bridge.h"
#include "ipc_handler.h"
#include "globals.h"
#include "shared/version.h"

#include "include/cef_browser.h"
#include "include/cef_app.h"
#include "include/cef_values.h"

#include <string>

// Prefix used by the JS bridge to send messages via console.log
static const char* NEXUS_PREFIX = "__NEXUS__:";
static const size_t NEXUS_PREFIX_LEN = 10;

InProcessBrowser::InProcessBrowser() = default;

InProcessBrowser::~InProcessBrowser() {
    Close();
}

bool InProcessBrowser::Create(const std::string& url, int width, int height) {
    m_width = width;
    m_height = height;
    m_creationFailed = false;
    m_creationRequestTick = GetTickCount();

    CefWindowInfo windowInfo;
    windowInfo.SetAsWindowless(0);

    CefBrowserSettings settings;
    settings.windowless_frame_rate = 30;

    // Pass non-null extra_info. GW2's CefHost.exe (renderer subprocess) has
    // custom CefRenderProcessHandler code that may dereference data from
    // extra_info. Passing an empty (but non-null) dictionary might prevent a
    // null-pointer crash in GW2's renderer code.
    CefRefPtr<CefDictionaryValue> extraInfo = CefDictionaryValue::Create();

    if (Globals::API) {
        char msg[256];
        snprintf(msg, sizeof(msg),
            "CreateBrowser: thread=%lu, url=%s, size=%dx%d",
            GetCurrentThreadId(), url.c_str(), width, height);
        Globals::API->Log(LOGL_INFO, ADDON_NAME, msg);
    }

    // Use async CreateBrowser to avoid potential deadlocks.
    // The browser will be created on the CEF UI thread and OnAfterCreated
    // will fire with the browser reference when it's ready.
    bool result = CefBrowserHost::CreateBrowser(
        windowInfo, this, url, settings, extraInfo, nullptr);

    if (!result) {
        if (Globals::API) {
            Globals::API->Log(LOGL_CRITICAL, ADDON_NAME,
                "CreateBrowser failed — GW2's CEF context may not be ready.");
        }
        m_creationFailed = true;
        return false;
    }

    if (Globals::API) {
        Globals::API->Log(LOGL_INFO, ADDON_NAME,
            "Browser creation requested (async). Waiting for OnAfterCreated...");
    }
    return true;
}

void InProcessBrowser::Close() {
    if (m_browser) {
        m_browser->GetHost()->CloseBrowser(true);
        m_browser = nullptr;
    }
    m_ready = false;
    m_texture.Release();
}

void InProcessBrowser::Navigate(const std::string& url) {
    if (m_browser) {
        m_browser->GetMainFrame()->LoadURL(url);
    }
}

void InProcessBrowser::Reload() {
    if (m_browser) {
        m_browser->Reload();
    }
}

void InProcessBrowser::Resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (width == m_width && height == m_height) return;
    m_width = width;
    m_height = height;
    if (m_browser) {
        m_browser->GetHost()->WasResized();
    }
}


// ---- Input forwarding ----

void InProcessBrowser::SendMouseMove(int x, int y, uint32_t modifiers) {
    if (!m_browser) return;
    CefMouseEvent event;
    event.x = x;
    event.y = y;
    event.modifiers = modifiers;
    m_browser->GetHost()->SendMouseMoveEvent(event, false);
}

void InProcessBrowser::SendMouseClick(int x, int y, uint32_t modifiers,
                                       int button, bool mouseUp, int clickCount) {
    if (!m_browser) return;
    CefMouseEvent event;
    event.x = x;
    event.y = y;
    event.modifiers = modifiers;

    CefBrowserHost::MouseButtonType mbType;
    switch (button) {
        case 0: mbType = MBT_LEFT; break;
        case 1: mbType = MBT_MIDDLE; break;
        case 2: mbType = MBT_RIGHT; break;
        default: mbType = MBT_LEFT; break;
    }

    m_browser->GetHost()->SendMouseClickEvent(event, mbType, mouseUp, clickCount);
}

void InProcessBrowser::SendMouseWheel(int x, int y, uint32_t modifiers,
                                       int deltaX, int deltaY) {
    if (!m_browser) return;
    CefMouseEvent event;
    event.x = x;
    event.y = y;
    event.modifiers = modifiers;
    m_browser->GetHost()->SendMouseWheelEvent(event, deltaX, deltaY);
}

void InProcessBrowser::SendKeyEvent(uint32_t type, uint32_t modifiers,
                                     int windowsKeyCode, int nativeKeyCode,
                                     bool isSystemKey, uint16_t character) {
    if (!m_browser) return;
    CefKeyEvent event;
    event.modifiers = modifiers;
    event.windows_key_code = windowsKeyCode;
    event.native_key_code = nativeKeyCode;
    event.is_system_key = isSystemKey;
    event.character = character;
    event.unmodified_character = character;

    switch (type) {
        case 0: event.type = KEYEVENT_RAWKEYDOWN; break;
        case 2: event.type = KEYEVENT_KEYUP; break;
        case 3: event.type = KEYEVENT_CHAR; break;
        default: event.type = KEYEVENT_RAWKEYDOWN; break;
    }

    m_browser->GetHost()->SendKeyEvent(event);
}

// ---- Frame access ----

void* InProcessBrowser::GetTextureHandle() const {
    return m_texture.GetShaderResourceView();
}

CefRefPtr<CefBrowser> InProcessBrowser::GetBrowser() const {
    return m_browser;
}

bool InProcessBrowser::IsReady() const {
    return m_ready && m_browser;
}

void InProcessBrowser::ExecuteJavaScript(const std::string& code) {
    if (m_browser && m_browser->GetMainFrame()) {
        m_browser->GetMainFrame()->ExecuteJavaScript(code, "nexus://bridge", 0);
    }
}

// ---- CefRenderHandler ----

void InProcessBrowser::GetViewRect(CefRefPtr<CefBrowser> /*browser*/, CefRect& rect) {
    rect = CefRect(0, 0, m_width, m_height);
}

void InProcessBrowser::OnPopupShow(CefRefPtr<CefBrowser> /*browser*/, bool show) {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    m_popupVisible = show;
    if (!show) {
        m_popupRect = CefRect();
    }
}

void InProcessBrowser::OnPopupSize(CefRefPtr<CefBrowser> /*browser*/, const CefRect& rect) {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    m_popupRect = rect;
}

void InProcessBrowser::OnPaint(CefRefPtr<CefBrowser> /*browser*/,
                                PaintElementType type,
                                const RectList& /*dirtyRects*/,
                                const void* buffer,
                                int width,
                                int height) {
    // Buffer the pixel data for the render thread to apply.
    // OnPaint is called on CEF's browser thread; D3D11 device context is
    // only safe to use from the render thread, so we defer the texture update.
    std::lock_guard<std::mutex> lock(m_frameMutex);

    if (type == PET_VIEW) {
        size_t size = static_cast<size_t>(width) * height * 4;
        m_frameBuffer.resize(size);
        memcpy(m_frameBuffer.data(), buffer, size);
        m_frameWidth = width;
        m_frameHeight = height;
    } else if (type == PET_POPUP && m_popupVisible && m_frameWidth > 0 && m_frameHeight > 0) {
        // Composite the popup (e.g. <select> dropdown) onto the main view buffer.
        const uint8_t* src = static_cast<const uint8_t*>(buffer);
        int srcStride = width * 4;
        int dstStride = m_frameWidth * 4;

        for (int row = 0; row < height; ++row) {
            int dstY = m_popupRect.y + row;
            if (dstY < 0 || dstY >= m_frameHeight) continue;

            int srcX0 = 0;
            int dstX0 = m_popupRect.x;
            int copyW = width;

            // Clip left
            if (dstX0 < 0) {
                srcX0 = -dstX0;
                copyW += dstX0;
                dstX0 = 0;
            }
            // Clip right
            if (dstX0 + copyW > m_frameWidth) {
                copyW = m_frameWidth - dstX0;
            }
            if (copyW <= 0) continue;

            memcpy(m_frameBuffer.data() + dstY * dstStride + dstX0 * 4,
                   src + row * srcStride + srcX0 * 4,
                   copyW * 4);
        }
    }

    m_frameDirty = true;
}

void InProcessBrowser::FlushFrame() {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    if (!m_frameDirty) return;
    m_texture.UpdateFromPixels(m_frameBuffer.data(), m_frameWidth, m_frameHeight);
    m_frameDirty = false;
}

// ---- CefDisplayHandler ----

bool InProcessBrowser::OnConsoleMessage(CefRefPtr<CefBrowser> /*browser*/,
                                         cef_log_severity_t /*level*/,
                                         const CefString& message,
                                         const CefString& /*source*/,
                                         int /*line*/) {
    std::string msg = message.ToString();

    // Check for bridge message prefix
    if (msg.size() > NEXUS_PREFIX_LEN &&
        msg.compare(0, NEXUS_PREFIX_LEN, NEXUS_PREFIX) == 0) {
        // Strip prefix and dispatch JSON to IpcHandler
        std::string json = msg.substr(NEXUS_PREFIX_LEN);
        IpcHandler::HandleBridgeMessage(json, this);
        return true; // Suppress from CEF console output
    }

    // Normal console.log — let it pass through
    return false;
}

// ---- CefLoadHandler ----

void InProcessBrowser::OnLoadEnd(CefRefPtr<CefBrowser> /*browser*/,
                                  CefRefPtr<CefFrame> frame,
                                  int /*httpStatusCode*/) {
    if (frame->IsMain()) {
        // Inject the nexus bridge JavaScript
        frame->ExecuteJavaScript(NexusBridge::GetBridgeScript(), "nexus://bridge", 0);

        if (Globals::API) {
            Globals::API->Log(LOGL_DEBUG, ADDON_NAME, "Nexus bridge injected.");
        }
    }
}

// ---- CefLifeSpanHandler ----

void InProcessBrowser::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    m_browser = browser;
    m_ready = true;

    if (Globals::API) {
        DWORD elapsed = GetTickCount() - m_creationRequestTick;
        char msg[128];
        snprintf(msg, sizeof(msg),
            "OnAfterCreated: browser ready (took %lu ms, id=%d)",
            elapsed, browser->GetIdentifier());
        Globals::API->Log(LOGL_INFO, ADDON_NAME, msg);
    }
}

void InProcessBrowser::OnBeforeClose(CefRefPtr<CefBrowser> /*browser*/) {
    m_browser = nullptr;
    m_ready = false;
}

// ---- CefRequestHandler ----

void InProcessBrowser::OnRenderProcessTerminated(
    CefRefPtr<CefBrowser> /*browser*/, TerminationStatus status) {
    m_creationFailed = true;
    m_ready = false;

    if (Globals::API) {
        const char* statusStr = "unknown";
        switch (status) {
            case TS_ABNORMAL_TERMINATION: statusStr = "abnormal"; break;
            case TS_PROCESS_WAS_KILLED:   statusStr = "killed"; break;
            case TS_PROCESS_CRASHED:      statusStr = "crashed"; break;
            case TS_PROCESS_OOM:          statusStr = "out-of-memory"; break;
            default: break;
        }
        char msg[256];
        snprintf(msg, sizeof(msg),
            "Renderer process terminated (%s). "
            "GW2's CefHost.exe may be incompatible with our browser. "
            "Browser feature disabled.",
            statusStr);
        Globals::API->Log(LOGL_CRITICAL, ADDON_NAME, msg);
    }
}
