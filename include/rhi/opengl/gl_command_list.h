#pragma once

#include "rhi/rhi_command_list.h"
#include <cstdint>
#include <cstring>
#include <variant>
#include <vector>
#include <string>

// ============================================================
//  Command variant types — each struct captures one RHI call
// ============================================================

namespace cmd {

struct BeginPass           { RHIFramebuffer* fb; };
struct BeginDefaultPass    {};
struct EndPass             {};

struct SetViewport         { int x, y, w, h; };
struct Clear               { float r, g, b, a, depth; };
struct ClearColor          { float r, g, b, a; };
struct ClearDepth          { float depth; };

struct SetDepthTest        { bool enable; CompareFunc fn; };
struct SetDepthWrite       { bool enable; };
struct SetSeamlessCubemap  { bool enable; };
struct SetMultisample      { bool enable; };

struct BindShader          { RHIShader* shader; };
struct BindVertexInput     { RHIVertexInput* vi; };
struct BindTexture         { int slot; RHITexture* tex; };

struct Draw                { PrimitiveType pt; int count; int first; };
struct DrawIndexed         { PrimitiveType pt; int indexCount; };
struct DrawIndexedInstanced { PrimitiveType pt; int indexCount; int instances; };

struct AttachTexture2D     { RHIFramebuffer* fb; RHITexture* tex; int level; };
struct AttachCubeFace      { RHIFramebuffer* fb; RHITexture* tex; int face; int level; };

struct BeginTimerQuery     { RHITimerQuery* q; };
struct EndTimerQuery       { RHITimerQuery* q; };

enum class UniformType : uint8_t { Int, Float, Vec3, Mat3, Mat4 };

struct SetShaderUniform {
    RHIShader*  s;
    std::string name;
    UniformType type;
    float       data[16];
};

struct UpdateBuffer        { RHIBuffer* buf; std::vector<char> data; };
struct ResizeFBDepth       { RHIFramebuffer* fb; int w; int h; };

} // namespace cmd

using RenderCommand = std::variant<
    cmd::BeginPass, cmd::BeginDefaultPass, cmd::EndPass,
    cmd::SetViewport, cmd::Clear, cmd::ClearColor, cmd::ClearDepth,
    cmd::SetDepthTest, cmd::SetDepthWrite, cmd::SetSeamlessCubemap, cmd::SetMultisample,
    cmd::BindShader, cmd::BindVertexInput, cmd::BindTexture,
    cmd::Draw, cmd::DrawIndexed, cmd::DrawIndexedInstanced,
    cmd::AttachTexture2D, cmd::AttachCubeFace,
    cmd::BeginTimerQuery, cmd::EndTimerQuery,
    cmd::SetShaderUniform,
    cmd::UpdateBuffer, cmd::ResizeFBDepth
>;

// ============================================================
//  GLCommandList — single-thread recording, main-thread replay
// ============================================================

class GLCommandList final : public RHICommandList {
public:
    GLCommandList() = default;
    ~GLCommandList() override = default;

    void reset() override;

    void beginPass(RHIFramebuffer* fb) override;
    void beginDefaultPass() override;
    void endPass() override;

    void setViewport(int x, int y, int w, int h) override;
    void clear(float r, float g, float b, float a, float depth) override;
    void clearColor(float r, float g, float b, float a) override;
    void clearDepth(float depth) override;

    void setDepthTest(bool enable, CompareFunc fn) override;
    void setDepthWrite(bool enable) override;
    void setSeamlessCubemap(bool enable) override;
    void setMultisample(bool enable) override;

    void bindShader(RHIShader* shader) override;
    void bindVertexInput(RHIVertexInput* vi) override;
    void bindTexture(int slot, RHITexture* tex) override;

    void draw(PrimitiveType pt, int count, int first) override;
    void drawIndexed(PrimitiveType pt, int indexCount) override;
    void drawIndexedInstanced(PrimitiveType pt, int indexCount, int instances) override;

    void attachTexture2D(RHIFramebuffer* fb, RHITexture* tex, int level) override;
    void attachCubeFace(RHIFramebuffer* fb, RHITexture* tex, int face, int level) override;

    void beginTimerQuery(RHITimerQuery* q) override;
    void endTimerQuery(RHITimerQuery* q) override;

    void setShaderInt(RHIShader* s, const std::string& name, int v) override;
    void setShaderFloat(RHIShader* s, const std::string& name, float v) override;
    void setShaderVec3(RHIShader* s, const std::string& name, const float* v) override;
    void setShaderMat3(RHIShader* s, const std::string& name, const float* m) override;
    void setShaderMat4(RHIShader* s, const std::string& name, const float* m) override;

    void updateBuffer(RHIBuffer* buf, const void* data, size_t size) override;
    void resizeFramebufferDepth(RHIFramebuffer* fb, int w, int h) override;

    const std::vector<RenderCommand>& commands() const { return commands_; }

private:
    std::vector<RenderCommand> commands_;
};
