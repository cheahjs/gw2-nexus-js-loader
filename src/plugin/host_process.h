#pragma once

#include <windows.h>
#include <string>

// Manages the lifecycle of the CEF host process (nexus_js_cef_host.exe).
// Launches the process, monitors its health, and terminates on shutdown.
class HostProcess {
public:
    HostProcess();
    ~HostProcess();

    // Launch the host process with the given arguments.
    bool Launch(const std::string& exePath,
                const std::string& cefDir,
                const std::string& pipeName,
                const std::string& shmemName);

    // Check if the host process is still running.
    bool IsRunning() const;

    // Graceful shutdown: wait for process to exit (up to timeoutMs).
    // Returns true if process exited within timeout.
    bool WaitForExit(DWORD timeoutMs = 5000);

    // Force terminate the process.
    void Terminate();

    // Get the process handle (for external monitoring).
    HANDLE GetProcessHandle() const { return m_processInfo.hProcess; }

private:
    PROCESS_INFORMATION m_processInfo = {};
    bool m_launched = false;
};
