#include "rhi/opengl/gl_command_list.h"
#include <cstring>

void GLCommandList::reset() { commands_.clear(); }

// Pass
void GLCommandList::beginPass(RHIFramebuffer* fb)   { commands_.emplace_back(cmd::BeginPass{fb}); }
void GLCommandList::beginDefaultPass()               { commands_.emplace_back(cmd::BeginDefaultPass{}); }
void GLCommandList::endPass()                        { commands_.emplace_back(cmd::EndPass{}); }

// State
void GLCommandList::setViewport(int x, int y, int w, int h) { commands_.emplace_back(cmd::SetViewport{x,y,w,h}); }
void GLCommandList::clear(float r, float g, float b, float a, float depth) { commands_.emplace_back(cmd::Clear{r,g,b,a,depth}); }
void GLCommandList::clearColor(float r, float g, float b, float a) { commands_.emplace_back(cmd::ClearColor{r,g,b,a}); }
void GLCommandList::clearDepth(float depth) { commands_.emplace_back(cmd::ClearDepth{depth}); }

void GLCommandList::setDepthTest(bool enable, CompareFunc fn) { commands_.emplace_back(cmd::SetDepthTest{enable, fn}); }
void GLCommandList::setDepthWrite(bool enable) { commands_.emplace_back(cmd::SetDepthWrite{enable}); }
void GLCommandList::setSeamlessCubemap(bool enable) { commands_.emplace_back(cmd::SetSeamlessCubemap{enable}); }
void GLCommandList::setMultisample(bool enable) { commands_.emplace_back(cmd::SetMultisample{enable}); }

// Binding
void GLCommandList::bindShader(RHIShader* shader) { commands_.emplace_back(cmd::BindShader{shader}); }
void GLCommandList::bindVertexInput(RHIVertexInput* vi) { commands_.emplace_back(cmd::BindVertexInput{vi}); }
void GLCommandList::bindTexture(int slot, RHITexture* tex) { commands_.emplace_back(cmd::BindTexture{slot, tex}); }

// Draw
void GLCommandList::draw(PrimitiveType pt, int count, int first) { commands_.emplace_back(cmd::Draw{pt, count, first}); }
void GLCommandList::drawIndexed(PrimitiveType pt, int indexCount) { commands_.emplace_back(cmd::DrawIndexed{pt, indexCount}); }
void GLCommandList::drawIndexedInstanced(PrimitiveType pt, int indexCount, int instances) { commands_.emplace_back(cmd::DrawIndexedInstanced{pt, indexCount, instances}); }

// Attachment
void GLCommandList::attachTexture2D(RHIFramebuffer* fb, RHITexture* tex, int level) { commands_.emplace_back(cmd::AttachTexture2D{fb, tex, level}); }
void GLCommandList::attachCubeFace(RHIFramebuffer* fb, RHITexture* tex, int face, int level) { commands_.emplace_back(cmd::AttachCubeFace{fb, tex, face, level}); }

// Timer
void GLCommandList::beginTimerQuery(RHITimerQuery* q) { commands_.emplace_back(cmd::BeginTimerQuery{q}); }
void GLCommandList::endTimerQuery(RHITimerQuery* q) { commands_.emplace_back(cmd::EndTimerQuery{q}); }

// Uniforms
void GLCommandList::setShaderInt(RHIShader* s, const std::string& name, int v)
{
    cmd::SetShaderUniform u{s, name, cmd::UniformType::Int, {}};
    std::memcpy(u.data, &v, sizeof(v));
    commands_.emplace_back(std::move(u));
}
void GLCommandList::setShaderFloat(RHIShader* s, const std::string& name, float v)
{
    cmd::SetShaderUniform u{s, name, cmd::UniformType::Float, {}};
    u.data[0] = v;
    commands_.emplace_back(std::move(u));
}
void GLCommandList::setShaderVec3(RHIShader* s, const std::string& name, const float* v)
{
    cmd::SetShaderUniform u{s, name, cmd::UniformType::Vec3, {}};
    std::memcpy(u.data, v, 3 * sizeof(float));
    commands_.emplace_back(std::move(u));
}
void GLCommandList::setShaderMat3(RHIShader* s, const std::string& name, const float* m)
{
    cmd::SetShaderUniform u{s, name, cmd::UniformType::Mat3, {}};
    std::memcpy(u.data, m, 9 * sizeof(float));
    commands_.emplace_back(std::move(u));
}
void GLCommandList::setShaderMat4(RHIShader* s, const std::string& name, const float* m)
{
    cmd::SetShaderUniform u{s, name, cmd::UniformType::Mat4, {}};
    std::memcpy(u.data, m, 16 * sizeof(float));
    commands_.emplace_back(std::move(u));
}

// Buffer update — deep-copies data so the caller's memory is safe
void GLCommandList::updateBuffer(RHIBuffer* buf, const void* data, size_t size)
{
    std::vector<char> copy(size);
    std::memcpy(copy.data(), data, size);
    commands_.emplace_back(cmd::UpdateBuffer{buf, std::move(copy)});
}

// Framebuffer depth resize
void GLCommandList::resizeFramebufferDepth(RHIFramebuffer* fb, int w, int h)
{
    commands_.emplace_back(cmd::ResizeFBDepth{fb, w, h});
}
