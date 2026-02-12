#pragma once

#include "include/cef_app.h"

// CefApp implementation for the host's browser process.
class HostBrowserApp : public CefApp,
                       public CefBrowserProcessHandler {
public:
    HostBrowserApp() = default;

    // CefApp
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
        return this;
    }

    // CefBrowserProcessHandler
    void OnContextInitialized() override;

private:
    IMPLEMENT_REFCOUNTING(HostBrowserApp);
    DISALLOW_COPY_AND_ASSIGN(HostBrowserApp);
};
