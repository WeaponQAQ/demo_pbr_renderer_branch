# PBR 渲染管线详解

本文档详细描述本项目中基于物理的渲染 (Physically Based Rendering, PBR) 的完整流程，包括理论基础、数学公式、IBL 预计算、着色器实现，以及各阶段在代码中的对应关系。

参考资料：[LearnOpenGL - PBR](https://learnopengl.com/PBR/Theory)

---

## 目录

1. [总体架构](#1-总体架构)
2. [渲染方程与 Cook-Torrance BRDF](#2-渲染方程与-cook-torrance-brdf)
3. [BRDF 三大函数](#3-brdf-三大函数)
4. [材质参数系统](#4-材质参数系统)
5. [直接光照计算](#5-直接光照计算)
6. [Image-Based Lighting (IBL) 预计算管线](#6-image-based-lighting-ibl-预计算管线)
7. [IBL 环境光照计算](#7-ibl-环境光照计算)
8. [HDR 与色调映射](#8-hdr-与色调映射)
9. [渲染管线执行流程](#9-渲染管线执行流程)
10. [着色器文件对照表](#10-着色器文件对照表)
11. [场景配置与动态 HDR 加载](#11-场景配置与动态-hdr-加载)

---

## 1. 总体架构

```
┌─────────────────────────────────────────────────────────┐
│                    初始化阶段 (一次性)                      │
│                                                         │
│  HDR 环境贴图 (.hdr)                                     │
│       │                                                 │
│       ▼                                                 │
│  ┌──────────────────┐                                   │
│  │ Equirect → Cubemap │  512×512 per face               │
│  └────────┬─────────┘                                   │
│           │                                             │
│     ┌─────┼──────────────────┐                          │
│     ▼     ▼                  ▼                          │
│  Irradiance    Pre-filter    BRDF LUT                   │
│  Cubemap       Cubemap       Texture                    │
│  (32×32)       (128×128,     (512×512)                  │
│                5 mip levels)                            │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│                   每帧渲染阶段                            │
│                                                         │
│  对每个物体:                                              │
│    ┌──────────────┐     ┌──────────────┐                │
│    │  直接光照      │  +  │  IBL 环境光照  │                │
│    │ (Cook-Torrance)│     │ (漫反射+镜面)  │                │
│    └──────┬───────┘     └──────┬───────┘                │
│           └────────┬───────────┘                        │
│                    ▼                                    │
│            HDR Tonemapping                              │
│                    ▼                                    │
│            Gamma Correction                             │
│                    ▼                                    │
│              最终颜色输出                                  │
└─────────────────────────────────────────────────────────┘
```

---

## 2. 渲染方程与 Cook-Torrance BRDF

### 2.1 反射方程

PBR 的核心是反射方程 (Reflectance Equation)：

$$
L_o(\mathbf{p}, \omega_o) = \int_{\Omega} f_r(\mathbf{p}, \omega_i, \omega_o) \cdot L_i(\mathbf{p}, \omega_i) \cdot (\mathbf{n} \cdot \omega_i) \, d\omega_i
$$

其中：
- $L_o$：出射辐射度 (outgoing radiance)
- $\omega_o$：观察方向（从表面点到摄像机）
- $\omega_i$：入射光方向
- $f_r$：双向反射分布函数 (BRDF)
- $L_i$：入射辐射度
- $\mathbf{n} \cdot \omega_i$：Lambert 余弦项

### 2.2 Cook-Torrance BRDF

我们将 BRDF 分解为 **漫反射** 和 **镜面反射** 两部分：

$$
f_r = k_d \cdot f_{Lambert} + k_s \cdot f_{CookTorrance}
$$

**漫反射项** (Lambertian)：

$$
f_{Lambert} = \frac{\text{albedo}}{\pi}
$$

**镜面反射项** (Cook-Torrance)：

$$
f_{CookTorrance} = \frac{D \cdot F \cdot G}{4 \cdot (\omega_o \cdot \mathbf{n}) \cdot (\omega_i \cdot \mathbf{n})}
$$

其中 D、F、G 分别是法线分布函数、菲涅尔方程和几何函数。

**能量守恒**：反射光（镜面反射）+ 折射光（漫反射）不超过入射光能量：

$$
k_s = F, \quad k_d = (1 - k_s) \times (1 - \text{metallic})
$$

金属表面没有漫反射（光被完全吸收或反射），因此 `kD` 乘以 `(1 - metallic)`。

**对应代码** (`pbr.frag` 第 143-148 行)：
```glsl
vec3 kS = F;
vec3 kD = vec3(1.0) - kS;
kD *= 1.0 - metallic;

float NdotL = max(dot(N, L), 0.0);
Lo += (kD * albedo / PI + specular) * radiance * NdotL;
```

---

## 3. BRDF 三大函数

### 3.1 法线分布函数 D — Trowbridge-Reitz GGX

描述微表面法线的统计分布，决定了高光区域的大小和形状：

$$
D_{GGX}(\mathbf{n}, \mathbf{h}, \alpha) = \frac{\alpha^2}{\pi \left( (\mathbf{n} \cdot \mathbf{h})^2 (\alpha^2 - 1) + 1 \right)^2}
$$

- $\mathbf{h}$：半程向量 $\mathbf{h} = \text{normalize}(\mathbf{v} + \mathbf{l})$
- $\alpha = \text{roughness}^2$（Disney 重映射，使滑块线性感知更自然）

| Roughness | 效果 |
|-----------|------|
| 0.0 | 理想镜面，高光极小极亮 |
| 0.5 | 中等粗糙，高光较大较柔和 |
| 1.0 | 完全粗糙，高光铺满整个半球 |

**对应代码** (`pbr.frag` 第 52-64 行)：
```glsl
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}
```

### 3.2 几何函数 G — Smith's Method with Schlick-GGX

描述微表面的自遮蔽（self-shadowing）和自遮挡（self-masking）：

**单方向 Schlick-GGX：**

$$
G_{SchlickGGX}(\mathbf{n}, \mathbf{v}, k) = \frac{\mathbf{n} \cdot \mathbf{v}}{(\mathbf{n} \cdot \mathbf{v})(1-k) + k}
$$

其中直接光照的 $k$ 值为：

$$
k_{direct} = \frac{(\text{roughness} + 1)^2}{8}
$$

**Smith's Method** 同时考虑观察方向和光照方向：

$$
G_{Smith}(\mathbf{n}, \mathbf{v}, \mathbf{l}, k) = G_{SchlickGGX}(\mathbf{n}, \mathbf{v}, k) \cdot G_{SchlickGGX}(\mathbf{n}, \mathbf{l}, k)
$$

**对应代码** (`pbr.frag` 第 66-85 行)：
```glsl
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float ggx2 = GeometrySchlickGGX(max(dot(N, V), 0.0), roughness);
    float ggx1 = GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
    return ggx1 * ggx2;
}
```

### 3.3 菲涅尔方程 F — Schlick 近似

描述不同观察角度下反射光与折射光的比率：

$$
F_{Schlick}(\mathbf{h}, \mathbf{v}, F_0) = F_0 + (1 - F_0)(1 - \mathbf{h} \cdot \mathbf{v})^5
$$

- $F_0$：基础反射率（0度入射角时的反射率）
- 非金属：$F_0 \approx 0.04$（大多数电介质）
- 金属：$F_0 = \text{albedo}$（金属的反射色即为表面颜色）

通过 `metallic` 参数在两者之间插值：

```glsl
vec3 F0 = vec3(0.04);
F0 = mix(F0, albedo, metallic);
```

掠射角 (grazing angle) 时 $\mathbf{h} \cdot \mathbf{v} \to 0$，$F \to 1$——所有表面在掠射角都趋近全反射。

**对应代码** (`pbr.frag` 第 87-90 行)：
```glsl
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
```

---

## 4. 材质参数系统

PBR 使用 **金属度/粗糙度** (Metallic/Roughness) 工作流：

| 参数 | 范围 | 含义 |
|------|------|------|
| **Albedo** (反照率) | RGB [0,1] | 表面基础颜色。非金属为漫反射色；金属为 F0 反射色 |
| **Metallic** (金属度) | [0,1] | 0=电介质(塑料、木头)，1=金属(金、铁、铜) |
| **Roughness** (粗糙度) | [0,1] | 0=光滑镜面，1=完全粗糙漫射 |
| **AO** (环境光遮蔽) | [0,1] | 模拟缝隙/角落处环境光被遮挡的程度 |
| **Normal** (法线贴图) | RGB | 像素级法线扰动，增加表面细节 |

本项目支持两种输入模式：
- **Uniform 模式**：通过 ImGui 滑块直接设置参数值，初始值从 `scene.json` 的 `material` 字段读取
- **纹理模式**：在 `scene.json` 中配置 PBR 贴图路径（`albedo_map`, `normal_map`, `metallic_map`, `roughness_map`, `ao_map`）

`scene.json` 材质配置示例：
```json
{
  "material": {
    "albedo": [0.5, 0.0, 0.0],
    "metallic": 0.5,
    "roughness": 0.5,
    "ao": 1.0,
    "albedo_map": "",
    "normal_map": "",
    "metallic_map": "",
    "roughness_map": "",
    "ao_map": ""
  }
}
```

```glsl
if (useTextures == 1)
{
    albedo    = pow(texture(albedoMap, TexCoords).rgb, vec3(2.2)); // sRGB → 线性
    N         = getNormalFromMap();
    metallic  = texture(metallicMap, TexCoords).r;
    roughness = texture(roughnessMap, TexCoords).r;
    ao        = texture(aoMap, TexCoords).r;
}
else
{
    albedo    = albedoValue;
    N         = normalize(Normal);
    metallic  = metallicValue;
    roughness = roughnessValue;
    ao        = aoValue;
}
```

---

## 5. 直接光照计算

对每个点光源，计算其对表面点的光照贡献：

```
对每个光源 i:
    1. 计算光线方向 L 和半程向量 H
    2. 计算距离衰减: attenuation = 1 / distance²
    3. 计算入射辐射度: radiance = lightColor * attenuation
    4. 计算 BRDF 三大函数: D(N,H,roughness), G(N,V,L,roughness), F(H,V,F0)
    5. 组合 Cook-Torrance 镜面项: specular = (D * G * F) / (4 * NdotV * NdotL)
    6. 分离能量: kD = (1 - F) * (1 - metallic)
    7. 累加: Lo += (kD * albedo/π + specular) * radiance * NdotL
```

**完整着色器流程** (`pbr.frag` 第 126-149 行)：

```glsl
vec3 Lo = vec3(0.0);
for(int i = 0; i < numLights; ++i)
{
    vec3 L = normalize(lightPositions[i] - WorldPos);
    vec3 H = normalize(V + L);
    float distance = length(lightPositions[i] - WorldPos);
    float attenuation = 1.0 / (distance * distance);
    vec3 radiance = lightColors[i] * attenuation;

    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 specular = (NDF * G * F) / (4.0 * max(dot(N,V),0.0) * max(dot(N,L),0.0) + 0.0001);

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    Lo += (kD * albedo / PI + specular) * radiance * max(dot(N, L), 0.0);
}
```

---

## 6. Image-Based Lighting (IBL) 预计算管线

IBL 将整个环境作为光源，通过预计算将昂贵的半球积分转化为纹理查找。整个管线在初始化时执行一次。

### 6.1 Stage 1：Equirectangular → Cubemap

**目的**：将 2D HDR 全景图转换为 6 面 Cubemap，便于后续以方向向量进行采样。

**映射原理**：球面坐标 ↔ UV 坐标

```glsl
vec2 SampleSphericalMap(vec3 v)
{
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= vec2(0.1591, 0.3183);   // 1/(2π), 1/π
    uv += 0.5;
    return uv;
}
```

**过程** (`pbr_renderer.cpp`)：
1. 创建 512×512 的 `GL_RGB16F` Cubemap 纹理
2. 设置 6 个 90° 透视投影的 view 矩阵（分别朝 +X, -X, +Y, -Y, +Z, -Z）
3. 用 FBO 逐面渲染，将全景图采样结果写入 Cubemap 的每个面
4. 生成 Mipmap 供后续预过滤使用

**输出**：`envCubemap` — 512×512 HDR 环境 Cubemap（带 Mipmap）

### 6.2 Stage 2：Irradiance Convolution（辐照度卷积）

**目的**：预计算漫反射环境光。对每个法线方向 N，在半球上积分所有入射光：

$$
L_{irradiance}(\mathbf{n}) = \int_{\Omega} L_i(\omega_i) \cos\theta \sin\theta \, d\omega_i
$$

这是一个低频信号（因为漫反射本身是低频的），所以只需要很低的分辨率。

**实现** (`irradiance.frag`)：
- 以当前像素位置的归一化方向作为法线 N
- 构建以 N 为 Z 轴的切线空间坐标系
- 在半球上均匀采样（步长 `sampleDelta = 0.025`，约 `~40000` 个采样点）
- 对每个采样方向，从 `envCubemap` 读取辐射度并加权

```glsl
for(phi = 0; phi < 2π; phi += 0.025)
    for(theta = 0; theta < π/2; theta += 0.025)
    {
        sampleVec = tangent_to_world(sin(θ)cos(φ), sin(θ)sin(φ), cos(θ));
        irradiance += texture(envCubemap, sampleVec).rgb * cos(θ) * sin(θ);
    }
irradiance = π * irradiance / nrSamples;
```

**输出**：`irradianceMap` — 32×32 Cubemap（低频，分辨率足够）

### 6.3 Stage 3：Pre-filtered Environment Map（预过滤环境贴图）

**目的**：预计算镜面反射环境光。不同粗糙度对应不同的高光扩散程度，存储在 Cubemap 的不同 Mip 级别中。

$$
L_{prefilter}(\mathbf{R}, \alpha) = \frac{\sum_{k=1}^{N} L_i(\mathbf{l}_k) \cdot (\mathbf{n} \cdot \mathbf{l}_k)}{\sum_{k=1}^{N} (\mathbf{n} \cdot \mathbf{l}_k)}
$$

**关键技术**：

1. **Importance Sampling (重要性采样)**：基于 GGX 分布，在高概率区域集中采样，减少方差
2. **Hammersley 序列**：低差异准随机序列，1024 个采样点
3. **Mip-level 采样**：根据 PDF 计算每个采样对应的 Mip 级别，避免亮点伪影

```
Mip Level │ roughness │ 分辨率
──────────┼───────────┼────────
    0     │   0.00    │ 128×128
    1     │   0.25    │  64×64
    2     │   0.50    │  32×32
    3     │   0.75    │  16×16
    4     │   1.00    │   8×8
```

**GGX 重要性采样** (`prefilter.frag`)：
```glsl
vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    // 转换到世界空间...
}
```

**输出**：`prefilterMap` — 128×128 Cubemap（5 个 Mip 级别，对应 roughness 0.0 → 1.0）

### 6.4 Stage 4：BRDF Integration LUT（BRDF 积分查找表）

**目的**：预计算 Split-Sum 近似的第二部分。将镜面 BRDF 积分拆分为两个独立查找：

$$
\int_{\Omega} f_r \cdot L_i \cdot \cos\theta \, d\omega \approx L_{prefilter}(\mathbf{R}, \alpha) \cdot \int_{\Omega} f_r \cdot \cos\theta \, d\omega
$$

BRDF 积分部分只依赖两个变量：`NdotV` 和 `roughness`，因此可预计算为 2D 查找表：

$$
\int_{\Omega} f_r \cos\theta \, d\omega = F_0 \cdot A(\text{NdotV}, \alpha) + B(\text{NdotV}, \alpha)
$$

- **R 通道 (A)**：Fresnel 缩放因子
- **G 通道 (B)**：Fresnel 偏移因子

**实现** (`brdf.frag`)：
```glsl
vec2 IntegrateBRDF(float NdotV, float roughness)
{
    // 固定 N = (0,0,1)
    // 对 1024 个 Hammersley 样本做 GGX 重要性采样
    // 计算 Geometry 项和 Fresnel 分离
    // 分别累加 A = (1-Fc)*G_Vis 和 B = Fc*G_Vis
    return vec2(A, B);
}
```

**注意**：BRDF LUT 中的几何项使用 IBL 版本的 k 值 $k_{IBL} = \frac{\alpha^2}{2}$，不同于直接光照的 $k_{direct} = \frac{(\alpha+1)^2}{8}$。

**输出**：`brdfLUT` — 512×512 `GL_RG16F` 2D 纹理

---

## 7. IBL 环境光照计算

在每帧 PBR 着色器中，利用预计算的三张贴图计算环境光照：

### 7.1 IBL 漫反射

```glsl
vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
vec3 kD = (1.0 - F) * (1.0 - metallic);

vec3 irradiance = texture(irradianceMap, N).rgb;    // 查找：方向 → 漫反射辐照度
vec3 diffuse = irradiance * albedo;
```

注意这里使用了 `fresnelSchlickRoughness`（考虑粗糙度的修正版）：

$$
F_{Schlick,rough} = F_0 + (\max(1 - \alpha, F_0) - F_0)(1 - \cos\theta)^5
$$

粗糙表面在掠射角处反射减弱（能量散射到更宽的方向），此修正避免了粗糙表面边缘过亮的问题。

### 7.2 IBL 镜面反射

```glsl
const float MAX_REFLECTION_LOD = 4.0;
vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
vec2 brdf = texture(brdfLUT, vec2(max(dot(N,V), 0.0), roughness)).rg;
vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);
```

- 反射方向 R 从预过滤 Cubemap 中采样，Mip 级别由粗糙度决定
- BRDF LUT 提供 Fresnel 积分的缩放 (brdf.x) 和偏移 (brdf.y) 因子
- 组合得到最终镜面反射色

### 7.3 合并

```glsl
vec3 ambient = (kD * diffuse + specular) * ao;
vec3 color = ambient + Lo;     // IBL 环境光 + 直接光照
```

---

## 8. HDR 与色调映射

PBR 在线性 HDR 色彩空间中计算（亮度值可超过 1.0），最终需要映射到 LDR 显示器的 [0,1] 范围。

### 8.1 Reinhard 色调映射

```glsl
color = color / (color + vec3(1.0));
```

将 [0,∞) 压缩到 [0,1)，保留相对亮度关系。

### 8.2 Gamma 校正

```glsl
color = pow(color, vec3(1.0/2.2));
```

将线性空间转换到 sRGB 空间以适配显示器的非线性响应。

**重要**：albedo 纹理在采样时需要从 sRGB 解码到线性空间：
```glsl
albedo = pow(texture(albedoMap, TexCoords).rgb, vec3(2.2));
```

---

## 9. 渲染管线执行流程

### 9.0 场景配置加载

```
SceneConfig::loadFromFile("scene.json")
    ├── 解析 JSON 文件
    │   ├── window   → 窗口尺寸、标题、MSAA
    │   ├── camera   → 位置、朝向、FOV、速度
    │   ├── lights[] → 每盏灯的位置、颜色、强度
    │   ├── material → albedo/metallic/roughness/ao + 贴图路径
    │   ├── environment.hdr → HDR 文件路径（不写死）
    │   └── grid     → 球体网格行列数、间距
    │
    └── applyConfig() → 将配置应用到 Camera、PBRMaterial、PointLight[]
```

### 9.1 初始化阶段

```
PBRRenderer::init(shaderDir)
    ├── 编译加载 6 个着色器程序
    │   ├── pbr.vert/frag          → pbrShader_
    │   ├── equirect_to_cubemap    → equirectToCubemapShader_
    │   ├── irradiance             → irradianceShader_
    │   ├── prefilter              → prefilterShader_
    │   ├── brdf                   → brdfShader_
    │   └── background             → backgroundShader_
    │
    ├── 绑定纹理单元
    │   ├── unit 0: irradianceMap
    │   ├── unit 1: prefilterMap
    │   ├── unit 2: brdfLUT
    │   └── unit 3-7: 材质贴图 (albedo, normal, metallic, roughness, ao)
    │
    ├── setupGeometry() → 统一创建几何体
    │   ├── Cube      → 用于 Cubemap 渲染和天空盒
    │   ├── Sphere    → 64×64 细分 UV 球体 (Triangle Strip)
    │   └── Quad      → 用于 BRDF LUT 全屏渲染
    │
    └── 创建 captureFBO_/captureRBO_ → 用于离屏渲染

SceneConfig::scanHDRFiles(directory)
    └── 扫描目录下所有 .hdr/.exr 文件 → 填充 ImGui 下拉列表
```

### 9.2 IBL 预计算阶段

```
PBRRenderer::setupIBL(hdrPath)            ← hdrPath 从 scene.json 读取
    ├── loadHDRTexture()                  → 加载 .hdr 文件为 GL_RGB16F 纹理
    │
    └── generateIBLMaps()
        │
        ├── [Pass 1] Equirect → Cubemap (512×512 × 6 faces)
        │   ├── 绑定 HDR 纹理
        │   ├── renderCubemapFaces() × 6 次 FBO 渲染
        │   └── 生成 Mipmap
        │
        ├── [Pass 2] Irradiance Convolution (32×32 × 6 faces)
        │   ├── 绑定 envCubemap_
        │   └── renderCubemapFaces() × 6 次 FBO 渲染
        │
        ├── [Pass 3] Pre-filter (128→64→32→16→8, 5 mip × 6 faces)
        │   ├── 绑定 envCubemap_
        │   └── renderCubemapFaces() × 5 mip × 6 面 = 30 次 FBO 渲染
        │
        └── [Pass 4] BRDF LUT (512×512, 一次全屏 quad 渲染)
            └── 纯数学计算，不依赖环境贴图

PBRRenderer::reloadIBL(newHdrPath)        ← 运行时切换 HDR
    ├── releaseIBL()                      → 释放旧 IBL 纹理
    └── setupIBL(newHdrPath)              → 重新生成
```

### 9.3 每帧渲染阶段

```
主循环每帧:
    │
    ├── 检测 HDR 切换请求 → reloadIBL()
    │
    ├── renderer.resize() → 同步窗口尺寸
    │
    ├── 更新灯光颜色 (lightColor × lightIntensity)
    │
    ├── 清屏
    │
    ├── renderScene()
    │   ├── setupCameraUniforms()   → projection, view, camPos
    │   ├── setupMaterialUniforms() → useTextures, albedo, metallic, roughness, ao
    │   ├── bindIBLTextures()       → irradiance, prefilter, brdfLUT
    │   ├── setupLightUniforms()    → lightPositions[], lightColors[], numLights
    │   └── 对每个球体:
    │       ├── 计算 model 矩阵和法线矩阵
    │       └── renderSphereGeometry() → drawElements(GL_TRIANGLE_STRIP)
    │
    ├── renderBackground()
    │   ├── 绑定 envCubemap_
    │   ├── 使用 view 矩阵的旋转部分（去除平移 → 天空盒）
    │   └── 深度设为 w (gl_Position.xyww) → 始终最远
    │
    └── drawUI() → ImGui 渲染叠加层
        ├── Material 面板     → albedo, metallic, roughness, ao 滑块
        ├── Sphere Grid 面板  → rows, cols, spacing
        ├── Lighting 面板     → 颜色、强度、每盏灯位置
        ├── Environment 面板  → HDR 文件扫描/选择/切换、背景开关
        ├── Camera 面板       → 位置、FOV、速度
        └── Save/Reload Config → scene.json 读写
```

---

## 10. 着色器文件对照表

| 着色器文件 | 阶段 | 输入 | 输出 | 说明 |
|-----------|------|------|------|------|
| `pbr.vert` | 每帧 | 顶点位置/法线/UV | WorldPos, Normal, TexCoords | 标准 MVP 变换 |
| `pbr.frag` | 每帧 | 材质参数 + IBL 贴图 + 光源 | 最终颜色 | Cook-Torrance BRDF + IBL |
| `equirectangular_to_cubemap.vert` | 预计算 | 立方体顶点 | WorldPos | 传递立方体方向 |
| `equirectangular_to_cubemap.frag` | 预计算 | HDR 全景 2D 纹理 | Cubemap 面颜色 | 球面坐标映射 |
| `irradiance.vert` | 预计算 | 立方体顶点 | WorldPos | 同上 |
| `irradiance.frag` | 预计算 | envCubemap | 漫反射辐照度 | 半球卷积积分 |
| `prefilter.vert` | 预计算 | 立方体顶点 | WorldPos | 同上 |
| `prefilter.frag` | 预计算 | envCubemap + roughness | 预过滤颜色 | GGX 重要性采样 |
| `brdf.vert` | 预计算 | 全屏 quad 顶点 | TexCoords | 直通 |
| `brdf.frag` | 预计算 | TexCoords (NdotV, roughness) | vec2(A, B) | BRDF 积分 LUT |
| `background.vert` | 每帧 | 立方体顶点 | WorldPos | 移除平移的天空盒 |
| `background.frag` | 每帧 | envCubemap | 天空盒颜色 | HDR 色调映射 |

---

## 11. 场景配置与动态 HDR 加载

### 11.1 JSON 配置系统

本项目通过 `scene.json` 外部化所有场景参数，避免硬编码。配置在启动时由 `SceneConfig::loadFromFile()` 解析，也可在运行时通过 ImGui 的 **Save Config / Reload Config** 按钮动态读写。

```
scene.json
├── window      → 窗口尺寸、标题、MSAA 采样数
├── camera      → 位置、yaw/pitch、FOV、移动速度、灵敏度、近远平面
├── lights[]    → 每盏灯: position, color, intensity（数量可变）
├── material    → albedo, metallic, roughness, ao + 5 个贴图路径
├── environment → HDR 文件路径, 清屏色, 是否显示天空盒背景
├── grid        → rows, cols, spacing, visible
└── shader_dir  → 着色器目录路径
```

支持命令行指定配置文件：`PBRRenderer.exe my_scene.json`

### 11.2 动态 HDR 切换

HDR 环境贴图不再硬编码文件名，而是通过以下机制管理：

1. **配置文件指定**：`environment.hdr` 字段指向 HDR 文件路径
2. **运行时扫描**：`SceneConfig::scanHDRFiles()` 遍历指定目录，收集所有 `.hdr`/`.exr` 文件
3. **ImGui 下拉选择**：用户可在 Environment 面板中选择不同的 HDR 文件
4. **热重载**：选择新 HDR 后，`PBRRenderer::reloadIBL()` 释放旧的 IBL 纹理，重新执行完整的 IBL 预计算管线

```
用户选择新 HDR
    │
    ▼
reloadIBL(newPath)
    ├── releaseIBL()          → 删除 envCubemap_, irradianceMap_, prefilterMap_, brdfLUTTexture_
    └── setupIBL(newPath)     → 重新执行 4-pass IBL 预计算
        ├── Pass 1: Equirect → Cubemap
        ├── Pass 2: Irradiance Convolution
        ├── Pass 3: Pre-filter (5 mip levels)
        └── Pass 4: BRDF LUT
```

### 11.3 代码架构

| 类/结构体 | 文件 | 职责 |
|-----------|------|------|
| `SceneConfig` | `scene_config.h/cpp` | JSON 解析、序列化、HDR 文件扫描 |
| `WindowConfig` | `scene_config.h` | 窗口参数 |
| `CameraConfig` | `scene_config.h` | 相机参数 |
| `LightConfig` | `scene_config.h` | 单盏灯参数（位置、颜色、强度） |
| `MaterialConfig` | `scene_config.h` | 材质参数 + 贴图路径 |
| `EnvironmentConfig` | `scene_config.h` | HDR 路径、清屏色、背景开关 |
| `GridConfig` | `scene_config.h` | 球体网格参数 |
| `AppState` | `main.cpp` | 运行时全局状态，集中管理 |
| `PBRRenderer` | `pbr_renderer.h/cpp` | 渲染核心，提供 `reloadIBL()` 热重载 |

---

## 参考文献

- [LearnOpenGL - PBR Theory](https://learnopengl.com/PBR/Theory)
- [LearnOpenGL - PBR Lighting](https://learnopengl.com/PBR/Lighting)
- [LearnOpenGL - IBL Diffuse Irradiance](https://learnopengl.com/PBR/IBL/Diffuse-irradiance)
- [LearnOpenGL - IBL Specular](https://learnopengl.com/PBR/IBL/Specular-IBL)
- Brian Karis, *Real Shading in Unreal Engine 4*, SIGGRAPH 2013
- Trowbridge & Reitz, *Average Irregularity Representation of a Rough Surface for Ray Reflection*, 1975
- Schlick, *An Inexpensive BRDF Model for Physically-Based Rendering*, 1994
