#include "DirectXWindow.h"
#include "engine/input/ColorBarsSource.h"
#include "common/logger/Logger.h"

#include <QEvent>

DirectXWindow::DirectXWindow(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    LOG_INFO("DirectXWindow created. Window ID: {}", reinterpret_cast<void*>(winId()));
}

DirectXWindow::~DirectXWindow() {
    if (m_renderer) {
        m_renderer->stop();
    }
    if (m_mediaSource) {
        m_mediaSource->close();
    }
    LOG_INFO("DirectXWindow destroyed.");
}

bool DirectXWindow::initDirectX() {
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = width();
    scd.BufferDesc.Height = height();
    scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = reinterpret_cast<HWND>(winId());
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
        LOG_ERROR("Failed to create D3D11 device and swap chain. HRESULT: {0:x}", static_cast<unsigned int>(hr));
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

    LOG_INFO("DirectX 11 initialized successfully.");

    // Instantiate test ColorBarsSource
    m_mediaSource = std::make_shared<ColorBarsSource>(1280, 720);
    m_mediaSource->open();

    // Initialize and start renderer
    m_renderer = std::make_unique<Renderer>();
    m_renderer->initialize(m_d3dDevice, m_d3dContext, m_swapChain, m_renderTargetView);
    m_renderer->setMediaSource(m_mediaSource);
    m_renderer->start();

    return true;
}

void DirectXWindow::setMediaSource(std::shared_ptr<IMediaSource> source) {
    if (m_mediaSource && m_mediaSource != source) {
        m_mediaSource->close();
    }
    m_mediaSource = source;
    if (m_mediaSource) {
        m_mediaSource->open();
    }
    if (m_renderer) {
        m_renderer->setMediaSource(m_mediaSource);
    }
}

QPaintEngine* DirectXWindow::paintEngine() const {
    return nullptr;
}

void DirectXWindow::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
}

void DirectXWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_swapChain && m_renderer) {
        LOG_INFO("DirectXWindow resized. SwapChain needs resize handling.");
    }
}
