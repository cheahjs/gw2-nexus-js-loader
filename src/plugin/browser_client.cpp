#include "browser_client.h"
#include "globals.h"
#include "ipc_handler.h"
#include "shared/version.h"

BrowserClient::BrowserClient(int width, int height)
    : m_renderHandler(new OsrRenderHandler(width, height)) {
}

bool BrowserClient::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message)
{
    return IpcHandler::OnProcessMessageReceived(browser, frame, source_process, message);
}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    if (Globals::API) {
        Globals::API->Log(LOGL_INFO, ADDON_NAME, "CEF browser created.");
    }
}

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    if (Globals::API) {
        Globals::API->Log(LOGL_INFO, ADDON_NAME, "CEF browser closed.");
    }
}

void BrowserClient::SetSize(int width, int height) {
    if (m_renderHandler) {
        m_renderHandler->SetSize(width, height);
    }
}
