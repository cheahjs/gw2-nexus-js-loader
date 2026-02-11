#include "renderer_app.h"
#include "js_bindings.h"
#include "ipc_client.h"
#include "shared/ipc_messages.h"

void RendererApp::OnWebKitInitialized() {
    // Register the nexus.* JavaScript extension
    JsBindings::RegisterExtension();
}

void RendererApp::OnContextCreated(CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<CefFrame> frame,
                                    CefRefPtr<CefV8Context> context) {
    // Store browser reference for IPC
    IpcClient::SetBrowser(browser);
}

bool RendererApp::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message)
{
    if (source_process != PID_BROWSER) return false;

    std::string msgName = message->GetName().ToString();

    // Handle async response (resolves JS Promises)
    if (msgName == IPC::ASYNC_RESPONSE) {
        return IpcClient::HandleAsyncResponse(message);
    }

    // Handle event dispatch from browser → JS callbacks
    if (msgName == IPC::EVENTS_DISPATCH) {
        return JsBindings::HandleEventDispatch(message);
    }

    // Handle keybind invocations from browser → JS callbacks
    if (msgName == IPC::KEYBINDS_INVOKE) {
        return JsBindings::HandleKeybindInvoke(message);
    }

    return false;
}
