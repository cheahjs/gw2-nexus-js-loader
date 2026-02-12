#include "cef_host_proxy.h"
#include "pipe_client.h"
#include "shared_frame_reader.h"
#include "host_process.h"
#include "ipc_handler.h"
#include "globals.h"
#include "shared/version.h"
#include "shared/ipc_messages.h"

#include <string>

namespace CefHostProxy {

static PipeClient*        s_pipe   = nullptr;
static SharedFrameReader* s_frame  = nullptr;
static HostProcess*       s_host   = nullptr;
static bool               s_ready  = false;

static std::string s_pipeName;
static std::string s_shmemName;

bool Initialize() {
    if (s_ready) return true;

    DWORD pid = GetCurrentProcessId();
    s_pipeName  = "\\\\.\\pipe\\nexus_js_cef_" + std::to_string(pid);
    s_shmemName = "nexus_js_frame_" + std::to_string(pid);

    // 1. Create shared memory
    s_frame = new SharedFrameReader();
    if (!s_frame->Create(s_shmemName)) {
        if (Globals::API) {
            Globals::API->Log(LOGL_CRITICAL, ADDON_NAME,
                "Failed to create shared memory for CEF frames.");
        }
        delete s_frame; s_frame = nullptr;
        return false;
    }

    // 2. Create named pipe server
    s_pipe = new PipeClient();
    if (!s_pipe->Create(s_pipeName)) {
        if (Globals::API) {
            Globals::API->Log(LOGL_CRITICAL, ADDON_NAME,
                "Failed to create named pipe for CEF host.");
        }
        s_frame->Close(); delete s_frame; s_frame = nullptr;
        delete s_pipe; s_pipe = nullptr;
        return false;
    }

    // 3. Launch host process
    std::string hostExePath = std::string(Globals::GetCefHostExePath());
    std::string cefDir = std::string(Globals::GetCefDirectory());

    if (Globals::API) {
        Globals::API->Log(LOGL_INFO, ADDON_NAME,
            (std::string("Launching CEF host: ") + hostExePath).c_str());
    }

    s_host = new HostProcess();
    if (!s_host->Launch(hostExePath, cefDir, s_pipeName, s_shmemName)) {
        DWORD err = GetLastError();
        if (Globals::API) {
            Globals::API->Log(LOGL_CRITICAL, ADDON_NAME,
                (std::string("Failed to launch CEF host (error ")
                 + std::to_string(err) + ")").c_str());
        }
        s_pipe->Close(); delete s_pipe; s_pipe = nullptr;
        s_frame->Close(); delete s_frame; s_frame = nullptr;
        delete s_host; s_host = nullptr;
        return false;
    }

    // 4. Wait for host to connect to pipe
    if (Globals::API) {
        Globals::API->Log(LOGL_INFO, ADDON_NAME, "Waiting for CEF host connection...");
    }

    if (!s_pipe->WaitForConnection(10000)) {
        if (Globals::API) {
            Globals::API->Log(LOGL_CRITICAL, ADDON_NAME,
                "CEF host did not connect within timeout.");
        }
        s_host->Terminate();
        delete s_host; s_host = nullptr;
        s_pipe->Close(); delete s_pipe; s_pipe = nullptr;
        s_frame->Close(); delete s_frame; s_frame = nullptr;
        return false;
    }

    // 5. Wait for HOST_READY message
    if (Globals::API) {
        Globals::API->Log(LOGL_INFO, ADDON_NAME, "Waiting for CEF host to initialize...");
    }

    bool gotReady = false;
    DWORD startTime = GetTickCount();
    while (GetTickCount() - startTime < 15000) {
        auto messages = s_pipe->Poll();
        for (const auto& msg : messages) {
            if (msg.type == PipeProtocol::MSG_HOST_READY) {
                gotReady = true;
                break;
            }
            if (msg.type == PipeProtocol::MSG_HOST_ERROR) {
                std::string err(msg.payload.begin(), msg.payload.end());
                if (Globals::API) {
                    Globals::API->Log(LOGL_CRITICAL, ADDON_NAME,
                        (std::string("CEF host error: ") + err).c_str());
                }
                break;
            }
        }
        if (gotReady) break;

        if (!s_host->IsRunning()) {
            if (Globals::API) {
                Globals::API->Log(LOGL_CRITICAL, ADDON_NAME,
                    "CEF host process exited unexpectedly during initialization.");
            }
            break;
        }

        Sleep(10);
    }

    if (!gotReady) {
        if (Globals::API) {
            Globals::API->Log(LOGL_CRITICAL, ADDON_NAME,
                "CEF host did not become ready within timeout.");
        }
        Shutdown();
        return false;
    }

    s_ready = true;
    if (Globals::API) {
        Globals::API->Log(LOGL_INFO, ADDON_NAME, "CEF host ready.");
    }
    return true;
}

void Shutdown() {
    s_ready = false;

    // 1. Send SHUTDOWN
    if (s_pipe && s_pipe->IsConnected()) {
        s_pipe->Send(PipeProtocol::MSG_SHUTDOWN);
    }

    // 2. Wait for host exit
    if (s_host) {
        if (!s_host->WaitForExit(5000)) {
            if (Globals::API) {
                Globals::API->Log(LOGL_WARNING, ADDON_NAME,
                    "CEF host did not exit gracefully, terminating.");
            }
            s_host->Terminate();
        }
        delete s_host;
        s_host = nullptr;
    }

    // 3. Close pipe
    if (s_pipe) {
        s_pipe->Close();
        delete s_pipe;
        s_pipe = nullptr;
    }

    // 4. Close shared memory
    if (s_frame) {
        s_frame->Close();
        delete s_frame;
        s_frame = nullptr;
    }
}

void Tick() {
    if (!s_ready || !s_pipe) return;

    // Check host health
    if (s_host && !s_host->IsRunning()) {
        if (Globals::API) {
            Globals::API->Log(LOGL_WARNING, ADDON_NAME,
                "CEF host process has exited unexpectedly.");
        }
        s_ready = false;
        return;
    }

    // Poll pipe messages
    auto messages = s_pipe->Poll();
    for (const auto& msg : messages) {
        switch (msg.type) {
            case PipeProtocol::MSG_NEXUS_API_REQUEST: {
                std::string messageName;
                std::vector<PipeProtocol::PipeArg> args;
                if (PipeProtocol::DeserializeIpcMessage(
                        msg.payload.data(), msg.payload.size(),
                        messageName, args)) {
                    IpcHandler::HandleApiRequest(messageName, args);
                }
                break;
            }
            case PipeProtocol::MSG_HOST_ERROR: {
                std::string err(msg.payload.begin(), msg.payload.end());
                if (Globals::API) {
                    Globals::API->Log(LOGL_WARNING, ADDON_NAME,
                        (std::string("CEF host error: ") + err).c_str());
                }
                break;
            }
            case PipeProtocol::MSG_BROWSER_CREATED: {
                if (Globals::API) {
                    Globals::API->Log(LOGL_INFO, ADDON_NAME, "Browser created in host.");
                }
                break;
            }
            default:
                break;
        }
    }

    // Poll shared memory for new frames
    if (s_frame) {
        s_frame->Poll();
    }

    // Flush pending events/keybinds to host
    IpcHandler::FlushPendingEvents();
}

bool CreateBrowser(const std::string& url, int width, int height) {
    if (!s_ready || !s_pipe) return false;

    // Payload: [int32 width][int32 height][url bytes]
    std::vector<uint8_t> payload;
    int32_t w = width, h = height;
    payload.insert(payload.end(), reinterpret_cast<uint8_t*>(&w),
                   reinterpret_cast<uint8_t*>(&w) + 4);
    payload.insert(payload.end(), reinterpret_cast<uint8_t*>(&h),
                   reinterpret_cast<uint8_t*>(&h) + 4);
    payload.insert(payload.end(), url.begin(), url.end());

    return s_pipe->Send(PipeProtocol::MSG_CREATE_BROWSER, payload);
}

void CloseBrowser() {
    if (s_pipe && s_pipe->IsConnected()) {
        s_pipe->Send(PipeProtocol::MSG_CLOSE_BROWSER);
    }
}

void ResizeBrowser(int width, int height) {
    if (!s_pipe || !s_pipe->IsConnected()) return;

    int32_t data[2] = { width, height };
    s_pipe->Send(PipeProtocol::MSG_RESIZE, data, sizeof(data));
}

void Navigate(const std::string& url) {
    if (!s_pipe || !s_pipe->IsConnected()) return;
    s_pipe->Send(PipeProtocol::MSG_NAVIGATE, url.data(),
                 static_cast<uint32_t>(url.size()));
}

void Reload() {
    if (s_pipe && s_pipe->IsConnected()) {
        s_pipe->Send(PipeProtocol::MSG_RELOAD);
    }
}

void SendMouseMove(int x, int y, uint32_t modifiers) {
    if (!s_pipe || !s_pipe->IsConnected()) return;
    PipeProtocol::MouseMoveData data = {x, y, modifiers};
    s_pipe->Send(PipeProtocol::MSG_MOUSE_MOVE, &data, sizeof(data));
}

void SendMouseClick(int x, int y, uint32_t modifiers, uint32_t button,
                    bool mouseUp, int clickCount) {
    if (!s_pipe || !s_pipe->IsConnected()) return;
    PipeProtocol::MouseClickData data = {
        x, y, modifiers, button,
        static_cast<uint8_t>(mouseUp ? 1 : 0), clickCount
    };
    s_pipe->Send(PipeProtocol::MSG_MOUSE_CLICK, &data, sizeof(data));
}

void SendMouseWheel(int x, int y, uint32_t modifiers, int deltaX, int deltaY) {
    if (!s_pipe || !s_pipe->IsConnected()) return;
    PipeProtocol::MouseWheelData data = {x, y, modifiers, deltaX, deltaY};
    s_pipe->Send(PipeProtocol::MSG_MOUSE_WHEEL, &data, sizeof(data));
}

void SendKeyEvent(uint32_t type, uint32_t modifiers, int windowsKeyCode,
                  int nativeKeyCode, bool isSystemKey, uint16_t character) {
    if (!s_pipe || !s_pipe->IsConnected()) return;
    PipeProtocol::KeyEventData data = {
        type, modifiers, windowsKeyCode, nativeKeyCode,
        static_cast<uint8_t>(isSystemKey ? 1 : 0), character
    };
    s_pipe->Send(PipeProtocol::MSG_KEY_EVENT, &data, sizeof(data));
}

void SendApiResponse(int requestId, bool success, const std::string& value) {
    if (!s_pipe || !s_pipe->IsConnected()) return;

    std::vector<PipeProtocol::PipeArg> args;
    args.push_back(PipeProtocol::PipeArg::Int(requestId));
    args.push_back(PipeProtocol::PipeArg::Bool(success));
    args.push_back(PipeProtocol::PipeArg::String(value));

    auto payload = PipeProtocol::SerializeIpcMessage(IPC::ASYNC_RESPONSE, args);
    s_pipe->Send(PipeProtocol::MSG_NEXUS_API_RESPONSE, payload);
}

void SendEventDispatch(const std::string& eventName, const std::string& jsonData) {
    if (!s_pipe || !s_pipe->IsConnected()) return;

    std::vector<PipeProtocol::PipeArg> args;
    args.push_back(PipeProtocol::PipeArg::String(eventName));
    args.push_back(PipeProtocol::PipeArg::String(jsonData));

    auto payload = PipeProtocol::SerializeIpcMessage(IPC::EVENTS_DISPATCH, args);
    s_pipe->Send(PipeProtocol::MSG_NEXUS_EVENT_DISPATCH, payload);
}

void SendKeybindInvoke(const std::string& identifier, bool isRelease) {
    if (!s_pipe || !s_pipe->IsConnected()) return;

    std::vector<PipeProtocol::PipeArg> args;
    args.push_back(PipeProtocol::PipeArg::String(identifier));
    args.push_back(PipeProtocol::PipeArg::Bool(isRelease));

    auto payload = PipeProtocol::SerializeIpcMessage(IPC::KEYBINDS_INVOKE, args);
    s_pipe->Send(PipeProtocol::MSG_NEXUS_KEYBIND_INVOKE, payload);
}

void* GetTextureHandle() {
    return s_frame ? s_frame->GetTextureHandle() : nullptr;
}

int GetWidth() {
    return s_frame ? s_frame->GetWidth() : 0;
}

int GetHeight() {
    return s_frame ? s_frame->GetHeight() : 0;
}

bool IsReady() {
    return s_ready;
}

} // namespace CefHostProxy
