#pragma once

#include <d3d11.h>
#include <wrl/client.h>

class Texture {
public:
    Texture(Microsoft::WRL::ComPtr<ID3D11Texture2D> texture, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv)
        : m_texture(texture)
        , m_srv(srv)
    {
    }

    ID3D11Texture2D* getTexture() const { return m_texture.Get(); }
    ID3D11ShaderResourceView* getSRV() const { return m_srv.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
};
