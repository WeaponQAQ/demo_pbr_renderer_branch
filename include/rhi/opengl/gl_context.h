#pragma once

#include "rhi/rhi_context.h"

class GLContext final : public RHIContext {
public:
    GLContext()  = default;
    ~GLContext() override = default;

    void submit(RHICommandList* cmdList) override;

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
    float getTimerResultMs(RHITimerQuery* q) override;
};
