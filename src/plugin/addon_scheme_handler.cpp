#include "addon_scheme_handler.h"
#include "globals.h"
#include "shared/version.h"

#include "include/cef_scheme.h"
#include "include/cef_parser.h"
#include "include/cef_stream.h"
#include "include/wrapper/cef_stream_resource_handler.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

// Maps addon ID → base path for file resolution.
static std::unordered_map<std::string, std::string> s_addonPaths;

// Registered domain names for cleanup.
static std::vector<std::string> s_registeredDomains;

// Validate that a path doesn't contain traversal sequences.
static bool IsPathSafe(const std::string& path) {
    // Reject any path containing ".." or absolute paths
    if (path.find("..") != std::string::npos) return false;
    if (!path.empty() && (path[0] == '/' || path[0] == '\\')) return false;
    if (path.size() >= 2 && path[1] == ':') return false; // C:\... style
    return true;
}

// Get file extension from a path.
static std::string GetFileExtension(const std::string& path) {
    auto pos = path.rfind('.');
    if (pos == std::string::npos) return "";
    return path.substr(pos + 1);
}

// CefSchemeHandlerFactory implementation that serves local addon files.
class AddonSchemeHandlerFactory : public CefSchemeHandlerFactory {
public:
    explicit AddonSchemeHandlerFactory(const std::string& addonId)
        : m_addonId(addonId) {}

    CefRefPtr<CefResourceHandler> Create(
        CefRefPtr<CefBrowser> /*browser*/,
        CefRefPtr<CefFrame> /*frame*/,
        const CefString& /*scheme_name*/,
        CefRefPtr<CefRequest> request) override {

        std::string url = request->GetURL().ToString();

        // Parse the URL path: https://<addon-id>.jsloader.local/<path>
        // Find the path after the domain
        std::string domain = m_addonId + ".jsloader.local";
        auto domainPos = url.find(domain);
        if (domainPos == std::string::npos) return nullptr;

        std::string path = url.substr(domainPos + domain.size());

        // Strip leading slash
        if (!path.empty() && path[0] == '/') {
            path = path.substr(1);
        }

        // Strip query string and fragment
        auto queryPos = path.find('?');
        if (queryPos != std::string::npos) path = path.substr(0, queryPos);
        auto fragPos = path.find('#');
        if (fragPos != std::string::npos) path = path.substr(0, fragPos);

        // URL-decode the path (handle %20 etc.)
        path = CefURIDecode(path, true,
            static_cast<cef_uri_unescape_rule_t>(
                UU_SPACES | UU_PATH_SEPARATORS | UU_URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS
            )).ToString();

        // Validate path safety (no traversal)
        if (!IsPathSafe(path)) {
            if (Globals::API) {
                Globals::API->Log(LOGL_WARNING, ADDON_NAME,
                    (std::string("Blocked path traversal attempt: ") + path).c_str());
            }
            return nullptr;
        }

        // Look up base path for this addon
        auto it = s_addonPaths.find(m_addonId);
        if (it == s_addonPaths.end()) return nullptr;

        // Construct full filesystem path
        std::string filePath = it->second + "\\" + path;

        // Replace forward slashes with backslashes for Windows
        std::replace(filePath.begin(), filePath.end(), '/', '\\');

        // Create file stream
        CefRefPtr<CefStreamReader> stream =
            CefStreamReader::CreateForFile(filePath);
        if (!stream) {
            if (Globals::API) {
                Globals::API->Log(LOGL_DEBUG, ADDON_NAME,
                    (std::string("File not found: ") + filePath).c_str());
            }
            return nullptr;
        }

        // Determine MIME type from file extension
        std::string ext = GetFileExtension(path);
        CefString mimeType = CefGetMimeType(ext);
        if (mimeType.empty()) {
            mimeType = "application/octet-stream";
        }

        // Return a stream resource handler
        return new CefStreamResourceHandler(mimeType, stream);
    }

private:
    std::string m_addonId;

    IMPLEMENT_REFCOUNTING(AddonSchemeHandlerFactory);
    DISALLOW_COPY_AND_ASSIGN(AddonSchemeHandlerFactory);
};

namespace AddonSchemeHandler {

void RegisterForAddon(const std::string& addonId, const std::string& basePath) {
    s_addonPaths[addonId] = basePath;

    std::string domain = addonId + ".jsloader.local";

    CefRefPtr<CefSchemeHandlerFactory> factory =
        new AddonSchemeHandlerFactory(addonId);

    CefRegisterSchemeHandlerFactory("https", domain, factory);
    s_registeredDomains.push_back(domain);

    if (Globals::API) {
        Globals::API->Log(LOGL_INFO, ADDON_NAME,
            (std::string("Registered scheme handler: https://") + domain + "/").c_str());
    }
}

void UnregisterAll() {
    for (const auto& domain : s_registeredDomains) {
        CefRegisterSchemeHandlerFactory("https", domain, nullptr);
    }
    s_registeredDomains.clear();
    s_addonPaths.clear();
}

} // namespace AddonSchemeHandler
