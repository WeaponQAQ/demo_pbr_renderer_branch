#include "scene_config.h"

#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using json = nlohmann::json;

static glm::vec3 readVec3(const json& j, const std::string& key, glm::vec3 def)
{
    if (!j.contains(key) || !j[key].is_array() || j[key].size() < 3)
        return def;
    return { j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>() };
}

static json writeVec3(const glm::vec3& v)
{
    return json::array({ v.x, v.y, v.z });
}

template <typename T>
static T readOr(const json& j, const std::string& key, T def)
{
    return j.contains(key) ? j[key].get<T>() : def;
}

bool MaterialConfig::hasTextures() const
{
    return !albedoMapPath.empty() || !normalMapPath.empty() ||
           !metallicMapPath.empty() || !roughnessMapPath.empty() || !aoMapPath.empty();
}

SceneConfig SceneConfig::makeDefault()
{
    SceneConfig cfg;
    cfg.lights = {
        { glm::vec3(-10.0f,  10.0f, 10.0f), glm::vec3(1.0f), 300.0f },
        { glm::vec3( 10.0f,  10.0f, 10.0f), glm::vec3(1.0f), 300.0f },
        { glm::vec3(-10.0f, -10.0f, 10.0f), glm::vec3(1.0f), 300.0f },
        { glm::vec3( 10.0f, -10.0f, 10.0f), glm::vec3(1.0f), 300.0f }
    };
    return cfg;
}

SceneConfig SceneConfig::loadFromFile(const std::string& path)
{
    SceneConfig cfg = makeDefault();

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[SceneConfig] Cannot open: " << path
                  << ", using defaults." << std::endl;
        return cfg;
    }

    json root;
    try {
        root = json::parse(file);
    } catch (const json::parse_error& e) {
        std::cerr << "[SceneConfig] Parse error in " << path << ": "
                  << e.what() << std::endl;
        return cfg;
    }

    if (root.contains("window")) {
        auto& w = root["window"];
        cfg.window.width  = readOr(w, "width", cfg.window.width);
        cfg.window.height = readOr(w, "height", cfg.window.height);
        cfg.window.title  = readOr<std::string>(w, "title", cfg.window.title);
        cfg.window.samples = readOr(w, "samples", cfg.window.samples);
    }

    if (root.contains("camera")) {
        auto& c = root["camera"];
        cfg.camera.position    = readVec3(c, "position", cfg.camera.position);
        cfg.camera.yaw         = readOr(c, "yaw", cfg.camera.yaw);
        cfg.camera.pitch       = readOr(c, "pitch", cfg.camera.pitch);
        cfg.camera.fov         = readOr(c, "fov", cfg.camera.fov);
        cfg.camera.speed       = readOr(c, "speed", cfg.camera.speed);
        cfg.camera.sensitivity = readOr(c, "sensitivity", cfg.camera.sensitivity);
        cfg.camera.nearPlane   = readOr(c, "near_plane", cfg.camera.nearPlane);
        cfg.camera.farPlane    = readOr(c, "far_plane", cfg.camera.farPlane);
    }

    if (root.contains("lights") && root["lights"].is_array()) {
        cfg.lights.clear();
        for (auto& lj : root["lights"]) {
            LightConfig l;
            l.position  = readVec3(lj, "position", l.position);
            l.color     = readVec3(lj, "color", l.color);
            l.intensity = readOr(lj, "intensity", l.intensity);
            cfg.lights.push_back(l);
        }
    }

    if (root.contains("material")) {
        auto& m = root["material"];
        cfg.material.albedo    = readVec3(m, "albedo", cfg.material.albedo);
        cfg.material.metallic  = readOr(m, "metallic", cfg.material.metallic);
        cfg.material.roughness = readOr(m, "roughness", cfg.material.roughness);
        cfg.material.ao        = readOr(m, "ao", cfg.material.ao);

        cfg.material.albedoMapPath    = readOr<std::string>(m, "albedo_map", "");
        cfg.material.normalMapPath    = readOr<std::string>(m, "normal_map", "");
        cfg.material.metallicMapPath  = readOr<std::string>(m, "metallic_map", "");
        cfg.material.roughnessMapPath = readOr<std::string>(m, "roughness_map", "");
        cfg.material.aoMapPath        = readOr<std::string>(m, "ao_map", "");
    }

    if (root.contains("environment")) {
        auto& e = root["environment"];
        cfg.environment.hdrPath       = readOr<std::string>(e, "hdr", "");
        cfg.environment.clearColor    = readVec3(e, "clear_color", cfg.environment.clearColor);
        cfg.environment.showBackground = readOr(e, "show_background", cfg.environment.showBackground);
    }

    if (root.contains("grid")) {
        auto& g = root["grid"];
        cfg.grid.rows    = readOr(g, "rows", cfg.grid.rows);
        cfg.grid.cols    = readOr(g, "cols", cfg.grid.cols);
        cfg.grid.spacing = readOr(g, "spacing", cfg.grid.spacing);
        cfg.grid.visible = readOr(g, "visible", cfg.grid.visible);
    }

    cfg.shaderDir = readOr<std::string>(root, "shader_dir", cfg.shaderDir);

    std::cout << "[SceneConfig] Loaded: " << path << std::endl;
    return cfg;
}

void SceneConfig::saveToFile(const std::string& path) const
{
    json root;

    root["window"] = {
        { "width",   window.width },
        { "height",  window.height },
        { "title",   window.title },
        { "samples", window.samples }
    };

    root["camera"] = {
        { "position",    writeVec3(camera.position) },
        { "yaw",         camera.yaw },
        { "pitch",       camera.pitch },
        { "fov",         camera.fov },
        { "speed",       camera.speed },
        { "sensitivity", camera.sensitivity },
        { "near_plane",  camera.nearPlane },
        { "far_plane",   camera.farPlane }
    };

    root["lights"] = json::array();
    for (auto& l : lights) {
        root["lights"].push_back({
            { "position",  writeVec3(l.position) },
            { "color",     writeVec3(l.color) },
            { "intensity", l.intensity }
        });
    }

    root["material"] = {
        { "albedo",        writeVec3(material.albedo) },
        { "metallic",      material.metallic },
        { "roughness",     material.roughness },
        { "ao",            material.ao },
        { "albedo_map",    material.albedoMapPath },
        { "normal_map",    material.normalMapPath },
        { "metallic_map",  material.metallicMapPath },
        { "roughness_map", material.roughnessMapPath },
        { "ao_map",        material.aoMapPath }
    };

    root["environment"] = {
        { "hdr",             environment.hdrPath },
        { "clear_color",     writeVec3(environment.clearColor) },
        { "show_background", environment.showBackground }
    };

    root["grid"] = {
        { "rows",    grid.rows },
        { "cols",    grid.cols },
        { "spacing", grid.spacing },
        { "visible", grid.visible }
    };

    root["shader_dir"] = shaderDir;

    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "[SceneConfig] Cannot write: " << path << std::endl;
        return;
    }
    file << root.dump(2) << std::endl;
    std::cout << "[SceneConfig] Saved: " << path << std::endl;
}

std::vector<std::string> SceneConfig::scanHDRFiles(const std::string& directory)
{
    std::vector<std::string> results;
    std::error_code ec;
    if (!fs::exists(directory, ec))
        return results;

    for (auto& entry : fs::directory_iterator(directory, ec)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(c));
        if (ext == ".hdr" || ext == ".exr")
            results.push_back(entry.path().string());
    }

    std::sort(results.begin(), results.end());
    return results;
}
