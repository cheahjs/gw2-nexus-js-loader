#include "ipc_client.h"
#include "shared/ipc_messages.h"

#include <mutex>
#include <unordered_map>

namespace IpcClient {

static CefRefPtr<CefBrowser> s_browser;
static std::mutex s_mutex;
static int s_nextRequestId = 1;

struct PendingRequest {
    CefRefPtr<CefV8Context> context;
    CefRefPtr<CefV8Value>   resolveFunc;
    CefRefPtr<CefV8Value>   rejectFunc;
};

static std::unordered_map<int, PendingRequest> s_pendingRequests;

void SetBrowser(CefRefPtr<CefBrowser> browser) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_browser = browser;
}

void SendMessage(const std::string& name, CefRefPtr<CefListValue> args) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_browser) return;

    auto msg = CefProcessMessage::Create(name);
    if (args) {
        // Copy args into the message
        auto msgArgs = msg->GetArgumentList();
        for (size_t i = 0; i < args->GetSize(); ++i) {
            auto type = args->GetType(i);
            switch (type) {
                case VTYPE_STRING:
                    msgArgs->SetString(i, args->GetString(i));
                    break;
                case VTYPE_INT:
                    msgArgs->SetInt(i, args->GetInt(i));
                    break;
                case VTYPE_DOUBLE:
                    msgArgs->SetDouble(i, args->GetDouble(i));
                    break;
                case VTYPE_BOOL:
                    msgArgs->SetBool(i, args->GetBool(i));
                    break;
                default:
                    break;
            }
        }
    }

    s_browser->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
}

int SendAsyncRequest(const std::string& name, CefRefPtr<CefListValue> args,
                      CefRefPtr<CefV8Context> context,
                      CefRefPtr<CefV8Value> resolveFunc,
                      CefRefPtr<CefV8Value> rejectFunc) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_browser) return -1;

    int requestId = s_nextRequestId++;

    // Store pending request for Promise resolution
    s_pendingRequests[requestId] = { context, resolveFunc, rejectFunc };

    // Build message with request ID as first argument
    auto msg = CefProcessMessage::Create(name);
    auto msgArgs = msg->GetArgumentList();
    msgArgs->SetInt(0, requestId);

    // Copy additional args starting at index 1
    if (args) {
        for (size_t i = 0; i < args->GetSize(); ++i) {
            auto type = args->GetType(i);
            switch (type) {
                case VTYPE_STRING:
                    msgArgs->SetString(i + 1, args->GetString(i));
                    break;
                case VTYPE_INT:
                    msgArgs->SetInt(i + 1, args->GetInt(i));
                    break;
                case VTYPE_DOUBLE:
                    msgArgs->SetDouble(i + 1, args->GetDouble(i));
                    break;
                case VTYPE_BOOL:
                    msgArgs->SetBool(i + 1, args->GetBool(i));
                    break;
                default:
                    break;
            }
        }
    }

    s_browser->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
    return requestId;
}

bool HandleAsyncResponse(CefRefPtr<CefProcessMessage> message) {
    auto args = message->GetArgumentList();
    int requestId = args->GetInt(0);
    bool success  = args->GetBool(1);
    std::string value = args->GetString(2).ToString();

    std::lock_guard<std::mutex> lock(s_mutex);

    auto it = s_pendingRequests.find(requestId);
    if (it == s_pendingRequests.end()) return false;

    PendingRequest& req = it->second;

    // Enter the V8 context to resolve/reject the Promise
    req.context->Enter();

    if (success && req.resolveFunc && req.resolveFunc->IsFunction()) {
        CefV8ValueList resolveArgs;
        resolveArgs.push_back(CefV8Value::CreateString(value));
        req.resolveFunc->ExecuteFunction(nullptr, resolveArgs);
    } else if (!success && req.rejectFunc && req.rejectFunc->IsFunction()) {
        CefV8ValueList rejectArgs;
        rejectArgs.push_back(CefV8Value::CreateString(value));
        req.rejectFunc->ExecuteFunction(nullptr, rejectArgs);
    }

    req.context->Exit();

    s_pendingRequests.erase(it);
    return true;
}

} // namespace IpcClient
