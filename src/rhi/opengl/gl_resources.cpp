#include "rhi/opengl/gl_resources.h"
#include <iostream>

// ============================================================
//  Enum mapping helpers
// ============================================================

GLenum toGLBufferTarget(BufferType type)
{
    switch (type) {
        case BufferType::Vertex:   return GL_ARRAY_BUFFER;
        case BufferType::Index:    return GL_ELEMENT_ARRAY_BUFFER;
        case BufferType::Instance: return GL_ARRAY_BUFFER;
    }
    return GL_ARRAY_BUFFER;
}

GLenum toGLBufferUsage(BufferAccess access)
{
    switch (access) {
        case BufferAccess::Static:  return GL_STATIC_DRAW;
        case BufferAccess::Dynamic: return GL_DYNAMIC_DRAW;
        case BufferAccess::Stream:  return GL_STREAM_DRAW;
    }
    return GL_STATIC_DRAW;
}

GLenum toGLTextureTarget(TextureType type)
{
    switch (type) {
        case TextureType::Texture2D:     return GL_TEXTURE_2D;
        case TextureType::TextureCubeMap: return GL_TEXTURE_CUBE_MAP;
    }
    return GL_TEXTURE_2D;
}

GLenum toGLWrap(WrapMode mode)
{
    switch (mode) {
        case WrapMode::Repeat:      return GL_REPEAT;
        case WrapMode::ClampToEdge: return GL_CLAMP_TO_EDGE;
    }
    return GL_REPEAT;
}

GLenum toGLFilter(FilterMode mode)
{
    switch (mode) {
        case FilterMode::Nearest:        return GL_NEAREST;
        case FilterMode::Linear:         return GL_LINEAR;
        case FilterMode::LinearMipLinear: return GL_LINEAR_MIPMAP_LINEAR;
    }
    return GL_LINEAR;
}

GLenum toGLPrimitive(PrimitiveType pt)
{
    switch (pt) {
        case PrimitiveType::Triangles:     return GL_TRIANGLES;
        case PrimitiveType::TriangleStrip: return GL_TRIANGLE_STRIP;
    }
    return GL_TRIANGLES;
}

void toGLFormats(TextureFormat fmt, GLenum& internalFmt, GLenum& pixelFmt, GLenum& dataType)
{
    switch (fmt) {
        case TextureFormat::R8:
            internalFmt = GL_RED; pixelFmt = GL_RED; dataType = GL_UNSIGNED_BYTE; break;
        case TextureFormat::RGB8:
            internalFmt = GL_RGB; pixelFmt = GL_RGB; dataType = GL_UNSIGNED_BYTE; break;
        case TextureFormat::RGBA8:
            internalFmt = GL_RGBA; pixelFmt = GL_RGBA; dataType = GL_UNSIGNED_BYTE; break;
        case TextureFormat::RG16F:
            internalFmt = GL_RG16F; pixelFmt = GL_RG; dataType = GL_FLOAT; break;
        case TextureFormat::RGB16F:
            internalFmt = GL_RGB16F; pixelFmt = GL_RGB; dataType = GL_FLOAT; break;
    }
}

// ============================================================
//  GLBuffer
// ============================================================

GLBuffer::GLBuffer(const BufferDesc& desc)
    : target_(toGLBufferTarget(desc.type))
    , usage_(toGLBufferUsage(desc.access))
{
    glGenBuffers(1, &id_);
    glBindBuffer(target_, id_);
    glBufferData(target_, static_cast<GLsizeiptr>(desc.size), desc.data, usage_);
    glBindBuffer(target_, 0);
}

GLBuffer::~GLBuffer()
{
    if (id_) glDeleteBuffers(1, &id_);
}

void GLBuffer::update(const void* data, size_t size)
{
    glBindBuffer(target_, id_);
    glBufferData(target_, static_cast<GLsizeiptr>(size), data, usage_);
    glBindBuffer(target_, 0);
}

// ============================================================
//  GLTexture
// ============================================================

GLTexture::GLTexture(const TextureDesc& desc)
    : type_(desc.type)
    , target_(toGLTextureTarget(desc.type))
    , width_(desc.width)
    , height_(desc.height)
{
    toGLFormats(desc.format, internalFmt_, pixelFmt_, dataType_);

    glGenTextures(1, &id_);
    glBindTexture(target_, id_);

    if (desc.type == TextureType::TextureCubeMap) {
        for (int i = 0; i < 6; ++i)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0,
                         internalFmt_, width_, height_, 0,
                         pixelFmt_, dataType_, nullptr);
        glTexParameteri(target_, GL_TEXTURE_WRAP_R, toGLWrap(desc.wrapR));
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, internalFmt_, width_, height_, 0,
                     pixelFmt_, dataType_, nullptr);
    }

    glTexParameteri(target_, GL_TEXTURE_WRAP_S, toGLWrap(desc.wrapS));
    glTexParameteri(target_, GL_TEXTURE_WRAP_T, toGLWrap(desc.wrapT));
    glTexParameteri(target_, GL_TEXTURE_MIN_FILTER, toGLFilter(desc.minFilter));
    glTexParameteri(target_, GL_TEXTURE_MAG_FILTER, toGLFilter(desc.magFilter));

    if (desc.mipmaps)
        glGenerateMipmap(target_);

    glBindTexture(target_, 0);
}

GLTexture::~GLTexture()
{
    if (id_) glDeleteTextures(1, &id_);
}

void GLTexture::upload(const void* data, int level, int face)
{
    glBindTexture(target_, id_);
    if (type_ == TextureType::TextureCubeMap && face >= 0) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level,
                     internalFmt_, width_ >> level, height_ >> level, 0,
                     pixelFmt_, dataType_, data);
    } else {
        glTexImage2D(GL_TEXTURE_2D, level, internalFmt_,
                     width_ >> level, height_ >> level, 0,
                     pixelFmt_, dataType_, data);
    }
    glBindTexture(target_, 0);
}

void GLTexture::generateMipmaps()
{
    glBindTexture(target_, id_);
    glGenerateMipmap(target_);
    glBindTexture(target_, 0);
}

// ============================================================
//  GLShader
// ============================================================

GLShader::GLShader(const ShaderDesc& desc)
{
    const char* vSrc = desc.vertexSrc.c_str();
    const char* fSrc = desc.fragmentSrc.c_str();

    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vSrc, nullptr);
    glCompileShader(vertex);
    checkCompileErrors(vertex, "VERTEX");

    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fSrc, nullptr);
    glCompileShader(fragment);
    checkCompileErrors(fragment, "FRAGMENT");

    programId_ = glCreateProgram();
    glAttachShader(programId_, vertex);
    glAttachShader(programId_, fragment);
    glLinkProgram(programId_);
    checkCompileErrors(programId_, "PROGRAM");

    glDeleteShader(vertex);
    glDeleteShader(fragment);
}

GLShader::~GLShader()
{
    if (programId_) glDeleteProgram(programId_);
}

GLint GLShader::loc(const std::string& name) const
{
    auto it = locationCache_.find(name);
    if (it != locationCache_.end()) return it->second;
    GLint location = glGetUniformLocation(programId_, name.c_str());
    locationCache_[name] = location;
    return location;
}

void GLShader::setInt(const std::string& name, int v)
{
    glUniform1i(loc(name), v);
}

void GLShader::setFloat(const std::string& name, float v)
{
    glUniform1f(loc(name), v);
}

void GLShader::setVec3(const std::string& name, const float* v)
{
    glUniform3fv(loc(name), 1, v);
}

void GLShader::setMat3(const std::string& name, const float* m)
{
    glUniformMatrix3fv(loc(name), 1, GL_FALSE, m);
}

void GLShader::setMat4(const std::string& name, const float* m)
{
    glUniformMatrix4fv(loc(name), 1, GL_FALSE, m);
}

void GLShader::checkCompileErrors(GLuint shader, const std::string& type)
{
    GLint success;
    GLchar infoLog[1024];
    if (type != "PROGRAM") {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
            std::cerr << "ERROR::SHADER_COMPILATION (" << type << ")\n"
                      << infoLog << std::endl;
        }
    } else {
        glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(shader, 1024, nullptr, infoLog);
            std::cerr << "ERROR::PROGRAM_LINKING\n" << infoLog << std::endl;
        }
    }
}

// ============================================================
//  GLFramebuffer
// ============================================================

GLFramebuffer::GLFramebuffer(const FramebufferDesc& desc)
{
    glGenFramebuffers(1, &fbo_);
    if (desc.hasDepth) {
        glGenRenderbuffers(1, &rbo_);
        if (desc.width > 0 && desc.height > 0) {
            glBindRenderbuffer(GL_RENDERBUFFER, rbo_);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                                  desc.width, desc.height);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                      GL_RENDERBUFFER, rbo_);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }
}

GLFramebuffer::~GLFramebuffer()
{
    if (rbo_) glDeleteRenderbuffers(1, &rbo_);
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
}

void GLFramebuffer::resizeDepth(int w, int h)
{
    if (!rbo_) return;
    glBindRenderbuffer(GL_RENDERBUFFER, rbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
}

// ============================================================
//  GLVertexInput
// ============================================================

GLVertexInput::GLVertexInput(const VertexInputDesc& desc)
{
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    for (auto& attr : desc.attributes) {
        auto* buf = static_cast<GLBuffer*>(desc.vertexBuffers[attr.bindingIndex]);
        glBindBuffer(GL_ARRAY_BUFFER, buf->glId());

        const auto& binding = desc.bindings[attr.bindingIndex];
        glEnableVertexAttribArray(attr.location);
        glVertexAttribPointer(attr.location, attr.components, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(binding.stride),
                              reinterpret_cast<const void*>(attr.offset));
        if (binding.divisor > 0)
            glVertexAttribDivisor(attr.location, binding.divisor);
    }

    if (desc.indexBuffer) {
        auto* idxBuf = static_cast<GLBuffer*>(desc.indexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, idxBuf->glId());
    }

    glBindVertexArray(0);
}

GLVertexInput::~GLVertexInput()
{
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

// ============================================================
//  GLTimerQuery
// ============================================================

GLTimerQuery::GLTimerQuery()
{
    glGenQueries(2, queries_);
}

GLTimerQuery::~GLTimerQuery()
{
    if (queries_[0]) glDeleteQueries(2, queries_);
}
