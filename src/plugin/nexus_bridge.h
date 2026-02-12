#pragma once

#include <string>

// Contains the nexus-bridge.js source as a C++ string literal.
// Injected into every page on load via OnLoadEnd → ExecuteJavaScript.
//
// JS→Native channel: console.log("__NEXUS__:" + JSON.stringify({action, ...}))
// Native→JS channel: window.__nexus_dispatch({type, ...}) via ExecuteJavaScript
namespace NexusBridge {

// Get the complete bridge JavaScript source code.
const std::string& GetBridgeScript();

} // namespace NexusBridge
