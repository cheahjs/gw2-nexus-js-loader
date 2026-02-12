#include "host_ipc_bridge.h"
#include "shared/ipc_messages.h"

HostIpcBridge::HostIpcBridge(HostPipeClient* pipe)
    : m_pipe(pipe) {
}

void HostIpcBridge::SetBrowser(CefRefPtr<CefBrowser> browser) {
    m_browser = browser;
}

// Serialize CefListValue args into PipeArg vector
static std::vector<PipeProtocol::PipeArg> SerializeCefArgs(CefRefPtr<CefListValue> list) {
    std::vector<PipeProtocol::PipeArg> args;
    size_t count = list->GetSize();
    args.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        auto type = list->GetType(i);
        switch (type) {
            case VTYPE_INT:
                args.push_back(PipeProtocol::PipeArg::Int(list->GetInt(i)));
                break;
            case VTYPE_STRING:
                args.push_back(PipeProtocol::PipeArg::String(list->GetString(i).ToString()));
                break;
            case VTYPE_BOOL:
                args.push_back(PipeProtocol::PipeArg::Bool(list->GetBool(i)));
                break;
            case VTYPE_DOUBLE:
                // Convert double to string for transport
                args.push_back(PipeProtocol::PipeArg::String(
                    std::to_string(list->GetDouble(i))));
                break;
            default:
                // Unsupported type - send as empty string
                args.push_back(PipeProtocol::PipeArg::String(""));
                break;
        }
    }
    return args;
}

bool HostIpcBridge::OnRendererMessage(CefRefPtr<CefBrowser> /*browser*/,
                                        CefRefPtr<CefProcessMessage> message) {
    if (!m_pipe || !m_pipe->IsConnected()) return false;

    std::string name = message->GetName().ToString();
    auto args = SerializeCefArgs(message->GetArgumentList());
    auto payload = PipeProtocol::SerializeIpcMessage(name, args);

    return m_pipe->Send(PipeProtocol::MSG_NEXUS_API_REQUEST, payload);
}

void HostIpcBridge::HandlePipeMessage(const PipeProtocol::PipeMessage& msg) {
    if (!m_browser) return;

    auto frame = m_browser->GetMainFrame();
    if (!frame) return;

    switch (msg.type) {
        case PipeProtocol::MSG_NEXUS_API_RESPONSE:
        case PipeProtocol::MSG_NEXUS_EVENT_DISPATCH:
        case PipeProtocol::MSG_NEXUS_KEYBIND_INVOKE: {
            std::string name;
            std::vector<PipeProtocol::PipeArg> args;
            if (!PipeProtocol::DeserializeIpcMessage(
                    msg.payload.data(), msg.payload.size(), name, args)) {
                break;
            }

            auto cefMsg = CefProcessMessage::Create(name);
            auto cefArgs = cefMsg->GetArgumentList();

            for (size_t i = 0; i < args.size(); ++i) {
                switch (args[i].type) {
                    case PipeProtocol::PipeArg::TYPE_INT:
                        cefArgs->SetInt(static_cast<int>(i), args[i].intVal);
                        break;
                    case PipeProtocol::PipeArg::TYPE_STRING:
                        cefArgs->SetString(static_cast<int>(i), args[i].strVal);
                        break;
                    case PipeProtocol::PipeArg::TYPE_BOOL:
                        cefArgs->SetBool(static_cast<int>(i), args[i].boolVal);
                        break;
                }
            }

            frame->SendProcessMessage(PID_RENDERER, cefMsg);
            break;
        }
        default:
            break;
    }
}
