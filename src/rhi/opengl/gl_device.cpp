#include "rhi/opengl/gl_device.h"
#include "rhi/opengl/gl_context.h"
#include "rhi/opengl/gl_command_list.h"
#include "rhi/opengl/gl_resources.h"

GLDevice::GLDevice()
    : ctx_(std::make_unique<GLContext>())
{
}

GLDevice::~GLDevice() = default;

std::unique_ptr<RHIBuffer> GLDevice::createBuffer(const BufferDesc& desc)
{
    return std::make_unique<GLBuffer>(desc);
}

std::unique_ptr<RHITexture> GLDevice::createTexture(const TextureDesc& desc)
{
    return std::make_unique<GLTexture>(desc);
}

std::unique_ptr<RHIShader> GLDevice::createShader(const ShaderDesc& desc)
{
    return std::make_unique<GLShader>(desc);
}

std::unique_ptr<RHIFramebuffer> GLDevice::createFramebuffer(const FramebufferDesc& desc)
{
    return std::make_unique<GLFramebuffer>(desc);
}

std::unique_ptr<RHIVertexInput> GLDevice::createVertexInput(const VertexInputDesc& desc)
{
    return std::make_unique<GLVertexInput>(desc);
}

std::unique_ptr<RHITimerQuery> GLDevice::createTimerQuery()
{
    return std::make_unique<GLTimerQuery>();
}

std::unique_ptr<RHICommandList> GLDevice::createCommandList()
{
    return std::make_unique<GLCommandList>();
}

RHIContext* GLDevice::context()
{
    return ctx_.get();
}

std::unique_ptr<RHIDevice> createOpenGLDevice()
{
    return std::make_unique<GLDevice>();
}
