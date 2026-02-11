#include "osr_render_handler.h"
#include "globals.h"
#include "shared/version.h"

OsrRenderHandler::OsrRenderHandler(int width, int height)
    : m_width(width)
    , m_height(height) {
}

OsrRenderHandler::~OsrRenderHandler() {
    m_texture.Release();
}

void OsrRenderHandler::GetViewRect(CefRefPtr<CefBrowser> /*browser*/, CefRect& rect) {
    rect = CefRect(0, 0, m_width, m_height);
}

void OsrRenderHandler::OnPaint(CefRefPtr<CefBrowser> /*browser*/,
                                PaintElementType type,
                                const RectList& /*dirtyRects*/,
                                const void* buffer,
                                int width,
                                int height) {
    if (type != PET_VIEW) return;

    // Upload the full BGRA buffer to the D3D11 texture
    m_texture.UpdateFromPixels(buffer, width, height);
}

void* OsrRenderHandler::GetTextureHandle() const {
    return m_texture.GetShaderResourceView();
}

void OsrRenderHandler::SetSize(int width, int height) {
    m_width = width;
    m_height = height;
    // Texture will be recreated on next OnPaint if size changed
}
