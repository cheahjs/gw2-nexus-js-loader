#include "host_browser_client.h"
#include <cstdio>

HostBrowserClient::HostBrowserClient(HostIpcBridge* ipcBridge, void* shmemView,
                                       int width, int height)
    : m_renderHandler(new HostOsrRenderHandler(shmemView, width, height))
    , m_ipcBridge(ipcBridge) {
}

bool HostBrowserClient::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> /*frame*/,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message)
{
    if (source_process != PID_RENDERER) return false;

    // Forward to IPC bridge, which sends over pipe to plugin
    return m_ipcBridge->OnRendererMessage(browser, message);
}

void HostBrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    fprintf(stderr, "[CEF Host] Browser created.\n");
    m_ipcBridge->SetBrowser(browser);
}

void HostBrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> /*browser*/) {
    fprintf(stderr, "[CEF Host] Browser closed.\n");
}

void HostBrowserClient::SetSize(int width, int height) {
    if (m_renderHandler) {
        m_renderHandler->SetSize(width, height);
    }
}
