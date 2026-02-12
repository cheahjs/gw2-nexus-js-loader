#include "host_browser_app.h"
#include <cstdio>

void HostBrowserApp::OnContextInitialized() {
    fprintf(stderr, "[CEF Host] Browser process context initialized.\n");
}
