#pragma once

#include <string>

#include "include/cef_browser.h"
#include "include/cef_process_message.h"

// Handles IPC messages from the renderer process, dispatching them to Nexus API functions.
namespace IpcHandler {

// Called by BrowserClient::OnProcessMessageReceived.
// Returns true if the message was handled.
bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               CefProcessId source_process,
                               CefRefPtr<CefProcessMessage> message);

// Subscribe to a Nexus event on behalf of the renderer process.
// The callback will queue events for IPC dispatch.
void SubscribeEvent(CefRefPtr<CefBrowser> browser, const std::string& eventName);

// Unsubscribe from a Nexus event.
void UnsubscribeEvent(const std::string& eventName);

// Flush queued events/keybind invocations to the renderer process.
// Call from RT_PreRender after CefDoMessageLoopWork.
void FlushPendingEvents(CefRefPtr<CefBrowser> browser);

// Clean up all subscriptions. Call from Unload().
void Cleanup();

} // namespace IpcHandler
