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
12. [渲染性能优化](#12-渲染性能优化)
13. [RHI 渲染硬件接口](#13-rhi-渲染硬件接口)

---

## 1. 总体架构

```
┌─────────────────────────────────────────────────────────┐
│                   Application Layer                     │
│                                                         │
│    main.cpp ──────► PBRRenderer                         │
│      │                 │                                │
│      │  createOpenGL   │  device_->create*()            │
│      │  Device()       │  ctx_->bindShader()            │
│      ▼                 │  ctx_->drawIndexed*()          │
│                        ▼                                │
├─────────────────────────────────────────────────────────┤
│              RHI Interface Layer (纯虚接口)               │
│                                                         │
│   RHIDevice   RHIContext   RHICommandList               │
│   RHIBuffer   RHITexture   RHIShader                    │
│   RHIFramebuffer   RHIVertexInput   RHITimerQuery       │
├─────────────────────────────────────────────────────────┤
│            OpenGL 3.3 Backend (具体实现)                  │
│                                                         │
│   GLDevice   GLContext   GLCommandList                  │
│   GLBuffer   GLTexture   GLShader                       │
│   GLFramebuffer   GLVertexInput   GLTimerQuery          │
│                                                         │
│   内部调用: glGen* / glBind* / glDraw* / glUniform*      │
└─────────────────────────────────────────────────────────┘
```

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
│  球体网格                                                │
│    ├── 视锥体剔除 → 跳过屏幕外球体                          │
│    ├── LOD 选择   → 按距离分配 64/32/16 段网格              │
│    └── 按 LOD 分桶 → ≤3 次 Instanced Draw Call            │
│                                                         │
│  对每个可见片段:                                           │
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
main():
    ├── GLFW/GLAD 初始化 → 创建窗口和 OpenGL 上下文
    ├── createOpenGLDevice()    → 创建 RHIDevice (多后端入口)
    └── ctx->setDepthTest() / setSeamlessCubemap() / setMultisample()

PBRRenderer::init(device, shaderDir)
    ├── 保存 device_ 和 ctx_ 指针
    │
    ├── 读取 GLSL 源码 → device_->createShader() 编译 6 个着色器
    │   ├── pbr.vert/frag          → pbrShader_        (unique_ptr<RHIShader>)
    │   ├── equirect_to_cubemap    → equirectToCubemapShader_
    │   ├── irradiance             → irradianceShader_
    │   ├── prefilter              → prefilterShader_
    │   ├── brdf                   → brdfShader_
    │   └── background             → backgroundShader_
    │
    ├── ctx_->bindShader() + setInt() → 绑定纹理单元
    │   ├── unit 0: irradianceMap
    │   ├── unit 1: prefilterMap
    │   ├── unit 2: brdfLUT
    │   └── unit 3-7: 材质贴图 (albedo, normal, metallic, roughness, ao)
    │
    ├── setupGeometry() → 通过 device_->createBuffer/createVertexInput 创建几何体
    │   ├── Cube      → cubeVBO_ + cubeInput_ (RHIVertexInput)
    │   ├── Sphere LOD 0 → 64×64 细分 (vertexBuffer + indexBuffer + instanceBuffer + vertexInput)
    │   ├── Sphere LOD 1 → 32×32 细分
    │   ├── Sphere LOD 2 → 16×16 细分
    │   └── Quad      → quadVBO_ + quadInput_
    │
    ├── device_->createTimerQuery()     → timerQuery_
    └── device_->createFramebuffer()    → captureFBO_ (用于离屏 IBL 渲染)

SceneConfig::scanHDRFiles(directory)
    └── 扫描目录下所有 .hdr/.exr 文件 → 填充 ImGui 下拉列表
```

### 9.2 IBL 预计算阶段

```
PBRRenderer::setupIBL(hdrPath)            ← hdrPath 从 scene.json 读取
    ├── loadHDRTexture()                  → device_->createTexture() + upload()
    │
    └── generateIBLMaps()                 ← 全部通过 RHI 接口操作
        │
        ├── [Pass 1] Equirect → Cubemap (512×512 × 6 faces)
        │   ├── device_->createTexture(CubeMap, RGB16F)
        │   ├── ctx_->beginPass(captureFBO_)
        │   ├── ctx_->attachCubeFace() × 6 面 + renderCubeGeometry()
        │   ├── ctx_->endPass()
        │   └── envCubemap_->generateMipmaps()
        │
        ├── [Pass 2] Irradiance Convolution (32×32 × 6 faces)
        │   ├── device_->createTexture(CubeMap, RGB16F)
        │   ├── ctx_->bindTexture(0, envCubemap_)
        │   └── ctx_->beginPass() → attachCubeFace() × 6 → endPass()
        │
        ├── [Pass 3] Pre-filter (128→64→32→16→8, 5 mip × 6 faces)
        │   ├── device_->createTexture(CubeMap, RGB16F, mipmaps=true)
        │   ├── ctx_->bindTexture(0, envCubemap_)
        │   └── 5 mip × 6 面 = 30 次 attachCubeFace() + draw
        │
        └── [Pass 4] BRDF LUT (512×512, 一次全屏 quad 渲染)
            ├── device_->createTexture(Texture2D, RG16F)
            └── ctx_->attachTexture2D() + renderQuadGeometry()

PBRRenderer::reloadIBL(newHdrPath)        ← 运行时切换 HDR
    ├── releaseIBL()                      → unique_ptr::reset() 自动释放
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
    ├── ctx->setViewport() + ctx->clear()           ← 通过 RHI 清屏
    │
    ├── renderScene()
    │   ├── ctx_->getTimerResultMs()        → 读取上帧 GPU 耗时（无阻塞）
    │   ├── ctx_->bindShader(pbrShader_)
    │   ├── setupCameraUniforms()           → shader->setMat4/setVec3
    │   ├── setupMaterialUniforms()         → ctx_->bindTexture(slot, tex)
    │   ├── bindIBLTextures()               → ctx_->bindTexture(0/1/2, ...)
    │   ├── setupLightUniforms()            → shader->setVec3/setInt
    │   ├── ctx_->beginTimerQuery()
    │   ├── extractFrustum(VP)              → 提取 6 个裁剪平面
    │   └── 对每个球体:
    │       ├── 视锥体剔除 → 不可见则跳过
    │       ├── LOD 选择   → 按距离分桶 (LOD 0/1/2)
    │       ├── instanceBuffer->update()    → 上传实例数据
    │       ├── ctx_->bindVertexInput()
    │       └── ctx_->drawIndexedInstanced()
    │   ctx_->endTimerQuery()
    │
    ├── renderBackground()
    │   ├── ctx_->bindShader(backgroundShader_)
    │   ├── ctx_->bindTexture(0, envCubemap_)
    │   ├── ctx_->bindVertexInput(cubeInput_)
    │   └── ctx_->draw(Triangles, 36)
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

**应用层**

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
| `PerfStats` | `main.cpp` | 帧时间、GPU 时间、剔除/LOD 统计 |
| `PBRRenderer` | `pbr_renderer.h/cpp` | 渲染核心，通过 RHI 接口操作，零 `gl*` 调用 |
| `PBRMaterial` | `pbr_renderer.h` | 材质参数 + `RHITexture*` 贴图指针 |
| `SphereMesh` | `pbr_renderer.h` (内嵌) | 单个 LOD 级别（`RHIBuffer` + `RHIVertexInput` + indexCount） |

**RHI 接口层** — 纯虚基类，与后端无关

| 类 | 文件 | 职责 |
|----|------|------|
| `RHIDevice` | `rhi/rhi_device.h` | 资源工厂（createBuffer/Texture/Shader/CommandList/...） |
| `RHIContext` | `rhi/rhi_context.h` | 命令提交（draw/bind/clear/submit/...） |
| `RHICommandList` | `rhi/rhi_command_list.h` | 延迟命令录制接口（多线程安全，主线程回放） |
| `RHIBuffer` | `rhi/rhi_resource.h` | GPU 缓冲区抽象（Vertex/Index/Instance） |
| `RHITexture` | `rhi/rhi_resource.h` | 纹理抽象（2D/CubeMap，upload/generateMipmaps） |
| `RHIShader` | `rhi/rhi_resource.h` | 着色器程序抽象（uniform 设置） |
| `RHIFramebuffer` | `rhi/rhi_resource.h` | 离屏渲染目标抽象 |
| `RHIVertexInput` | `rhi/rhi_resource.h` | 顶点输入布局抽象（VAO 概念） |
| `RHITimerQuery` | `rhi/rhi_resource.h` | GPU 计时查询抽象 |

**OpenGL 3.3 后端** — 具体实现

| 类 | 文件 | 职责 |
|----|------|------|
| `GLDevice` | `rhi/opengl/gl_device.h/cpp` | 通过 `glGen*` 创建 GL 资源 |
| `GLContext` | `rhi/opengl/gl_context.h/cpp` | 通过 `glBind*/glDraw*` 执行命令 + `submit()` 回放命令列表 |
| `GLCommandList` | `rhi/opengl/gl_command_list.h/cpp` | `std::variant` 命令录制，无 GL 依赖，线程安全 |
| `GLBuffer` | `rhi/opengl/gl_resources.h/cpp` | 封装 `glGenBuffers` / `glBufferData` |
| `GLTexture` | `rhi/opengl/gl_resources.h/cpp` | 封装 `glGenTextures` / `glTexImage2D` |
| `GLShader` | `rhi/opengl/gl_resources.h/cpp` | 吸收原 `Shader` 类，封装 `glCreateProgram` |
| `GLFramebuffer` | `rhi/opengl/gl_resources.h/cpp` | 封装 FBO + RBO |
| `GLVertexInput` | `rhi/opengl/gl_resources.h/cpp` | 封装 VAO + 顶点属性配置 |
| `GLTimerQuery` | `rhi/opengl/gl_resources.h/cpp` | 封装 `GL_TIME_ELAPSED` 双缓冲查询 |

---

## 12. 渲染性能优化

### 12.1 性能瓶颈分析

在大规模球体网格场景中（如 50×50 = 2500 球体），最初的优化方向是通过 instancing 合批 Draw Call（从 N 次 `glDrawElements` 降为 1 次 `glDrawElementsInstanced`）。但实测表明此优化收效甚微。

**原因：瓶颈在 GPU 片段着色器，而非 CPU 端 Draw Call 开销。**

每个像素的 PBR 片段着色器需要执行：

| 操作 | 次数/开销 |
|------|-----------|
| 点光源 BRDF（GGX NDF + Smith G + Fresnel） | ×4 光源 |
| IBL 漫反射采样 (`irradianceMap`) | 1 次 Cubemap 采样 |
| IBL 镜面反射采样 (`prefilterMap`) | 1 次 Cubemap LOD 采样 |
| BRDF LUT 查找 (`brdfLUT`) | 1 次 2D 纹理采样 |
| 材质纹理采样（纹理模式下） | 最多 5 次（albedo, normal, metallic, roughness, ao） |
| 法线贴图 TBN 构建 | `dFdx`/`dFdy` 偏导计算 |
| 色调映射 + Gamma 校正 | 2 次 `pow` |

总计每像素 **10+ 次纹理采样 + 大量数学运算**。合批 Draw Call 仅减少 CPU 端的 OpenGL API 调用开销（驱动验证、状态检查），对 GPU 的顶点/片段吞吐量毫无影响。在 7×7 = 49 球体规模下，49 次 Draw Call 对现代驱动几乎没有负担。

因此，真正有效的优化必须减少 GPU 需要处理的**几何量**和**片段量**。

### 12.2 视锥体剔除 (Frustum Culling)

**原理**：从 View-Projection 矩阵中提取 6 个裁剪平面（左、右、上、下、近、远），对每个球体执行球体-平面距离测试。若球体完全在任一平面外侧，则跳过绘制。

**平面提取**（Gribb-Hartmann 方法）：

设 $M = P \times V$ 为组合矩阵，$\text{row}_i$ 为其第 $i$ 行（0-indexed），则 6 个平面为：

$$
\begin{aligned}
\text{Left}   &= \text{row}_3 + \text{row}_0 \\
\text{Right}  &= \text{row}_3 - \text{row}_0 \\
\text{Bottom} &= \text{row}_3 + \text{row}_1 \\
\text{Top}    &= \text{row}_3 - \text{row}_1 \\
\text{Near}   &= \text{row}_3 + \text{row}_2 \\
\text{Far}    &= \text{row}_3 - \text{row}_2
\end{aligned}
$$

每个平面 $(a, b, c, d)$ 归一化后，球心 $\mathbf{c}$ 与半径 $r$ 的判定条件：

$$
\text{visible} = \forall i \in [0,5]: \; a_i c_x + b_i c_y + c_i c_z + d_i \geq -r
$$

**性能影响**：

- 相机靠近网格时，大量远处球体被剔除，可减少 **50-80%** 的几何提交
- 剔除发生在 CPU 端，不产生 GPU 开销
- 每个球体仅需 6 次点积运算，即使 10000 个球体也在微秒级完成

**实现** (`pbr_renderer.cpp`)：

```cpp
struct Frustum { glm::vec4 planes[6]; };

Frustum extractFrustum(const glm::mat4& vp) {
    Frustum f;
    auto row = [&](int r) {
        return glm::vec4(vp[0][r], vp[1][r], vp[2][r], vp[3][r]);
    };
    f.planes[0] = row(3) + row(0);  // left
    f.planes[1] = row(3) - row(0);  // right
    // ... bottom, top, near, far
    for (auto& p : f.planes)
        p /= glm::length(glm::vec3(p));  // normalize
    return f;
}

bool sphereInFrustum(const Frustum& f, const glm::vec3& center, float radius) {
    for (int i = 0; i < 6; ++i)
        if (glm::dot(glm::vec3(f.planes[i]), center) + f.planes[i].w < -radius)
            return false;
    return true;
}
```

### 12.3 LOD 系统 (Level of Detail)

**原理**：远处球体在屏幕上只占少量像素，使用高面数网格浪费顶点处理能力。根据球体到相机的距离，选择不同细分级别的网格。

**LOD 级别配置**：

| LOD | 细分段数 | 顶点数 | 索引数 | 适用距离 | 相对 LOD 0 顶点比 |
|-----|---------|--------|--------|----------|-------------------|
| 0 | 64×64 | 4225 | 8320 | < 25 单位 | 100% |
| 1 | 32×32 | 1089 | 2112 | 25–60 单位 | 25.8% |
| 2 | 16×16 | 289 | 544 | > 60 单位 | 6.8% |

远处球体顶点数降低最高 **~15 倍**。由于远处球体像素覆盖面积小，视觉差异几乎不可察觉。

**与 Instancing 结合**：

将通过视锥剔除的球体按 LOD 级别分桶，每个 LOD 级别拥有独立的 `RHIVertexInput` 和 Instance `RHIBuffer`，通过 `ctx_->drawIndexedInstanced()` 调用。最多 3 次 Draw Call 即可渲染整个场景。

```
渲染流程:
    ├── 对每个球体:
    │   ├── 视锥体剔除 → 不可见则跳过
    │   ├── 计算到相机距离 → 选择 LOD 级别
    │   └── 写入对应 LOD 桶的 InstanceData
    │
    └── 对每个非空 LOD 桶:
        ├── instanceBuffer->update()             → 上传实例数据
        ├── ctx_->bindVertexInput(vertexInput)   → 绑定该 LOD 的顶点输入
        └── ctx_->drawIndexedInstanced()         → 一次绘制所有实例
```

**网格创建** (`pbr_renderer.cpp`)：

每个 LOD 级别在初始化时调用 `createSphereMesh()` 生成不同分辨率的 UV 球体。该函数通过 `device_->createBuffer()` 创建顶点/索引/实例缓冲区，再通过 `device_->createVertexInput()` 配置顶点输入布局（per-vertex: location 0-2，per-instance: model 矩阵 → location 3-6，材质参数 → location 7）。

### 12.4 GPU Timer Query

CPU 端的帧时间（`glfwGetTime()` 差值）包含了 CPU 逻辑、驱动开销、VSync 等待等成分，无法准确反映 GPU 实际渲染耗时。通过 RHI 的 `RHITimerQuery` 可精确测量 GPU 执行命令所花费的纳秒数（OpenGL 后端内部使用 `GL_TIME_ELAPSED` 查询）。

**双缓冲策略**：

为避免查询结果读取阻塞 CPU 等待 GPU 完成，`GLTimerQuery` 内部维护两个 Query 对象交替工作——当前帧启动查询 A，读取上一帧查询 B 的结果（此时 B 已确保完成）。

```
帧 N:   beginTimerQuery(q)  ...渲染...  endTimerQuery(q)
帧 N+1: getTimerResultMs(q) → 读取帧 N 的 GPU 耗时（无阻塞）
        beginTimerQuery(q)  ...渲染...  endTimerQuery(q)
帧 N+2: getTimerResultMs(q) → 读取帧 N+1 的 GPU 耗时（无阻塞）
        beginTimerQuery(q)  ...渲染...  endTimerQuery(q)
```

**实现** (`pbr_renderer.cpp`)：

```cpp
// 读取上一帧的查询结果（延迟一帧，无阻塞，内部自动交换双缓冲索引）
gpuTimeMs_ = ctx_->getTimerResultMs(timerQuery_.get());

// 启动本帧查询
ctx_->beginTimerQuery(timerQuery_.get());
// ... 渲染 ...
ctx_->endTimerQuery(timerQuery_.get());
```

### 12.5 Performance 面板

ImGui 的 Performance 面板展示以下实时指标：

| 指标 | 说明 |
|------|------|
| **FPS** | 每秒帧数 |
| **CPU Frame** | CPU 端帧时间（含 avg/min/max） |
| **GPU Time** | GPU 实际渲染耗时（毫秒） |
| **Draw Calls** | 当前帧的绘制调用次数 |
| **Visible / Culled** | 通过/被剔除的球体数量 |
| **LOD 0/1/2** | 各级 LOD 的球体分布 |
| **Vertices** | 考虑 LOD 后的实际顶点总量 |

通过对比开关 Instancing 前后的 GPU Time 和 FPS，可以直观验证"Draw Call 合批对 GPU-bound 场景无效，而视锥剔除 + LOD 才是有效优化"这一结论。

### 12.6 优化效果对比

以 50×50 = 2500 球体（`scene_stress_test.json`）为例，相机距离网格中心约 80 单位：

| 优化策略 | 可见球体 | 顶点总量 | Draw Calls | 效果 |
|----------|---------|---------|------------|------|
| 无优化（逐球绘制） | 2500 | ~10.6M | 2500 | 基准 |
| 仅 Instancing | 2500 | ~10.6M | 1 | CPU 减负，GPU 无变化 |
| Instancing + 视锥剔除 | ~800* | ~3.4M | ≤1 | GPU 工作量降低 ~68% |
| Instancing + 视锥剔除 + LOD | ~800* | ~0.5M* | ≤3 | GPU 工作量降低 **~95%** |

*实际数值取决于相机位置和朝向

---

## 13. RHI 渲染硬件接口

### 13.1 设计动机

在引入 RHI 之前，`PBRRenderer` 直接调用 `gl*` 函数（如 `glGenBuffers`、`glBindTexture`、`glDrawElementsInstanced`），与 OpenGL API 强耦合。这带来两个问题：

1. **不可移植**：若未来需要支持 Vulkan 或 D3D12，必须重写整个渲染器
2. **职责混乱**：GPU 资源管理、渲染状态设置、绘制命令提交散布在渲染逻辑中

RHI (Rendering Hardware Interface) 通过纯虚接口将图形 API 抽象为统一操作，使渲染器仅依赖抽象接口，后端实现可独立替换。

### 13.2 架构分层

```
include/rhi/
├── rhi_types.h          # 枚举（BufferType, TextureFormat, PrimitiveType...）
│                        # 描述符结构体（BufferDesc, TextureDesc, ShaderDesc...）
├── rhi_resource.h       # 资源纯虚基类（RHIBuffer, RHITexture, RHIShader...）
├── rhi_command_list.h   # 命令列表纯虚接口（多线程录制）
├── rhi_device.h         # 资源工厂接口 + createOpenGLDevice() 入口
├── rhi_context.h        # 命令提交接口（draw, bind, clear, pass, submit...）
└── rhi.h                # umbrella include

include/rhi/opengl/
├── gl_device.h          # GLDevice：实现 RHIDevice
├── gl_context.h         # GLContext：实现 RHIContext（含 submit 回放）
├── gl_command_list.h    # GLCommandList：variant 命令录制
└── gl_resources.h       # GLBuffer/GLTexture/GLShader/GLFramebuffer/
                         # GLVertexInput/GLTimerQuery 实现 + 格式映射函数

src/rhi/opengl/
├── gl_device.cpp
├── gl_context.cpp
├── gl_command_list.cpp
└── gl_resources.cpp
```

### 13.3 类型系统

所有枚举和描述符结构体定义在 `rhi_types.h` 中，与后端无关：

| 枚举 | 值 | 用途 |
|------|----|------|
| `BufferType` | Vertex, Index, Instance | 缓冲区用途 |
| `BufferAccess` | Static, Dynamic, Stream | 数据更新频率 |
| `TextureType` | Texture2D, TextureCubeMap | 纹理类型 |
| `TextureFormat` | R8, RGB8, RGBA8, RG16F, RGB16F | 像素格式 |
| `PrimitiveType` | Triangles, TriangleStrip | 图元类型 |
| `CompareFunc` | Less, LessEqual, Always | 深度比较函数 |
| `WrapMode` | Repeat, ClampToEdge | 纹理环绕模式 |
| `FilterMode` | Nearest, Linear, LinearMipLinear | 纹理过滤模式 |

描述符结构体采用 POD 风格，创建资源时传入：

```cpp
TextureDesc desc;
desc.type      = TextureType::TextureCubeMap;
desc.format    = TextureFormat::RGB16F;
desc.width     = 512;
desc.height    = 512;
desc.minFilter = FilterMode::LinearMipLinear;
auto tex = device->createTexture(desc);
```

### 13.4 资源生命周期

所有 GPU 资源通过 `RHIDevice` 工厂方法创建，返回 `std::unique_ptr`，由应用层持有。RAII 保证资源在 unique_ptr 析构时自动释放（OpenGL 后端调用对应的 `glDelete*`）。

```
创建:  auto buf = device->createBuffer(desc);    → GLBuffer 构造时调用 glGenBuffers + glBufferData
使用:  buf->update(data, size);                   → glBufferData 重新分配
销毁:  buf.reset();                               → ~GLBuffer() 调用 glDeleteBuffers
```

`PBRRenderer` 的成员变量从原来的 `unsigned int` GL 句柄全部替换为 `std::unique_ptr<RHI*>` 智能指针：

| 原成员 | 新成员 | 类型 |
|--------|--------|------|
| `unsigned int cubeVAO_` | `cubeInput_` | `unique_ptr<RHIVertexInput>` |
| `unsigned int cubeVBO_` | `cubeVBO_` | `unique_ptr<RHIBuffer>` |
| `unsigned int envCubemap_` | `envCubemap_` | `unique_ptr<RHITexture>` |
| `Shader pbrShader_` | `pbrShader_` | `unique_ptr<RHIShader>` |
| `unsigned int captureFBO_` | `captureFBO_` | `unique_ptr<RHIFramebuffer>` |
| `unsigned int gpuTimerQueries_[2]` | `timerQuery_` | `unique_ptr<RHITimerQuery>` |

### 13.5 命令提交模型

RHI 支持两种命令提交方式：

**1. Immediate Mode — `RHIContext` 直接调用**

每个方法调用立即执行对应的 GPU 命令，适用于单线程场景：

```cpp
ctx->bindShader(shader);
ctx->bindVertexInput(vi);
ctx->drawIndexedInstanced(PrimitiveType::TriangleStrip, indexCount, instances);
```

**2. Deferred Mode — `RHICommandList` 多线程录制 + 主线程提交**

`RHICommandList` 将命令录制与执行分离。录制阶段不调用任何图形 API，可安全地在任意工作线程上进行；提交阶段在持有 GL 上下文的主线程上回放所有录制的命令。

```
┌──────────────────────────────┐
│        Worker Thread(s)      │    ← 无 GL 上下文
│                              │
│  cmdList->bindShader(s)      │    录制为 cmd::BindShader{s}
│  cmdList->bindTexture(0, t)  │    录制为 cmd::BindTexture{0, t}
│  cmdList->drawIndexed(...)   │    录制为 cmd::DrawIndexed{...}
│  cmdList->setShaderMat4(...) │    录制为 cmd::SetShaderMat4{...}
│  cmdList->updateBuffer(...)  │    深拷贝数据到 vector<char>
└──────────────┬───────────────┘
               │ 传递 cmdList
               ▼
┌──────────────────────────────┐
│       Render Thread          │    ← 持有 GL 上下文
│                              │
│  ctx->submit(cmdList)        │    遍历 variant 列表，
│                              │    逐条调用 GLContext 的对应方法
│  cmdList->reset()            │    清空命令列表以复用
└──────────────────────────────┘
```

使用示例：

```cpp
auto cmdList = device->createCommandList();

// 工作线程可安全录制（不触发 gl* 调用）
cmdList->bindShader(pbrShader.get());
cmdList->setShaderMat4(pbrShader.get(), "projection", projMatrix);
cmdList->bindVertexInput(sphereInput.get());
cmdList->drawIndexedInstanced(PrimitiveType::TriangleStrip, indexCount, 100);

// 主线程提交（在 GL 上下文中回放）
ctx->submit(cmdList.get());
cmdList->reset();
```

**OpenGL 后端实现**：`GLCommandList` 使用 `std::variant` 存储命令，每条命令是一个轻量 POD 结构体（如 `cmd::BindShader{shader}`、`cmd::Draw{pt, count, first}`）。`GLContext::submit()` 通过 `std::visit` 遍历命令列表，对每个 variant 调用对应的 GL 方法。

`RHICommandList` 在 `RHIContext` 基础上额外提供：

| 方法 | 说明 |
|------|------|
| `reset()` | 清空命令列表，复用同一对象避免重复分配 |
| `setShaderInt/Float/Vec3/Mat3/Mat4` | 录制 uniform 设置（捕获 shader 指针 + 参数值） |
| `updateBuffer(buf, data, size)` | 录制缓冲区更新，深拷贝数据确保线程安全 |
| `resizeFramebufferDepth(fb, w, h)` | 录制 FBO 深度附件尺寸调整 |

关键方法分组（`RHIContext` + `RHICommandList` 共有）：

| 分组 | 方法 | 说明 |
|------|------|------|
| Pass 管理 | `beginPass(fb)` / `beginDefaultPass()` / `endPass()` | 绑定/解绑 FBO |
| 状态 | `setViewport` / `clear` / `setDepthTest` / `setSeamlessCubemap` | 渲染状态设置 |
| 绑定 | `bindShader` / `bindVertexInput` / `bindTexture(slot, tex)` | 资源绑定 |
| 绘制 | `draw` / `drawIndexed` / `drawIndexedInstanced` | 提交绘制命令 |
| FBO 附件 | `attachTexture2D` / `attachCubeFace` | IBL 预计算时动态切换渲染目标 |
| 计时 | `beginTimerQuery` / `endTimerQuery` / `getTimerResultMs`(仅 Context) | GPU 性能测量 |
| 提交 | `submit(cmdList)`（仅 Context） | 回放命令列表中的所有命令 |

### 13.6 OpenGL 后端实现要点

GL 后端通过 `static_cast` 将 `RHI*` 基类指针转换为 `GL*` 具体类型，获取内部 GL 句柄：

```cpp
void GLContext::bindTexture(int slot, RHITexture* tex) {
    auto* glTex = static_cast<GLTexture*>(tex);
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(glTex->glTarget(), glTex->glId());
}
```

格式映射通过独立的转换函数集中管理：

| 函数 | 输入 | 输出 |
|------|------|------|
| `toGLBufferTarget()` | `BufferType` | `GL_ARRAY_BUFFER` / `GL_ELEMENT_ARRAY_BUFFER` |
| `toGLBufferUsage()` | `BufferAccess` | `GL_STATIC_DRAW` / `GL_STREAM_DRAW` |
| `toGLFormats()` | `TextureFormat` | `internalFormat` + `pixelFormat` + `dataType` |
| `toGLPrimitive()` | `PrimitiveType` | `GL_TRIANGLES` / `GL_TRIANGLE_STRIP` |

`GLShader` 吸收了原独立的 `Shader` 类（`shader.h/cpp`），包含完整的着色器编译/链接逻辑和 uniform location 缓存。

### 13.7 扩展性

添加新的图形后端（如 Vulkan）只需：

1. 实现 `VkDevice : RHIDevice`、`VkContext : RHIContext`、`VkCommandList : RHICommandList` 及所有资源类
2. 创建 `createVulkanDevice()` 工厂函数
3. `main.cpp` 中根据配置选择 `createOpenGLDevice()` 或 `createVulkanDevice()`

`PBRRenderer` 和所有渲染逻辑无需任何修改。

对于 Vulkan 后端，`RHICommandList` 可自然映射为 `VkCommandBuffer`：
- `VkCommandList::beginPass()` → `vkCmdBeginRenderPass()`
- `VkCommandList::drawIndexedInstanced()` → `vkCmdDrawIndexed()` with instanceCount
- `VkContext::submit()` → `vkQueueSubmit()` 提交 command buffer
- 多个 command list 可在不同线程录制后批量提交到同一个 queue

---

## 参考文献

- [LearnOpenGL - PBR Theory](https://learnopengl.com/PBR/Theory)
- [LearnOpenGL - PBR Lighting](https://learnopengl.com/PBR/Lighting)
- [LearnOpenGL - IBL Diffuse Irradiance](https://learnopengl.com/PBR/IBL/Diffuse-irradiance)
- [LearnOpenGL - IBL Specular](https://learnopengl.com/PBR/IBL/Specular-IBL)
- Brian Karis, *Real Shading in Unreal Engine 4*, SIGGRAPH 2013
- Trowbridge & Reitz, *Average Irregularity Representation of a Rough Surface for Ray Reflection*, 1975
- Schlick, *An Inexpensive BRDF Model for Physically-Based Rendering*, 1994
- Gribb & Hartmann, *Fast Extraction of Viewing Frustum Planes from the World-View-Projection Matrix*, 2001
