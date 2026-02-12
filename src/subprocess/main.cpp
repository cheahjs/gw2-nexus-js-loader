#include <windows.h>
#include <cstdio>
#include <string>
#include "include/cef_app.h"
#include "renderer_app.h"

// CEF subprocess entry point.
// This executable is launched by CEF for renderer, GPU, and other processes.
int APIENTRY wWinMain(HINSTANCE hInstance,
                      HINSTANCE /*hPrevInstance*/,
                      LPWSTR /*lpCmdLine*/,
                      int /*nCmdShow*/) {
    auto ResolveModuleDir = []() -> std::string {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string fullPath(path);
        size_t pos = fullPath.find_last_of("\\/");
        if (pos == std::string::npos) {
            return ".";
        }
        return fullPath.substr(0, pos);
    };

    std::string stderrPath = ResolveModuleDir() + "\\cef_subprocess_stderr.log";
    freopen(stderrPath.c_str(), "a", stderr);
    fprintf(stderr, "\n[CEF Subprocess] ===========================================\n");
    fprintf(stderr, "[CEF Subprocess] PID: %lu\n", GetCurrentProcessId());
    fprintf(stderr, "[CEF Subprocess] Command line: %s\n", GetCommandLineA());
    fflush(stderr);

    CefMainArgs mainArgs(hInstance);

    CefRefPtr<RendererApp> app = new RendererApp();

    // CefExecuteProcess returns -1 for the browser process (shouldn't happen here)
    // and the exit code for sub-processes.
    int exitCode = CefExecuteProcess(mainArgs, app, nullptr);
    fprintf(stderr, "[CEF Subprocess] CefExecuteProcess returned: %d\n", exitCode);
    fflush(stderr);
    return exitCode;
}
