#include "shared_frame_reader.h"

SharedFrameReader::SharedFrameReader() = default;

SharedFrameReader::~SharedFrameReader() {
    Close();
}

bool SharedFrameReader::Create(const std::string& shmemName) {
    m_mapping = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        PipeProtocol::SHMEM_TOTAL_SIZE,
        shmemName.c_str()
    );

    if (!m_mapping) {
        return false;
    }

    m_view = MapViewOfFile(m_mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!m_view) {
        CloseHandle(m_mapping);
        m_mapping = nullptr;
        return false;
    }

    // Zero-initialize the header
    memset(m_view, 0, PipeProtocol::HEADER_SIZE);

    return true;
}

void SharedFrameReader::Poll() {
    if (!m_view) return;

    auto* header = static_cast<PipeProtocol::SharedFrameHeader*>(m_view);

    // Check if a new frame is available
    uint32_t seq = header->writerSeqNum;
    if (seq == m_lastSeqNum) return;

    // Read barrier - ensure we read the header before the pixel data
    MemoryBarrier();

    uint32_t w = header->width;
    uint32_t h = header->height;
    uint32_t active = header->activeBuffer;

    if (w == 0 || h == 0 || w > PipeProtocol::MAX_FRAME_WIDTH ||
        h > PipeProtocol::MAX_FRAME_HEIGHT || active > 1) {
        return;
    }

    // Read barrier - ensure we read dimensions before pixel data
    MemoryBarrier();

    const uint8_t* srcPixels = PipeProtocol::GetBufferPtr(m_view, active);

    // Upload to D3D11 texture
    m_texture.UpdateFromPixels(srcPixels, static_cast<int>(w), static_cast<int>(h));
    m_width = static_cast<int>(w);
    m_height = static_cast<int>(h);
    m_lastSeqNum = seq;
}

void* SharedFrameReader::GetTextureHandle() const {
    return m_texture.GetShaderResourceView();
}

void SharedFrameReader::Close() {
    m_texture.Release();

    if (m_view) {
        UnmapViewOfFile(m_view);
        m_view = nullptr;
    }
    if (m_mapping) {
        CloseHandle(m_mapping);
        m_mapping = nullptr;
    }

    m_width = 0;
    m_height = 0;
    m_lastSeqNum = 0;
}
