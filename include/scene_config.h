#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <filesystem>

struct WindowConfig {
    int width         = 1280;
    int height        = 720;
    std::string title = "PBR Renderer";
    int samples       = 4;
};

struct CameraConfig {
    glm::vec3 position = glm::vec3(0.0f, 0.0f, 10.0f);
    float yaw          = -90.0f;
    float pitch        = 0.0f;
    float fov          = 45.0f;
    float speed        = 2.5f;
    float sensitivity  = 0.1f;
    float nearPlane    = 0.1f;
    float farPlane     = 100.0f;
};

struct LightConfig {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 color    = glm::vec3(1.0f);
    float intensity    = 300.0f;
};

struct MaterialConfig {
    glm::vec3 albedo = glm::vec3(0.5f, 0.0f, 0.0f);
    float metallic   = 0.5f;
    float roughness  = 0.5f;
    float ao         = 1.0f;

    std::string albedoMapPath;
    std::string normalMapPath;
    std::string metallicMapPath;
    std::string roughnessMapPath;
    std::string aoMapPath;

    bool hasTextures() const;
};

struct EnvironmentConfig {
    std::string hdrPath;
    glm::vec3 clearColor = glm::vec3(0.1f);
    bool showBackground  = true;
};

struct GridConfig {
    int rows       = 7;
    int cols       = 7;
    float spacing  = 2.5f;
    bool visible   = true;
};

struct SceneConfig {
    WindowConfig window;
    CameraConfig camera;
    std::vector<LightConfig> lights;
    MaterialConfig material;
    EnvironmentConfig environment;
    GridConfig grid;
    std::string shaderDir = "shaders";

    static SceneConfig loadFromFile(const std::string& path);
    void saveToFile(const std::string& path) const;
    static SceneConfig makeDefault();

    static std::vector<std::string> scanHDRFiles(const std::string& directory);
};
