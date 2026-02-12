#pragma once

#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_render_handler.h"
#include "host_osr_render_handler.h"
#include "host_ipc_bridge.h"

// CefClient implementation for the host process.
// Routes renderer IPC messages through HostIpcBridge to the plugin pipe.
class HostBrowserClient : public CefClient,
                          public CefLifeSpanHandler {
public:
    HostBrowserClient(HostIpcBridge* ipcBridge, void* shmemView, int width, int height);

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

    // Resize the offscreen rendering target
    void SetSize(int width, int height);

private:
    CefRefPtr<HostOsrRenderHandler> m_renderHandler;
    HostIpcBridge* m_ipcBridge;

    IMPLEMENT_REFCOUNTING(HostBrowserClient);
    DISALLOW_COPY_AND_ASSIGN(HostBrowserClient);
};
