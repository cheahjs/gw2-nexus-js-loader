#include <windows.h>
#include <cstdio>
#include <string>
#include <cstring>
#include <winnt.h>
#include <objbase.h>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "include/cef_version.h"
#include "include/cef_api_hash.h"

// Vectored exception handler to log ALL exceptions (even internally caught ones)
static int g_exceptionCount = 0;
static LONG WINAPI VectoredExHandler(EXCEPTION_POINTERS* ep) {
    g_exceptionCount++;
    // Only log the first 20 to avoid flooding
    if (g_exceptionCount <= 20) {
        fprintf(stderr, "[CEF Host] VEH exception #%d: code=0x%08lX addr=%p\n",
                g_exceptionCount,
                ep->ExceptionRecord->ExceptionCode,
                ep->ExceptionRecord->ExceptionAddress);
        fflush(stderr);
    }
    return EXCEPTION_CONTINUE_SEARCH; // let normal handlers process it
}

#include "host_browser_app.h"
#include "host_browser_client.h"
#include "host_ipc_bridge.h"
#include "host_pipe_client.h"
#include "shared/pipe_protocol.h"

// Parse a command-line argument of the form --key="value" or --key=value
// from the full command line string.
static std::string GetArg(const char* key, const std::string& cmdLine) {
    std::string prefix = std::string("--") + key + "=";
    size_t pos = cmdLine.find(prefix);
    if (pos == std::string::npos) return "";

    size_t valStart = pos + prefix.size();
    if (valStart >= cmdLine.size()) return "";

    std::string val;
    if (cmdLine[valStart] == '"') {
        // Quoted value
        size_t endQuote = cmdLine.find('"', valStart + 1);
        if (endQuote != std::string::npos) {
            val = cmdLine.substr(valStart + 1, endQuote - valStart - 1);
        }
    } else {
        // Unquoted value — ends at space or end of string
        size_t end = cmdLine.find(' ', valStart);
        val = cmdLine.substr(valStart, end - valStart);
    }
    return val;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    std::string fullCmdLine = GetCommandLineA();
    bool diagInitOnly = (fullCmdLine.find("--diag-init-only") != std::string::npos);

    auto ResolveModuleDir = []() -> std::string {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string fullPath(path);
        size_t pos = fullPath.find_last_of("\\/");
        if (pos == std::string::npos) {
            return ".";
        }
        return fullPath.substr(0, pos);
    };

    // Parse cef-dir first so we can redirect stderr there.
    std::string cefDir = GetArg("cef-dir", fullCmdLine);

    // Redirect stderr for diagnostics. Use append mode so multiple process
    // instances (including possible CEF child process launches) don't clobber
    // previous output.
    std::string stderrBaseDir = cefDir.empty() ? ResolveModuleDir() : cefDir;
    std::string stderrPath = stderrBaseDir + "\\cef_host_stderr.log";
    freopen(stderrPath.c_str(), "a", stderr);

    fprintf(stderr, "\n[CEF Host] ==================================================\n");
    fprintf(stderr, "[CEF Host] PID: %lu\n", GetCurrentProcessId());
    fprintf(stderr, "[CEF Host] Command line: %s\n", fullCmdLine.c_str());
    fflush(stderr);

    // Diagnose job-object restrictions inherited from the parent process.
    BOOL inJob = FALSE;
    if (IsProcessInJob(GetCurrentProcess(), nullptr, &inJob)) {
        fprintf(stderr, "[CEF Host] In job object: %s\n", inJob ? "YES" : "NO");
        if (inJob) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
            if (QueryInformationJobObject(
                    nullptr,
                    JobObjectExtendedLimitInformation,
                    &info,
                    sizeof(info),
                    nullptr)) {
                DWORD flags = info.BasicLimitInformation.LimitFlags;
                fprintf(stderr,
                        "[CEF Host] Job limit flags: 0x%08lX (BREAKAWAY_OK=%d, SILENT_BREAKAWAY=%d)\n",
                        flags,
                        (flags & JOB_OBJECT_LIMIT_BREAKAWAY_OK) ? 1 : 0,
                        (flags & JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK) ? 1 : 0);
            } else {
                fprintf(stderr, "[CEF Host] QueryInformationJobObject failed: %lu\n", GetLastError());
            }
        }
    } else {
        fprintf(stderr, "[CEF Host] IsProcessInJob failed: %lu\n", GetLastError());
    }
    fflush(stderr);

    // If this executable is launched by Chromium as a child process
    // (--type=renderer/gpu/utility/...), forward immediately to CEF.
    // This guards against startup failure if browser_subprocess_path is ignored
    // or unavailable for any reason.
    if (fullCmdLine.find("--type=") != std::string::npos) {
        fprintf(stderr, "[CEF Host] Detected CEF child-process invocation.\n");
        CefMainArgs childArgs(hInstance);
        int childExit = CefExecuteProcess(childArgs, nullptr, nullptr);
        fprintf(stderr, "[CEF Host] Child CefExecuteProcess returned: %d\n", childExit);
        fflush(stderr);
        return childExit;
    }

    // Parse remaining arguments
    std::string pipeName  = GetArg("pipe-name", fullCmdLine);
    std::string shmemName = GetArg("shmem-name", fullCmdLine);

    if (diagInitOnly && cefDir.empty()) {
        cefDir = ResolveModuleDir();
    }

    if (cefDir.empty() || (!diagInitOnly && (pipeName.empty() || shmemName.empty()))) {
        fprintf(stderr, "[CEF Host] Missing required arguments.\n");
        fprintf(stderr, "Usage: nexus_js_cef_host.exe "
                "--cef-dir=<path> --pipe-name=<name> --shmem-name=<name> [--diag-init-only]\n");
        fprintf(stderr, "Command line was: %s\n", fullCmdLine.c_str());
        return 1;
    }

    fprintf(stderr, "[CEF Host] Starting with:\n");
    fprintf(stderr, "  cef-dir:    %s\n", cefDir.c_str());
    if (!diagInitOnly) {
        fprintf(stderr, "  pipe-name:  %s\n", pipeName.c_str());
        fprintf(stderr, "  shmem-name: %s\n", shmemName.c_str());
    } else {
        fprintf(stderr, "  diag-init-only: true\n");
    }

    auto LogPathStatus = [](const char* label, const std::string& path, bool expectDir) {
        DWORD attrs = GetFileAttributesA(path.c_str());
        bool exists = (attrs != INVALID_FILE_ATTRIBUTES);
        bool isDir = exists && ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
        bool okType = exists && ((expectDir && isDir) || (!expectDir && !isDir));
        fprintf(stderr, "[CEF Host] %s: %s (%s)\n",
                label,
                okType ? "FOUND" : "MISSING",
                path.c_str());
    };
    auto LogOptionalPathStatus = [](const char* label, const std::string& path, bool expectDir) {
        DWORD attrs = GetFileAttributesA(path.c_str());
        bool exists = (attrs != INVALID_FILE_ATTRIBUTES);
        bool isDir = exists && ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
        bool okType = exists && ((expectDir && isDir) || (!expectDir && !isDir));
        fprintf(stderr, "[CEF Host] %s: %s (%s)\n",
                label,
                okType ? "FOUND" : "OPTIONAL_MISSING",
                path.c_str());
    };
    auto LogDirWritable = [](const char* label, const std::string& dirPath) {
        std::string probe = dirPath + "\\nexus_js_write_probe.tmp";
        FILE* f = fopen(probe.c_str(), "wb");
        if (f) {
            fputs("ok", f);
            fclose(f);
            DeleteFileA(probe.c_str());
            fprintf(stderr, "[CEF Host] %s: WRITABLE (%s)\n", label, dirPath.c_str());
        } else {
            fprintf(stderr, "[CEF Host] %s: NOT_WRITABLE (%s) (error %lu)\n",
                    label, dirPath.c_str(), GetLastError());
        }
    };

    // Required CEF runtime files. Missing any of these can cause early
    // CefInitialize failure with little/no logging.
    LogPathStatus("libcef.dll", cefDir + "\\libcef.dll", false);
    LogPathStatus("chrome_elf.dll", cefDir + "\\chrome_elf.dll", false);
    LogPathStatus("icudtl.dat", cefDir + "\\icudtl.dat", false);
    LogPathStatus("snapshot_blob.bin", cefDir + "\\snapshot_blob.bin", false);
    LogPathStatus("v8_context_snapshot.bin", cefDir + "\\v8_context_snapshot.bin", false);
    LogPathStatus("chrome_100_percent.pak", cefDir + "\\chrome_100_percent.pak", false);
    LogPathStatus("chrome_200_percent.pak", cefDir + "\\chrome_200_percent.pak", false);
    LogPathStatus("resources.pak", cefDir + "\\resources.pak", false);
    LogPathStatus("locales dir", cefDir + "\\locales", true);
    LogPathStatus("en-US locale", cefDir + "\\locales\\en-US.pak", false);
    LogPathStatus("subprocess", cefDir + "\\nexus_js_subprocess.exe", false);
    // Newer CEF builds may optionally use bootstrap binaries on Windows.
    LogOptionalPathStatus("bootstrap.exe", cefDir + "\\bootstrap.exe", false);
    LogOptionalPathStatus("bootstrapc.exe", cefDir + "\\bootstrapc.exe", false);
    // GPU/ANGLE/SwiftShader binaries are optional in CEF, but often required
    // on Wine/CrossOver configurations.
    LogOptionalPathStatus("d3dcompiler_47.dll", cefDir + "\\d3dcompiler_47.dll", false);
    LogOptionalPathStatus("libEGL.dll", cefDir + "\\libEGL.dll", false);
    LogOptionalPathStatus("libGLESv2.dll", cefDir + "\\libGLESv2.dll", false);
    LogOptionalPathStatus("vk_swiftshader.dll", cefDir + "\\vk_swiftshader.dll", false);
    LogOptionalPathStatus("vulkan-1.dll", cefDir + "\\vulkan-1.dll", false);
    LogOptionalPathStatus("vk_swiftshader_icd.json", cefDir + "\\vk_swiftshader_icd.json", false);
    fflush(stderr);

    // 1. Shared memory is mapped after CEF initialization. Under Wine this can
    // avoid early address-space fragmentation before Chromium startup.
    HostPipeClient pipe;
    HANDLE hMapping = nullptr;
    void* shmemView = nullptr;

    // 2. Initialize CEF
    CefMainArgs mainArgs(hInstance);

    // Ensure libcef.dll's dependencies can be found
    SetDllDirectoryA(cefDir.c_str());

    // Force-load libcef.dll from the requested --cef-dir before the first CEF
    // API call. This requires /DELAYLOAD:libcef.dll at link time.
    std::string forcedCefPath = cefDir + "\\libcef.dll";
    HMODULE forcedCef = LoadLibraryA(forcedCefPath.c_str());
    if (!forcedCef) {
        fprintf(stderr, "[CEF Host] Failed to LoadLibraryA('%s') (error %lu)\n",
                forcedCefPath.c_str(), GetLastError());
        if (SUCCEEDED(coInitHr)) CoUninitialize();
        return 1;
    }
    {
        char loadedPath[MAX_PATH] = {};
        GetModuleFileNameA(forcedCef, loadedPath, MAX_PATH);
        fprintf(stderr, "[CEF Host] Forced libcef load path: %s\n", loadedPath);
        fflush(stderr);
    }

    std::string subprocessPath = cefDir + "\\nexus_js_subprocess.exe";
    CefRefPtr<HostBrowserApp> app = new HostBrowserApp();

    // CefExecuteProcess must be called before CefInitialize. In the browser
    // process this should return -1.
    fprintf(stderr, "[CEF Host] Calling CefExecuteProcess...\n");
    fflush(stderr);
    int exitCode = CefExecuteProcess(mainArgs, app, nullptr);
    fprintf(stderr, "[CEF Host] CefExecuteProcess returned: %d\n", exitCode);
    if (exitCode >= 0) {
        pipe.Close();
        return exitCode;
    }

    // --- Full initialization ---

    // Verify API hash match between wrapper and libcef.dll
    // CEF 103: entry 0=platform, 1=universal, 2=commit
    const char* runtime_hash = cef_api_hash(0);
    fprintf(stderr, "[CEF Host] API hash (platform): runtime=%s header=%s => %s\n",
            runtime_hash ? runtime_hash : "(null)",
            CEF_API_HASH_PLATFORM,
            (runtime_hash && strcmp(runtime_hash, CEF_API_HASH_PLATFORM) == 0) ? "MATCH" : "MISMATCH");
    fprintf(stderr, "[CEF Host] CEF version: %s (Chromium %d)\n",
            CEF_VERSION, CHROME_VERSION_MAJOR);

    // Report which libcef.dll was actually loaded
    HMODULE hCef = GetModuleHandleA("libcef.dll");
    if (hCef) {
        char cefPath[MAX_PATH] = {};
        GetModuleFileNameA(hCef, cefPath, MAX_PATH);
        fprintf(stderr, "[CEF Host] Loaded libcef.dll: %s\n", cefPath);
    } else {
        fprintf(stderr, "[CEF Host] WARNING: libcef.dll not loaded!\n");
    }

    // Clear potentially problematic inherited Chromium-related environment
    // variables from the parent process.
    auto LogAndClearEnv = [](const char* name) {
        char buf[512] = {};
        DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
        if (n > 0 && n < sizeof(buf)) {
            fprintf(stderr, "[CEF Host] Env %s was set to '%s' (clearing)\n", name, buf);
            SetEnvironmentVariableA(name, nullptr);
        } else if (n >= sizeof(buf)) {
            fprintf(stderr, "[CEF Host] Env %s was set (clearing)\n", name);
            SetEnvironmentVariableA(name, nullptr);
        }
    };
    LogAndClearEnv("CHROME_CRASHPAD_PIPE_NAME");
    LogAndClearEnv("CHROME_HEADLESS");
    LogAndClearEnv("CHROME_WRAPPER");
    LogAndClearEnv("CHROME_LOG_FILE");
    LogAndClearEnv("CEF_FORCE_SANDBOX");
    LogAndClearEnv("QTWEBENGINE_CHROMIUM_FLAGS");
    LogAndClearEnv("ELECTRON_RUN_AS_NODE");
    LogAndClearEnv("VK_ICD_FILENAMES");
    LogAndClearEnv("VK_LAYER_PATH");

    // Initialize COM on the main thread before CEF startup.
    HRESULT coInitHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    fprintf(stderr, "[CEF Host] CoInitializeEx(COINIT_APARTMENTTHREADED) => 0x%08lX\n",
            static_cast<unsigned long>(coInitHr));

    // Recommended on Windows before CEF initialization.
    CefEnableHighDPISupport();

    // Set up temp directory for cache/logs
    char tempPath[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tempPath);
    std::string tempDir = std::string(tempPath) + "nexus_js_cef_"
                        + std::to_string(GetCurrentProcessId())
                        + "_" + std::to_string((unsigned long long)GetTickCount64());
    CreateDirectoryA(tempDir.c_str(), nullptr);
    LogDirWritable("temp_dir", tempDir);

    std::string logPath = tempDir + "\\cef_debug.log";

    // Chromium also honors CHROME_LOG_FILE in many startup paths.
    SetEnvironmentVariableA("CHROME_LOG_FILE", logPath.c_str());

    // Register vectored exception handler to capture ALL exceptions during init
    PVOID veh = AddVectoredExceptionHandler(1, VectoredExHandler);
    g_exceptionCount = 0;

    // Ensure Windows message queue exists on main thread before CEF init.
    // Under Wine, the message queue may not be created until the first
    // message-related API call. CEF/Chromium may require it.
    {
        MSG msg;
        PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE);
        fprintf(stderr, "[CEF Host] Message queue initialized.\n");
    }

    std::string localesDir = cefDir + "\\locales";
    std::string rootCachePath = tempDir;
    std::string cachePath = rootCachePath + "\\cache";
    CreateDirectoryA(cachePath.c_str(), nullptr);
    LogDirWritable("cache_dir", cachePath);

    // Try multiple CefInitialize configurations to isolate failure
    struct InitAttempt {
        const char* name;
        bool windowless;
        bool multiThreadedML;
        bool cmdArgsDisabled;
        bool setCustomPaths;
        bool setCacheAndLogPaths;
    };
    InitAttempt attempts[] = {
        {"bare_minimal",                  false, false, true,  false, false},
        {"minimal+cache_log_paths",       false, false, true,  false, true},
        {"minimal+cmdline_enabled",       false, false, false, false, true},
        {"windowless+cmdline_disabled",   true,  false, true,  true,  true},
        {"multi_threaded_ml",             false, true,  true,  true,  true},
        {"full",                          true,  false, false, true,  true},
    };

    bool initSuccess = false;
    int attemptIdx = 0;
    for (auto& a : attempts) {
        attemptIdx++;
        g_exceptionCount = 0;
        fprintf(stderr, "[CEF Host] Attempt %d (%s)...\n", attemptIdx, a.name);
        fflush(stderr);

        CefSettings s = {};
        s.size = sizeof(CefSettings);
        s.no_sandbox = true;
        s.windowless_rendering_enabled = a.windowless;
        s.multi_threaded_message_loop = a.multiThreadedML;
        s.command_line_args_disabled = a.cmdArgsDisabled;
        CefString(&s.locale).FromString("en-US");

        if (a.setCacheAndLogPaths) {
            CefString(&s.root_cache_path).FromString(rootCachePath);
            CefString(&s.cache_path).FromString(cachePath);
            CefString(&s.log_file).FromString(logPath);
            s.log_severity = LOGSEVERITY_VERBOSE;
        }

        if (a.setCustomPaths) {
            CefString(&s.browser_subprocess_path).FromString(subprocessPath);
            CefString(&s.resources_dir_path).FromString(cefDir);
            CefString(&s.locales_dir_path).FromString(localesDir);
        }

        SetLastError(0);
        bool result = CefInitialize(mainArgs, s, nullptr, nullptr);
        DWORD err = GetLastError();
        fprintf(stderr, "[CEF Host] Attempt %d result: %s (GetLastError=%lu, exceptions=%d)\n",
                attemptIdx, result ? "SUCCESS" : "FAILED", err, g_exceptionCount);
        fflush(stderr);

        if (result) {
            CefShutdown();
            fprintf(stderr, "[CEF Host] Attempt %d succeeded! Doing real init with app...\n", attemptIdx);
            fflush(stderr);
            // Re-initialize with proper settings
            CefMainArgs mainArgs2(hInstance);
            CefExecuteProcess(mainArgs2, app, nullptr);
            initSuccess = false; // will be set below
            break;
        }

        // Check if CEF log was created for this attempt
        FILE* logCheck = fopen(logPath.c_str(), "r");
        if (logCheck) {
            fprintf(stderr, "[CEF Host] --- cef_debug.log from attempt %d ---\n", attemptIdx);
            char buf[512];
            int lines = 0;
            while (fgets(buf, sizeof(buf), logCheck) && lines++ < 50) {
                fprintf(stderr, "  %s", buf);
            }
            fclose(logCheck);
            fprintf(stderr, "[CEF Host] --- end attempt %d log ---\n", attemptIdx);
            // Delete for next attempt
            DeleteFileA(logPath.c_str());
        }
        fflush(stderr);
    }

    // Final attempt with full settings and app
    fprintf(stderr, "[CEF Host] Final CefInitialize with all settings + app...\n");
    fprintf(stderr, "  subprocess:      %s\n", subprocessPath.c_str());
    fprintf(stderr, "  resources_dir:   %s\n", cefDir.c_str());
    fprintf(stderr, "  locales_dir:     %s\n", localesDir.c_str());
    fprintf(stderr, "  root_cache_path: %s\n", rootCachePath.c_str());
    fprintf(stderr, "  cache_path:      %s\n", cachePath.c_str());
    fprintf(stderr, "  log_file:        %s\n", logPath.c_str());
    fflush(stderr);

    CefSettings settings = {};
    settings.size = sizeof(CefSettings);
    settings.no_sandbox = true;
    settings.multi_threaded_message_loop = false;
    settings.windowless_rendering_enabled = true;
    settings.command_line_args_disabled = true;
    CefString(&settings.locale).FromString("en-US");
    CefString(&settings.browser_subprocess_path).FromString(subprocessPath);
    CefString(&settings.resources_dir_path).FromString(cefDir);
    CefString(&settings.locales_dir_path).FromString(localesDir);
    CefString(&settings.root_cache_path).FromString(rootCachePath);
    CefString(&settings.cache_path).FromString(cachePath);
    CefString(&settings.log_file).FromString(logPath);
    settings.log_severity = LOGSEVERITY_VERBOSE;

    SetLastError(0);
    bool initResult = CefInitialize(mainArgs, settings, app, nullptr);
    DWORD lastErr = GetLastError();
    if (!initResult) {
        fprintf(stderr, "[CEF Host] CefInitialize failed! (GetLastError=%lu, totalExceptions=%d)\n",
                lastErr, g_exceptionCount);
        if (veh) RemoveVectoredExceptionHandler(veh);

        // Try to read and report CEF debug log for diagnostics.
        fprintf(stderr, "[CEF Host] --- cef_debug.log contents ---\n");
        FILE* cefLog = fopen(logPath.c_str(), "r");
        if (cefLog) {
            char buf[512];
            while (fgets(buf, sizeof(buf), cefLog)) {
                fprintf(stderr, "  %s", buf);
            }
            fclose(cefLog);
        } else {
            fprintf(stderr, "  (no log file created at %s)\n", logPath.c_str());
        }
        fprintf(stderr, "[CEF Host] --- end cef_debug.log ---\n");

        // Print runtime environment details for Wine/CrossOver diagnostics.
        OSVERSIONINFOW osvi = {};
        osvi.dwOSVersionInfoSize = sizeof(osvi);
        typedef LONG (WINAPI *RtlGetVersionPtr)(OSVERSIONINFOW*);
        RtlGetVersionPtr rtlGetVersion = (RtlGetVersionPtr)GetProcAddress(
            GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
        if (rtlGetVersion && rtlGetVersion(&osvi) == 0) {
            fprintf(stderr, "[CEF Host] Windows version (Wine): %lu.%lu.%lu\n",
                    osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber);
        }
        typedef const char* (WINAPI *wine_get_version_ptr)(void);
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        wine_get_version_ptr wine_get_version = (wine_get_version_ptr)
            GetProcAddress(ntdll, "wine_get_version");
        if (wine_get_version) {
            fprintf(stderr, "[CEF Host] Wine version: %s\n", wine_get_version());
        }
        fflush(stderr);

        std::string errMsg = "CefInitialize failed";
        fprintf(stderr, "[CEF Host] %s\n", errMsg.c_str());
        if (SUCCEEDED(coInitHr)) CoUninitialize();
        pipe.Close();
        return 1;
    }

    fprintf(stderr, "[CEF Host] CEF initialized successfully.\n");
    if (veh) RemoveVectoredExceptionHandler(veh);

    if (diagInitOnly) {
        fprintf(stderr, "[CEF Host] diag-init-only succeeded; shutting down CEF.\n");
        CefShutdown();
        if (SUCCEEDED(coInitHr)) CoUninitialize();
        return 0;
    }

    // 3. Connect to plugin pipe after successful CEF initialization.
    if (!pipe.Connect(pipeName, 10000)) {
        fprintf(stderr, "[CEF Host] Failed to connect to plugin pipe.\n");
        CefShutdown();
        if (SUCCEEDED(coInitHr)) CoUninitialize();
        return 1;
    }
    fprintf(stderr, "[CEF Host] Connected to plugin pipe.\n");

    // 4. Open shared memory after successful CEF initialization.
    hMapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, shmemName.c_str());
    if (!hMapping) {
        fprintf(stderr, "[CEF Host] Failed to open shared memory '%s' (error %lu).\n",
                shmemName.c_str(), GetLastError());
        std::string errMsg = "OpenFileMapping failed";
        pipe.Send(PipeProtocol::MSG_HOST_ERROR, errMsg.data(),
                  static_cast<uint32_t>(errMsg.size()));
        CefShutdown();
        if (SUCCEEDED(coInitHr)) CoUninitialize();
        pipe.Close();
        return 1;
    }

    shmemView = MapViewOfFile(hMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!shmemView) {
        fprintf(stderr, "[CEF Host] Failed to map shared memory (error %lu).\n", GetLastError());
        std::string errMsg = "MapViewOfFile failed";
        pipe.Send(PipeProtocol::MSG_HOST_ERROR, errMsg.data(),
                  static_cast<uint32_t>(errMsg.size()));
        CloseHandle(hMapping);
        hMapping = nullptr;
        CefShutdown();
        if (SUCCEEDED(coInitHr)) CoUninitialize();
        pipe.Close();
        return 1;
    }
    fprintf(stderr, "[CEF Host] Shared memory mapped.\n");

    // 5. Create IPC bridge
    HostIpcBridge ipcBridge(&pipe);

    // 6. Send HOST_READY to plugin
    pipe.Send(PipeProtocol::MSG_HOST_READY);
    fprintf(stderr, "[CEF Host] Sent HOST_READY.\n");

    // 7. Main loop
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
    if (SUCCEEDED(coInitHr)) CoUninitialize();

    if (shmemView) UnmapViewOfFile(shmemView);
    if (hMapping) CloseHandle(hMapping);
    pipe.Close();

    fprintf(stderr, "[CEF Host] Exited cleanly.\n");
    return 0;
}
