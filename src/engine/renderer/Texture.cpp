#include "Texture.h"
#include "common/logger/Logger.h"

Texture::Texture()
    : m_width(0)
    , m_height(0)
{
}

bool Texture::initialize(ID3D11Device* device, int width, int height) {
    if (!device || width <= 0 || height <= 0) return false;

    m_width = width;
    m_height = height;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = 0;

    HRESULT hr = device->CreateTexture2D(&desc, nullptr, m_texture.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create D3D11 2D Texture. HRESULT: 0x{:X}", static_cast<unsigned int>(hr));
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    hr = device->CreateShaderResourceView(m_texture.Get(), &srvDesc, m_srv.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create D3D11 Shader Resource View. HRESULT: 0x{:X}", static_cast<unsigned int>(hr));
        return false;
    }

    return true;
}

bool Texture::update(ID3D11DeviceContext* context, const Frame& frame) {
    if (!context || !m_texture) return false;

    if (frame.width() != m_width || frame.height() != m_height) {
        LOG_WARN("Frame size ({}x{}) does not match Texture size ({}x{})",
                 frame.width(), frame.height(), m_width, m_height);
        return false;
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = context->Map(m_texture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to map D3D11 Texture resource. HRESULT: 0x{:X}", static_cast<unsigned int>(hr));
        return false;
    }

    const uint8_t* srcData = frame.data();
    uint8_t* dstData = static_cast<uint8_t*>(mapped.pData);

    int srcStride = frame.stride();
    UINT dstStride = mapped.RowPitch;

    for (int row = 0; row < m_height; ++row) {
        memcpy(dstData + row * dstStride, srcData + row * srcStride, std::min<size_t>(srcStride, dstStride));
    }

    context->Unmap(m_texture.Get(), 0);
    return true;
}
