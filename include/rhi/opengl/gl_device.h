#pragma once

#include "rhi/rhi_device.h"
#include <memory>

class GLContext;

class GLDevice final : public RHIDevice {
public:
    GLDevice();
    ~GLDevice() override;

    std::unique_ptr<RHIBuffer>      createBuffer(const BufferDesc&) override;
    std::unique_ptr<RHITexture>     createTexture(const TextureDesc&) override;
    std::unique_ptr<RHIShader>      createShader(const ShaderDesc&) override;
    std::unique_ptr<RHIFramebuffer> createFramebuffer(const FramebufferDesc&) override;
    std::unique_ptr<RHIVertexInput> createVertexInput(const VertexInputDesc&) override;
    std::unique_ptr<RHITimerQuery>  createTimerQuery() override;
    std::unique_ptr<RHICommandList> createCommandList() override;

    RHIContext* context() override;

private:
    std::unique_ptr<GLContext> ctx_;
};
