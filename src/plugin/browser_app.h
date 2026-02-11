#pragma once

#include "include/cef_app.h"

// CefApp implementation for the browser process.
// Handles browser process lifecycle and settings.
class BrowserApp : public CefApp,
                   public CefBrowserProcessHandler {
public:
    BrowserApp() = default;

    // CefApp
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
        return this;
    }

    // CefBrowserProcessHandler
    void OnContextInitialized() override;

private:
    IMPLEMENT_REFCOUNTING(BrowserApp);
    DISALLOW_COPY_AND_ASSIGN(BrowserApp);
};
