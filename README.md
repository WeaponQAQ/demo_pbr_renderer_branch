# PBR Renderer
![alt text](image.png)

A Physically Based Rendering (PBR) demo built with OpenGL 3.3, ImGui, and Image-Based Lighting (IBL), following the [LearnOpenGL PBR tutorial](https://learnopengl.com/PBR/Theory).

## Features

- **PBR Shading**: Cook-Torrance BRDF with GGX distribution, Smith geometry, and Fresnel-Schlick
- **Image-Based Lighting**: Diffuse irradiance + specular IBL with split-sum approximation
- **HDR Environment Maps**: Equirectangular-to-cubemap conversion, irradiance convolution, pre-filtered environment maps, BRDF LUT
- **ImGui Controls**: Real-time material editing (albedo, metallic, roughness, AO), light configuration, camera parameters
- **Sphere Grid**: Visualize PBR response across metallic/roughness parameter space

## Dependencies

Managed via **vcpkg** (submodule) and **git submodules**:

| Dependency | Source | Purpose |
|-----------|--------|---------|
| GLFW | vcpkg | Window/context management |
| GLM | vcpkg | Math library |
| Assimp | vcpkg | Model loading |
| glad | thirdparty/ (pre-generated) | OpenGL loader |
| stb | submodule | Image loading |
| Dear ImGui | submodule | UI framework |

## Build Instructions

### Prerequisites

- CMake 3.20+
- C++17 compiler (MSVC 2019+, GCC 9+, Clang 10+)
- Git

### Clone

```bash
git clone --recursive <repository-url>
cd OpenGL
```

If you already cloned without `--recursive`:
```bash
git submodule update --init --recursive
```

### Bootstrap vcpkg

**Windows:**
```cmd
vcpkg\bootstrap-vcpkg.bat
```

**Linux/macOS:**
```bash
./vcpkg/bootstrap-vcpkg.sh
```

### Configure & Build

```bash
cmake -B build -S .
cmake --build build --config Release
```

Or with a specific generator:
```bash
cmake -B build -S . -G "Visual Studio 17 2022"
cmake --build build --config Release
```

### Run

```bash
cd build/Release    # or build/Debug
./PBRRenderer
```

### HDR Environment Map

Place an HDR environment map at `resources/textures/background.hdr` for IBL. You can download free HDR maps from:
- [Poly Haven](https://polyhaven.com/hdris)
- [HDRIHaven (archive)](https://hdrihaven.com/)

The renderer will run without an HDR map but IBL features will be disabled.

## Controls

| Input | Action |
|-------|--------|
| Right-click + drag | Rotate camera |
| WASD | Move camera (while right-clicking) |
| Q/E | Move camera up/down |
| Scroll | Zoom |
| ESC | Quit |

## Project Structure

```
OpenGL/
├── CMakeLists.txt          # Build configuration
├── vcpkg.json              # vcpkg manifest
├── vcpkg/                  # vcpkg (submodule)
├── thirdparty/
│   ├── glad/               # OpenGL loader (pre-generated)
│   ├── stb/                # stb_image (submodule)
│   └── imgui/              # Dear ImGui (submodule)
├── include/
│   ├── shader.h
│   ├── camera.h
│   └── pbr_renderer.h
├── src/
│   ├── main.cpp
│   ├── shader.cpp
│   ├── camera.cpp
│   └── pbr_renderer.cpp
├── shaders/
│   ├── pbr.vert / pbr.frag
│   ├── equirectangular_to_cubemap.vert / .frag
│   ├── irradiance.vert / .frag
│   ├── prefilter.vert / .frag
│   ├── brdf.vert / .frag
│   └── background.vert / .frag
└── resources/
    └── textures/           # Place HDR maps here
```

## References

- [LearnOpenGL - PBR Theory](https://learnopengl.com/PBR/Theory)
- [LearnOpenGL - PBR Lighting](https://learnopengl.com/PBR/Lighting)
- [LearnOpenGL - IBL Diffuse](https://learnopengl.com/PBR/IBL/Diffuse-irradiance)
- [LearnOpenGL - IBL Specular](https://learnopengl.com/PBR/IBL/Specular-IBL)
