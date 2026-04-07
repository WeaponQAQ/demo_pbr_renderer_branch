#pragma once

#include <glad/glad.h>
#include "rhi/rhi_resource.h"
#include "rhi/rhi_types.h"

#include <string>
#include <unordered_map>

// ============================================================
//  Format / enum mapping helpers
// ============================================================

GLenum toGLBufferTarget(BufferType type);
GLenum toGLBufferUsage(BufferAccess access);
GLenum toGLTextureTarget(TextureType type);
GLenum toGLWrap(WrapMode mode);
GLenum toGLFilter(FilterMode mode);
GLenum toGLPrimitive(PrimitiveType pt);
void   toGLFormats(TextureFormat fmt, GLenum& internalFmt, GLenum& pixelFmt, GLenum& dataType);

// ============================================================
//  GLBuffer
// ============================================================

class GLBuffer final : public RHIBuffer {
public:
    GLBuffer(const BufferDesc& desc);
    ~GLBuffer() override;

    void update(const void* data, size_t size) override;

    GLuint glId()     const { return id_; }
    GLenum glTarget() const { return target_; }

private:
    GLuint id_     = 0;
    GLenum target_ = 0;
    GLenum usage_  = 0;
};

// ============================================================
//  GLTexture
// ============================================================

class GLTexture final : public RHITexture {
public:
    GLTexture(const TextureDesc& desc);
    ~GLTexture() override;

    TextureType type() const override { return type_; }
    void upload(const void* data, int level = 0, int face = -1) override;
    void generateMipmaps() override;

    GLuint glId()     const { return id_; }
    GLenum glTarget() const { return target_; }

private:
    GLuint      id_     = 0;
    GLenum      target_ = 0;
    TextureType type_;
    GLenum      internalFmt_ = 0;
    GLenum      pixelFmt_    = 0;
    GLenum      dataType_    = 0;
    int         width_       = 0;
    int         height_      = 0;
};

// ============================================================
//  GLShader
// ============================================================

class GLShader final : public RHIShader {
public:
    GLShader(const ShaderDesc& desc);
    ~GLShader() override;

    void setInt(const std::string& name, int v) override;
    void setFloat(const std::string& name, float v) override;
    void setVec3(const std::string& name, const float* v) override;
    void setMat3(const std::string& name, const float* m) override;
    void setMat4(const std::string& name, const float* m) override;

    GLuint glId() const { return programId_; }

private:
    GLint loc(const std::string& name) const;
    static void checkCompileErrors(GLuint shader, const std::string& type);

    GLuint programId_ = 0;
    mutable std::unordered_map<std::string, GLint> locationCache_;
};

// ============================================================
//  GLFramebuffer
// ============================================================

class GLFramebuffer final : public RHIFramebuffer {
public:
    GLFramebuffer(const FramebufferDesc& desc);
    ~GLFramebuffer() override;

    void resizeDepth(int w, int h) override;

    GLuint glFBO() const { return fbo_; }
    GLuint glRBO() const { return rbo_; }

private:
    GLuint fbo_ = 0;
    GLuint rbo_ = 0;
};

// ============================================================
//  GLVertexInput
// ============================================================

class GLVertexInput final : public RHIVertexInput {
public:
    GLVertexInput(const VertexInputDesc& desc);
    ~GLVertexInput() override;

    GLuint glVAO() const { return vao_; }

private:
    GLuint vao_ = 0;
};

// ============================================================
//  GLTimerQuery
// ============================================================

class GLTimerQuery final : public RHITimerQuery {
public:
    GLTimerQuery();
    ~GLTimerQuery() override;

    GLuint currentQuery() const { return queries_[idx_]; }
    GLuint previousQuery() const { return queries_[1 - idx_]; }
    void   swap() { idx_ = 1 - idx_; }
    bool   ready() const { return ready_; }
    void   markReady() { ready_ = true; }

private:
    GLuint queries_[2] = {};
    int    idx_   = 0;
    bool   ready_ = false;
};
