#include "DirectXWindow.h"
#include "common/logger/Logger.h"

#include <QEvent>
#include <QResizeEvent>
#include <dxgi.h>

DirectXWindow::DirectXWindow(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    LOG_INFO("DirectXWindow created.");
}

DirectXWindow::~DirectXWindow() {
    if (m_renderer) {
        m_renderer->stop();
    }
    LOG_INFO("DirectXWindow destroyed.");
}

void DirectXWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!m_d3dDevice) {
        initDirectX();
    }
}

bool DirectXWindow::initDirectX() {
    if (m_d3dDevice) return true;

    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) {
        LOG_ERROR("DirectXWindow: Cannot init Direct3D 11 with null HWND!");
        return false;
    }

    m_swapChainHwnd = hwnd;
    int initialW = std::max(1280, static_cast<int>(width() * devicePixelRatio()));
    int initialH = std::max(720, static_cast<int>(height() * devicePixelRatio()));
    m_currentWidth = initialW;
    m_currentHeight = initialH;

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = initialW;
    scd.BufferDesc.Height = initialH;
    scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevels,
        1,
        D3D11_SDK_VERSION,
        &scd,
        m_swapChain.GetAddressOf(),
        m_d3dDevice.GetAddressOf(),
        &featureLevel,
        m_d3dContext.GetAddressOf()
    );

    if (FAILED(hr)) {
        LOG_ERROR("Failed to create D3D11 device and swap chain. HRESULT: 0x{:X}", static_cast<unsigned int>(hr));
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuffer.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to get back buffer.");
        return false;
    }

    hr = m_d3dDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, m_renderTargetView.GetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create render target view.");
        return false;
    }

    LOG_INFO("DirectX 11 initialized successfully on HWND {} ({}x{}).", reinterpret_cast<void*>(hwnd), initialW, initialH);

    // Initialize and start renderer
    m_renderer = std::make_unique<Renderer>();
    m_renderer->initialize(m_d3dDevice, m_d3dContext, m_swapChain, m_renderTargetView);
    m_renderer->setMediaSource(m_mediaSource);
    m_renderer->start();

    return true;
}

void DirectXWindow::setMediaSource(std::shared_ptr<IMediaSource> source) {
    m_mediaSource = source;
    if (m_mediaSource) {
        m_mediaSource->open();
    }
    if (m_renderer) {
        m_renderer->setMediaSource(m_mediaSource);
    }
}

void DirectXWindow::resizeSwapChain(int w, int h) {
    if (!m_d3dDevice || !m_d3dContext || w <= 0 || h <= 0) return;

    HWND currentHwnd = reinterpret_cast<HWND>(winId());
    if (!currentHwnd) return;

    // Check if Qt reparented native window handle after show()
    if (m_swapChainHwnd != currentHwnd || !m_swapChain) {
        LOG_INFO("DirectXWindow: Re-binding SwapChain to HWND {} ({}x{})", reinterpret_cast<void*>(currentHwnd), w, h);
        m_swapChainHwnd = currentHwnd;

        if (m_renderer) {
            m_renderer->stop();
            m_renderer->releaseRenderTargetView();
        }

        m_d3dContext->OMSetRenderTargets(0, nullptr, nullptr);
        m_renderTargetView.Reset();
        m_swapChain.Reset();

        DXGI_SWAP_CHAIN_DESC scd = {};
        scd.BufferCount = 2;
        scd.BufferDesc.Width = w;
        scd.BufferDesc.Height = h;
        scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        scd.BufferDesc.RefreshRate.Numerator = 60;
        scd.BufferDesc.RefreshRate.Denominator = 1;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.OutputWindow = currentHwnd;
        scd.SampleDesc.Count = 1;
        scd.SampleDesc.Quality = 0;
        scd.Windowed = TRUE;
        scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        if (SUCCEEDED(m_d3dDevice.As(&dxgiDevice))) {
            Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
            if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) {
                Microsoft::WRL::ComPtr<IDXGIFactory> factory;
                if (SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
                    factory->CreateSwapChain(m_d3dDevice.Get(), &scd, m_swapChain.ReleaseAndGetAddressOf());
                }
            }
        }

        if (m_swapChain) {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
            if (SUCCEEDED(m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuffer.GetAddressOf())))) {
                m_d3dDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, m_renderTargetView.GetAddressOf());
            }

            if (m_renderer && m_renderTargetView) {
                m_renderer->initialize(m_d3dDevice, m_d3dContext, m_swapChain, m_renderTargetView);
                m_renderer->setMediaSource(m_mediaSource);
                m_renderer->start();
            }
        }
        return;
    }

    if (m_renderer) {
        m_renderer->stop();
        m_renderer->releaseRenderTargetView();
    }

    m_d3dContext->OMSetRenderTargets(0, nullptr, nullptr);
    m_renderTargetView.Reset();
    m_d3dContext->Flush();

    HRESULT hr = m_swapChain->ResizeBuffers(0, static_cast<UINT>(w), static_cast<UINT>(h), DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to resize D3D11 SwapChain buffers. HRESULT: 0x{:X}", static_cast<unsigned int>(hr));
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuffer.GetAddressOf()));
    if (SUCCEEDED(hr)) {
        m_d3dDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, m_renderTargetView.GetAddressOf());
    }

    if (m_renderer && m_renderTargetView) {
        m_renderer->initialize(m_d3dDevice, m_d3dContext, m_swapChain, m_renderTargetView);
        m_renderer->setMediaSource(m_mediaSource);
        m_renderer->start();
    }

    LOG_INFO("DirectX 11 SwapChain resized successfully to {}x{}.", w, h);
}

QPaintEngine* DirectXWindow::paintEngine() const {
    return nullptr;
}

void DirectXWindow::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
}

void DirectXWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    int realW = static_cast<int>(width() * devicePixelRatio());
    int realH = static_cast<int>(height() * devicePixelRatio());
    if (realW > 0 && realH > 0 && (realW != m_currentWidth || realH != m_currentHeight || m_swapChainHwnd != reinterpret_cast<HWND>(winId()))) {
        m_currentWidth = realW;
        m_currentHeight = realH;
        resizeSwapChain(realW, realH);
    }
}
