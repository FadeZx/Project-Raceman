#pragma once

#include <memory>
#include <string>

namespace raceman {

// Windows-only bridge from the OpenGL HDR render target to an FP16 DXGI
// flip-model swapchain. It uses WGL_NV_DX_interop for a GPU-only transfer and
// cleanly declines activation when the driver does not expose that interop.
class WindowsHdrPresenter {
public:
    WindowsHdrPresenter();
    ~WindowsHdrPresenter();

    WindowsHdrPresenter(const WindowsHdrPresenter&) = delete;
    WindowsHdrPresenter& operator=(const WindowsHdrPresenter&) = delete;

    bool Initialize(void* nativeWindow, int width, int height);
    bool Present(unsigned int sourceGlTexture, int width, int height, bool vsync);
    void Shutdown();

    bool IsActive() const;
    const std::string& GetStatusMessage() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace raceman
