#pragma once

#include "include/cef_app.h"
#include "include/cef_render_process_handler.h"

// CefApp implementation for the renderer (sub)process.
// Registers JS extensions and handles IPC with the browser process.
class RendererApp : public CefApp,
                    public CefRenderProcessHandler {
public:
    RendererApp() = default;

    // CefApp
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
        return this;
    }

    // CefRenderProcessHandler
    void OnWebKitInitialized() override;
    void OnContextCreated(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          CefRefPtr<CefV8Context> context) override;
    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefProcessId source_process,
                                  CefRefPtr<CefProcessMessage> message) override;

private:
    IMPLEMENT_REFCOUNTING(RendererApp);
    DISALLOW_COPY_AND_ASSIGN(RendererApp);
};
