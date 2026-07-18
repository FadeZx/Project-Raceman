#include "WindowsHdrPresenter.h"

#if defined(_WIN32)

#include <glad/glad.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <sstream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

namespace raceman {

using Microsoft::WRL::ComPtr;

namespace {

constexpr GLenum WglAccessWriteDiscardNv = 0x0002;

using WglDxOpenDeviceNv = HANDLE(WINAPI*)(void* dxDevice);
using WglDxCloseDeviceNv = BOOL(WINAPI*)(HANDLE interopDevice);
using WglDxRegisterObjectNv = HANDLE(WINAPI*)(HANDLE interopDevice, void* dxObject,
    GLuint glName, GLenum glType, GLenum access);
using WglDxUnregisterObjectNv = BOOL(WINAPI*)(HANDLE interopDevice, HANDLE interopObject);
using WglDxLockObjectsNv = BOOL(WINAPI*)(HANDLE interopDevice, GLint count, HANDLE* objects);
using WglDxUnlockObjectsNv = BOOL(WINAPI*)(HANDLE interopDevice, GLint count, HANDLE* objects);

template <typename T>
T LoadWglFunction(const char* name) {
    return reinterpret_cast<T>(wglGetProcAddress(name));
}

std::string HresultMessage(const char* operation, HRESULT result) {
    std::ostringstream stream;
    stream << operation << " failed (0x" << std::hex << static_cast<unsigned long>(result) << ")";
    return stream.str();
}

} // namespace

struct WindowsHdrPresenter::Impl {
    HWND hwnd{nullptr};
    int width{0};
    int height{0};
    bool active{false};
    bool initializationAttempted{false};
    std::string status{"Native HDR presenter not initialized"};

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain4> swapChain;
    ComPtr<ID3D11RenderTargetView> backBufferView;
    ComPtr<ID3D11Texture2D> sharedTexture;
    ComPtr<ID3D11ShaderResourceView> sharedTextureView;
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> pixelShader;
    ComPtr<ID3D11SamplerState> sampler;

    WglDxOpenDeviceNv openInteropDevice{nullptr};
    WglDxCloseDeviceNv closeInteropDevice{nullptr};
    WglDxRegisterObjectNv registerInteropObject{nullptr};
    WglDxUnregisterObjectNv unregisterInteropObject{nullptr};
    WglDxLockObjectsNv lockInteropObjects{nullptr};
    WglDxUnlockObjectsNv unlockInteropObjects{nullptr};
    HANDLE interopDevice{nullptr};
    HANDLE interopObject{nullptr};
    GLuint sharedGlTexture{0};

    void ReleaseSizeResources() {
        if (interopObject != nullptr && unregisterInteropObject && interopDevice != nullptr) {
            unregisterInteropObject(interopDevice, interopObject);
            interopObject = nullptr;
        }
        if (sharedGlTexture != 0) {
            glDeleteTextures(1, &sharedGlTexture);
            sharedGlTexture = 0;
        }
        sharedTextureView.Reset();
        sharedTexture.Reset();
        backBufferView.Reset();
    }

    void Shutdown() {
        if (context) {
            context->ClearState();
            context->Flush();
        }
        ReleaseSizeResources();
        if (interopDevice != nullptr && closeInteropDevice) {
            closeInteropDevice(interopDevice);
            interopDevice = nullptr;
        }
        sampler.Reset();
        pixelShader.Reset();
        vertexShader.Reset();
        swapChain.Reset();
        context.Reset();
        device.Reset();
        active = false;
    }

    bool CreateShaders() {
        static constexpr char shaderSource[] = R"(
Texture2D<float4> SourceTexture : register(t0);
SamplerState LinearClamp : register(s0);
struct VertexOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; };
VertexOutput VSMain(uint id : SV_VertexID) {
    VertexOutput output;
    float2 uv = float2((id << 1) & 2, id & 2);
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    output.uv = uv;
    return output;
}
float4 PSMain(VertexOutput input) : SV_Target {
    return SourceTexture.Sample(LinearClamp, float2(input.uv.x, 1.0 - input.uv.y));
}
)";
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        ComPtr<ID3DBlob> vertexBytecode;
        ComPtr<ID3DBlob> pixelBytecode;
        ComPtr<ID3DBlob> errors;
        HRESULT result = D3DCompile(shaderSource, sizeof(shaderSource) - 1, "RacemanHdrPresent",
            nullptr, nullptr, "VSMain", "vs_5_0", flags, 0, &vertexBytecode, &errors);
        if (FAILED(result)) {
            status = errors ? static_cast<const char*>(errors->GetBufferPointer()) : HresultMessage("HDR vertex shader", result);
            return false;
        }
        errors.Reset();
        result = D3DCompile(shaderSource, sizeof(shaderSource) - 1, "RacemanHdrPresent",
            nullptr, nullptr, "PSMain", "ps_5_0", flags, 0, &pixelBytecode, &errors);
        if (FAILED(result)) {
            status = errors ? static_cast<const char*>(errors->GetBufferPointer()) : HresultMessage("HDR pixel shader", result);
            return false;
        }
        result = device->CreateVertexShader(vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(),
            nullptr, &vertexShader);
        if (FAILED(result)) { status = HresultMessage("CreateVertexShader", result); return false; }
        result = device->CreatePixelShader(pixelBytecode->GetBufferPointer(), pixelBytecode->GetBufferSize(),
            nullptr, &pixelShader);
        if (FAILED(result)) { status = HresultMessage("CreatePixelShader", result); return false; }

        D3D11_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        result = device->CreateSamplerState(&samplerDesc, &sampler);
        if (FAILED(result)) { status = HresultMessage("CreateSamplerState", result); return false; }
        return true;
    }

    bool CreateSizeResources(int newWidth, int newHeight) {
        newWidth = (std::max)(1, newWidth);
        newHeight = (std::max)(1, newHeight);
        ReleaseSizeResources();
        if (width != newWidth || height != newHeight) {
            HRESULT result = swapChain->ResizeBuffers(0, static_cast<UINT>(newWidth),
                static_cast<UINT>(newHeight), DXGI_FORMAT_UNKNOWN, 0);
            if (FAILED(result)) { status = HresultMessage("ResizeBuffers", result); return false; }
        }
        width = newWidth;
        height = newHeight;

        ComPtr<ID3D11Texture2D> backBuffer;
        HRESULT result = swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(result)) { status = HresultMessage("GetBuffer", result); return false; }
        result = device->CreateRenderTargetView(backBuffer.Get(), nullptr, &backBufferView);
        if (FAILED(result)) { status = HresultMessage("CreateRenderTargetView", result); return false; }

        D3D11_TEXTURE2D_DESC textureDesc{};
        textureDesc.Width = static_cast<UINT>(width);
        textureDesc.Height = static_cast<UINT>(height);
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        textureDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        result = device->CreateTexture2D(&textureDesc, nullptr, &sharedTexture);
        if (FAILED(result)) { status = HresultMessage("Create shared HDR texture", result); return false; }
        result = device->CreateShaderResourceView(sharedTexture.Get(), nullptr, &sharedTextureView);
        if (FAILED(result)) { status = HresultMessage("CreateShaderResourceView", result); return false; }

        glGenTextures(1, &sharedGlTexture);
        glBindTexture(GL_TEXTURE_2D, sharedGlTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
        interopObject = registerInteropObject(interopDevice, sharedTexture.Get(), sharedGlTexture,
            GL_TEXTURE_2D, WglAccessWriteDiscardNv);
        if (interopObject == nullptr) {
            status = "wglDXRegisterObjectNV rejected the shared FP16 texture";
            return false;
        }
        return true;
    }
};

WindowsHdrPresenter::WindowsHdrPresenter() : impl_(std::make_unique<Impl>()) {}
WindowsHdrPresenter::~WindowsHdrPresenter() { Shutdown(); }

bool WindowsHdrPresenter::Initialize(void* nativeWindow, int width, int height) {
    if (impl_->active) return true;
    if (impl_->initializationAttempted) return false;
    impl_->initializationAttempted = true;
    impl_->hwnd = static_cast<HWND>(nativeWindow);
    if (impl_->hwnd == nullptr) { impl_->status = "No native window for HDR presentation"; return false; }

    impl_->openInteropDevice = LoadWglFunction<WglDxOpenDeviceNv>("wglDXOpenDeviceNV");
    impl_->closeInteropDevice = LoadWglFunction<WglDxCloseDeviceNv>("wglDXCloseDeviceNV");
    impl_->registerInteropObject = LoadWglFunction<WglDxRegisterObjectNv>("wglDXRegisterObjectNV");
    impl_->unregisterInteropObject = LoadWglFunction<WglDxUnregisterObjectNv>("wglDXUnregisterObjectNV");
    impl_->lockInteropObjects = LoadWglFunction<WglDxLockObjectsNv>("wglDXLockObjectsNV");
    impl_->unlockInteropObjects = LoadWglFunction<WglDxUnlockObjectsNv>("wglDXUnlockObjectsNV");
    if (!impl_->openInteropDevice || !impl_->closeInteropDevice || !impl_->registerInteropObject ||
        !impl_->unregisterInteropObject || !impl_->lockInteropObjects || !impl_->unlockInteropObjects) {
        impl_->status = "GPU driver does not support WGL_NV_DX_interop";
        return false;
    }

    D3D_FEATURE_LEVEL requestedLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL featureLevel{};
    HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, requestedLevels, 2, D3D11_SDK_VERSION,
        &impl_->device, &featureLevel, &impl_->context);
    if (result == E_INVALIDARG) {
        result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, &requestedLevels[1], 1, D3D11_SDK_VERSION,
            &impl_->device, &featureLevel, &impl_->context);
    }
    if (FAILED(result)) { impl_->status = HresultMessage("D3D11CreateDevice", result); return false; }

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    result = impl_->device.As(&dxgiDevice);
    if (SUCCEEDED(result)) result = dxgiDevice->GetAdapter(&adapter);
    if (SUCCEEDED(result)) result = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (FAILED(result)) { impl_->status = HresultMessage("Acquire DXGI factory", result); return false; }

    DXGI_SWAP_CHAIN_DESC1 swapDesc{};
    swapDesc.Width = static_cast<UINT>((std::max)(1, width));
    swapDesc.Height = static_cast<UINT>((std::max)(1, height));
    swapDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = 2;
    swapDesc.Scaling = DXGI_SCALING_STRETCH;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
    result = factory->CreateSwapChainForHwnd(impl_->device.Get(), impl_->hwnd, &swapDesc,
        nullptr, nullptr, &swapChain1);
    if (FAILED(result)) { impl_->status = HresultMessage("Create FP16 HDR swapchain", result); return false; }
    result = swapChain1.As(&impl_->swapChain);
    if (FAILED(result)) { impl_->status = HresultMessage("Acquire IDXGISwapChain4", result); return false; }
    UINT colorSpaceSupport = 0;
    result = impl_->swapChain->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709,
        &colorSpaceSupport);
    if (FAILED(result) || (colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0) {
        impl_->status = "DXGI does not support FP16 scRGB presentation on this display";
        return false;
    }
    result = impl_->swapChain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
    if (FAILED(result)) { impl_->status = HresultMessage("Set scRGB colorspace", result); return false; }

    if (!impl_->CreateShaders()) return false;
    impl_->interopDevice = impl_->openInteropDevice(impl_->device.Get());
    if (impl_->interopDevice == nullptr) {
        impl_->status = "wglDXOpenDeviceNV could not connect OpenGL to D3D11";
        return false;
    }
    impl_->width = 0;
    impl_->height = 0;
    if (!impl_->CreateSizeResources(width, height)) { impl_->Shutdown(); return false; }
    impl_->active = true;
    impl_->status = "Native FP16 scRGB DXGI presenter active";
    return true;
}

bool WindowsHdrPresenter::Present(unsigned int sourceGlTexture, int width, int height, bool vsync) {
    if (!impl_->active || sourceGlTexture == 0) return false;
    if (width != impl_->width || height != impl_->height) {
        if (!impl_->CreateSizeResources(width, height)) { impl_->Shutdown(); return false; }
    }

    if (!impl_->lockInteropObjects(impl_->interopDevice, 1, &impl_->interopObject)) {
        impl_->status = "wglDXLockObjectsNV failed";
        impl_->Shutdown();
        return false;
    }
    glCopyImageSubData(sourceGlTexture, GL_TEXTURE_2D, 0, 0, 0, 0,
        impl_->sharedGlTexture, GL_TEXTURE_2D, 0, 0, 0, 0,
        impl_->width, impl_->height, 1);
    glFlush();
    if (!impl_->unlockInteropObjects(impl_->interopDevice, 1, &impl_->interopObject)) {
        impl_->status = "wglDXUnlockObjectsNV failed";
        impl_->Shutdown();
        return false;
    }

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(impl_->width);
    viewport.Height = static_cast<float>(impl_->height);
    viewport.MaxDepth = 1.0f;
    impl_->context->RSSetViewports(1, &viewport);
    ID3D11RenderTargetView* renderTarget = impl_->backBufferView.Get();
    impl_->context->OMSetRenderTargets(1, &renderTarget, nullptr);
    impl_->context->IASetInputLayout(nullptr);
    impl_->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    impl_->context->VSSetShader(impl_->vertexShader.Get(), nullptr, 0);
    impl_->context->PSSetShader(impl_->pixelShader.Get(), nullptr, 0);
    ID3D11ShaderResourceView* sourceView = impl_->sharedTextureView.Get();
    ID3D11SamplerState* sourceSampler = impl_->sampler.Get();
    impl_->context->PSSetShaderResources(0, 1, &sourceView);
    impl_->context->PSSetSamplers(0, 1, &sourceSampler);
    impl_->context->Draw(3, 0);
    ID3D11ShaderResourceView* nullView = nullptr;
    impl_->context->PSSetShaderResources(0, 1, &nullView);
    const HRESULT result = impl_->swapChain->Present(vsync ? 1 : 0, 0);
    if (FAILED(result)) {
        impl_->status = HresultMessage("HDR Present", result);
        impl_->Shutdown();
        return false;
    }
    return true;
}

void WindowsHdrPresenter::Shutdown() {
    if (impl_) {
        impl_->Shutdown();
        impl_->initializationAttempted = false;
        impl_->status = "Native HDR presenter stopped";
    }
}

bool WindowsHdrPresenter::IsActive() const { return impl_ && impl_->active; }
const std::string& WindowsHdrPresenter::GetStatusMessage() const { return impl_->status; }

} // namespace raceman

#else

namespace raceman {
struct WindowsHdrPresenter::Impl { std::string status{"Native HDR presentation is Windows-only"}; };
WindowsHdrPresenter::WindowsHdrPresenter() : impl_(std::make_unique<Impl>()) {}
WindowsHdrPresenter::~WindowsHdrPresenter() = default;
bool WindowsHdrPresenter::Initialize(void*, int, int) { return false; }
bool WindowsHdrPresenter::Present(unsigned int, int, int, bool) { return false; }
void WindowsHdrPresenter::Shutdown() {}
bool WindowsHdrPresenter::IsActive() const { return false; }
const std::string& WindowsHdrPresenter::GetStatusMessage() const { return impl_->status; }
} // namespace raceman

#endif
