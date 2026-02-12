#include "host_pipe_client.h"

HostPipeClient::HostPipeClient() = default;

HostPipeClient::~HostPipeClient() {
    Close();
}

bool HostPipeClient::Connect(const std::string& pipeName, DWORD timeoutMs) {
    DWORD startTime = GetTickCount();

    while (GetTickCount() - startTime < timeoutMs) {
        m_pipe = CreateFileA(
            pipeName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );

        if (m_pipe != INVALID_HANDLE_VALUE) {
            break;
        }

        DWORD err = GetLastError();
        if (err != ERROR_PIPE_BUSY) {
            return false;
        }

        // Wait for the pipe to become available
        if (!WaitNamedPipeA(pipeName.c_str(), 2000)) {
            // Timeout or error, retry
        }
    }

    if (m_pipe == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Set pipe to message mode (we handle framing ourselves, but byte mode is fine)
    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(m_pipe, &mode, nullptr, nullptr);

    m_connected = true;
    m_stopping = false;
    m_readerThread = std::thread(&HostPipeClient::ReaderThread, this);

    return true;
}

bool HostPipeClient::Send(uint32_t type, const void* payload, uint32_t length) {
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

bool HostPipeClient::Send(uint32_t type, const std::vector<uint8_t>& payload) {
    return Send(type, payload.data(), static_cast<uint32_t>(payload.size()));
}

std::vector<PipeProtocol::PipeMessage> HostPipeClient::Poll() {
    std::lock_guard<std::mutex> lock(m_readMutex);
    std::vector<PipeProtocol::PipeMessage> messages;
    messages.swap(m_readQueue);
    return messages;
}

bool HostPipeClient::IsConnected() const {
    return m_connected;
}

void HostPipeClient::Close() {
    m_stopping = true;
    m_connected = false;

    if (m_pipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(m_pipe, nullptr);
    }

    if (m_readerThread.joinable()) {
        m_readerThread.join();
    }

    if (m_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
    }
}

// Read exact number of bytes from pipe, handling partial reads
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

void HostPipeClient::ReaderThread() {
    while (!m_stopping) {
        PipeProtocol::WireHeader header;
        if (!ReadExact(m_pipe, &header, sizeof(header))) {
            break;
        }

        PipeProtocol::PipeMessage msg;
        msg.type = header.type;
        if (header.length > 0) {
            msg.payload.resize(header.length);
            if (!ReadExact(m_pipe, msg.payload.data(), header.length)) {
                break;
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_readMutex);
            m_readQueue.push_back(std::move(msg));
        }
    }

    m_connected = false;
}
