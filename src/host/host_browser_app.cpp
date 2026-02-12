#include "host_browser_app.h"
#include "include/cef_command_line.h"
#include <cstdio>

void HostBrowserApp::OnBeforeCommandLineProcessing(
    const CefString& process_type,
    CefRefPtr<CefCommandLine> command_line) {
    // Extra startup logging for diagnosing early initialization failures.
    command_line->AppendSwitch("enable-logging");
    command_line->AppendSwitchWithValue("log-severity", "verbose");
    command_line->AppendSwitchWithValue("v", "1");

    // Disable GPU features that are problematic under Wine/CrossOver
    command_line->AppendSwitch("disable-gpu");
    command_line->AppendSwitch("disable-gpu-compositing");
    command_line->AppendSwitch("disable-gpu-sandbox");
    command_line->AppendSwitch("no-sandbox");
    command_line->AppendSwitch("allow-no-sandbox-job");
    command_line->AppendSwitch("disable-breakpad");

    // Disable features that may not work under Wine
    command_line->AppendSwitch("disable-extensions");
    command_line->AppendSwitch("disable-component-update");

    fprintf(stderr, "[CEF Host] Added Wine-compatible command-line switches.\n");
    fprintf(stderr, "[CEF Host] Full CEF command line: %s\n",
            command_line->GetCommandLineString().ToString().c_str());
    fflush(stderr);
}

void HostBrowserApp::OnContextInitialized() {
    fprintf(stderr, "[CEF Host] Browser process context initialized.\n");
}
