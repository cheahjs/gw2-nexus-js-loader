#include "host_process.h"
#include <vector>
#include <cstring>

HostProcess::HostProcess() {
    memset(&m_processInfo, 0, sizeof(m_processInfo));
}

HostProcess::~HostProcess() {
    if (m_launched) {
        Terminate();
    }
}

bool HostProcess::Launch(const std::string& exePath,
                          const std::string& cefDir,
                          const std::string& pipeName,
                          const std::string& shmemName) {
    // Build command line with app-specific arguments and Wine-compatible
    // Chromium switches. These must be on the command line (not just in
    // OnBeforeCommandLineProcessing) because CEF reads some switches very
    // early during initialization, before the CefApp callback fires.
    std::string cmdLine = "\"" + exePath + "\""
        + " --cef-dir=\"" + cefDir + "\""
        + " --pipe-name=\"" + pipeName + "\""
        + " --shmem-name=\"" + shmemName + "\""
        + " --disable-gpu"
        + " --disable-gpu-compositing"
        + " --disable-gpu-sandbox"
        + " --no-sandbox"
        + " --allow-no-sandbox-job"
        + " --disable-breakpad"
        + " --disable-extensions"
        + " --disable-component-update"
        + " --enable-logging"
        + " --log-severity=verbose"
        + " --v=1";

    STARTUPINFOA si = {};
    si.cb = sizeof(si);

    // CreateProcessA needs a mutable command line buffer
    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back('\0');

    // Try to break away from any parent job object. Chromium/CEF startup can
    // fail in restrictive jobs even when no sandbox is used.
    DWORD creationFlags = CREATE_BREAKAWAY_FROM_JOB;
    if (!CreateProcessA(
            nullptr,           // application name (use command line)
            cmdBuf.data(),     // command line
            nullptr,           // process security attributes
            nullptr,           // thread security attributes
            FALSE,             // inherit handles
            creationFlags,     // creation flags
            nullptr,           // environment
            cefDir.c_str(),    // current directory = CEF folder
            &si,
            &m_processInfo)) {
        DWORD firstErr = GetLastError();
        if (firstErr == ERROR_ACCESS_DENIED) {
            // Parent job may not allow breakaway. Retry without the flag.
            if (!CreateProcessA(
                    nullptr,
                    cmdBuf.data(),
                    nullptr,
                    nullptr,
                    FALSE,
                    0,
                    nullptr,
                    cefDir.c_str(),
                    &si,
                    &m_processInfo)) {
                return false;
            }
        } else {
            return false;
        }
    }

    m_launched = true;
    // Close the thread handle immediately - we only need the process handle
    CloseHandle(m_processInfo.hThread);
    m_processInfo.hThread = nullptr;

    return true;
}

bool HostProcess::IsRunning() const {
    if (!m_launched || !m_processInfo.hProcess) return false;

    DWORD exitCode;
    if (GetExitCodeProcess(m_processInfo.hProcess, &exitCode)) {
        return exitCode == STILL_ACTIVE;
    }
    return false;
}

bool HostProcess::WaitForExit(DWORD timeoutMs) {
    if (!m_launched || !m_processInfo.hProcess) return true;

    DWORD result = WaitForSingleObject(m_processInfo.hProcess, timeoutMs);
    if (result == WAIT_OBJECT_0) {
        CloseHandle(m_processInfo.hProcess);
        m_processInfo.hProcess = nullptr;
        m_launched = false;
        return true;
    }
    return false;
}

void HostProcess::Terminate() {
    if (!m_launched || !m_processInfo.hProcess) return;

    TerminateProcess(m_processInfo.hProcess, 1);
    WaitForSingleObject(m_processInfo.hProcess, 1000);
    CloseHandle(m_processInfo.hProcess);
    m_processInfo.hProcess = nullptr;
    m_launched = false;
}
