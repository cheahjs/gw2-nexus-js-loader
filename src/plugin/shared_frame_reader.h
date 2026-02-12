#pragma once

#include "shared/pipe_protocol.h"
#include "d3d11_texture.h"
#include <windows.h>
#include <string>

// Reads frames from the shared memory region written by the CEF host process,
// and uploads them to a D3D11 texture for overlay rendering.
class SharedFrameReader {
public:
    SharedFrameReader();
    ~SharedFrameReader();

    // Create the shared memory section. Called before launching host.
    bool Create(const std::string& shmemName);

    // Poll for a new frame. If available, copies pixels and updates the D3D11 texture.
    // Call once per frame from OnPreRender.
    void Poll();

    // Get the D3D11 shader resource view for ImGui rendering.
    void* GetTextureHandle() const;

    // Get current frame dimensions.
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

    // Release shared memory and texture resources.
    void Close();

private:
    HANDLE m_mapping = nullptr;
    void*  m_view    = nullptr;

    D3D11Texture m_texture;
    int          m_width  = 0;
    int          m_height = 0;
    uint32_t     m_lastSeqNum = 0;
};
