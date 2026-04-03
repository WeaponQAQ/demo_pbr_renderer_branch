#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "shader.h"
#include "camera.h"

struct PBRMaterial {
    glm::vec3 albedo    = glm::vec3(0.5f, 0.0f, 0.0f);
    float metallic      = 0.5f;
    float roughness     = 0.5f;
    float ao            = 1.0f;
    bool useTextures    = false;

    unsigned int albedoMap    = 0;
    unsigned int normalMap    = 0;
    unsigned int metallicMap  = 0;
    unsigned int roughnessMap = 0;
    unsigned int aoMap        = 0;
};

struct PointLight {
    glm::vec3 position;
    glm::vec3 color;
};

class PBRRenderer
{
public:
    PBRRenderer();
    ~PBRRenderer();

    PBRRenderer(const PBRRenderer&)            = delete;
    PBRRenderer& operator=(const PBRRenderer&) = delete;

    bool init(const std::string& shaderDir);
    void resize(int width, int height);

    void setupIBL(const std::string& hdrPath);
    void reloadIBL(const std::string& hdrPath);

    void renderScene(const Camera& camera, const PBRMaterial& material,
                     const std::vector<PointLight>& lights,
                     int gridRows, int gridCols, float gridSpacing,
                     bool renderGrid, bool useInstancing = true);
    void renderSingleSphere(const Camera& camera, const PBRMaterial& material,
                            const std::vector<PointLight>& lights,
                            const glm::mat4& modelMatrix);
    void renderBackground(const Camera& camera);

    unsigned int loadTexture(const char* path);
    unsigned int loadHDRTexture(const char* path);

    bool iblReady() const { return iblInitialized_; }
    int  width()    const { return screenWidth_; }
    int  height()   const { return screenHeight_; }

    const std::string& currentHDR() const { return currentHDRPath_; }

    float gpuTimeMs()    const { return gpuTimeMs_; }
    int   visibleCount() const { return visibleCount_; }
    int   culledCount()  const { return culledCount_; }
    const int* lodCounts() const { return lodCounts_; }

private:
    void setupGeometry();
    void releaseIBL();

    void bindIBLTextures();
    void setupLightUniforms(const std::vector<PointLight>& lights);
    void setupMaterialUniforms(const PBRMaterial& material);
    void setupCameraUniforms(const Camera& camera);
    glm::mat4 projectionMatrix(const Camera& camera) const;

    void renderCubeGeometry();
    void renderSphereGeometry(int lod = 0);
    void renderQuadGeometry();

    void generateIBLMaps(unsigned int hdrTexture);

    struct SphereMesh {
        unsigned int vao = 0, vbo = 0, ebo = 0;
        unsigned int indexCount = 0;
        unsigned int instanceVBO = 0;
    };
    void createSphereMesh(SphereMesh& mesh, int segments);
    void setupMeshInstanceAttribs(SphereMesh& mesh);

    Shader pbrShader_;
    Shader equirectToCubemapShader_;
    Shader irradianceShader_;
    Shader prefilterShader_;
    Shader brdfShader_;
    Shader backgroundShader_;

    unsigned int envCubemap_     = 0;
    unsigned int irradianceMap_  = 0;
    unsigned int prefilterMap_   = 0;
    unsigned int brdfLUTTexture_ = 0;

    unsigned int cubeVAO_ = 0, cubeVBO_ = 0;

    static constexpr int NUM_LODS = 3;
    SphereMesh sphereLODs_[NUM_LODS];

    unsigned int quadVAO_ = 0, quadVBO_ = 0;

    int visibleCount_ = 0, culledCount_ = 0;
    int lodCounts_[NUM_LODS] = {};

    unsigned int gpuTimerQueries_[2] = {};
    int gpuQueryIdx_ = 0;
    float gpuTimeMs_ = 0.0f;
    bool gpuTimerReady_ = false;

    unsigned int captureFBO_ = 0, captureRBO_ = 0;

    int screenWidth_  = 1280;
    int screenHeight_ = 720;
    bool iblInitialized_ = false;
    std::string currentHDRPath_;
};
