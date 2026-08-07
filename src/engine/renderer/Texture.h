#pragma once

#include "engine/frame/Frame.h"
#include <d3d11.h>
#include <wrl/client.h>

class Texture {
public:
    Texture();
    ~Texture() = default;

    bool initialize(ID3D11Device* device, int width, int height);
    bool update(ID3D11DeviceContext* context, const Frame& frame);

    ID3D11ShaderResourceView* getShaderResourceView() const { return m_srv.Get(); }
    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    int m_width{0};
    int m_height{0};

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
};
