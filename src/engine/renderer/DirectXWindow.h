#pragma once

#include <QWidget>
#include "Renderer.h"
#include "engine/input/IMediaSource.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

class DirectXWindow : public QWidget {
    Q_OBJECT

public:
    explicit DirectXWindow(QWidget *parent = nullptr);
    ~DirectXWindow() override;

    bool initDirectX();

protected:
    QPaintEngine* paintEngine() const override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    Microsoft::WRL::ComPtr<ID3D11Device> m_d3dDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_d3dContext;
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTargetView;

    std::unique_ptr<Renderer> m_renderer;
    std::shared_ptr<IMediaSource> m_mediaSource;
};
