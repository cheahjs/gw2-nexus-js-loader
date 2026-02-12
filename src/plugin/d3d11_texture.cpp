#include "d3d11_texture.h"
#include "globals.h"
#include "shared/version.h"

#include <dxgi.h>

D3D11Texture::D3D11Texture() = default;

D3D11Texture::~D3D11Texture() {
    Release();
}

void D3D11Texture::CreateTexture(int width, int height) {
    Release();

    if (!Globals::API || !Globals::API->SwapChain) return;

    // Get D3D11 device from swap chain
    IDXGISwapChain* swapChain = static_cast<IDXGISwapChain*>(Globals::API->SwapChain);
    ID3D11Device* device = nullptr;
    HRESULT hr = swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device));
    if (FAILED(hr) || !device) return;

    // Create the texture
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width            = static_cast<UINT>(width);
    desc.Height           = static_cast<UINT>(height);
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM; // CEF uses BGRA
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_DYNAMIC;
    desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags   = D3D11_CPU_ACCESS_WRITE;

    hr = device->CreateTexture2D(&desc, nullptr, &m_texture);
    if (FAILED(hr)) {
        device->Release();
        return;
    }

    // Create shader resource view
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = desc.Format;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels       = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;

    hr = device->CreateShaderResourceView(m_texture, &srvDesc, &m_srv);
    device->Release();

    if (FAILED(hr)) {
        m_texture->Release();
        m_texture = nullptr;
        return;
    }

    m_width  = width;
    m_height = height;
}

void D3D11Texture::UpdateFromPixels(const void* pixels, int width, int height) {
    if (!pixels || width <= 0 || height <= 0) return;
    if (!Globals::API || !Globals::API->SwapChain) return;

    // Recreate if size changed
    if (width != m_width || height != m_height || !m_texture) {
        CreateTexture(width, height);
        if (!m_texture) return;
    }

    // Get device context
    IDXGISwapChain* swapChain = static_cast<IDXGISwapChain*>(Globals::API->SwapChain);
    ID3D11Device* device = nullptr;
    swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device));
    if (!device) return;

    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);
    device->Release();
    if (!context) return;

    // Map, copy pixels, unmap
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = context->Map(m_texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        const int srcPitch = width * 4;
        const uint8_t* src = static_cast<const uint8_t*>(pixels);
        uint8_t* dst = static_cast<uint8_t*>(mapped.pData);

        for (int y = 0; y < height; ++y) {
            memcpy(dst, src, srcPitch);
            src += srcPitch;
            dst += mapped.RowPitch;
        }

        context->Unmap(m_texture, 0);
    }

    context->Release();
}

void* D3D11Texture::GetShaderResourceView() const {
    return m_srv;
}

void D3D11Texture::Release() {
    if (m_srv) {
        m_srv->Release();
        m_srv = nullptr;
    }
    if (m_texture) {
        m_texture->Release();
        m_texture = nullptr;
    }
    m_width  = 0;
    m_height = 0;
}
