#pragma once

#include "rhi_types.h"
#include <cstddef>

class RHIShader;
class RHITexture;
class RHIVertexInput;
class RHIFramebuffer;
class RHITimerQuery;

class RHICommandList {
public:
    virtual ~RHICommandList() = default;

    virtual void reset() = 0;

    // Pass management
    virtual void beginPass(RHIFramebuffer* fb) = 0;
    virtual void beginDefaultPass() = 0;
    virtual void endPass() = 0;

    // State
    virtual void setViewport(int x, int y, int w, int h) = 0;
    virtual void clear(float r, float g, float b, float a, float depth) = 0;
    virtual void clearColor(float r, float g, float b, float a) = 0;
    virtual void clearDepth(float depth = 1.0f) = 0;

    virtual void setDepthTest(bool enable, CompareFunc fn = CompareFunc::LessEqual) = 0;
    virtual void setDepthWrite(bool enable) = 0;
    virtual void setSeamlessCubemap(bool enable) = 0;
    virtual void setMultisample(bool enable) = 0;

    // Binding
    virtual void bindShader(RHIShader* shader) = 0;
    virtual void bindVertexInput(RHIVertexInput* vi) = 0;
    virtual void bindTexture(int slot, RHITexture* tex) = 0;

    // Draw
    virtual void draw(PrimitiveType pt, int count, int first = 0) = 0;
    virtual void drawIndexed(PrimitiveType pt, int indexCount) = 0;
    virtual void drawIndexedInstanced(PrimitiveType pt, int indexCount, int instances) = 0;

    // Framebuffer attachment
    virtual void attachTexture2D(RHIFramebuffer* fb, RHITexture* tex, int level = 0) = 0;
    virtual void attachCubeFace(RHIFramebuffer* fb, RHITexture* tex, int face, int level = 0) = 0;

    // Timer queries
    virtual void beginTimerQuery(RHITimerQuery* q) = 0;
    virtual void endTimerQuery(RHITimerQuery* q) = 0;

    // Uniform setting (recorded with captured shader pointer)
    virtual void setShaderInt(RHIShader* s, const std::string& name, int v) = 0;
    virtual void setShaderFloat(RHIShader* s, const std::string& name, float v) = 0;
    virtual void setShaderVec3(RHIShader* s, const std::string& name, const float* v) = 0;
    virtual void setShaderMat3(RHIShader* s, const std::string& name, const float* m) = 0;
    virtual void setShaderMat4(RHIShader* s, const std::string& name, const float* m) = 0;

    // Buffer data update
    virtual void updateBuffer(RHIBuffer* buf, const void* data, size_t size) = 0;

    // Framebuffer depth resize
    virtual void resizeFramebufferDepth(RHIFramebuffer* fb, int w, int h) = 0;
};
