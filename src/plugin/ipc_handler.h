#pragma once

#include "shared/pipe_protocol.h"
#include <string>
#include <vector>

// Handles IPC messages from the CEF host process (forwarded from the renderer),
// dispatching them to Nexus API functions and sending responses back over the pipe.
namespace IpcHandler {

// Handle an API request received from the host process via pipe.
// Returns true if the message was handled.
bool HandleApiRequest(const std::string& messageName,
                       const std::vector<PipeProtocol::PipeArg>& args);

// Subscribe to a Nexus event on behalf of the renderer process.
void SubscribeEvent(const std::string& eventName);

// Unsubscribe from a Nexus event.
void UnsubscribeEvent(const std::string& eventName);

// Flush queued events/keybind invocations to the host process.
// Call from OnPreRender after polling pipe.
void FlushPendingEvents();

// Clean up all subscriptions. Call from Unload().
void Cleanup();

} // namespace IpcHandler
