#pragma once

#include <string>

class InProcessBrowser;

// Handles bridge messages from the JS nexus-bridge.js (received as JSON via
// console.log interception in OnConsoleMessage), dispatches them to Nexus API
// functions, and sends responses back via ExecuteJavaScript.
//
// Per-addon IPC state (event subscriptions, keybind registrations, pending
// queues) lives in AddonInstance. This namespace handles message routing and
// the Nexus API call implementations.
namespace IpcHandler {

// Handle a JSON bridge message from the JS bridge.
// Called by InProcessBrowser::OnConsoleMessage after stripping the __NEXUS__: prefix.
// Extracts __addonId and __windowId from the message to route to the correct addon.
bool HandleBridgeMessage(const std::string& json, InProcessBrowser* browser);

} // namespace IpcHandler
