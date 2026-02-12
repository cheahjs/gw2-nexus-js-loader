#pragma once

#include "d3d11_texture.h"

#include "include/cef_client.h"
#include "include/cef_render_handler.h"
#include "include/cef_display_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_request_handler.h"

#include <string>
#include <mutex>
#include <vector>
#include <cstdint>

// In-process CEF browser client. Reuses GW2's already-initialized CEF context
// to create an offscreen browser. Implements all necessary CefClient interfaces
// for rendering, input, JS bridge, and lifecycle management.
class InProcessBrowser : public CefClient,
                         public CefRenderHandler,
                         public CefDisplayHandler,
                         public CefLoadHandler,
                         public CefLifeSpanHandler,
                         public CefRequestHandler {
public:
    InProcessBrowser();
    ~InProcessBrowser() override;

    // Create an offscreen browser. Must be called on a CEF-compatible thread.
    bool Create(const std::string& url, int width, int height);

    // Close the browser gracefully.
    void Close();

    // Navigation
    void Navigate(const std::string& url);
    void Reload();
    void Resize(int width, int height);

    // Input forwarding — call CefBrowser::GetHost()->Send*Event directly
    void SendMouseMove(int x, int y, uint32_t modifiers);
    void SendMouseClick(int x, int y, uint32_t modifiers, int button,
                        bool mouseUp, int clickCount);
    void SendMouseWheel(int x, int y, uint32_t modifiers, int deltaX, int deltaY);
    void SendKeyEvent(uint32_t type, uint32_t modifiers, int windowsKeyCode,
                      int nativeKeyCode, bool isSystemKey, uint16_t character);

    // Frame access
    void* GetTextureHandle() const;
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

    // Apply buffered pixel data to the D3D11 texture.
    // Must be called on the render thread (e.g. from OnPreRender).
    void FlushFrame();

    // Browser access for ExecuteJavaScript calls
    CefRefPtr<CefBrowser> GetBrowser() const;
    bool IsReady() const;

    // Execute JavaScript in the main frame
    void ExecuteJavaScript(const std::string& code);

    // Creation failure tracking — renderer subprocess may crash
    bool HasCreationFailed() const { return m_creationFailed; }
    DWORD GetCreationRequestTick() const { return m_creationRequestTick; }

    // Addon/window identity — used for bridge script contextualization
    void SetAddonId(const std::string& id) { m_addonId = id; }
    void SetWindowId(const std::string& id) { m_windowId = id; }
    const std::string& GetAddonId() const { return m_addonId; }
    const std::string& GetWindowId() const { return m_windowId; }

    // CefClient
    CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }

    // CefRenderHandler
    void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
    void OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) override;
    void OnPopupSize(CefRefPtr<CefBrowser> browser, const CefRect& rect) override;
    void OnPaint(CefRefPtr<CefBrowser> browser,
                 PaintElementType type,
                 const RectList& dirtyRects,
                 const void* buffer,
                 int width,
                 int height) override;

    // CefDisplayHandler
    bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                          cef_log_severity_t level,
                          const CefString& message,
                          const CefString& source,
                          int line) override;

    // CefLoadHandler
    void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   int httpStatusCode) override;

    // CefLifeSpanHandler
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

    // CefRequestHandler — detect renderer subprocess crashes
    void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                    TerminationStatus status) override;

private:
    CefRefPtr<CefBrowser> m_browser;
    D3D11Texture          m_texture;
    int                   m_width  = 1280;
    int                   m_height = 720;
    bool                  m_ready  = false;

    // Creation failure tracking
    bool  m_creationFailed      = false;
    DWORD m_creationRequestTick = 0;

    // Addon/window identity
    std::string m_addonId;
    std::string m_windowId;

    // OnPaint pixel buffer (CEF thread writes, render thread reads via FlushFrame)
    std::mutex             m_frameMutex;
    std::vector<uint8_t>   m_frameBuffer;
    int                    m_frameWidth  = 0;
    int                    m_frameHeight = 0;
    bool                   m_frameDirty  = false;

    // Popup (dropdown) tracking for compositing PET_POPUP onto PET_VIEW
    bool m_popupVisible = false;
    CefRect m_popupRect;  // position and size of popup within the view

    IMPLEMENT_REFCOUNTING(InProcessBrowser);
    DISALLOW_COPY_AND_ASSIGN(InProcessBrowser);
};
