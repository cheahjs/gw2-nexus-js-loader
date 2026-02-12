#pragma once

// Delay-load gate for CEF. Checks whether GW2 has loaded libcef.dll and
// verifies the API hash matches the version we compiled against.
// All CEF calls must be gated behind CefLoader::IsAvailable().

namespace CefLoader {

// Check if GW2's libcef.dll is loaded and the API hash matches.
// Safe to call at any time — does not trigger delay-load resolution.
bool IsAvailable();

// Try to initialize: verify API hash, log libcef.dll path.
// Returns true if CEF is ready to use. Idempotent.
bool TryInitialize();

} // namespace CefLoader
