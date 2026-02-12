#pragma once

// Handles forwarding Windows input messages to the CEF host process
// when the overlay is focused, via CefHostProxy pipe messages.
namespace InputHandler {

// Register WndProc callback with Nexus. Call from Load().
void Initialize();

// Deregister WndProc callback. Call from Unload().
void Shutdown();

} // namespace InputHandler
