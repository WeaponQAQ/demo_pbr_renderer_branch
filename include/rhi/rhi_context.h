#pragma once

#include "rhi_types.h"

class RHIShader;
class RHITexture;
class RHIVertexInput;
class RHIFramebuffer;
class RHITimerQuery;
class RHICommandList;

class RHIContext {
public:
    virtual ~RHIContext() = default;

    virtual void submit(RHICommandList* cmdList) = 0;

    virtual void beginPass(RHIFramebuffer* fb) = 0;
    virtual void beginDefaultPass() = 0;
    virtual void endPass() = 0;

    virtual void setViewport(int x, int y, int w, int h) = 0;
    virtual void clear(float r, float g, float b, float a, float depth) = 0;
    virtual void clearColor(float r, float g, float b, float a) = 0;
    virtual void clearDepth(float depth = 1.0f) = 0;

    virtual void setDepthTest(bool enable, CompareFunc fn = CompareFunc::LessEqual) = 0;
    virtual void setDepthWrite(bool enable) = 0;
    virtual void setSeamlessCubemap(bool enable) = 0;
    virtual void setMultisample(bool enable) = 0;

    virtual void bindShader(RHIShader* shader) = 0;
    virtual void bindVertexInput(RHIVertexInput* vi) = 0;
    virtual void bindTexture(int slot, RHITexture* tex) = 0;

    virtual void draw(PrimitiveType pt, int count, int first = 0) = 0;
    virtual void drawIndexed(PrimitiveType pt, int indexCount) = 0;
    virtual void drawIndexedInstanced(PrimitiveType pt, int indexCount, int instances) = 0;

    virtual void attachTexture2D(RHIFramebuffer* fb, RHITexture* tex, int level = 0) = 0;
    virtual void attachCubeFace(RHIFramebuffer* fb, RHITexture* tex, int face, int level = 0) = 0;

    virtual void beginTimerQuery(RHITimerQuery* q) = 0;
    virtual void endTimerQuery(RHITimerQuery* q) = 0;
    virtual float getTimerResultMs(RHITimerQuery* q) = 0;
};
