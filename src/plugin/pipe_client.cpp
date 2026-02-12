#include "pipe_client.h"

PipeClient::PipeClient() = default;

PipeClient::~PipeClient() {
    Close();
}

bool PipeClient::Create(const std::string& pipeName) {
    m_pipe = CreateNamedPipeA(
        pipeName.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1,       // max instances
        65536,   // out buffer size
        65536,   // in buffer size
        0,       // default timeout
        nullptr  // security attributes
    );

    if (m_pipe == INVALID_HANDLE_VALUE) {
        return false;
    }

    m_connectEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    return true;
}

bool PipeClient::WaitForConnection(DWORD timeoutMs) {
    if (m_pipe == INVALID_HANDLE_VALUE) return false;

    OVERLAPPED ov = {};
    ov.hEvent = m_connectEvent;

    BOOL result = ConnectNamedPipe(m_pipe, &ov);
    if (result) {
        // Connected immediately
        m_connected = true;
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_PIPE_CONNECTED) {
            // Already connected
            m_connected = true;
        } else if (err == ERROR_IO_PENDING) {
            // Wait for connection
            DWORD waitResult = WaitForSingleObject(m_connectEvent, timeoutMs);
            if (waitResult == WAIT_OBJECT_0) {
                DWORD dummy;
                if (GetOverlappedResult(m_pipe, &ov, &dummy, FALSE)) {
                    m_connected = true;
                }
            }
            if (!m_connected) {
                CancelIo(m_pipe);
                return false;
            }
        } else {
            return false;
        }
    }

    // Start reader thread
    m_stopping = false;
    m_readerThread = std::thread(&PipeClient::ReaderThread, this);
    return true;
}

bool PipeClient::Send(uint32_t type, const void* payload, uint32_t length) {
    if (!m_connected || m_pipe == INVALID_HANDLE_VALUE) return false;

    std::lock_guard<std::mutex> lock(m_writeMutex);

    PipeProtocol::WireHeader header;
    header.type = type;
    header.length = length;

    DWORD written;
    if (!WriteFile(m_pipe, &header, sizeof(header), &written, nullptr) ||
        written != sizeof(header)) {
        return false;
    }

    if (length > 0 && payload) {
        if (!WriteFile(m_pipe, payload, length, &written, nullptr) ||
            written != length) {
            return false;
        }
    }

    return true;
}

bool PipeClient::Send(uint32_t type, const std::vector<uint8_t>& payload) {
    return Send(type, payload.data(), static_cast<uint32_t>(payload.size()));
}

std::vector<PipeProtocol::PipeMessage> PipeClient::Poll() {
    std::lock_guard<std::mutex> lock(m_readMutex);
    std::vector<PipeProtocol::PipeMessage> messages;
    messages.swap(m_readQueue);
    return messages;
}

bool PipeClient::IsConnected() const {
    return m_connected;
}

void PipeClient::Close() {
    m_stopping = true;
    m_connected = false;

    // Cancel any blocking I/O on the pipe so the reader thread can exit
    if (m_pipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(m_pipe, nullptr);
    }

    if (m_readerThread.joinable()) {
        m_readerThread.join();
    }

    if (m_connectEvent) {
        CloseHandle(m_connectEvent);
        m_connectEvent = nullptr;
    }

    if (m_pipe != INVALID_HANDLE_VALUE) {
        DisconnectNamedPipe(m_pipe);
        CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
    }
}

// Read bytes from pipe, handling partial reads
static bool ReadExact(HANDLE pipe, void* buffer, DWORD size) {
    DWORD totalRead = 0;
    auto* buf = static_cast<uint8_t*>(buffer);
    while (totalRead < size) {
        DWORD bytesRead = 0;
        if (!ReadFile(pipe, buf + totalRead, size - totalRead, &bytesRead, nullptr)) {
            return false;
        }
        if (bytesRead == 0) return false;
        totalRead += bytesRead;
    }
    return true;
}

void PipeClient::ReaderThread() {
    while (!m_stopping) {
        // Read wire header
        PipeProtocol::WireHeader header;
        if (!ReadExact(m_pipe, &header, sizeof(header))) {
            break;
        }

        // Read payload
        PipeProtocol::PipeMessage msg;
        msg.type = header.type;
        if (header.length > 0) {
            msg.payload.resize(header.length);
            if (!ReadExact(m_pipe, msg.payload.data(), header.length)) {
                break;
            }
        }

        // Queue the message
        {
            std::lock_guard<std::mutex> lock(m_readMutex);
            m_readQueue.push_back(std::move(msg));
        }
    }

    m_connected = false;
}
