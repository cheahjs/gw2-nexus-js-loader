#pragma once

#include <string>

class InProcessBrowser;

// Handles bridge messages from the JS nexus-bridge.js (received as JSON via
// console.log interception in OnConsoleMessage), dispatches them to Nexus API
// functions, and sends responses back via ExecuteJavaScript.
namespace IpcHandler {

// Handle a JSON bridge message from the JS bridge.
// Called by InProcessBrowser::OnConsoleMessage after stripping the __NEXUS__: prefix.
bool HandleBridgeMessage(const std::string& json, InProcessBrowser* browser);

// Subscribe to a Nexus event on behalf of the JS page.
void SubscribeEvent(const std::string& eventName);

// Unsubscribe from a Nexus event.
void UnsubscribeEvent(const std::string& eventName);

// Flush queued events/keybind invocations to JS via ExecuteJavaScript.
// Call from OnPreRender.
void FlushPendingEvents();

// Set the browser instance for dispatching events/keybinds to JS.
void SetBrowser(InProcessBrowser* browser);

// Clean up all subscriptions. Call from Unload().
void Cleanup();

} // namespace IpcHandler
