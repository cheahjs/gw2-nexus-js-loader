#pragma once

#include <d3d11.h>

// Manages a D3D11 texture that is updated from a CPU pixel buffer (BGRA).
// Used to display CEF off-screen rendering output via ImGui.
class D3D11Texture {
public:
    D3D11Texture();
    ~D3D11Texture();

    // Upload a BGRA pixel buffer to the texture.
    // Recreates the texture if dimensions changed.
    void UpdateFromPixels(const void* pixels, int width, int height);

    // Get the shader resource view suitable for ImGui::Image().
    // Returns nullptr if no texture has been created yet.
    void* GetShaderResourceView() const;

    // Release all D3D11 resources.
    void Release();

private:
    void CreateTexture(int width, int height);

    ID3D11Texture2D*          m_texture = nullptr;
    ID3D11ShaderResourceView* m_srv     = nullptr;
    int                       m_width   = 0;
    int                       m_height  = 0;
};
