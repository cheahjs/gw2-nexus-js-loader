#include <windows.h>
#include <cstdio>
#include <string>
#include <cstring>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "include/cef_version.h"
#include "include/cef_api_hash.h"

#include "host_browser_app.h"
#include "host_browser_client.h"
#include "host_ipc_bridge.h"
#include "host_pipe_client.h"
#include "shared/pipe_protocol.h"

// Parse a command-line argument of the form --key="value" or --key=value
// from the full command line string.
static std::string GetArg(const char* key, const std::string& cmdLine) {
    std::string prefix = std::string("--") + key + "=";
    size_t pos = cmdLine.find(prefix);
    if (pos == std::string::npos) return "";

    size_t valStart = pos + prefix.size();
    if (valStart >= cmdLine.size()) return "";

    std::string val;
    if (cmdLine[valStart] == '"') {
        // Quoted value
        size_t endQuote = cmdLine.find('"', valStart + 1);
        if (endQuote != std::string::npos) {
            val = cmdLine.substr(valStart + 1, endQuote - valStart - 1);
        }
    } else {
        // Unquoted value — ends at space or end of string
        size_t end = cmdLine.find(' ', valStart);
        val = cmdLine.substr(valStart, end - valStart);
    }
    return val;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    std::string fullCmdLine = GetCommandLineA();

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

    // Parse cef-dir first so we can redirect stderr there.
    std::string cefDir = GetArg("cef-dir", fullCmdLine);

    // Redirect stderr for diagnostics. Use append mode so multiple process
    // instances (including possible CEF child process launches) don't clobber
    // previous output.
    std::string stderrBaseDir = cefDir.empty() ? ResolveModuleDir() : cefDir;
    std::string stderrPath = stderrBaseDir + "\\cef_host_stderr.log";
    freopen(stderrPath.c_str(), "a", stderr);

    fprintf(stderr, "\n[CEF Host] ==================================================\n");
    fprintf(stderr, "[CEF Host] PID: %lu\n", GetCurrentProcessId());
    fprintf(stderr, "[CEF Host] Command line: %s\n", fullCmdLine.c_str());
    fflush(stderr);

    // If this executable is launched by Chromium as a child process
    // (--type=renderer/gpu/utility/...), forward immediately to CEF.
    // This guards against startup failure if browser_subprocess_path is ignored
    // or unavailable for any reason.
    if (fullCmdLine.find("--type=") != std::string::npos) {
        fprintf(stderr, "[CEF Host] Detected CEF child-process invocation.\n");
        CefMainArgs childArgs(hInstance);
        int childExit = CefExecuteProcess(childArgs, nullptr, nullptr);
        fprintf(stderr, "[CEF Host] Child CefExecuteProcess returned: %d\n", childExit);
        fflush(stderr);
        return childExit;
    }

    // Parse remaining arguments
    std::string pipeName  = GetArg("pipe-name", fullCmdLine);
    std::string shmemName = GetArg("shmem-name", fullCmdLine);

    if (cefDir.empty() || pipeName.empty() || shmemName.empty()) {
        fprintf(stderr, "[CEF Host] Missing required arguments.\n");
        fprintf(stderr, "Usage: nexus_js_cef_host.exe "
                "--cef-dir=<path> --pipe-name=<name> --shmem-name=<name>\n");
        fprintf(stderr, "Command line was: %s\n", fullCmdLine.c_str());
        return 1;
    }

    fprintf(stderr, "[CEF Host] Starting with:\n");
    fprintf(stderr, "  cef-dir:    %s\n", cefDir.c_str());
    fprintf(stderr, "  pipe-name:  %s\n", pipeName.c_str());
    fprintf(stderr, "  shmem-name: %s\n", shmemName.c_str());

    auto LogPathStatus = [](const char* label, const std::string& path, bool expectDir) {
        DWORD attrs = GetFileAttributesA(path.c_str());
        bool exists = (attrs != INVALID_FILE_ATTRIBUTES);
        bool isDir = exists && ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
        bool okType = exists && ((expectDir && isDir) || (!expectDir && !isDir));
        fprintf(stderr, "[CEF Host] %s: %s (%s)\n",
                label,
                okType ? "FOUND" : "MISSING",
                path.c_str());
    };

    // Required CEF runtime files. Missing any of these can cause early
    // CefInitialize failure with little/no logging.
    LogPathStatus("libcef.dll", cefDir + "\\libcef.dll", false);
    LogPathStatus("chrome_elf.dll", cefDir + "\\chrome_elf.dll", false);
    LogPathStatus("icudtl.dat", cefDir + "\\icudtl.dat", false);
    LogPathStatus("v8_context_snapshot.bin", cefDir + "\\v8_context_snapshot.bin", false);
    LogPathStatus("chrome_100_percent.pak", cefDir + "\\chrome_100_percent.pak", false);
    LogPathStatus("chrome_200_percent.pak", cefDir + "\\chrome_200_percent.pak", false);
    LogPathStatus("resources.pak", cefDir + "\\resources.pak", false);
    LogPathStatus("locales dir", cefDir + "\\locales", true);
    LogPathStatus("subprocess", cefDir + "\\nexus_js_subprocess.exe", false);
    // Newer CEF builds may optionally use bootstrap binaries on Windows.
    LogPathStatus("bootstrap.exe", cefDir + "\\bootstrap.exe", false);
    LogPathStatus("bootstrapc.exe", cefDir + "\\bootstrapc.exe", false);
    fflush(stderr);

    // 1. Connect to plugin's named pipe
    HostPipeClient pipe;
    if (!pipe.Connect(pipeName, 10000)) {
        fprintf(stderr, "[CEF Host] Failed to connect to plugin pipe.\n");
        return 1;
    }
    fprintf(stderr, "[CEF Host] Connected to plugin pipe.\n");

    // 2. Open shared memory
    HANDLE hMapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, shmemName.c_str());
    if (!hMapping) {
        fprintf(stderr, "[CEF Host] Failed to open shared memory '%s' (error %lu).\n",
                shmemName.c_str(), GetLastError());
        pipe.Close();
        return 1;
    }

    void* shmemView = MapViewOfFile(hMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!shmemView) {
        fprintf(stderr, "[CEF Host] Failed to map shared memory.\n");
        CloseHandle(hMapping);
        pipe.Close();
        return 1;
    }
    fprintf(stderr, "[CEF Host] Shared memory mapped.\n");

    // 3. Initialize CEF
    CefMainArgs mainArgs(hInstance);

    // Ensure libcef.dll's dependencies can be found
    SetDllDirectoryA(cefDir.c_str());

    std::string subprocessPath = cefDir + "\\nexus_js_subprocess.exe";
    CefRefPtr<HostBrowserApp> app = new HostBrowserApp();

    // CefExecuteProcess must be called before CefInitialize. In the browser
    // process this should return -1.
    fprintf(stderr, "[CEF Host] Calling CefExecuteProcess...\n");
    fflush(stderr);
    int exitCode = CefExecuteProcess(mainArgs, app, nullptr);
    fprintf(stderr, "[CEF Host] CefExecuteProcess returned: %d\n", exitCode);
    if (exitCode >= 0) {
        UnmapViewOfFile(shmemView);
        CloseHandle(hMapping);
        pipe.Close();
        return exitCode;
    }

    // --- Full initialization ---
    fprintf(stderr, "[CEF Host] Calling CefInitialize...\n");
    fflush(stderr);

    CefSettings settings = {};
    settings.size = sizeof(CefSettings);
    settings.no_sandbox = true;
    settings.multi_threaded_message_loop = false;
    settings.windowless_rendering_enabled = true;
    settings.command_line_args_disabled = true;

    CefString(&settings.browser_subprocess_path).FromString(subprocessPath);

    CefString(&settings.resources_dir_path).FromString(cefDir);

    std::string localesDir = cefDir + "\\locales";
    CefString(&settings.locales_dir_path).FromString(localesDir);

    char tempPath[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tempPath);
    std::string tempDir = std::string(tempPath) + "nexus_js_cef_"
                        + std::to_string(GetCurrentProcessId())
                        + "_" + std::to_string((unsigned long long)GetTickCount64());
    CreateDirectoryA(tempDir.c_str(), nullptr);

    std::string rootCachePath = tempDir;
    CefString(&settings.root_cache_path).FromString(rootCachePath);

    std::string cachePath = rootCachePath + "\\cache";
    CreateDirectoryA(cachePath.c_str(), nullptr);
    CefString(&settings.cache_path).FromString(cachePath);

    std::string logPath = tempDir + "\\cef_debug.log";
    CefString(&settings.log_file).FromString(logPath);
    settings.log_severity = LOGSEVERITY_VERBOSE;

    // Chromium also honors CHROME_LOG_FILE in many startup paths.
    SetEnvironmentVariableA("CHROME_LOG_FILE", logPath.c_str());

    // Verify API hash match between wrapper and libcef.dll
    // CEF 103 uses 1-param cef_api_hash(entry): 0=universal, 1=platform
    const char* runtime_hash = cef_api_hash(1);
    fprintf(stderr, "[CEF Host] API hash: %s\n",
            (runtime_hash && strcmp(runtime_hash, CEF_API_HASH_PLATFORM) == 0) ? "MATCH" : "MISMATCH");
    fprintf(stderr, "[CEF Host] CEF version: %s (Chromium %d)\n",
            CEF_VERSION, CHROME_VERSION_MAJOR);

    fprintf(stderr, "[CEF Host] CEF settings:\n");
    fprintf(stderr, "  subprocess:      %s\n", subprocessPath.c_str());
    fprintf(stderr, "  resources_dir:   %s\n", cefDir.c_str());
    fprintf(stderr, "  locales_dir:     %s\n", localesDir.c_str());
    fprintf(stderr, "  root_cache_path: %s\n", rootCachePath.c_str());
    fprintf(stderr, "  cache_path:      %s\n", cachePath.c_str());
    fprintf(stderr, "  log_file:        %s\n", logPath.c_str());

    // Verify cache/log dir write permissions explicitly.
    std::string testFile = tempDir + "\\write_test.tmp";
    FILE* tf = fopen(testFile.c_str(), "w");
    if (tf) {
        fprintf(tf, "test");
        fclose(tf);
        DeleteFileA(testFile.c_str());
        fprintf(stderr, "[CEF Host] Cache/log dir writable: YES (%s)\n", tempDir.c_str());
    } else {
        fprintf(stderr, "[CEF Host] Cache/log dir writable: NO (%s) (error %lu)\n",
                tempDir.c_str(), GetLastError());
    }
    fflush(stderr);

    if (!CefInitialize(mainArgs, settings, app, nullptr)) {
        fprintf(stderr, "[CEF Host] CefInitialize failed!\n");

        // Try to read and report CEF debug log for diagnostics.
        fprintf(stderr, "[CEF Host] --- cef_debug.log contents ---\n");
        FILE* cefLog = fopen(logPath.c_str(), "r");
        if (cefLog) {
            char buf[512];
            while (fgets(buf, sizeof(buf), cefLog)) {
                fprintf(stderr, "  %s", buf);
            }
            fclose(cefLog);
        } else {
            fprintf(stderr, "  (no log file created at %s)\n", logPath.c_str());
        }
        fprintf(stderr, "[CEF Host] --- end cef_debug.log ---\n");

        // Print runtime environment details for Wine/CrossOver diagnostics.
        OSVERSIONINFOW osvi = {};
        osvi.dwOSVersionInfoSize = sizeof(osvi);
        typedef LONG (WINAPI *RtlGetVersionPtr)(OSVERSIONINFOW*);
        RtlGetVersionPtr rtlGetVersion = (RtlGetVersionPtr)GetProcAddress(
            GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
        if (rtlGetVersion && rtlGetVersion(&osvi) == 0) {
            fprintf(stderr, "[CEF Host] Windows version (Wine): %lu.%lu.%lu\n",
                    osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber);
        }
        typedef const char* (WINAPI *wine_get_version_ptr)(void);
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        wine_get_version_ptr wine_get_version = (wine_get_version_ptr)
            GetProcAddress(ntdll, "wine_get_version");
        if (wine_get_version) {
            fprintf(stderr, "[CEF Host] Wine version: %s\n", wine_get_version());
        }
        fflush(stderr);

        std::string errMsg = "CefInitialize failed";
        pipe.Send(PipeProtocol::MSG_HOST_ERROR, errMsg.data(),
                  static_cast<uint32_t>(errMsg.size()));
        UnmapViewOfFile(shmemView);
        CloseHandle(hMapping);
        pipe.Close();
        return 1;
    }

    fprintf(stderr, "[CEF Host] CEF initialized successfully.\n");

    // 4. Create IPC bridge
    HostIpcBridge ipcBridge(&pipe);

    // 5. Send HOST_READY to plugin
    pipe.Send(PipeProtocol::MSG_HOST_READY);
    fprintf(stderr, "[CEF Host] Sent HOST_READY.\n");

    // 6. Main loop
    CefRefPtr<HostBrowserClient> client;
    CefRefPtr<CefBrowser> browser;
    bool running = true;

    while (running) {
        // Pump CEF message loop
        CefDoMessageLoopWork();

        // Poll pipe for messages from plugin
        auto messages = pipe.Poll();
        for (const auto& msg : messages) {
            switch (msg.type) {
                case PipeProtocol::MSG_CREATE_BROWSER: {
                    if (msg.payload.size() < 8) break;
                    int32_t w, h;
                    memcpy(&w, msg.payload.data(), 4);
                    memcpy(&h, msg.payload.data() + 4, 4);
                    std::string url(msg.payload.begin() + 8, msg.payload.end());

                    fprintf(stderr, "[CEF Host] Creating browser %dx%d: %s\n", w, h, url.c_str());

                    client = new HostBrowserClient(&ipcBridge, shmemView, w, h);

                    CefWindowInfo windowInfo;
                    windowInfo.SetAsWindowless(nullptr);

                    CefBrowserSettings browserSettings = {};
                    browserSettings.size = sizeof(CefBrowserSettings);
                    browserSettings.windowless_frame_rate = 60;

                    browser = CefBrowserHost::CreateBrowserSync(
                        windowInfo, client, url, browserSettings, nullptr, nullptr);

                    if (browser) {
                        ipcBridge.SetBrowser(browser);
                        pipe.Send(PipeProtocol::MSG_BROWSER_CREATED);
                        fprintf(stderr, "[CEF Host] Browser created.\n");
                    } else {
                        std::string err = "CreateBrowserSync failed";
                        pipe.Send(PipeProtocol::MSG_HOST_ERROR, err.data(),
                                  static_cast<uint32_t>(err.size()));
                    }
                    break;
                }

                case PipeProtocol::MSG_CLOSE_BROWSER: {
                    if (browser) {
                        browser->GetHost()->CloseBrowser(true);
                        browser = nullptr;
                    }
                    client = nullptr;
                    break;
                }

                case PipeProtocol::MSG_SHUTDOWN: {
                    fprintf(stderr, "[CEF Host] Shutdown requested.\n");
                    running = false;
                    break;
                }

                case PipeProtocol::MSG_RESIZE: {
                    if (msg.payload.size() < 8 || !browser) break;
                    int32_t w, h;
                    memcpy(&w, msg.payload.data(), 4);
                    memcpy(&h, msg.payload.data() + 4, 4);
                    if (client) client->SetSize(w, h);
                    if (browser && browser->GetHost()) {
                        browser->GetHost()->WasResized();
                    }
                    break;
                }

                case PipeProtocol::MSG_NAVIGATE: {
                    if (!browser) break;
                    std::string url(msg.payload.begin(), msg.payload.end());
                    auto frame = browser->GetMainFrame();
                    if (frame) frame->LoadURL(url);
                    break;
                }

                case PipeProtocol::MSG_RELOAD: {
                    if (browser) browser->Reload();
                    break;
                }

                case PipeProtocol::MSG_MOUSE_MOVE: {
                    if (!browser || msg.payload.size() < sizeof(PipeProtocol::MouseMoveData)) break;
                    PipeProtocol::MouseMoveData data;
                    memcpy(&data, msg.payload.data(), sizeof(data));

                    CefMouseEvent event;
                    event.x = data.x;
                    event.y = data.y;
                    event.modifiers = data.modifiers;
                    browser->GetHost()->SendMouseMoveEvent(event, false);
                    break;
                }

                case PipeProtocol::MSG_MOUSE_CLICK: {
                    if (!browser || msg.payload.size() < sizeof(PipeProtocol::MouseClickData)) break;
                    PipeProtocol::MouseClickData data;
                    memcpy(&data, msg.payload.data(), sizeof(data));

                    CefMouseEvent event;
                    event.x = data.x;
                    event.y = data.y;
                    event.modifiers = data.modifiers;

                    cef_mouse_button_type_t btn = MBT_LEFT;
                    if (data.button == 1) btn = MBT_MIDDLE;
                    else if (data.button == 2) btn = MBT_RIGHT;

                    browser->GetHost()->SendMouseClickEvent(
                        event, btn, data.mouseUp != 0, data.clickCount);
                    break;
                }

                case PipeProtocol::MSG_MOUSE_WHEEL: {
                    if (!browser || msg.payload.size() < sizeof(PipeProtocol::MouseWheelData)) break;
                    PipeProtocol::MouseWheelData data;
                    memcpy(&data, msg.payload.data(), sizeof(data));

                    CefMouseEvent event;
                    event.x = data.x;
                    event.y = data.y;
                    event.modifiers = data.modifiers;
                    browser->GetHost()->SendMouseWheelEvent(event, data.deltaX, data.deltaY);
                    break;
                }

                case PipeProtocol::MSG_KEY_EVENT: {
                    if (!browser || msg.payload.size() < sizeof(PipeProtocol::KeyEventData)) break;
                    PipeProtocol::KeyEventData data;
                    memcpy(&data, msg.payload.data(), sizeof(data));

                    cef_key_event_t keyEvent = {};
                    keyEvent.type = static_cast<cef_key_event_type_t>(data.type);
                    keyEvent.modifiers = data.modifiers;
                    keyEvent.windows_key_code = data.windowsKeyCode;
                    keyEvent.native_key_code = data.nativeKeyCode;
                    keyEvent.is_system_key = data.isSystemKey;
                    keyEvent.character = data.character;
                    keyEvent.unmodified_character = data.character;
                    browser->GetHost()->SendKeyEvent(keyEvent);
                    break;
                }

                // Pipe messages from plugin to forward to renderer
                case PipeProtocol::MSG_NEXUS_API_RESPONSE:
                case PipeProtocol::MSG_NEXUS_EVENT_DISPATCH:
                case PipeProtocol::MSG_NEXUS_KEYBIND_INVOKE: {
                    ipcBridge.HandlePipeMessage(msg);
                    break;
                }

                default:
                    break;
            }
        }

        // If no messages and no CEF work, sleep briefly to avoid busy-waiting
        if (messages.empty()) {
            Sleep(1);
        }
    }

    // Cleanup
    fprintf(stderr, "[CEF Host] Shutting down...\n");

    if (browser) {
        browser->GetHost()->CloseBrowser(true);
        // Let CEF process the close
        for (int i = 0; i < 100; ++i) {
            CefDoMessageLoopWork();
            Sleep(10);
        }
        browser = nullptr;
    }
    client = nullptr;
    app = nullptr;

    CefShutdown();

    UnmapViewOfFile(shmemView);
    CloseHandle(hMapping);
    pipe.Close();

    fprintf(stderr, "[CEF Host] Exited cleanly.\n");
    return 0;
}
