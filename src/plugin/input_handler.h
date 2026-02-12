#pragma once

// Handles forwarding Windows input messages to the in-process CEF browser
// when the overlay is focused.
namespace InputHandler {

// Register WndProc callback with Nexus. Call from Load().
void Initialize();

// Deregister WndProc callback. Call from Unload().
void Shutdown();

} // namespace InputHandler
