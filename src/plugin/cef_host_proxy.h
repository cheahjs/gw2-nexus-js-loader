#pragma once

#include "shared/pipe_protocol.h"
#include <string>
#include <cstdint>

// Orchestrates the out-of-process CEF host: manages the pipe connection,
// shared memory frame reader, and host process lifecycle.
// Replaces CefManager from the in-process architecture.
namespace CefHostProxy {

// Initialize: create shared memory + pipe, launch host, wait for HOST_READY.
bool Initialize();

// Shut down: send SHUTDOWN, wait for host exit, clean up.
void Shutdown();

// Poll pipe messages and shared memory frames. Call from OnPreRender.
void Tick();

// Browser management
bool CreateBrowser(const std::string& url, int width, int height);
void CloseBrowser();
void ResizeBrowser(int width, int height);
void Navigate(const std::string& url);
void Reload();

// Input forwarding
void SendMouseMove(int x, int y, uint32_t modifiers);
void SendMouseClick(int x, int y, uint32_t modifiers, uint32_t button,
                    bool mouseUp, int clickCount);
void SendMouseWheel(int x, int y, uint32_t modifiers, int deltaX, int deltaY);
void SendKeyEvent(uint32_t type, uint32_t modifiers, int windowsKeyCode,
                  int nativeKeyCode, bool isSystemKey, uint16_t character);

// Send an API response back to the host (for forwarding to renderer)
void SendApiResponse(int requestId, bool success, const std::string& value);

// Send an event dispatch to the host (for forwarding to renderer)
void SendEventDispatch(const std::string& eventName, const std::string& jsonData);

// Send a keybind invocation to the host (for forwarding to renderer)
void SendKeybindInvoke(const std::string& identifier, bool isRelease);

// Frame access for overlay rendering
void* GetTextureHandle();
int   GetWidth();
int   GetHeight();

// Status
bool IsReady();

} // namespace CefHostProxy
