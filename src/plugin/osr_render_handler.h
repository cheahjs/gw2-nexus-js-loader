#pragma once

#include "include/cef_render_handler.h"
#include "d3d11_texture.h"

// Off-screen render handler: receives pixel buffers from CEF and uploads to D3D11 texture.
class OsrRenderHandler : public CefRenderHandler {
public:
    OsrRenderHandler(int width, int height);
    ~OsrRenderHandler();

    // CefRenderHandler
    void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
    void OnPaint(CefRefPtr<CefBrowser> browser,
                 PaintElementType type,
                 const RectList& dirtyRects,
                 const void* buffer,
                 int width,
                 int height) override;

    // Get the D3D11 shader resource view for ImGui rendering
    void* GetTextureHandle() const;

    // Resize the viewport
    void SetSize(int width, int height);

    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

private:
    int           m_width;
    int           m_height;
    D3D11Texture  m_texture;

    IMPLEMENT_REFCOUNTING(OsrRenderHandler);
    DISALLOW_COPY_AND_ASSIGN(OsrRenderHandler);
};
