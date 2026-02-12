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

    // Add Wine/CrossOver-compatible command-line switches
    void OnBeforeCommandLineProcessing(
        const CefString& process_type,
        CefRefPtr<CefCommandLine> command_line) override;

    // CefBrowserProcessHandler
    void OnContextInitialized() override;

private:
    IMPLEMENT_REFCOUNTING(HostBrowserApp);
    DISALLOW_COPY_AND_ASSIGN(HostBrowserApp);
};
