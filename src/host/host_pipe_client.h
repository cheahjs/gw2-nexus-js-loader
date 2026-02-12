#pragma once

#include "shared/pipe_protocol.h"
#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

// Host-side named pipe client.
// Connects to the plugin's named pipe server and provides
// thread-safe send/receive for PipeProtocol messages.
class HostPipeClient {
public:
    HostPipeClient();
    ~HostPipeClient();

    // Connect to the plugin's named pipe.
    bool Connect(const std::string& pipeName, DWORD timeoutMs = 10000);

    // Send a message (thread-safe).
    bool Send(uint32_t type, const void* payload = nullptr, uint32_t length = 0);

    // Send a message from a vector payload.
    bool Send(uint32_t type, const std::vector<uint8_t>& payload);

    // Poll for received messages. Returns all queued messages.
    std::vector<PipeProtocol::PipeMessage> Poll();

    // Check if connected.
    bool IsConnected() const;

    // Close the connection.
    void Close();

private:
    void ReaderThread();

    HANDLE m_pipe = INVALID_HANDLE_VALUE;

    std::thread m_readerThread;
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_stopping{false};

    std::mutex m_readMutex;
    std::vector<PipeProtocol::PipeMessage> m_readQueue;

    std::mutex m_writeMutex;
};
