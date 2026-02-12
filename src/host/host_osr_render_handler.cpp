#include "host_osr_render_handler.h"
#include "shared/pipe_protocol.h"
#include <cstring>
#include <windows.h>

HostOsrRenderHandler::HostOsrRenderHandler(void* shmemView, int width, int height)
    : m_shmemView(shmemView)
    , m_width(width)
    , m_height(height) {
}

void HostOsrRenderHandler::GetViewRect(CefRefPtr<CefBrowser> /*browser*/, CefRect& rect) {
    rect = CefRect(0, 0, m_width, m_height);
}

void HostOsrRenderHandler::OnPaint(CefRefPtr<CefBrowser> /*browser*/,
                                     PaintElementType type,
                                     const RectList& /*dirtyRects*/,
                                     const void* buffer,
                                     int width,
                                     int height) {
    if (type != PET_VIEW || !m_shmemView || !buffer) return;

    auto* header = static_cast<PipeProtocol::SharedFrameHeader*>(m_shmemView);

    // Determine which buffer is inactive (the one we write to)
    uint32_t currentActive = header->activeBuffer;
    uint32_t writeBuffer = 1 - currentActive;

    // Clamp dimensions to max
    uint32_t w = static_cast<uint32_t>(width);
    uint32_t h = static_cast<uint32_t>(height);
    if (w > PipeProtocol::MAX_FRAME_WIDTH) w = PipeProtocol::MAX_FRAME_WIDTH;
    if (h > PipeProtocol::MAX_FRAME_HEIGHT) h = PipeProtocol::MAX_FRAME_HEIGHT;

    // Write pixels to inactive buffer
    uint8_t* dst = PipeProtocol::GetBufferPtr(m_shmemView, writeBuffer);
    size_t frameSize = static_cast<size_t>(w) * h * PipeProtocol::BYTES_PER_PIXEL;
    memcpy(dst, buffer, frameSize);

    // Update header: dimensions, flip active buffer, increment sequence
    header->width = w;
    header->height = h;

    // Write barrier: ensure pixel data is visible before header update
    MemoryBarrier();

    header->activeBuffer = writeBuffer;

    // Write barrier: ensure activeBuffer is updated before seqNum
    MemoryBarrier();

    header->writerSeqNum++;
}

void HostOsrRenderHandler::SetSize(int width, int height) {
    m_width = width;
    m_height = height;
}
