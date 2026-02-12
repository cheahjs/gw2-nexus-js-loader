#pragma once

#include "host_pipe_client.h"
#include "include/cef_browser.h"
#include "include/cef_process_message.h"

// Translates between CefProcessMessage (renderer <-> host) and pipe messages (host <-> plugin).
// Converts renderer API requests into NEXUS_API_REQUEST pipe messages.
// Converts API response / event dispatch / keybind invoke pipe messages into CefProcessMessages.
class HostIpcBridge {
public:
    HostIpcBridge(HostPipeClient* pipe);

    // Set the browser reference (for sending messages to renderer).
    void SetBrowser(CefRefPtr<CefBrowser> browser);

    // Called by HostBrowserClient::OnProcessMessageReceived.
    // Serializes the CefProcessMessage and sends as NEXUS_API_REQUEST over pipe.
    bool OnRendererMessage(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefProcessMessage> message);

    // Handle incoming pipe messages that need to be forwarded to the renderer.
    // Called from the host main loop after polling pipe.
    void HandlePipeMessage(const PipeProtocol::PipeMessage& msg);

private:
    HostPipeClient* m_pipe = nullptr;
    CefRefPtr<CefBrowser> m_browser;
};
