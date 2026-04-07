#pragma once

#include "rhi_types.h"
#include <string>

class RHIBuffer {
public:
    virtual ~RHIBuffer() = default;
    virtual void update(const void* data, size_t size) = 0;
};

class RHITexture {
public:
    virtual ~RHITexture() = default;
    virtual TextureType type() const = 0;
    virtual void upload(const void* data, int level = 0, int face = -1) = 0;
    virtual void generateMipmaps() = 0;
};

class RHIShader {
public:
    virtual ~RHIShader() = default;
    virtual void setInt(const std::string& name, int v) = 0;
    virtual void setFloat(const std::string& name, float v) = 0;
    virtual void setVec3(const std::string& name, const float* v) = 0;
    virtual void setMat3(const std::string& name, const float* m) = 0;
    virtual void setMat4(const std::string& name, const float* m) = 0;
};

class RHIFramebuffer {
public:
    virtual ~RHIFramebuffer() = default;
    virtual void resizeDepth(int w, int h) = 0;
};

class RHIVertexInput {
public:
    virtual ~RHIVertexInput() = default;
};

class RHITimerQuery {
public:
    virtual ~RHITimerQuery() = default;
};
