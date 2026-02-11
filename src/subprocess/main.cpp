#include <windows.h>
#include "include/cef_app.h"
#include "renderer_app.h"

// CEF subprocess entry point.
// This executable is launched by CEF for renderer, GPU, and other processes.
int APIENTRY wWinMain(HINSTANCE hInstance,
                      HINSTANCE /*hPrevInstance*/,
                      LPWSTR /*lpCmdLine*/,
                      int /*nCmdShow*/) {
    CefMainArgs mainArgs(hInstance);

    CefRefPtr<RendererApp> app = new RendererApp();

    // CefExecuteProcess returns -1 for the browser process (shouldn't happen here)
    // and the exit code for sub-processes.
    int exitCode = CefExecuteProcess(mainArgs, app, nullptr);
    return exitCode;
}
