#pragma once

#include "include/cef_browser.h"

// Handles forwarding Windows input messages to the CEF browser when the overlay is focused.
namespace InputHandler {

// Register WndProc callback with Nexus. Call from Load().
void Initialize();

// Deregister WndProc callback. Call from Unload().
void Shutdown();

// Set the active browser to forward input to.
void SetBrowser(CefRefPtr<CefBrowser> browser);

} // namespace InputHandler
