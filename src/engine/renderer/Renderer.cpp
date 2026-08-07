#include "Renderer.h"
#include "common/logger/Logger.h"

#include <chrono>

Renderer::Renderer() : m_running(false) {
}

Renderer::~Renderer() {
    stop();
}

void Renderer::initialize(Microsoft::WRL::ComPtr<ID3D11Device> device, 
                          Microsoft::WRL::ComPtr<ID3D11DeviceContext> context,
                          Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain,
                          Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv) 
{
    m_device = device;
    m_context = context;
    m_swapChain = swapChain;
    m_renderTargetView = rtv;
    LOG_INFO("Renderer initialized with D3D11 device.");
}

void Renderer::start() {
    if (m_running) return;

    m_running = true;
    m_renderThread = std::thread(&Renderer::renderLoop, this);
    LOG_INFO("Render thread started.");
}

void Renderer::stop() {
    if (!m_running) return;

    m_running = false;
    if (m_renderThread.joinable()) {
        m_renderThread.join();
    }
    LOG_INFO("Render thread stopped.");
}

void Renderer::renderLoop() {
    // Target 60 FPS (~16.6ms per frame)
    const std::chrono::milliseconds frameDuration(16); 

    while (m_running) {
        auto startTime = std::chrono::steady_clock::now();

        renderFrame();

        auto endTime = std::chrono::steady_clock::now();
        auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

        if (elapsedTime < frameDuration) {
            std::this_thread::sleep_for(frameDuration - elapsedTime);
        }
    }
}

void Renderer::renderFrame() {
    if (!m_context || !m_renderTargetView || !m_swapChain) return;

    std::lock_guard<std::mutex> lock(m_renderMutex);

    // Clear the screen with a specific color (e.g., Deep Blue) to prove D3D is working
    float clearColor[4] = { 0.0f, 0.2f, 0.4f, 1.0f };
    m_context->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);

    // Set the render target
    m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), nullptr);

    // TODO: Draw actual video textures here in later milestones.

    // Present the frame
    // SyncInterval = 1 for VSync, 0 for immediate
    m_swapChain->Present(1, 0); 
}
