#include "Renderer.h"
#include "common/logger/Logger.h"
#include <d3dcompiler.h>
#include <algorithm>

const char* g_vsCode = R"(
struct VSInput {
    float3 pos : POSITION;
    float2 tex : TEXCOORD0;
};

struct PSInput {
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};

PSInput main(VSInput input) {
    PSInput output;
    output.pos = float4(input.pos, 1.0f);
    output.tex = input.tex;
    return output;
}
)";

const char* g_psCode = R"(
cbuffer AlphaBuffer : register(b0) {
    float alpha;
    float3 padding;
};

Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PSInput {
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
    float4 col = gTexture.Sample(gSampler, input.tex);
    col.a *= alpha;
    return col;
}
)";

Renderer::Renderer() {}

Renderer::~Renderer() {
    stop();
}

void Renderer::initialize(Microsoft::WRL::ComPtr<ID3D11Device> device,
                          Microsoft::WRL::ComPtr<ID3D11DeviceContext> context,
                          Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain,
                          Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv)
{
    std::lock_guard<std::mutex> lock(m_renderMutex);
    m_device = device;
    m_context = context;
    m_swapChain = swapChain;
    m_renderTargetView = rtv;

    if (m_device && !m_vertexShader) {
        initShadersAndBuffers();
    }
    LOG_INFO("Renderer initialized with D3D11 device and dual-texture transition pipeline.");
}

void Renderer::setMediaSource(std::shared_ptr<IMediaSource> source) {
    std::lock_guard<std::mutex> lock(m_renderMutex);
    if (m_mediaSource == source) return;
    m_mediaSource = source;
    m_isTransitioning = false;
    m_texAInit = false;
    m_texBInit = false;
}

void Renderer::startTransition(std::shared_ptr<IMediaSource> fromSource, 
                               std::shared_ptr<IMediaSource> toSource, 
                               float durationMs)
{
    std::lock_guard<std::mutex> lock(m_renderMutex);
    if (!toSource) return;

    m_fromSource = fromSource;
    m_toSource = toSource;
    m_transitionDurationMs = durationMs <= 0.0f ? 500.0f : durationMs;
    m_transitionStartTime = std::chrono::steady_clock::now();
    m_isTransitioning = true;
    m_texAInit = false;
    m_texBInit = false;

    LOG_INFO("Renderer: Triggered transition cross-dissolve ({:.0f} ms).", m_transitionDurationMs);
}

void Renderer::releaseRenderTargetView() {
    std::lock_guard<std::mutex> lock(m_renderMutex);
    m_renderTargetView.Reset();
}

bool Renderer::initShadersAndBuffers() {
    if (!m_device) return false;

    HRESULT hr;

    // Compile Vertex Shader
    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    hr = D3DCompile(g_vsCode, strlen(g_vsCode), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, vsBlob.GetAddressOf(), errorBlob.GetAddressOf());
    if (FAILED(hr)) {
        if (errorBlob) LOG_ERROR("VS Compilation Error: {}", static_cast<char*>(errorBlob->GetBufferPointer()));
        return false;
    }

    hr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vertexShader.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return false;

    // Compile Pixel Shader
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    hr = D3DCompile(g_psCode, strlen(g_psCode), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf());
    if (FAILED(hr)) {
        if (errorBlob) LOG_ERROR("PS Compilation Error: {}", static_cast<char*>(errorBlob->GetBufferPointer()));
        return false;
    }

    hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_pixelShader.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return false;

    // Input Layout
    D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    hr = m_device->CreateInputLayout(layoutDesc, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_inputLayout.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return false;

    // Quad Vertex Buffer (Dynamic)
    Vertex quadVertices[] = {
        { { -1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f } },
        { {  1.0f,  1.0f, 0.0f }, { 1.0f, 0.0f } },
        { { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f } },
        { {  1.0f,  1.0f, 0.0f }, { 1.0f, 0.0f } },
        { {  1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f } },
        { { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f } }
    };

    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.ByteWidth = sizeof(quadVertices);
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = quadVertices;

    hr = m_device->CreateBuffer(&bufferDesc, &initData, m_vertexBuffer.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return false;

    // Alpha Constant Buffer
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.ByteWidth = sizeof(AlphaBufferData);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = m_device->CreateBuffer(&cbDesc, nullptr, m_alphaConstantBuffer.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return false;

    // Sampler State (Anisotropic)
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxAnisotropy = 16;

    hr = m_device->CreateSamplerState(&samplerDesc, m_samplerState.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return false;

    // Rasterizer State
    D3D11_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.FrontCounterClockwise = FALSE;
    rasterDesc.DepthClipEnable = TRUE;

    hr = m_device->CreateRasterizerState(&rasterDesc, m_rasterizerState.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return false;

    // Alpha Blend State
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = m_device->CreateBlendState(&blendDesc, m_blendState.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return false;

    return true;
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
    const std::chrono::milliseconds frameDuration(16); // ~60 FPS

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

    DXGI_SWAP_CHAIN_DESC scd = {};
    float windowW = 1280.0f;
    float windowH = 720.0f;

    if (SUCCEEDED(m_swapChain->GetDesc(&scd)) && scd.BufferDesc.Width > 0 && scd.BufferDesc.Height > 0) {
        windowW = static_cast<float>(scd.BufferDesc.Width);
        windowH = static_cast<float>(scd.BufferDesc.Height);

        D3D11_VIEWPORT vp = {};
        vp.Width = windowW;
        vp.Height = windowH;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        m_context->RSSetViewports(1, &vp);
    }

    float clearColor[4] = { 0.05f, 0.05f, 0.07f, 1.0f };
    m_context->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);
    m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), nullptr);

    if (m_rasterizerState) m_context->RSSetState(m_rasterizerState.Get());
    if (m_blendState) m_context->OMSetBlendState(m_blendState.Get(), nullptr, 0xFFFFFFFF);

    if (m_isTransitioning) {
        auto now = std::chrono::steady_clock::now();
        float elapsedMs = std::chrono::duration<float, std::milli>(now - m_transitionStartTime).count();
        float progress = elapsedMs / m_transitionDurationMs;

        if (progress >= 1.0f) {
            m_isTransitioning = false;
            m_mediaSource = m_toSource;
            m_fromSource.reset();
            m_toSource.reset();
            progress = 1.0f;
        }

        // Draw Source A (From)
        if (m_fromSource) {
            auto frameA = m_fromSource->getFrame();
            if (frameA) {
                if (!m_texAInit || m_textureA.width() != frameA->width() || m_textureA.height() != frameA->height()) {
                    if (m_textureA.initialize(m_device.Get(), frameA->width(), frameA->height())) m_texAInit = true;
                }
                if (m_texAInit) m_textureA.update(m_context.Get(), *frameA);
            }
            if (m_texAInit) drawTexture(m_textureA, 1.0f - progress, windowW, windowH);
        }

        // Draw Source B (To) over A
        if (m_toSource) {
            auto frameB = m_toSource->getFrame();
            if (frameB) {
                if (!m_texBInit || m_textureB.width() != frameB->width() || m_textureB.height() != frameB->height()) {
                    if (m_textureB.initialize(m_device.Get(), frameB->width(), frameB->height())) m_texBInit = true;
                }
                if (m_texBInit) m_textureB.update(m_context.Get(), *frameB);
            }
            if (m_texBInit) drawTexture(m_textureB, progress, windowW, windowH);
        }
    } else if (m_mediaSource) {
        auto frame = m_mediaSource->getFrame();
        if (frame) {
            if (!m_texAInit || m_textureA.width() != frame->width() || m_textureA.height() != frame->height()) {
                if (m_textureA.initialize(m_device.Get(), frame->width(), frame->height())) {
                    m_texAInit = true;
                    LOG_INFO("Renderer: Initialized D3D11 texture A ({}x{})", frame->width(), frame->height());
                } else {
                    LOG_ERROR("Renderer: Failed to initialize D3D11 texture A ({}x{})", frame->width(), frame->height());
                }
            }
            if (m_texAInit) {
                m_textureA.update(m_context.Get(), *frame);
            }
        } else {
            static int nullCount = 0;
            if (++nullCount % 60 == 1) {
                LOG_WARN("Renderer: m_mediaSource->getFrame() returned nullptr");
            }
        }
        if (m_texAInit) {
            drawTexture(m_textureA, 1.0f, windowW, windowH);
        }
    }

    m_swapChain->Present(1, 0);
}

void Renderer::drawTexture(Texture& tex, float alpha, float windowW, float windowH) {
    if (!tex.getShaderResourceView() || alpha <= 0.0f) return;

    float videoW = static_cast<float>(tex.width());
    float videoH = static_cast<float>(tex.height());
    float videoAspect = videoW / videoH;
    float windowAspect = windowW / windowH;

    float scaleX = 1.0f;
    float scaleY = 1.0f;

    if (windowAspect > videoAspect) {
        scaleX = videoAspect / windowAspect;
    } else {
        scaleY = windowAspect / videoAspect;
    }

    Vertex quadVertices[] = {
        { { -scaleX,  scaleY, 0.0f }, { 0.0f, 0.0f } },
        { {  scaleX,  scaleY, 0.0f }, { 1.0f, 0.0f } },
        { { -scaleX, -scaleY, 0.0f }, { 0.0f, 1.0f } },
        { {  scaleX,  scaleY, 0.0f }, { 1.0f, 0.0f } },
        { {  scaleX, -scaleY, 0.0f }, { 1.0f, 1.0f } },
        { { -scaleX, -scaleY, 0.0f }, { 0.0f, 1.0f } }
    };

    D3D11_MAPPED_SUBRESOURCE mappedVB;
    if (SUCCEEDED(m_context->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedVB))) {
        memcpy(mappedVB.pData, quadVertices, sizeof(quadVertices));
        m_context->Unmap(m_vertexBuffer.Get(), 0);
    }

    // Update Alpha Constant Buffer
    D3D11_MAPPED_SUBRESOURCE mappedCB;
    if (SUCCEEDED(m_context->Map(m_alphaConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedCB))) {
        AlphaBufferData data;
        data.alpha = std::clamp(alpha, 0.0f, 1.0f);
        memcpy(mappedCB.pData, &data, sizeof(data));
        m_context->Unmap(m_alphaConstantBuffer.Get(), 0);
    }

    UINT stride = sizeof(Vertex);
    UINT offset = 0;

    m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetInputLayout(m_inputLayout.Get());
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

    ID3D11ShaderResourceView* srv = tex.getShaderResourceView();
    m_context->PSSetShaderResources(0, 1, &srv);
    m_context->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());
    m_context->PSSetConstantBuffers(0, 1, m_alphaConstantBuffer.GetAddressOf());

    m_context->Draw(6, 0);
}
