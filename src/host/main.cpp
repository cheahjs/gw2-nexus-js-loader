#include <windows.h>
#include <cstdio>
#include <string>
#include <cstring>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_command_line.h"

#include "host_browser_app.h"
#include "host_browser_client.h"
#include "host_ipc_bridge.h"
#include "host_pipe_client.h"
#include "shared/pipe_protocol.h"

// Parse a command-line argument of the form --key="value" or --key=value
static std::string GetArg(const char* key, int argc, char* argv[]) {
    std::string prefix = std::string("--") + key + "=";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind(prefix, 0) == 0) {
            std::string val = arg.substr(prefix.size());
            // Strip surrounding quotes if present
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
                val = val.substr(1, val.size() - 2);
            }
            return val;
        }
    }
    return "";
}

int main(int argc, char* argv[]) {
    // Parse required arguments
    std::string cefDir   = GetArg("cef-dir", argc, argv);
    std::string pipeName = GetArg("pipe-name", argc, argv);
    std::string shmemName = GetArg("shmem-name", argc, argv);

    if (cefDir.empty() || pipeName.empty() || shmemName.empty()) {
        fprintf(stderr, "[CEF Host] Missing required arguments.\n");
        fprintf(stderr, "Usage: nexus_js_cef_host.exe "
                "--cef-dir=<path> --pipe-name=<name> --shmem-name=<name>\n");
        return 1;
    }

    fprintf(stderr, "[CEF Host] Starting with:\n");
    fprintf(stderr, "  cef-dir:    %s\n", cefDir.c_str());
    fprintf(stderr, "  pipe-name:  %s\n", pipeName.c_str());
    fprintf(stderr, "  shmem-name: %s\n", shmemName.c_str());

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
    CefMainArgs mainArgs(GetModuleHandle(nullptr));
    CefRefPtr<HostBrowserApp> app = new HostBrowserApp();

    CefSettings settings = {};
    settings.size = sizeof(CefSettings);
    settings.no_sandbox = true;
    settings.multi_threaded_message_loop = false;
    settings.windowless_rendering_enabled = true;

    std::string subprocessPath = cefDir + "\\nexus_js_subprocess.exe";
    CefString(&settings.browser_subprocess_path).FromString(subprocessPath);

    CefString(&settings.resources_dir_path).FromString(cefDir);

    std::string localesDir = cefDir + "\\locales";
    CefString(&settings.locales_dir_path).FromString(localesDir);

    std::string cachePath = cefDir + "\\cef_cache";
    CefString(&settings.cache_path).FromString(cachePath);

    std::string logPath = cefDir + "\\cef_debug.log";
    CefString(&settings.log_file).FromString(logPath);
    settings.log_severity = LOGSEVERITY_INFO;

    // Ensure libcef.dll's dependencies can be found
    SetDllDirectoryA(cefDir.c_str());

    if (!CefInitialize(mainArgs, settings, app, nullptr)) {
        fprintf(stderr, "[CEF Host] CefInitialize failed!\n");
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
