#pragma once

#include "include/cef_render_handler.h"
#include <cstdint>

// Off-screen render handler for the host process.
// Writes BGRA pixel buffers from CEF into shared memory (double-buffered).
class HostOsrRenderHandler : public CefRenderHandler {
public:
    HostOsrRenderHandler(void* shmemView, int width, int height);

    // CefRenderHandler
    void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
    void OnPaint(CefRefPtr<CefBrowser> browser,
                 PaintElementType type,
                 const RectList& dirtyRects,
                 const void* buffer,
                 int width,
                 int height) override;

    // Update viewport dimensions
    void SetSize(int width, int height);

    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

private:
    void* m_shmemView;   // Mapped shared memory base pointer
    int   m_width;
    int   m_height;

    IMPLEMENT_REFCOUNTING(HostOsrRenderHandler);
    DISALLOW_COPY_AND_ASSIGN(HostOsrRenderHandler);
};
