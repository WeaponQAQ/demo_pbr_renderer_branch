#include "pbr_renderer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <filesystem>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// ============================================================
//  Shared types
// ============================================================

struct InstanceData {
    glm::mat4 model;
    glm::vec4 material;  // (metallic, roughness, 0, 0)
};

// ============================================================
//  Lifecycle
// ============================================================

PBRRenderer::PBRRenderer() = default;

PBRRenderer::~PBRRenderer() = default;

void PBRRenderer::releaseIBL()
{
    envCubemap_.reset();
    irradianceMap_.reset();
    prefilterMap_.reset();
    brdfLUTTexture_.reset();
    iblInitialized_ = false;
    currentHDRPath_.clear();
}

static std::string readFile(const std::string& path)
{
    std::ifstream f;
    f.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    try {
        f.open(path);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    } catch (std::ifstream::failure& e) {
        std::cerr << "ERROR::SHADER::FILE_NOT_SUCCESSFULLY_READ\n"
                  << "  path:   " << std::filesystem::absolute(path) << "\n"
                  << "  CWD:    " << std::filesystem::current_path() << "\n"
                  << "  reason: " << e.what() << std::endl;
        return {};
    }
}

bool PBRRenderer::init(RHIDevice* device, const std::string& shaderDir)
{
    device_ = device;
    ctx_    = device_->context();

    auto loadShader = [&](const std::string& name) -> std::unique_ptr<RHIShader> {
        ShaderDesc desc;
        desc.vertexSrc   = readFile(shaderDir + "/" + name + ".vert");
        desc.fragmentSrc = readFile(shaderDir + "/" + name + ".frag");
        return device_->createShader(desc);
    };

    pbrShader_               = loadShader("pbr");
    equirectToCubemapShader_ = loadShader("equirectangular_to_cubemap");
    irradianceShader_        = loadShader("irradiance");
    prefilterShader_         = loadShader("prefilter");
    brdfShader_              = loadShader("brdf");
    backgroundShader_        = loadShader("background");

    ctx_->bindShader(pbrShader_.get());
    pbrShader_->setInt("irradianceMap", 0);
    pbrShader_->setInt("prefilterMap",  1);
    pbrShader_->setInt("brdfLUT",       2);
    pbrShader_->setInt("albedoMap",     3);
    pbrShader_->setInt("normalMap",     4);
    pbrShader_->setInt("metallicMap",   5);
    pbrShader_->setInt("roughnessMap",  6);
    pbrShader_->setInt("aoMap",         7);

    ctx_->bindShader(backgroundShader_.get());
    backgroundShader_->setInt("environmentMap", 0);

    setupGeometry();

    timerQuery_ = device_->createTimerQuery();
    captureFBO_ = device_->createFramebuffer({ 0, 0, true });

    return true;
}

void PBRRenderer::resize(int width, int height)
{
    screenWidth_  = width;
    screenHeight_ = height;
}

// ============================================================
//  IBL
// ============================================================

void PBRRenderer::setupIBL(const std::string& hdrPath)
{
    RHITexture* hdrTex = loadHDRTexture(hdrPath.c_str());
    if (!hdrTex) {
        std::cerr << "[PBRRenderer] Failed to load HDR: " << hdrPath << std::endl;
        return;
    }
    generateIBLMaps(hdrTex);

    // The HDR texture was stored in loadedTextures_; remove it to free memory
    if (!loadedTextures_.empty()) loadedTextures_.pop_back();

    iblInitialized_ = true;
    currentHDRPath_ = hdrPath;
    std::cout << "[PBRRenderer] IBL loaded: " << hdrPath << std::endl;
}

void PBRRenderer::reloadIBL(const std::string& hdrPath)
{
    if (hdrPath == currentHDRPath_ && iblInitialized_)
        return;
    releaseIBL();
    setupIBL(hdrPath);
}

void PBRRenderer::generateIBLMaps(RHITexture* hdrTexture)
{
    glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    glm::mat4 captureViews[] = {
        glm::lookAt(glm::vec3(0), glm::vec3( 1, 0, 0), glm::vec3(0,-1, 0)),
        glm::lookAt(glm::vec3(0), glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0)),
        glm::lookAt(glm::vec3(0), glm::vec3( 0, 1, 0), glm::vec3(0, 0, 1)),
        glm::lookAt(glm::vec3(0), glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1)),
        glm::lookAt(glm::vec3(0), glm::vec3( 0, 0, 1), glm::vec3(0,-1, 0)),
        glm::lookAt(glm::vec3(0), glm::vec3( 0, 0,-1), glm::vec3(0,-1, 0))
    };

    auto renderCubemapFaces = [&](RHIShader* shader, RHITexture* cubemap,
                                  int size, int mipLevel = 0) {
        ctx_->setViewport(0, 0, size, size);
        for (int i = 0; i < 6; ++i) {
            shader->setMat4("view", glm::value_ptr(captureViews[i]));
            ctx_->attachCubeFace(captureFBO_.get(), cubemap, i, mipLevel);
            ctx_->clear(0, 0, 0, 1, 1.0f);
            renderCubeGeometry();
        }
    };

    // --- Environment cubemap (512x512) ---
    constexpr int ENV_SIZE = 512;
    {
        TextureDesc desc;
        desc.type      = TextureType::TextureCubeMap;
        desc.format    = TextureFormat::RGB16F;
        desc.width     = ENV_SIZE;
        desc.height    = ENV_SIZE;
        desc.wrapS     = WrapMode::ClampToEdge;
        desc.wrapT     = WrapMode::ClampToEdge;
        desc.wrapR     = WrapMode::ClampToEdge;
        desc.minFilter = FilterMode::LinearMipLinear;
        desc.magFilter = FilterMode::Linear;
        envCubemap_ = device_->createTexture(desc);
    }

    ctx_->bindShader(equirectToCubemapShader_.get());
    equirectToCubemapShader_->setInt("equirectangularMap", 0);
    equirectToCubemapShader_->setMat4("projection", glm::value_ptr(captureProjection));
    ctx_->bindTexture(0, hdrTexture);

    ctx_->beginPass(captureFBO_.get());
    renderCubemapFaces(equirectToCubemapShader_.get(), envCubemap_.get(), ENV_SIZE);
    ctx_->endPass();

    envCubemap_->generateMipmaps();

    // --- Irradiance map (32x32) ---
    constexpr int IRR_SIZE = 32;
    {
        TextureDesc desc;
        desc.type      = TextureType::TextureCubeMap;
        desc.format    = TextureFormat::RGB16F;
        desc.width     = IRR_SIZE;
        desc.height    = IRR_SIZE;
        desc.wrapS     = WrapMode::ClampToEdge;
        desc.wrapT     = WrapMode::ClampToEdge;
        desc.wrapR     = WrapMode::ClampToEdge;
        desc.minFilter = FilterMode::Linear;
        desc.magFilter = FilterMode::Linear;
        irradianceMap_ = device_->createTexture(desc);
    }

    captureFBO_->resizeDepth(IRR_SIZE, IRR_SIZE);

    ctx_->bindShader(irradianceShader_.get());
    irradianceShader_->setInt("environmentMap", 0);
    irradianceShader_->setMat4("projection", glm::value_ptr(captureProjection));
    ctx_->bindTexture(0, envCubemap_.get());

    ctx_->beginPass(captureFBO_.get());
    renderCubemapFaces(irradianceShader_.get(), irradianceMap_.get(), IRR_SIZE);
    ctx_->endPass();

    // --- Prefilter map (128x128, 5 mip levels) ---
    constexpr int PF_SIZE = 128;
    constexpr int PF_MIP_LEVELS = 5;
    {
        TextureDesc desc;
        desc.type      = TextureType::TextureCubeMap;
        desc.format    = TextureFormat::RGB16F;
        desc.width     = PF_SIZE;
        desc.height    = PF_SIZE;
        desc.mipmaps   = true;
        desc.wrapS     = WrapMode::ClampToEdge;
        desc.wrapT     = WrapMode::ClampToEdge;
        desc.wrapR     = WrapMode::ClampToEdge;
        desc.minFilter = FilterMode::LinearMipLinear;
        desc.magFilter = FilterMode::Linear;
        prefilterMap_ = device_->createTexture(desc);
    }

    ctx_->bindShader(prefilterShader_.get());
    prefilterShader_->setInt("environmentMap", 0);
    prefilterShader_->setMat4("projection", glm::value_ptr(captureProjection));
    ctx_->bindTexture(0, envCubemap_.get());

    ctx_->beginPass(captureFBO_.get());
    for (int mip = 0; mip < PF_MIP_LEVELS; ++mip) {
        int mipSize = static_cast<int>(PF_SIZE * std::pow(0.5, mip));
        captureFBO_->resizeDepth(mipSize, mipSize);

        float roughness = static_cast<float>(mip) / static_cast<float>(PF_MIP_LEVELS - 1);
        prefilterShader_->setFloat("roughness", roughness);
        renderCubemapFaces(prefilterShader_.get(), prefilterMap_.get(), mipSize, mip);
    }
    ctx_->endPass();

    // --- BRDF LUT (512x512) ---
    constexpr int BRDF_SIZE = 512;
    {
        TextureDesc desc;
        desc.type      = TextureType::Texture2D;
        desc.format    = TextureFormat::RG16F;
        desc.width     = BRDF_SIZE;
        desc.height    = BRDF_SIZE;
        desc.wrapS     = WrapMode::ClampToEdge;
        desc.wrapT     = WrapMode::ClampToEdge;
        desc.minFilter = FilterMode::Linear;
        desc.magFilter = FilterMode::Linear;
        brdfLUTTexture_ = device_->createTexture(desc);
    }

    captureFBO_->resizeDepth(BRDF_SIZE, BRDF_SIZE);

    ctx_->beginPass(captureFBO_.get());
    ctx_->attachTexture2D(captureFBO_.get(), brdfLUTTexture_.get(), 0);
    ctx_->setViewport(0, 0, BRDF_SIZE, BRDF_SIZE);
    ctx_->bindShader(brdfShader_.get());
    ctx_->clear(0, 0, 0, 1, 1.0f);
    renderQuadGeometry();
    ctx_->endPass();

    ctx_->setViewport(0, 0, screenWidth_, screenHeight_);
}

// ============================================================
//  Uniform helpers
// ============================================================

glm::mat4 PBRRenderer::projectionMatrix(const Camera& camera) const
{
    float aspect = static_cast<float>(screenWidth_) / static_cast<float>(screenHeight_);
    return glm::perspective(glm::radians(camera.Zoom), aspect, nearPlane_, farPlane_);
}

void PBRRenderer::setupCameraUniforms(const Camera& camera)
{
    glm::mat4 proj = projectionMatrix(camera);
    glm::mat4 view = camera.GetViewMatrix();
    pbrShader_->setMat4("projection", glm::value_ptr(proj));
    pbrShader_->setMat4("view", glm::value_ptr(view));
    pbrShader_->setVec3("camPos", glm::value_ptr(camera.Position));
}

void PBRRenderer::bindIBLTextures()
{
    ctx_->bindTexture(0, irradianceMap_.get());
    ctx_->bindTexture(1, prefilterMap_.get());
    ctx_->bindTexture(2, brdfLUTTexture_.get());
}

void PBRRenderer::setupLightUniforms(const std::vector<PointLight>& lights)
{
    int n = static_cast<int>(std::min(lights.size(), size_t(4)));
    pbrShader_->setInt("numLights", n);
    for (int i = 0; i < n; ++i) {
        std::string idx = std::to_string(i);
        pbrShader_->setVec3("lightPositions[" + idx + "]", glm::value_ptr(lights[i].position));
        pbrShader_->setVec3("lightColors[" + idx + "]",    glm::value_ptr(lights[i].color));
    }
}

void PBRRenderer::setupMaterialUniforms(const PBRMaterial& material)
{
    pbrShader_->setInt("useTextures", material.useTextures ? 1 : 0);

    if (material.useTextures) {
        if (material.albedoMap)    ctx_->bindTexture(3, material.albedoMap);
        if (material.normalMap)    ctx_->bindTexture(4, material.normalMap);
        if (material.metallicMap)  ctx_->bindTexture(5, material.metallicMap);
        if (material.roughnessMap) ctx_->bindTexture(6, material.roughnessMap);
        if (material.aoMap)        ctx_->bindTexture(7, material.aoMap);
    } else {
        pbrShader_->setVec3("albedoValue",    glm::value_ptr(material.albedo));
        pbrShader_->setFloat("metallicValue", material.metallic);
        pbrShader_->setFloat("roughnessValue", material.roughness);
        pbrShader_->setFloat("aoValue",       material.ao);
    }
}

// ============================================================
//  Frustum culling
// ============================================================

namespace {

struct Frustum { glm::vec4 planes[6]; };

Frustum extractFrustum(const glm::mat4& vp)
{
    Frustum f;
    auto row = [&](int r) {
        return glm::vec4(vp[0][r], vp[1][r], vp[2][r], vp[3][r]);
    };
    f.planes[0] = row(3) + row(0);
    f.planes[1] = row(3) - row(0);
    f.planes[2] = row(3) + row(1);
    f.planes[3] = row(3) - row(1);
    f.planes[4] = row(3) + row(2);
    f.planes[5] = row(3) - row(2);
    for (auto& p : f.planes)
        p /= glm::length(glm::vec3(p));
    return f;
}

bool sphereInFrustum(const Frustum& f, const glm::vec3& center, float radius)
{
    for (int i = 0; i < 6; ++i)
        if (glm::dot(glm::vec3(f.planes[i]), center) + f.planes[i].w < -radius)
            return false;
    return true;
}

constexpr float LOD_DISTANCES[] = { 25.0f, 60.0f };

int selectLOD(float dist)
{
    if (dist < LOD_DISTANCES[0]) return 0;
    if (dist < LOD_DISTANCES[1]) return 1;
    return 2;
}

} // anonymous namespace

// ============================================================
//  Rendering
// ============================================================

void PBRRenderer::renderScene(const Camera& camera, const PBRMaterial& material,
                               const std::vector<PointLight>& lights,
                               int gridRows, int gridCols, float gridSpacing,
                               bool renderGrid, bool useInstancing)
{
    gpuTimeMs_ = ctx_->getTimerResultMs(timerQuery_.get());

    ctx_->bindShader(pbrShader_.get());
    setupCameraUniforms(camera);
    setupMaterialUniforms(material);
    bindIBLTextures();
    setupLightUniforms(lights);

    if (!renderGrid) {
        visibleCount_ = culledCount_ = 0;
        for (int i = 0; i < NUM_LODS; ++i) lodCounts_[i] = 0;
        return;
    }

    ctx_->beginTimerQuery(timerQuery_.get());

    glm::mat4 vp = projectionMatrix(camera) * camera.GetViewMatrix();
    Frustum frustum = extractFrustum(vp);

    visibleCount_ = 0;
    culledCount_  = 0;
    for (int i = 0; i < NUM_LODS; ++i) lodCounts_[i] = 0;

    if (useInstancing) {
        std::vector<InstanceData> lodBuckets[NUM_LODS];

        for (int row = 0; row < gridRows; ++row) {
            float metallic = static_cast<float>(row) /
                             static_cast<float>(std::max(gridRows - 1, 1));
            for (int col = 0; col < gridCols; ++col) {
                glm::vec3 pos((col - gridCols / 2) * gridSpacing,
                              (row - gridRows / 2) * gridSpacing, 0.0f);

                if (!sphereInFrustum(frustum, pos, 1.0f)) {
                    ++culledCount_;
                    continue;
                }

                float roughness = glm::clamp(
                    static_cast<float>(col) / static_cast<float>(std::max(gridCols - 1, 1)),
                    0.05f, 1.0f);

                int lod = selectLOD(glm::length(camera.Position - pos));

                InstanceData inst;
                inst.model    = glm::translate(glm::mat4(1.0f), pos);
                inst.material = glm::vec4(metallic, roughness, 0.0f, 0.0f);
                lodBuckets[lod].push_back(inst);
            }
        }

        pbrShader_->setInt("useInstancing", 1);
        for (int lod = 0; lod < NUM_LODS; ++lod) {
            lodCounts_[lod] = static_cast<int>(lodBuckets[lod].size());
            visibleCount_ += lodCounts_[lod];
            if (lodBuckets[lod].empty()) continue;

            sphereLODs_[lod].instanceBuffer->update(
                lodBuckets[lod].data(),
                lodBuckets[lod].size() * sizeof(InstanceData));

            ctx_->bindVertexInput(sphereLODs_[lod].vertexInput.get());
            ctx_->drawIndexedInstanced(PrimitiveType::TriangleStrip,
                                       sphereLODs_[lod].indexCount,
                                       static_cast<int>(lodBuckets[lod].size()));
        }
        pbrShader_->setInt("useInstancing", 0);
    } else {
        pbrShader_->setInt("useInstancing", 0);
        for (int row = 0; row < gridRows; ++row) {
            float metallic = static_cast<float>(row) /
                             static_cast<float>(std::max(gridRows - 1, 1));
            if (!material.useTextures)
                pbrShader_->setFloat("metallicValue", metallic);

            for (int col = 0; col < gridCols; ++col) {
                glm::vec3 pos((col - gridCols / 2) * gridSpacing,
                              (row - gridRows / 2) * gridSpacing, 0.0f);

                if (!sphereInFrustum(frustum, pos, 1.0f)) {
                    ++culledCount_;
                    continue;
                }

                if (!material.useTextures)
                    pbrShader_->setFloat("roughnessValue",
                        glm::clamp(static_cast<float>(col) /
                                   static_cast<float>(std::max(gridCols - 1, 1)),
                                   0.05f, 1.0f));

                int lod = selectLOD(glm::length(camera.Position - pos));
                ++lodCounts_[lod];
                ++visibleCount_;

                glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);
                pbrShader_->setMat4("model", glm::value_ptr(model));
                glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(model)));
                pbrShader_->setMat3("normalMatrix", glm::value_ptr(normalMat));
                renderSphereGeometry(lod);
            }
        }
    }

    ctx_->endTimerQuery(timerQuery_.get());
}

void PBRRenderer::renderSingleSphere(const Camera& camera, const PBRMaterial& material,
                                      const std::vector<PointLight>& lights,
                                      const glm::mat4& modelMatrix)
{
    ctx_->bindShader(pbrShader_.get());
    pbrShader_->setInt("useInstancing", 0);
    setupCameraUniforms(camera);
    setupMaterialUniforms(material);
    bindIBLTextures();
    setupLightUniforms(lights);

    pbrShader_->setMat4("model", glm::value_ptr(modelMatrix));
    glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(modelMatrix)));
    pbrShader_->setMat3("normalMatrix", glm::value_ptr(normalMat));
    renderSphereGeometry();
}

void PBRRenderer::renderBackground(const Camera& camera)
{
    ctx_->bindShader(backgroundShader_.get());
    glm::mat4 proj = projectionMatrix(camera);
    glm::mat4 view = camera.GetViewMatrix();
    backgroundShader_->setMat4("projection", glm::value_ptr(proj));
    backgroundShader_->setMat4("view", glm::value_ptr(view));
    ctx_->bindTexture(0, envCubemap_.get());
    renderCubeGeometry();
}

// ============================================================
//  Geometry setup
// ============================================================

void PBRRenderer::setupGeometry()
{
    // --- Cube ---
    float cubeVerts[] = {
        -1.0f,-1.0f,-1.0f, 0.0f, 0.0f,-1.0f, 0.0f,0.0f,
         1.0f, 1.0f,-1.0f, 0.0f, 0.0f,-1.0f, 1.0f,1.0f,
         1.0f,-1.0f,-1.0f, 0.0f, 0.0f,-1.0f, 1.0f,0.0f,
         1.0f, 1.0f,-1.0f, 0.0f, 0.0f,-1.0f, 1.0f,1.0f,
        -1.0f,-1.0f,-1.0f, 0.0f, 0.0f,-1.0f, 0.0f,0.0f,
        -1.0f, 1.0f,-1.0f, 0.0f, 0.0f,-1.0f, 0.0f,1.0f,

        -1.0f,-1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,0.0f,
         1.0f,-1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,0.0f,
         1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,1.0f,
         1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,1.0f,
        -1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,1.0f,
        -1.0f,-1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,0.0f,

        -1.0f, 1.0f, 1.0f,-1.0f, 0.0f, 0.0f, 1.0f,0.0f,
        -1.0f, 1.0f,-1.0f,-1.0f, 0.0f, 0.0f, 1.0f,1.0f,
        -1.0f,-1.0f,-1.0f,-1.0f, 0.0f, 0.0f, 0.0f,1.0f,
        -1.0f,-1.0f,-1.0f,-1.0f, 0.0f, 0.0f, 0.0f,1.0f,
        -1.0f,-1.0f, 1.0f,-1.0f, 0.0f, 0.0f, 0.0f,0.0f,
        -1.0f, 1.0f, 1.0f,-1.0f, 0.0f, 0.0f, 1.0f,0.0f,

         1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,0.0f,
         1.0f,-1.0f,-1.0f, 1.0f, 0.0f, 0.0f, 0.0f,1.0f,
         1.0f, 1.0f,-1.0f, 1.0f, 0.0f, 0.0f, 1.0f,1.0f,
         1.0f,-1.0f,-1.0f, 1.0f, 0.0f, 0.0f, 0.0f,1.0f,
         1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,0.0f,
         1.0f,-1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f,0.0f,

        -1.0f,-1.0f,-1.0f, 0.0f,-1.0f, 0.0f, 0.0f,1.0f,
         1.0f,-1.0f,-1.0f, 0.0f,-1.0f, 0.0f, 1.0f,1.0f,
         1.0f,-1.0f, 1.0f, 0.0f,-1.0f, 0.0f, 1.0f,0.0f,
         1.0f,-1.0f, 1.0f, 0.0f,-1.0f, 0.0f, 1.0f,0.0f,
        -1.0f,-1.0f, 1.0f, 0.0f,-1.0f, 0.0f, 0.0f,0.0f,
        -1.0f,-1.0f,-1.0f, 0.0f,-1.0f, 0.0f, 0.0f,1.0f,

        -1.0f, 1.0f,-1.0f, 0.0f, 1.0f, 0.0f, 0.0f,1.0f,
        -1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f,0.0f,
         1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f,0.0f,
         1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f,0.0f,
         1.0f, 1.0f,-1.0f, 0.0f, 1.0f, 0.0f, 1.0f,1.0f,
        -1.0f, 1.0f,-1.0f, 0.0f, 1.0f, 0.0f, 0.0f,1.0f
    };

    constexpr size_t cubeStride = 8 * sizeof(float);
    cubeVBO_ = device_->createBuffer({
        BufferType::Vertex, BufferAccess::Static,
        sizeof(cubeVerts), cubeVerts
    });

    VertexInputDesc cubeVI;
    cubeVI.bindings    = { { cubeStride, 0 } };
    cubeVI.attributes  = {
        { 0, 3, 0, 0 },
        { 1, 3, 0, 3 * sizeof(float) },
        { 2, 2, 0, 6 * sizeof(float) }
    };
    cubeVI.vertexBuffers = { cubeVBO_.get() };
    cubeInput_ = device_->createVertexInput(cubeVI);

    // --- Sphere LOD meshes ---
    constexpr int LOD_SEGMENTS[NUM_LODS] = { 64, 32, 16 };
    for (int i = 0; i < NUM_LODS; ++i)
        createSphereMesh(sphereLODs_[i], LOD_SEGMENTS[i]);

    // --- Fullscreen quad ---
    float quadVerts[] = {
        -1.0f, 1.0f, 0.0f, 0.0f,1.0f,
        -1.0f,-1.0f, 0.0f, 0.0f,0.0f,
         1.0f, 1.0f, 0.0f, 1.0f,1.0f,
         1.0f,-1.0f, 0.0f, 1.0f,0.0f,
    };

    constexpr size_t quadStride = 5 * sizeof(float);
    quadVBO_ = device_->createBuffer({
        BufferType::Vertex, BufferAccess::Static,
        sizeof(quadVerts), quadVerts
    });

    VertexInputDesc quadVI;
    quadVI.bindings    = { { quadStride, 0 } };
    quadVI.attributes  = {
        { 0, 3, 0, 0 },
        { 1, 2, 0, 3 * sizeof(float) }
    };
    quadVI.vertexBuffers = { quadVBO_.get() };
    quadInput_ = device_->createVertexInput(quadVI);
}

void PBRRenderer::renderCubeGeometry()
{
    ctx_->bindVertexInput(cubeInput_.get());
    ctx_->draw(PrimitiveType::Triangles, 36);
}

void PBRRenderer::renderSphereGeometry(int lod)
{
    ctx_->bindVertexInput(sphereLODs_[lod].vertexInput.get());
    ctx_->drawIndexed(PrimitiveType::TriangleStrip, sphereLODs_[lod].indexCount);
}

void PBRRenderer::renderQuadGeometry()
{
    ctx_->bindVertexInput(quadInput_.get());
    ctx_->draw(PrimitiveType::TriangleStrip, 4);
}

// ============================================================
//  Sphere mesh creation
// ============================================================

void PBRRenderer::createSphereMesh(SphereMesh& mesh, int segments)
{
    constexpr float PI = 3.14159265359f;
    unsigned int xSeg = static_cast<unsigned int>(segments);
    unsigned int ySeg = xSeg;

    std::vector<float> data;
    data.reserve((xSeg + 1) * (ySeg + 1) * 8);
    for (unsigned int x = 0; x <= xSeg; ++x) {
        for (unsigned int y = 0; y <= ySeg; ++y) {
            float xf = static_cast<float>(x) / xSeg;
            float yf = static_cast<float>(y) / ySeg;
            float px = std::cos(xf * 2.0f * PI) * std::sin(yf * PI);
            float py = std::cos(yf * PI);
            float pz = std::sin(xf * 2.0f * PI) * std::sin(yf * PI);
            data.insert(data.end(), { px, py, pz, px, py, pz, xf, yf });
        }
    }

    std::vector<unsigned int> indices;
    bool oddRow = false;
    for (unsigned int y = 0; y < ySeg; ++y) {
        for (unsigned int x = 0; x <= xSeg; ++x) {
            unsigned int cur  = oddRow ? (xSeg - x) : x;
            unsigned int row0 = y       * (xSeg + 1) + cur;
            unsigned int row1 = (y + 1) * (xSeg + 1) + cur;
            if (oddRow) { indices.push_back(row1); indices.push_back(row0); }
            else        { indices.push_back(row0); indices.push_back(row1); }
        }
        oddRow = !oddRow;
    }
    mesh.indexCount = static_cast<int>(indices.size());

    constexpr size_t vertStride = 8 * sizeof(float);

    mesh.vertexBuffer = device_->createBuffer({
        BufferType::Vertex, BufferAccess::Static,
        data.size() * sizeof(float), data.data()
    });

    mesh.indexBuffer = device_->createBuffer({
        BufferType::Index, BufferAccess::Static,
        indices.size() * sizeof(unsigned int), indices.data()
    });

    mesh.instanceBuffer = device_->createBuffer({
        BufferType::Instance, BufferAccess::Stream,
        sizeof(InstanceData), nullptr
    });

    VertexInputDesc vi;
    vi.bindings = {
        { vertStride, 0 },       // binding 0: per-vertex
        { sizeof(InstanceData), 1 } // binding 1: per-instance
    };
    vi.attributes = {
        // Per-vertex attributes
        { 0, 3, 0, 0 },
        { 1, 3, 0, 3 * sizeof(float) },
        { 2, 2, 0, 6 * sizeof(float) },
        // Per-instance: model matrix (4 vec4 columns)
        { 3, 4, 1, offsetof(InstanceData, model) + 0 * sizeof(glm::vec4) },
        { 4, 4, 1, offsetof(InstanceData, model) + 1 * sizeof(glm::vec4) },
        { 5, 4, 1, offsetof(InstanceData, model) + 2 * sizeof(glm::vec4) },
        { 6, 4, 1, offsetof(InstanceData, model) + 3 * sizeof(glm::vec4) },
        // Per-instance: material vec4
        { 7, 4, 1, offsetof(InstanceData, material) }
    };
    vi.vertexBuffers = { mesh.vertexBuffer.get(), mesh.instanceBuffer.get() };
    vi.indexBuffer   = mesh.indexBuffer.get();

    mesh.vertexInput = device_->createVertexInput(vi);
}

// ============================================================
//  Texture loading
// ============================================================

RHITexture* PBRRenderer::loadTexture(const char* path)
{
    int width, height, nrComponents;
    unsigned char* data = stbi_load(path, &width, &height, &nrComponents, 0);
    if (!data) {
        std::cerr << "[PBRRenderer] Texture load failed: " << path << std::endl;
        stbi_image_free(data);
        return nullptr;
    }

    TextureFormat fmt = TextureFormat::RGB8;
    if      (nrComponents == 1) fmt = TextureFormat::R8;
    else if (nrComponents == 3) fmt = TextureFormat::RGB8;
    else if (nrComponents == 4) fmt = TextureFormat::RGBA8;

    TextureDesc desc;
    desc.type      = TextureType::Texture2D;
    desc.format    = fmt;
    desc.width     = width;
    desc.height    = height;
    desc.mipmaps   = true;
    desc.minFilter = FilterMode::LinearMipLinear;
    desc.magFilter = FilterMode::Linear;
    desc.wrapS     = WrapMode::Repeat;
    desc.wrapT     = WrapMode::Repeat;

    auto tex = device_->createTexture(desc);
    tex->upload(data, 0, -1);
    tex->generateMipmaps();

    stbi_image_free(data);

    loadedTextures_.push_back(std::move(tex));
    return loadedTextures_.back().get();
}

RHITexture* PBRRenderer::loadHDRTexture(const char* path)
{
    stbi_set_flip_vertically_on_load(true);
    int width, height, nrComponents;
    float* data = stbi_loadf(path, &width, &height, &nrComponents, 0);
    if (!data) {
        std::cerr << "[PBRRenderer] HDR load failed: " << path << std::endl;
        return nullptr;
    }

    TextureDesc desc;
    desc.type      = TextureType::Texture2D;
    desc.format    = TextureFormat::RGB16F;
    desc.width     = width;
    desc.height    = height;
    desc.wrapS     = WrapMode::ClampToEdge;
    desc.wrapT     = WrapMode::ClampToEdge;
    desc.minFilter = FilterMode::Linear;
    desc.magFilter = FilterMode::Linear;

    auto tex = device_->createTexture(desc);
    tex->upload(data, 0, -1);

    stbi_image_free(data);

    loadedTextures_.push_back(std::move(tex));
    return loadedTextures_.back().get();
}
