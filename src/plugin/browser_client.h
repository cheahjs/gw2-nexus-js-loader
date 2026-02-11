#pragma once

#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_render_handler.h"
#include "osr_render_handler.h"
#include "ipc_handler.h"

// CefClient implementation that ties together the render handler, life span handler,
// and IPC message dispatch for the browser process.
class BrowserClient : public CefClient,
                      public CefLifeSpanHandler {
public:
    BrowserClient(int width, int height);

    // CefClient
    CefRefPtr<CefRenderHandler> GetRenderHandler() override {
        return m_renderHandler;
    }

    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
        return this;
    }

    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefProcessId source_process,
                                  CefRefPtr<CefProcessMessage> message) override;

    // CefLifeSpanHandler
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

    // Access the render handler for texture retrieval
    OsrRenderHandler* GetOsrRenderHandler() const { return m_renderHandler.get(); }

    // Resize the offscreen rendering target
    void SetSize(int width, int height);

private:
    CefRefPtr<OsrRenderHandler> m_renderHandler;

    IMPLEMENT_REFCOUNTING(BrowserClient);
    DISALLOW_COPY_AND_ASSIGN(BrowserClient);
};
