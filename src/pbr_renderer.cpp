#include "pbr_renderer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <vector>
#include <cmath>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// ============================================================
//  Lifecycle
// ============================================================

PBRRenderer::PBRRenderer() = default;

PBRRenderer::~PBRRenderer()
{
    if (cubeVAO_) { glDeleteVertexArrays(1, &cubeVAO_); glDeleteBuffers(1, &cubeVBO_); }
    for (auto& lod : sphereLODs_) {
        if (lod.vao) { glDeleteVertexArrays(1, &lod.vao); glDeleteBuffers(1, &lod.vbo); glDeleteBuffers(1, &lod.ebo); }
        if (lod.instanceVBO) glDeleteBuffers(1, &lod.instanceVBO);
    }
    if (quadVAO_) { glDeleteVertexArrays(1, &quadVAO_); glDeleteBuffers(1, &quadVBO_); }
    if (gpuTimerQueries_[0]) glDeleteQueries(2, gpuTimerQueries_);
    if (captureFBO_) { glDeleteFramebuffers(1, &captureFBO_); glDeleteRenderbuffers(1, &captureRBO_); }
    releaseIBL();
}

void PBRRenderer::releaseIBL()
{
    auto del = [](unsigned int& tex) { if (tex) { glDeleteTextures(1, &tex); tex = 0; } };
    del(envCubemap_);
    del(irradianceMap_);
    del(prefilterMap_);
    del(brdfLUTTexture_);
    iblInitialized_ = false;
    currentHDRPath_.clear();
}

bool PBRRenderer::init(const std::string& shaderDir)
{
    auto load = [&](const std::string& name) {
        return Shader((shaderDir + "/" + name + ".vert").c_str(),
                      (shaderDir + "/" + name + ".frag").c_str());
    };

    pbrShader_                = load("pbr");
    equirectToCubemapShader_  = load("equirectangular_to_cubemap");
    irradianceShader_         = load("irradiance");
    prefilterShader_          = load("prefilter");
    brdfShader_               = load("brdf");
    backgroundShader_         = load("background");

    pbrShader_.use();
    pbrShader_.setInt("irradianceMap", 0);
    pbrShader_.setInt("prefilterMap",  1);
    pbrShader_.setInt("brdfLUT",       2);
    pbrShader_.setInt("albedoMap",     3);
    pbrShader_.setInt("normalMap",     4);
    pbrShader_.setInt("metallicMap",   5);
    pbrShader_.setInt("roughnessMap",  6);
    pbrShader_.setInt("aoMap",         7);

    backgroundShader_.use();
    backgroundShader_.setInt("environmentMap", 0);

    setupGeometry();
    glGenQueries(2, gpuTimerQueries_);

    glGenFramebuffers(1, &captureFBO_);
    glGenRenderbuffers(1, &captureRBO_);

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
    unsigned int hdrTexture = loadHDRTexture(hdrPath.c_str());
    if (hdrTexture == 0) {
        std::cerr << "[PBRRenderer] Failed to load HDR: " << hdrPath << std::endl;
        return;
    }
    generateIBLMaps(hdrTexture);
    glDeleteTextures(1, &hdrTexture);
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

void PBRRenderer::generateIBLMaps(unsigned int hdrTexture)
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

    auto renderCubemapFaces = [&](Shader& shader, unsigned int cubemap,
                                  unsigned int size, int mipLevel = 0) {
        glViewport(0, 0, size, size);
        for (unsigned int i = 0; i < 6; ++i) {
            shader.setMat4("view", captureViews[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cubemap, mipLevel);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderCubeGeometry();
        }
    };

    // --- Environment cubemap (512x512) ---
    constexpr unsigned int ENV_SIZE = 512;
    glGenTextures(1, &envCubemap_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap_);
    for (unsigned int i = 0; i < 6; ++i)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                     ENV_SIZE, ENV_SIZE, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    equirectToCubemapShader_.use();
    equirectToCubemapShader_.setInt("equirectangularMap", 0);
    equirectToCubemapShader_.setMat4("projection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrTexture);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO_);
    renderCubemapFaces(equirectToCubemapShader_, envCubemap_, ENV_SIZE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap_);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    // --- Irradiance map (32x32) ---
    constexpr unsigned int IRR_SIZE = 32;
    glGenTextures(1, &irradianceMap_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap_);
    for (unsigned int i = 0; i < 6; ++i)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                     IRR_SIZE, IRR_SIZE, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO_);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, IRR_SIZE, IRR_SIZE);

    irradianceShader_.use();
    irradianceShader_.setInt("environmentMap", 0);
    irradianceShader_.setMat4("projection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap_);

    renderCubemapFaces(irradianceShader_, irradianceMap_, IRR_SIZE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // --- Prefilter map (128x128, 5 mip levels) ---
    constexpr unsigned int PF_SIZE = 128;
    constexpr unsigned int PF_MIP_LEVELS = 5;
    glGenTextures(1, &prefilterMap_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap_);
    for (unsigned int i = 0; i < 6; ++i)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                     PF_SIZE, PF_SIZE, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    prefilterShader_.use();
    prefilterShader_.setInt("environmentMap", 0);
    prefilterShader_.setMat4("projection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap_);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO_);
    for (unsigned int mip = 0; mip < PF_MIP_LEVELS; ++mip) {
        unsigned int mipSize = static_cast<unsigned int>(PF_SIZE * std::pow(0.5, mip));
        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipSize, mipSize);

        float roughness = static_cast<float>(mip) / static_cast<float>(PF_MIP_LEVELS - 1);
        prefilterShader_.setFloat("roughness", roughness);
        renderCubemapFaces(prefilterShader_, prefilterMap_, mipSize, mip);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // --- BRDF LUT (512x512) ---
    constexpr unsigned int BRDF_SIZE = 512;
    glGenTextures(1, &brdfLUTTexture_);
    glBindTexture(GL_TEXTURE_2D, brdfLUTTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, BRDF_SIZE, BRDF_SIZE, 0, GL_RG, GL_FLOAT, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO_);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, BRDF_SIZE, BRDF_SIZE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLUTTexture_, 0);

    glViewport(0, 0, BRDF_SIZE, BRDF_SIZE);
    brdfShader_.use();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderQuadGeometry();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, screenWidth_, screenHeight_);
}

// ============================================================
//  Uniform helpers (eliminate duplication)
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
    pbrShader_.setMat4("projection", proj);
    pbrShader_.setMat4("view", view);
    pbrShader_.setVec3("camPos", camera.Position);
}

void PBRRenderer::bindIBLTextures()
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap_);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, brdfLUTTexture_);
}

void PBRRenderer::setupLightUniforms(const std::vector<PointLight>& lights)
{
    int n = static_cast<int>(std::min(lights.size(), size_t(4)));
    pbrShader_.setInt("numLights", n);
    for (int i = 0; i < n; ++i) {
        std::string idx = std::to_string(i);
        pbrShader_.setVec3("lightPositions[" + idx + "]", lights[i].position);
        pbrShader_.setVec3("lightColors[" + idx + "]",    lights[i].color);
    }
}

void PBRRenderer::setupMaterialUniforms(const PBRMaterial& material)
{
    pbrShader_.setInt("useTextures", material.useTextures ? 1 : 0);

    if (material.useTextures) {
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, material.albedoMap);
        glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, material.normalMap);
        glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, material.metallicMap);
        glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D, material.roughnessMap);
        glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D, material.aoMap);
    } else {
        pbrShader_.setVec3("albedoValue",    material.albedo);
        pbrShader_.setFloat("metallicValue", material.metallic);
        pbrShader_.setFloat("roughnessValue", material.roughness);
        pbrShader_.setFloat("aoValue",       material.ao);
    }
}

// ============================================================
//  Shared types
// ============================================================

struct InstanceData {
    glm::mat4 model;
    glm::vec4 material;  // (metallic, roughness, 0, 0)
};

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
    f.planes[0] = row(3) + row(0);  // left
    f.planes[1] = row(3) - row(0);  // right
    f.planes[2] = row(3) + row(1);  // bottom
    f.planes[3] = row(3) - row(1);  // top
    f.planes[4] = row(3) + row(2);  // near
    f.planes[5] = row(3) - row(2);  // far
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
    if (gpuTimerReady_) {
        GLuint64 elapsed = 0;
        glGetQueryObjectui64v(gpuTimerQueries_[1 - gpuQueryIdx_],
                              GL_QUERY_RESULT, &elapsed);
        gpuTimeMs_ = static_cast<float>(elapsed) / 1e6f;
    }

    pbrShader_.use();
    setupCameraUniforms(camera);
    setupMaterialUniforms(material);
    bindIBLTextures();
    setupLightUniforms(lights);

    if (!renderGrid) {
        visibleCount_ = culledCount_ = 0;
        for (int i = 0; i < NUM_LODS; ++i) lodCounts_[i] = 0;
        return;
    }

    glBeginQuery(GL_TIME_ELAPSED, gpuTimerQueries_[gpuQueryIdx_]);

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

        pbrShader_.setInt("useInstancing", 1);
        for (int lod = 0; lod < NUM_LODS; ++lod) {
            lodCounts_[lod] = static_cast<int>(lodBuckets[lod].size());
            visibleCount_ += lodCounts_[lod];
            if (lodBuckets[lod].empty()) continue;

            auto& mesh = sphereLODs_[lod];
            glBindBuffer(GL_ARRAY_BUFFER, mesh.instanceVBO);
            glBufferData(GL_ARRAY_BUFFER,
                         lodBuckets[lod].size() * sizeof(InstanceData),
                         lodBuckets[lod].data(), GL_STREAM_DRAW);

            glBindVertexArray(mesh.vao);
            glDrawElementsInstanced(GL_TRIANGLE_STRIP, mesh.indexCount,
                                    GL_UNSIGNED_INT, 0,
                                    static_cast<int>(lodBuckets[lod].size()));
        }
        pbrShader_.setInt("useInstancing", 0);
    } else {
        pbrShader_.setInt("useInstancing", 0);
        for (int row = 0; row < gridRows; ++row) {
            float metallic = static_cast<float>(row) /
                             static_cast<float>(std::max(gridRows - 1, 1));
            if (!material.useTextures)
                pbrShader_.setFloat("metallicValue", metallic);

            for (int col = 0; col < gridCols; ++col) {
                glm::vec3 pos((col - gridCols / 2) * gridSpacing,
                              (row - gridRows / 2) * gridSpacing, 0.0f);

                if (!sphereInFrustum(frustum, pos, 1.0f)) {
                    ++culledCount_;
                    continue;
                }

                if (!material.useTextures)
                    pbrShader_.setFloat("roughnessValue",
                        glm::clamp(static_cast<float>(col) /
                                   static_cast<float>(std::max(gridCols - 1, 1)),
                                   0.05f, 1.0f));

                int lod = selectLOD(glm::length(camera.Position - pos));
                ++lodCounts_[lod];
                ++visibleCount_;

                glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);
                pbrShader_.setMat4("model", model);
                pbrShader_.setMat3("normalMatrix",
                    glm::transpose(glm::inverse(glm::mat3(model))));
                renderSphereGeometry(lod);
            }
        }
    }

    glEndQuery(GL_TIME_ELAPSED);
    gpuQueryIdx_ = 1 - gpuQueryIdx_;
    gpuTimerReady_ = true;
}

void PBRRenderer::renderSingleSphere(const Camera& camera, const PBRMaterial& material,
                                      const std::vector<PointLight>& lights,
                                      const glm::mat4& modelMatrix)
{
    pbrShader_.use();
    pbrShader_.setInt("useInstancing", 0);
    setupCameraUniforms(camera);
    setupMaterialUniforms(material);
    bindIBLTextures();
    setupLightUniforms(lights);

    pbrShader_.setMat4("model", modelMatrix);
    pbrShader_.setMat3("normalMatrix",
        glm::transpose(glm::inverse(glm::mat3(modelMatrix))));
    renderSphereGeometry();
}

void PBRRenderer::renderBackground(const Camera& camera)
{
    backgroundShader_.use();
    backgroundShader_.setMat4("projection", projectionMatrix(camera));
    backgroundShader_.setMat4("view", camera.GetViewMatrix());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap_);
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

    glGenVertexArrays(1, &cubeVAO_);
    glGenBuffers(1, &cubeVBO_);
    glBindVertexArray(cubeVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVerts), cubeVerts, GL_STATIC_DRAW);
    constexpr GLsizei cubeStride = 8 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, cubeStride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, cubeStride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, cubeStride, (void*)(6 * sizeof(float)));
    glBindVertexArray(0);

    // --- Sphere LOD meshes ---
    constexpr int LOD_SEGMENTS[NUM_LODS] = { 64, 32, 16 };
    for (int i = 0; i < NUM_LODS; ++i) {
        createSphereMesh(sphereLODs_[i], LOD_SEGMENTS[i]);
        setupMeshInstanceAttribs(sphereLODs_[i]);
    }

    // --- Fullscreen quad ---
    float quadVerts[] = {
        -1.0f, 1.0f, 0.0f, 0.0f,1.0f,
        -1.0f,-1.0f, 0.0f, 0.0f,0.0f,
         1.0f, 1.0f, 0.0f, 1.0f,1.0f,
         1.0f,-1.0f, 0.0f, 1.0f,0.0f,
    };

    glGenVertexArrays(1, &quadVAO_);
    glGenBuffers(1, &quadVBO_);
    glBindVertexArray(quadVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
}

void PBRRenderer::renderCubeGeometry()
{
    glBindVertexArray(cubeVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 36);
}

void PBRRenderer::renderSphereGeometry(int lod)
{
    auto& m = sphereLODs_[lod];
    glBindVertexArray(m.vao);
    glDrawElements(GL_TRIANGLE_STRIP, m.indexCount, GL_UNSIGNED_INT, 0);
}

void PBRRenderer::renderQuadGeometry()
{
    glBindVertexArray(quadVAO_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// ============================================================
//  Sphere mesh creation & instancing setup
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
    mesh.indexCount = static_cast<unsigned int>(indices.size());

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);
    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    constexpr GLsizei stride = 8 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glBindVertexArray(0);
}

void PBRRenderer::setupMeshInstanceAttribs(SphereMesh& mesh)
{
    glGenBuffers(1, &mesh.instanceVBO);

    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.instanceVBO);

    for (int i = 0; i < 4; ++i) {
        glEnableVertexAttribArray(3 + i);
        glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE,
                              sizeof(InstanceData),
                              (void*)(offsetof(InstanceData, model) + sizeof(glm::vec4) * i));
        glVertexAttribDivisor(3 + i, 1);
    }

    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE,
                          sizeof(InstanceData),
                          (void*)offsetof(InstanceData, material));
    glVertexAttribDivisor(7, 1);

    glBindVertexArray(0);
}

// ============================================================
//  Texture loading
// ============================================================

unsigned int PBRRenderer::loadTexture(const char* path)
{
    unsigned int textureID;
    glGenTextures(1, &textureID);

    int width, height, nrComponents;
    unsigned char* data = stbi_load(path, &width, &height, &nrComponents, 0);
    if (data) {
        GLenum format = GL_RGB;
        if      (nrComponents == 1) format = GL_RED;
        else if (nrComponents == 3) format = GL_RGB;
        else if (nrComponents == 4) format = GL_RGBA;

        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        stbi_image_free(data);
    } else {
        std::cerr << "[PBRRenderer] Texture load failed: " << path << std::endl;
        stbi_image_free(data);
        glDeleteTextures(1, &textureID);
        return 0;
    }

    return textureID;
}

unsigned int PBRRenderer::loadHDRTexture(const char* path)
{
    stbi_set_flip_vertically_on_load(true);
    int width, height, nrComponents;
    float* data = stbi_loadf(path, &width, &height, &nrComponents, 0);
    if (!data) {
        std::cerr << "[PBRRenderer] HDR load failed: " << path << std::endl;
        return 0;
    }

    unsigned int hdrTexture;
    glGenTextures(1, &hdrTexture);
    glBindTexture(GL_TEXTURE_2D, hdrTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(data);
    return hdrTexture;
}
