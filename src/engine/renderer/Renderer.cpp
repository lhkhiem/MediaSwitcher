#include "Renderer.h"
#include "common/logger/Logger.h"

#include <chrono>

static const char* g_shaderCode = R"(
struct VSInput {
    float3 pos : POSITION;
    float2 tex : TEXCOORD0;
};

struct PSInput {
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

PSInput VSMain(VSInput input) {
    PSInput output;
    output.pos = float4(input.pos, 1.0f);
    output.tex = input.tex;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET {
    return g_texture.Sample(g_sampler, input.tex);
}
)";

Renderer::Renderer()
    : m_running(false)
{
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

    if (initShadersAndBuffers()) {
        LOG_INFO("Renderer initialized with D3D11 device and textured quad pipeline.");
    } else {
        LOG_ERROR("Renderer failed to initialize shaders and buffers.");
    }
}

void Renderer::setMediaSource(std::shared_ptr<IMediaSource> source) {
    std::lock_guard<std::mutex> lock(m_renderMutex);
    m_mediaSource = source;
    m_textureInitialized = false;
}

bool Renderer::initShadersAndBuffers() {
    if (!m_device) return false;

    // Compile Vertex Shader
    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompile(g_shaderCode, strlen(g_shaderCode), "Shader", nullptr, nullptr, 
                            "VSMain", "vs_4_0", 0, 0, vsBlob.GetAddressOf(), errorBlob.GetAddressOf());
    if (FAILED(hr)) {
        if (errorBlob) {
            LOG_ERROR("VS Compilation Error: {}", static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        return false;
    }

    hr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vertexShader.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return false;

    // Compile Pixel Shader
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    hr = D3DCompile(g_shaderCode, strlen(g_shaderCode), "Shader", nullptr, nullptr, 
                            "PSMain", "ps_4_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf());
    if (FAILED(hr)) {
        if (errorBlob) {
            LOG_ERROR("PS Compilation Error: {}", static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        return false;
    }

    hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_pixelShader.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return false;

    // Create Input Layout
    D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    hr = m_device->CreateInputLayout(layoutDesc, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_inputLayout.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return false;

    // Create Quad Vertex Buffer
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

    // Create Sampler State (Anisotropic filtering)
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxAnisotropy = 16;

    hr = m_device->CreateSamplerState(&samplerDesc, m_samplerState.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return false;

    // Create Rasterizer State (No culling)
    D3D11_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.FrontCounterClockwise = FALSE;
    rasterDesc.DepthClipEnable = TRUE;

    hr = m_device->CreateRasterizerState(&rasterDesc, m_rasterizerState.ReleaseAndGetAddressOf());
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

    // Set Viewport to match swap chain backbuffer size
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

    // Clear background
    float clearColor[4] = { 0.08f, 0.08f, 0.1f, 1.0f };
    m_context->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);
    m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), nullptr);

    if (m_rasterizerState) {
        m_context->RSSetState(m_rasterizerState.Get());
    }

    // Process media source frame if attached
    if (m_mediaSource) {
        auto frame = m_mediaSource->getFrame();
        if (frame) {
            if (!m_textureInitialized || m_texture.width() != frame->width() || m_texture.height() != frame->height()) {
                if (m_texture.initialize(m_device.Get(), frame->width(), frame->height())) {
                    m_textureInitialized = true;
                }
            }

            if (m_textureInitialized) {
                m_texture.update(m_context.Get(), *frame);
            }
        }
    }

    // Draw textured quad if texture is valid
    if (m_textureInitialized && m_texture.getShaderResourceView()) {
        float videoW = static_cast<float>(m_texture.width());
        float videoH = static_cast<float>(m_texture.height());

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

        UINT stride = sizeof(Vertex);
        UINT offset = 0;

        m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
        m_context->IASetInputLayout(m_inputLayout.Get());
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
        m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

        ID3D11ShaderResourceView* srv = m_texture.getShaderResourceView();
        m_context->PSSetShaderResources(0, 1, &srv);
        m_context->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());

        m_context->Draw(6, 0);
    }

    m_swapChain->Present(1, 0);
}
