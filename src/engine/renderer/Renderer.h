#pragma once

#include "engine/input/IMediaSource.h"
#include "Texture.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

struct Vertex {
    float pos[3];
    float tex[2];
};

struct AlphaBufferData {
    float alpha{1.0f};
    float padding[3]{0.0f, 0.0f, 0.0f};
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
    void startTransition(std::shared_ptr<IMediaSource> fromSource, 
                         std::shared_ptr<IMediaSource> toSource, 
                         float durationMs);

    void setFTB(bool active, float durationMs = 500.0f);
    bool isFTB() const { return m_ftbActive; }

    void setManualTransition(std::shared_ptr<IMediaSource> fromSource,
                            std::shared_ptr<IMediaSource> toSource,
                            float progress);

    void releaseRenderTargetView();

    void start();
    void stop();

private:
    bool initShadersAndBuffers();
    void renderLoop();
    void renderFrame();
    void drawTexture(Texture& tex, float alpha, float windowW, float windowH);

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTargetView;

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_alphaConstantBuffer;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_samplerState;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizerState;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendState;

    Texture m_textureA;
    Texture m_textureB;
    bool m_texAInit{false};
    bool m_texBInit{false};

    std::shared_ptr<IMediaSource> m_mediaSource;
    std::shared_ptr<IMediaSource> m_fromSource;
    std::shared_ptr<IMediaSource> m_toSource;

    bool m_isTransitioning{false};
    float m_transitionDurationMs{500.0f};
    std::chrono::steady_clock::time_point m_transitionStartTime;

    bool m_ftbActive{false};
    float m_ftbCurrentAlpha{0.0f};
    float m_ftbDurationMs{500.0f};
    std::chrono::steady_clock::time_point m_ftbStartTime;

    bool m_isManualTransition{false};
    float m_manualProgress{0.0f};

    std::thread m_renderThread;
    std::atomic<bool> m_running{false};
    std::mutex m_renderMutex;
};
