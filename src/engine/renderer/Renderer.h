#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <thread>
#include <atomic>
#include <mutex>

class Renderer {
public:
    Renderer();
    ~Renderer();

    // Sets the D3D device and context to use for rendering
    void initialize(Microsoft::WRL::ComPtr<ID3D11Device> device, 
                    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context,
                    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain,
                    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv);

    void start();
    void stop();

private:
    void renderLoop();
    void renderFrame();

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTargetView;

    std::thread m_renderThread;
    std::atomic<bool> m_running;
    
    // Mutex to protect D3D calls if necessary
    std::mutex m_renderMutex;
};
