#pragma once

#include "rhi_types.h"
#include "rhi_resource.h"
#include "rhi_context.h"
#include "rhi_command_list.h"
#include <memory>

class RHIDevice {
public:
    virtual ~RHIDevice() = default;

    virtual std::unique_ptr<RHIBuffer>      createBuffer(const BufferDesc&) = 0;
    virtual std::unique_ptr<RHITexture>     createTexture(const TextureDesc&) = 0;
    virtual std::unique_ptr<RHIShader>      createShader(const ShaderDesc&) = 0;
    virtual std::unique_ptr<RHIFramebuffer> createFramebuffer(const FramebufferDesc&) = 0;
    virtual std::unique_ptr<RHIVertexInput> createVertexInput(const VertexInputDesc&) = 0;
    virtual std::unique_ptr<RHITimerQuery>  createTimerQuery() = 0;
    virtual std::unique_ptr<RHICommandList> createCommandList() = 0;

    virtual RHIContext* context() = 0;
};

std::unique_ptr<RHIDevice> createOpenGLDevice();
