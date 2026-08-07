#pragma once

#include "Texture.h"
#include "engine/input/IMediaSource.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>

struct Vertex {
    float pos[3];
    float tex[2];
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    void initialize(Microsoft::WRL::ComPtr<ID3D11Device> device, 
                    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context,
                    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain,
                    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv);

    void setMediaSource(std::shared_ptr<IMediaSource> source);

    void start();
    void stop();

private:
    bool initShadersAndBuffers();
    void renderLoop();
    void renderFrame();

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTargetView;

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_samplerState;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizerState;

    Texture m_texture;
    bool m_textureInitialized{false};

    std::shared_ptr<IMediaSource> m_mediaSource;

    std::thread m_renderThread;
    std::atomic<bool> m_running{false};
    std::mutex m_renderMutex;
};
