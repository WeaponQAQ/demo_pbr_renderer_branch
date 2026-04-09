#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>

#include "rhi/rhi.h"
#include "camera.h"

struct PBRMaterial {
    glm::vec3 albedo    = glm::vec3(0.5f, 0.0f, 0.0f);
    float metallic      = 0.5f;
    float roughness     = 0.5f;
    float ao            = 1.0f;
    bool useTextures    = false;

    RHITexture* albedoMap    = nullptr;
    RHITexture* normalMap    = nullptr;
    RHITexture* metallicMap  = nullptr;
    RHITexture* roughnessMap = nullptr;
    RHITexture* aoMap        = nullptr;
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

    bool init(RHIDevice* device, const std::string& shaderDir);
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

    void recordScene(RHICommandList* cmd, const Camera& camera, const PBRMaterial& material,
                     const std::vector<PointLight>& lights,
                     int gridRows, int gridCols, float gridSpacing,
                     bool renderGrid, bool useInstancing = true);
    void recordBackground(RHICommandList* cmd, const Camera& camera);
    void updateTimerResult();

    RHITexture* loadTexture(const char* path);
    RHITexture* loadHDRTexture(const char* path);

    void setClipPlanes(float nearPlane, float farPlane)
        { nearPlane_ = nearPlane; farPlane_ = farPlane; }

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

    void generateIBLMaps(RHITexture* hdrTexture);

    struct SphereMesh {
        std::unique_ptr<RHIBuffer>      vertexBuffer;
        std::unique_ptr<RHIBuffer>      indexBuffer;
        std::unique_ptr<RHIBuffer>      instanceBuffer;
        std::unique_ptr<RHIVertexInput> vertexInput;
        int indexCount = 0;
    };
    void createSphereMesh(SphereMesh& mesh, int segments);

    RHIDevice*  device_ = nullptr;
    RHIContext* ctx_    = nullptr;

    std::unique_ptr<RHIShader> pbrShader_;
    std::unique_ptr<RHIShader> equirectToCubemapShader_;
    std::unique_ptr<RHIShader> irradianceShader_;
    std::unique_ptr<RHIShader> prefilterShader_;
    std::unique_ptr<RHIShader> brdfShader_;
    std::unique_ptr<RHIShader> backgroundShader_;

    std::unique_ptr<RHITexture> envCubemap_;
    std::unique_ptr<RHITexture> irradianceMap_;
    std::unique_ptr<RHITexture> prefilterMap_;
    std::unique_ptr<RHITexture> brdfLUTTexture_;

    std::unique_ptr<RHIBuffer>      cubeVBO_;
    std::unique_ptr<RHIVertexInput> cubeInput_;

    static constexpr int NUM_LODS = 3;
    SphereMesh sphereLODs_[NUM_LODS];

    std::unique_ptr<RHIBuffer>      quadVBO_;
    std::unique_ptr<RHIVertexInput> quadInput_;

    int visibleCount_ = 0, culledCount_ = 0;
    int lodCounts_[NUM_LODS] = {};

    std::unique_ptr<RHITimerQuery> timerQuery_;
    float gpuTimeMs_ = 0.0f;

    std::unique_ptr<RHIFramebuffer> captureFBO_;
    std::unique_ptr<RHICommandList> sceneCmdList_;
    std::unique_ptr<RHICommandList> bgCmdList_;

    int screenWidth_  = 1280;
    int screenHeight_ = 720;
    float nearPlane_  = 0.1f;
    float farPlane_   = 500.0f;
    bool iblInitialized_ = false;
    std::string currentHDRPath_;

    std::vector<std::unique_ptr<RHITexture>> loadedTextures_;
};
