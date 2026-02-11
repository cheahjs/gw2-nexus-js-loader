#pragma once

#include "include/cef_browser.h"
#include "include/cef_process_message.h"
#include "include/cef_v8.h"

#include <string>
#include <functional>

// Renderer-side IPC client: sends messages to the browser process and manages
// async request tracking for Promise resolution.
namespace IpcClient {

// Set the browser to send IPC messages through.
void SetBrowser(CefRefPtr<CefBrowser> browser);

// Send a fire-and-forget IPC message to the browser process.
void SendMessage(const std::string& name, CefRefPtr<CefListValue> args);

// Send an async request and get a request ID for tracking.
// The callback will be invoked when the browser sends back ASYNC_RESPONSE.
using AsyncCallback = std::function<void(bool success, const std::string& value)>;
int SendAsyncRequest(const std::string& name, CefRefPtr<CefListValue> args,
                      CefRefPtr<CefV8Context> context,
                      CefRefPtr<CefV8Value> resolveFunc,
                      CefRefPtr<CefV8Value> rejectFunc);

// Handle an ASYNC_RESPONSE message from the browser.
bool HandleAsyncResponse(CefRefPtr<CefProcessMessage> message);

} // namespace IpcClient
