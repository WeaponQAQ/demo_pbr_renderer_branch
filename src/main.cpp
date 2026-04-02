#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "camera.h"
#include "pbr_renderer.h"
#include "scene_config.h"

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>

// ============================================================
//  Application state
// ============================================================

struct AppState {
    Camera camera;
    PBRMaterial material;
    std::vector<PointLight> lights;

    int   gridRows    = 7;
    int   gridCols    = 7;
    float gridSpacing = 2.5f;
    bool  gridVisible = true;
    bool  showBackground = true;

    float bgColor[3]     = { 0.1f, 0.1f, 0.1f };
    float lightColor[3]  = { 1.0f, 1.0f, 1.0f };
    float lightIntensity = 300.0f;

    std::vector<std::string> hdrFiles;
    int  selectedHDR     = -1;
    bool hdrNeedsReload  = false;
    std::string hdrScanDir = "resources/textures";

    std::string configPath = "scene.json";

    float lastX = 0, lastY = 0;
    bool  firstMouse = true;
    bool  mouseControlCamera = false;
    float deltaTime = 0, lastFrame = 0;
    int   screenWidth = 1280, screenHeight = 720;
};

static AppState g_app;

// ============================================================
//  Callbacks
// ============================================================

static void framebufferSizeCallback(GLFWwindow*, int w, int h)
{
    glViewport(0, 0, w, h);
    g_app.screenWidth  = w;
    g_app.screenHeight = h;
}

static void mouseButtonCallback(GLFWwindow*, int button, int action, int)
{
    if (ImGui::GetIO().WantCaptureMouse) return;
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        g_app.mouseControlCamera = (action == GLFW_PRESS);
        if (action == GLFW_PRESS) g_app.firstMouse = true;
    }
}

static void cursorPosCallback(GLFWwindow*, double xpos, double ypos)
{
    if (ImGui::GetIO().WantCaptureMouse || !g_app.mouseControlCamera) return;
    float x = static_cast<float>(xpos), y = static_cast<float>(ypos);
    if (g_app.firstMouse) { g_app.lastX = x; g_app.lastY = y; g_app.firstMouse = false; }
    g_app.camera.ProcessMouseMovement(x - g_app.lastX, g_app.lastY - y);
    g_app.lastX = x;
    g_app.lastY = y;
}

static void scrollCallback(GLFWwindow*, double, double yoff)
{
    if (ImGui::GetIO().WantCaptureMouse) return;
    g_app.camera.ProcessMouseScroll(static_cast<float>(yoff));
}

static void processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
    if (!g_app.mouseControlCamera) return;

    float dt = g_app.deltaTime;
    auto key = [&](int k) { return glfwGetKey(window, k) == GLFW_PRESS; };
    if (key(GLFW_KEY_W)) g_app.camera.ProcessKeyboard(FORWARD,  dt);
    if (key(GLFW_KEY_S)) g_app.camera.ProcessKeyboard(BACKWARD, dt);
    if (key(GLFW_KEY_A)) g_app.camera.ProcessKeyboard(LEFT,     dt);
    if (key(GLFW_KEY_D)) g_app.camera.ProcessKeyboard(RIGHT,    dt);
    if (key(GLFW_KEY_Q)) g_app.camera.ProcessKeyboard(DOWN,     dt);
    if (key(GLFW_KEY_E)) g_app.camera.ProcessKeyboard(UP,       dt);
}

// ============================================================
//  Config ↔ Runtime conversion
// ============================================================

static void applyConfig(const SceneConfig& cfg, PBRRenderer& renderer)
{
    g_app.camera = Camera(cfg.camera.position, glm::vec3(0,1,0),
                          cfg.camera.yaw, cfg.camera.pitch);
    g_app.camera.Zoom             = cfg.camera.fov;
    g_app.camera.MovementSpeed    = cfg.camera.speed;
    g_app.camera.MouseSensitivity = cfg.camera.sensitivity;

    g_app.material.albedo    = cfg.material.albedo;
    g_app.material.metallic  = cfg.material.metallic;
    g_app.material.roughness = cfg.material.roughness;
    g_app.material.ao        = cfg.material.ao;
    g_app.material.useTextures = cfg.material.hasTextures();

    if (g_app.material.useTextures) {
        auto tryLoad = [&](const std::string& p) -> unsigned int {
            return p.empty() ? 0 : renderer.loadTexture(p.c_str());
        };
        g_app.material.albedoMap    = tryLoad(cfg.material.albedoMapPath);
        g_app.material.normalMap    = tryLoad(cfg.material.normalMapPath);
        g_app.material.metallicMap  = tryLoad(cfg.material.metallicMapPath);
        g_app.material.roughnessMap = tryLoad(cfg.material.roughnessMapPath);
        g_app.material.aoMap        = tryLoad(cfg.material.aoMapPath);
    }

    g_app.lights.clear();
    for (auto& lc : cfg.lights)
        g_app.lights.push_back({ lc.position, lc.color * lc.intensity });

    g_app.lightIntensity = cfg.lights.empty() ? 300.0f : cfg.lights[0].intensity;
    if (!cfg.lights.empty()) {
        g_app.lightColor[0] = cfg.lights[0].color.r;
        g_app.lightColor[1] = cfg.lights[0].color.g;
        g_app.lightColor[2] = cfg.lights[0].color.b;
    }

    g_app.gridRows    = cfg.grid.rows;
    g_app.gridCols    = cfg.grid.cols;
    g_app.gridSpacing = cfg.grid.spacing;
    g_app.gridVisible = cfg.grid.visible;

    g_app.bgColor[0] = cfg.environment.clearColor.r;
    g_app.bgColor[1] = cfg.environment.clearColor.g;
    g_app.bgColor[2] = cfg.environment.clearColor.b;
    g_app.showBackground = cfg.environment.showBackground;

    renderer.resize(cfg.window.width, cfg.window.height);
}

static SceneConfig captureConfig()
{
    SceneConfig cfg;
    cfg.window.width  = g_app.screenWidth;
    cfg.window.height = g_app.screenHeight;
    cfg.window.title  = "PBR Renderer";

    cfg.camera.position    = g_app.camera.Position;
    cfg.camera.yaw         = g_app.camera.Yaw;
    cfg.camera.pitch       = g_app.camera.Pitch;
    cfg.camera.fov         = g_app.camera.Zoom;
    cfg.camera.speed       = g_app.camera.MovementSpeed;
    cfg.camera.sensitivity = g_app.camera.MouseSensitivity;

    cfg.material.albedo    = g_app.material.albedo;
    cfg.material.metallic  = g_app.material.metallic;
    cfg.material.roughness = g_app.material.roughness;
    cfg.material.ao        = g_app.material.ao;

    cfg.lights.clear();
    for (auto& l : g_app.lights) {
        LightConfig lc;
        lc.position  = l.position;
        lc.color     = glm::vec3(g_app.lightColor[0], g_app.lightColor[1], g_app.lightColor[2]);
        lc.intensity = g_app.lightIntensity;
        cfg.lights.push_back(lc);
    }

    cfg.grid.rows    = g_app.gridRows;
    cfg.grid.cols    = g_app.gridCols;
    cfg.grid.spacing = g_app.gridSpacing;
    cfg.grid.visible = g_app.gridVisible;

    cfg.environment.clearColor    = glm::vec3(g_app.bgColor[0], g_app.bgColor[1], g_app.bgColor[2]);
    cfg.environment.showBackground = g_app.showBackground;

    if (g_app.selectedHDR >= 0 && g_app.selectedHDR < (int)g_app.hdrFiles.size())
        cfg.environment.hdrPath = g_app.hdrFiles[g_app.selectedHDR];

    return cfg;
}

// ============================================================
//  HDR scanning
// ============================================================

static void scanHDRFiles(const std::string& dir)
{
    g_app.hdrFiles = SceneConfig::scanHDRFiles(dir);
    g_app.selectedHDR = -1;
}

static void syncSelectedHDR(const std::string& currentPath)
{
    for (int i = 0; i < (int)g_app.hdrFiles.size(); ++i) {
        namespace fs = std::filesystem;
        if (fs::path(g_app.hdrFiles[i]).filename() == fs::path(currentPath).filename()) {
            g_app.selectedHDR = i;
            return;
        }
    }
}

// ============================================================
//  ImGui panel
// ============================================================

static void drawUI(PBRRenderer& renderer, const ImGuiIO& io)
{
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 640), ImGuiCond_FirstUseEver);
    ImGui::Begin("PBR Controls");
    ImGui::Text("FPS: %.1f  (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);
    ImGui::Separator();

    // --- Material ---
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Albedo", &g_app.material.albedo[0]);
        ImGui::SliderFloat("Metallic",  &g_app.material.metallic,  0.0f, 1.0f);
        ImGui::SliderFloat("Roughness", &g_app.material.roughness, 0.0f, 1.0f);
        ImGui::SliderFloat("AO",        &g_app.material.ao,        0.0f, 1.0f);
    }

    // --- Grid ---
    if (ImGui::CollapsingHeader("Sphere Grid", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Show Grid", &g_app.gridVisible);
        ImGui::SliderInt("Rows",     &g_app.gridRows,    1, 10);
        ImGui::SliderInt("Columns",  &g_app.gridCols,    1, 10);
        ImGui::SliderFloat("Spacing", &g_app.gridSpacing, 1.0f, 5.0f);
    }

    // --- Lighting ---
    if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Light Color", g_app.lightColor);
        ImGui::SliderFloat("Intensity",  &g_app.lightIntensity, 10.0f, 1000.0f);
        ImGui::Separator();
        for (int i = 0; i < (int)g_app.lights.size(); ++i) {
            ImGui::PushID(i);
            if (ImGui::TreeNode(("Light " + std::to_string(i)).c_str())) {
                ImGui::DragFloat3("Position", &g_app.lights[i].position[0], 0.1f);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    // --- Environment ---
    if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Show Background", &g_app.showBackground);
        ImGui::ColorEdit3("Clear Color", g_app.bgColor);
        ImGui::Separator();

        ImGui::Text("HDR Environment Maps:");
        if (ImGui::Button("Scan Directory")) {
            scanHDRFiles(g_app.hdrScanDir);
            syncSelectedHDR(renderer.currentHDR());
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        char scanBuf[256];
        strncpy(scanBuf, g_app.hdrScanDir.c_str(), sizeof(scanBuf) - 1);
        scanBuf[sizeof(scanBuf) - 1] = '\0';
        if (ImGui::InputText("##scandir", scanBuf, sizeof(scanBuf)))
            g_app.hdrScanDir = scanBuf;

        if (!g_app.hdrFiles.empty()) {
            auto filenameOf = [](const std::string& p) {
                return std::filesystem::path(p).filename().string();
            };
            std::string preview = g_app.selectedHDR >= 0
                ? filenameOf(g_app.hdrFiles[g_app.selectedHDR]) : "-- Select HDR --";

            if (ImGui::BeginCombo("HDR File", preview.c_str())) {
                for (int i = 0; i < (int)g_app.hdrFiles.size(); ++i) {
                    bool selected = (i == g_app.selectedHDR);
                    if (ImGui::Selectable(filenameOf(g_app.hdrFiles[i]).c_str(), selected)) {
                        g_app.selectedHDR    = i;
                        g_app.hdrNeedsReload = true;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        } else {
            ImGui::TextWrapped("No .hdr/.exr files found. Place HDR files in the scan directory and click Scan.");
        }

        if (!renderer.currentHDR().empty()) {
            ImGui::TextWrapped("Active: %s",
                std::filesystem::path(renderer.currentHDR()).filename().string().c_str());
        }
    }

    // --- Camera ---
    if (ImGui::CollapsingHeader("Camera")) {
        ImGui::DragFloat3("Position", &g_app.camera.Position[0], 0.1f);
        ImGui::SliderFloat("FOV",   &g_app.camera.Zoom,          1.0f, 90.0f);
        ImGui::SliderFloat("Speed", &g_app.camera.MovementSpeed,  0.5f, 20.0f);
        ImGui::TextWrapped("Right-click + drag to rotate | WASD to move | Scroll to zoom");
    }

    // --- Config I/O ---
    ImGui::Separator();
    if (ImGui::Button("Save Config")) {
        auto cfg = captureConfig();
        cfg.saveToFile(g_app.configPath);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload Config")) {
        auto cfg = SceneConfig::loadFromFile(g_app.configPath);
        // Keep current renderer, just re-apply params
        g_app.camera = Camera(cfg.camera.position, glm::vec3(0,1,0),
                              cfg.camera.yaw, cfg.camera.pitch);
        g_app.camera.Zoom             = cfg.camera.fov;
        g_app.camera.MovementSpeed    = cfg.camera.speed;
        g_app.camera.MouseSensitivity = cfg.camera.sensitivity;
        g_app.material.albedo    = cfg.material.albedo;
        g_app.material.metallic  = cfg.material.metallic;
        g_app.material.roughness = cfg.material.roughness;
        g_app.material.ao        = cfg.material.ao;
        g_app.gridRows    = cfg.grid.rows;
        g_app.gridCols    = cfg.grid.cols;
        g_app.gridSpacing = cfg.grid.spacing;
        g_app.gridVisible = cfg.grid.visible;
        g_app.bgColor[0]  = cfg.environment.clearColor.r;
        g_app.bgColor[1]  = cfg.environment.clearColor.g;
        g_app.bgColor[2]  = cfg.environment.clearColor.b;
        g_app.showBackground = cfg.environment.showBackground;

        if (!cfg.environment.hdrPath.empty() && cfg.environment.hdrPath != renderer.currentHDR()) {
            renderer.reloadIBL(cfg.environment.hdrPath);
            scanHDRFiles(g_app.hdrScanDir);
            syncSelectedHDR(cfg.environment.hdrPath);
        }
    }

    ImGui::End();
}

// ============================================================
//  Main
// ============================================================

int main(int argc, char** argv)
{
    std::string configPath = "scene.json";
    if (argc > 1) configPath = argv[1];
    g_app.configPath = configPath;

    SceneConfig cfg = SceneConfig::loadFromFile(configPath);

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, cfg.window.samples);

    GLFWwindow* window = glfwCreateWindow(
        cfg.window.width, cfg.window.height,
        cfg.window.title.c_str(), nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    // --- ImGui ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding  = 4.0f;
    style.GrabRounding   = 4.0f;
    style.Colors[ImGuiCol_WindowBg]      = ImVec4(0.08f, 0.08f, 0.10f, 0.94f);
    style.Colors[ImGuiCol_TitleBg]       = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.15f, 0.22f, 1.00f);
    style.Colors[ImGuiCol_FrameBg]       = ImVec4(0.14f, 0.14f, 0.18f, 1.00f);
    style.Colors[ImGuiCol_SliderGrab]    = ImVec4(0.40f, 0.50f, 0.80f, 1.00f);
    style.Colors[ImGuiCol_Button]        = ImVec4(0.20f, 0.25f, 0.40f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.35f, 0.55f, 1.00f);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    glEnable(GL_MULTISAMPLE);

    // --- Renderer ---
    PBRRenderer renderer;
    renderer.init(cfg.shaderDir);
    applyConfig(cfg, renderer);

    g_app.screenWidth  = cfg.window.width;
    g_app.screenHeight = cfg.window.height;
    g_app.lastX = cfg.window.width / 2.0f;
    g_app.lastY = cfg.window.height / 2.0f;

    // Scan HDR directory and load if configured
    scanHDRFiles(g_app.hdrScanDir);
    if (!cfg.environment.hdrPath.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(cfg.environment.hdrPath, ec)) {
            renderer.setupIBL(cfg.environment.hdrPath);
            syncSelectedHDR(cfg.environment.hdrPath);
        } else {
            std::cout << "[Main] HDR not found: " << cfg.environment.hdrPath << std::endl;
        }
    }

    // --- Main loop ---
    while (!glfwWindowShouldClose(window)) {
        float now = static_cast<float>(glfwGetTime());
        g_app.deltaTime = now - g_app.lastFrame;
        g_app.lastFrame = now;

        processInput(window);

        // Dynamic HDR reload
        if (g_app.hdrNeedsReload && g_app.selectedHDR >= 0) {
            renderer.reloadIBL(g_app.hdrFiles[g_app.selectedHDR]);
            g_app.hdrNeedsReload = false;
        }

        renderer.resize(g_app.screenWidth, g_app.screenHeight);

        // Update light colors from UI
        glm::vec3 lc(g_app.lightColor[0], g_app.lightColor[1], g_app.lightColor[2]);
        for (auto& light : g_app.lights)
            light.color = lc * g_app.lightIntensity;

        glClearColor(g_app.bgColor[0], g_app.bgColor[1], g_app.bgColor[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        renderer.renderScene(g_app.camera, g_app.material, g_app.lights,
                             g_app.gridRows, g_app.gridCols, g_app.gridSpacing,
                             g_app.gridVisible);

        if (g_app.showBackground && renderer.iblReady())
            renderer.renderBackground(g_app.camera);

        // --- ImGui ---
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        drawUI(renderer, io);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
    return 0;
}
