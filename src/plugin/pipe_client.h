#pragma once

#include "shared/pipe_protocol.h"
#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

// Plugin-side named pipe server.
// Creates a named pipe, accepts the host connection, and provides
// thread-safe send/receive for PipeProtocol messages.
class PipeClient {
public:
    PipeClient();
    ~PipeClient();

    // Create the named pipe server and start listening.
    bool Create(const std::string& pipeName);

    // Wait for the host to connect (blocking, with timeout in ms).
    bool WaitForConnection(DWORD timeoutMs = 10000);

    // Send a message (thread-safe).
    bool Send(uint32_t type, const void* payload = nullptr, uint32_t length = 0);

    // Send a message from a vector payload (convenience).
    bool Send(uint32_t type, const std::vector<uint8_t>& payload);

    // Poll for received messages. Returns all queued messages and clears the queue.
    std::vector<PipeProtocol::PipeMessage> Poll();

    // Check if connected to the host.
    bool IsConnected() const;

    // Close the pipe and stop the reader thread.
    void Close();

private:
    void ReaderThread();

    HANDLE m_pipe = INVALID_HANDLE_VALUE;
    HANDLE m_connectEvent = nullptr;

    std::thread m_readerThread;
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_stopping{false};

    std::mutex m_readMutex;
    std::vector<PipeProtocol::PipeMessage> m_readQueue;

    std::mutex m_writeMutex;
};
