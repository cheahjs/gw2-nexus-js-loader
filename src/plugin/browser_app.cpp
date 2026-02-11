#include "browser_app.h"
#include "globals.h"
#include "shared/version.h"

void BrowserApp::OnContextInitialized() {
    if (Globals::API) {
        Globals::API->Log(LOGL_DEBUG, ADDON_NAME, "CEF browser process context initialized.");
    }
}
