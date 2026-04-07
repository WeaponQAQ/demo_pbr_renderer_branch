#pragma once

#include <string>
#include <vector>
#include <cstddef>

class RHIBuffer;

enum class BufferType   { Vertex, Index, Instance };
enum class BufferAccess { Static, Dynamic, Stream };

enum class TextureType   { Texture2D, TextureCubeMap };
enum class TextureFormat { R8, RGB8, RGBA8, RG16F, RGB16F };

enum class PrimitiveType { Triangles, TriangleStrip };
enum class CompareFunc   { Less, LessEqual, Always };
enum class WrapMode      { Repeat, ClampToEdge };
enum class FilterMode    { Nearest, Linear, LinearMipLinear };

struct BufferDesc {
    BufferType  type;
    BufferAccess access;
    size_t      size;
    const void* data = nullptr;
};

struct TextureDesc {
    TextureType   type      = TextureType::Texture2D;
    TextureFormat format    = TextureFormat::RGB8;
    int           width     = 1;
    int           height    = 1;
    bool          mipmaps   = false;
    WrapMode      wrapS     = WrapMode::Repeat;
    WrapMode      wrapT     = WrapMode::Repeat;
    WrapMode      wrapR     = WrapMode::ClampToEdge;
    FilterMode    minFilter = FilterMode::LinearMipLinear;
    FilterMode    magFilter = FilterMode::Linear;
};

struct ShaderDesc {
    std::string vertexSrc;
    std::string fragmentSrc;
};

struct FramebufferDesc {
    int  width    = 0;
    int  height   = 0;
    bool hasDepth = true;
};

struct VertexAttribute {
    int    location;
    int    components;
    int    bindingIndex;
    size_t offset;
};

struct VertexBinding {
    size_t stride;
    int    divisor = 0;
};

struct VertexInputDesc {
    std::vector<VertexBinding>   bindings;
    std::vector<VertexAttribute> attributes;
    std::vector<RHIBuffer*>      vertexBuffers;
    RHIBuffer*                   indexBuffer = nullptr;
};

struct ClearValue {
    float color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float depth    = 1.0f;
    bool  clearColor = true;
    bool  clearDepth = true;
};
