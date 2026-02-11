#pragma once

#include "include/cef_process_message.h"
#include "include/cef_v8.h"

// Registers the nexus.* JavaScript API extension in the renderer process,
// and handles event/keybind dispatch from the browser process to JS callbacks.
namespace JsBindings {

// Register the V8 extension. Call from OnWebKitInitialized().
void RegisterExtension();

// Handle an event dispatched from the browser process.
bool HandleEventDispatch(CefRefPtr<CefProcessMessage> message);

// Handle a keybind invocation from the browser process.
bool HandleKeybindInvoke(CefRefPtr<CefProcessMessage> message);

} // namespace JsBindings
