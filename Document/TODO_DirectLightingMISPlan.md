# 双样本直接光照：光源采样 + GGX 镜面采样

2026-11-17 与用户确认。已验证的 BRDFResult、RT 投影开关等改动提交于 `a502d44f`。
本计划只补齐实时直接光照采样，不接入 GI、NRD 或 ReSTIR DI。

## 目标与预算

每个有效 GBuffer 像素每帧分配两个方向样本：

1. 一个光源样本：估计 diffuse，同时估计 specular 的光源采样部分。
2. 一个纯 GGX VNDF 反射方向样本：估计 specular 的 BRDF 采样部分。

Diffuse 只使用光源样本，不应用镜面采样的 MIS 权重。
Specular 使用两种采样策略，并通过 MIS 合并。两项合并后不再除以 2。
两方向样本是采样预算，不承诺固定两次 ray query：实际次数取决于命中和遮挡实现。

路径始终是相机 → 当前表面 → 光源。普通表面命中可以作为遮挡和距离数据，
不继续计算它的反射光。天空背景暂不作为可采样环境光。

## 现有基础

| 能力 | 代码 | 当前情况 |
|---|---|---|
| 分离 BRDF 求值 | `Lib/BRDF/BRDF.hlsli` | `EvaluateBRDF` 返回 `BRDFResult`，两项均已包含 NoL |
| GGX VNDF 半向量采样 | `Lib/Sample/BRDFSampling.hlsli` | `SampleGGXVisibleNormal` 已有 |
| 完整 BRDF 混合采样 | 同上 | `SampleBRDF` 在 cosine diffuse / GGX specular 之间选择 |
| 光源采样和 PDF | `Lib/Sample/LightSampling.hlsli` | 场景灯光 + 自发光网格；PDF 包含面积到立体角转换 |
| 几何求交和材质 | `Lib/RayTracing/RayTracing*.hlsli` | 能获得最近三角形及其自发光 |
| MIS | `Lib/Sample/MIS.hlsli` | 已有数值缩放后的 power heuristic，beta = 2 |
| 灯光投影开关 | `LightData` → `LightSample.CastShadow` | 关闭时跳过 RT 可见性查询 |
| 实时 DI 输出 | `Lighting/DirectLighting.hlsl` | 已接入一个光源样本 + 一个纯 GGX 样本，specular 使用 MIS |

矩形灯命中与查询接口已在步骤 2 补齐，步骤 3 新增双样本估计器，步骤 4 已接入实时 Pass。不能仅打开 BRDFSampleCount 或全局修改
SupportsBSDFSampling，否则光源侧会降低权重，却没有另一种估计补回贡献。

## 状态

| 步骤 | 内容 | 状态 |
|---|---|---|
| 1 | 提取 GGX 反射方向采样及纯镜面 PDF，原混合采样复用 | 代码已完成，待用户验证；未编译 |
| 2 | 解析矩形灯射线求交、命中位置重建及直接光源查询 | 代码已完成，待用户验证；未编译 |
| 3 | 实时 DI 双样本估计与 specular MIS | 代码已完成，待用户验证；未编译 |
| 4 | Pass 接线、独立 diffuse/specular 距离输出 | 代码已完成，待用户验证；未编译 |

按用户要求逐步写代码供审阅，不自动编译或运行测试；验证由用户进行。

## 步骤 1：镜面采样接口

在 `Engine/Asset/Shaders/Lib/Sample/BRDFSampling.hlsli` 增加：

```hlsl
float EvaluateGGXReflectionPdf(float3 N, float3 V, float3 L, float perceptualRoughness);

bool SampleGGXReflection(float3 N, float3 V, float perceptualRoughness,
                         float2 randomSample, out float3 direction, out float pdf);
```

接口只处理方向和对应 PDF，不读取场景、灯光或材质绑定，不计算光照贡献。

- N、V 和求 PDF 时的 L 均为世界空间单位向量，V / L 从表面向外。
- 随机数为 `[0, 1)^2`，内部复用现有 Heitz VNDF 半向量采样。
- 返回的是世界空间反射方向，不是半向量。
- PDF 以立体角为测度，已包含半向量到反射方向的 Jacobian。
- 纯 GGX PDF 不包含 diffuse/specular 的选择 PMF，也不是完整混合 PDF。
- 两个接口均使用现有 `BRDF_MIN_PERCEPTUAL_ROUGHNESS = 0.045` 下限。
- NoV <= 0、反射落在 shading hemisphere 以下等情况返回失败，输出清零。
- 失败样本作为零贡献保留，不重采样，不对剩余有效方向重新归一化 PDF。

将原混合接口改为复用公共函数：

```text
EvaluateBRDFPdf
    = (1 - specularPmf) * cosinePdf + specularPmf * EvaluateGGXReflectionPdf

SampleBRDF
    = 原有随机选择
      ├─ diffuse：原 cosine 采样
      └─ specular：SampleGGXReflection
    → 返回原有完整混合 PDF 和 diffuse + specular 响应
```

保留 `BRDFSample`、`SampleRayTracingBRDF` 和参考积分器的现有语义与随机维度分配。
这一步不启用新的实时镜面样本，预期画面不因新增接口改变。

## 步骤 2：光源命中与可见性

### 解析矩形灯

新增纯几何求交函数：射线先与灯所在平面求交，再判断灯面范围和发光朝向。
返回距离和 `[0, 1]^2` 灯面坐标，构造现有 `LightSampleLocation`。
通过 `ReconstructLightSample` 统一重建 Radiance、Distance、条件 PDF 和 CastShadow，
确保与光源采样侧的颜色、强度、单面发光和 range window 完全一致。

第一版遍历场景矩形灯，复杂度随矩形灯数量增长；本计划不新增光源 BVH。
解析矩形灯仍然不是遮挡几何，不能引入“最近解析灯挡住其他解析灯”的新规则。
同一方向可对多个相交的解析矩形灯分别评估、分别计算 MIS，再求和。

已实现的分层：

| 层次 | 接口 | 文件 |
|---|---|---|
| 纯矩形几何 | `WorldRectangle`、`RectangleHit`、`IntersectRectangle` | `Lib/Geometry/Rectangle.hlsli` |
| 灯光几何适配 | `IntersectRectLight`，单面发光 | `Lib/Sample/LightIntersection.hlsli` |
| 候选灯枚举 | `FindNextRectLightCandidate`，当前为线性扫描 | 同上 |
| 灯面命中枚举 | `FindNextRectLightIntersection`，返回位置引用与射线参数 | 同上 |
| 重建与可见性 | `QueryNextVisibleRectLight` | `Lib/RayTracing/RayTracingLighting.hlsli` |

`IntersectRectangle` 本身支持两面，使用单位法线及单位 Right 构成的正交坐标系。
半尺寸必须为正，射线区间为闭区间 `[TMin, TMax]`；平行、不在区间内、非有限距离及矩形外命中返回失败。
只有方向归一化时，返回的 T 才是世界距离。灯光适配层额外拒绝背面命中。
解析求交是长期保留的几何算法，未来可替换候选搜索层，不要求把灯构建成场景 TLAS 中的三角形。

每条新射线将 `nextLightIndex` 初始化为 0，重复调用查询接口直到返回 false。
枚举不会将 TMax 缩短为最近灯或最近场景命中，查询会为每盏命中的非零亮度灯分别重建并判断可见性。
查询接口不会主动开启矩形灯的 SupportsBSDFSampling，也不计算 BRDF 或 MIS。

### 自发光网格

复用 `TraceClosestOpaqueTriangle`、表面重建和自发光求值。
只计算最近场景命中的自发光，不继续传播普通表面的反射光。
通过实例、三角形和重心坐标构造 `LightSampleLocation`，复用 `EvaluateLightPdf`。

已增加 `MakeEmissiveLightSampleLocation` 和 `ReconstructEmissiveLightSample`。
调用者先执行已有的 `TraceClosestOpaqueTriangle`，成功后才重建发光样本；
普通/黑色表面返回 false、光照样本清零，但调用者持有的场景命中和距离仍可保留。
发光样本重建复用现有单/双面发光策略、纹理 LOD 和 PDF；非候选发光实例仍能重建，竞争光源 PDF 为 0。
`EvaluateLightPdfAtHit` 也复用相同的位置引用构造，避免两份实例/三角形/重心坐标映射。

### 可见性与射线偏移

- `CastShadow = true`：矩形灯前有场景遮挡时无贡献。
- `CastShadow = false`：即使矩形灯在场景命中后面，也保留其无遮挡贡献。
  不能在查到最近场景命中后立即结束所有矩形灯检查。
- 自发光网格继续遵循实际几何遮挡。
- 采样位置、BRDF 和 PDF 使用原始接收点；射线偏移只服务可见性。
- 解析灯几何命中和修正起点后的可见性需要明确协调，不能静默改变采样方向或重复使用不匹配的 PDF。
- 沿用当前透明/裁剪几何的查询限制，不在本步骤扩展透明阴影。

`QueryNextVisibleRectLight` 要求 ray.Origin、context.WorldPosition 和 surface.WorldPosition
均为原始接收点，ray.Direction 是采样得到的单位方向。灯面事件由这条射线定义；
实际遮挡调用已有 `TraceSampledLightVisibility`，从修正起点重新瞄准该灯面点，并排除终点。
网格命中的射线区间、起点策略由后续估计器显式指定，重建函数不会隐式改动它们。
本步骤未修改 DirectLighting Pass 的样本数或参考积分器的能力标记。

## 步骤 3：双样本估计与 MIS

设 `D_L` / `S_L` 为光源方向上的 diffuse / specular 响应乘以入射光和可见性，
`S_G` 为 GGX 方向的 specular 对应项。BRDFResult 已含 NoL，不能再乘一次余弦。

```text
p_L = 选灯 PMF * 光源条件立体角 PDF
p_G = 纯 GGX 反射方向 PDF

w_L = p_L^2 / (p_L^2 + p_G^2)
w_G = p_G^2 / (p_G^2 + p_L^2)

Diffuse  = D_L / p_L
Specular = w_L * S_L / p_L + w_G * S_G / p_G
```

两个权重分别在各自样本的方向和光源事件上计算，使用现有 `MISPowerHeuristic`。
单个 GGX 方向评估多个解析光源时，每个光源事件使用自己的光源选择 PMF / 条件 PDF。
不要遗漏选灯 PMF，不要额外乘镜面选择 PMF，也不要将合并结果再平均为一半。

点光、聚光和当前零角尺寸方向光不能通过连续 GGX 方向采样命中，其光源侧权重保持 1。
不在候选列表中的发光几何没有竞争光源 PDF，其 GGX 命中权重同样为 1。

新的实时 DI 入口需要显式使用纯镜面采样策略和相应能力。
参考积分器仍使用原有混合 BRDF 采样，其 PDF 和矩形灯命中能力不能被全局标志修改影响。
在实际命中分支完成并分配样本之前，不得提前降低任何光源样本的 MIS 权重。

### 已实现的入口

新增 `Lib/RayTracing/RayTracingDirectLighting.hlsli`，与参考积分器的混合 BRDF 估计入口分开：

```hlsl
DirectLightingEstimate EvaluateDirectLightingMIS(GeometrySurface surface, RayTracingMaterial material,
    float3 V, LightSamplingContext context, float4 lightRandomSample, float2 ggxRandomSample,
    SamplerState materialSampler, RayTracingLightingSettings settings, float3 shadowOriginOffset);
```

- `EvaluateDirectLightingLightSample`：一个光源样本，diffuse 不使用 MIS；specular 的竞争 PDF 为纯 GGX PDF。
- `EvaluateDirectLightingGGXSample`：一个 GGX 方向，只累加自发光网格及解析矩形灯的 specular。
  普通表面命中不计算反射光，场景 miss 不增加天空光。
- `EvaluateDirectLightingGGXContribution`：对每个发光事件查询完整光源 PDF 并计算权重。
  自发光网格不在候选列表时，光源 PDF 为零，GGX 权重为 1。
- 返回 `Diffuse` / `Specular` 及两份独立的原始距离元数据；不进行去调制、曝光或 NRD 打包。
  `LightSampleDistance` 沿用光源采样距离；`GGXHitDistance` 保留原始 GGX 射线的最近命中距离，
  包括场景三角形及可见的非零发光矩形灯，miss 为 MaxDistance，拒绝的方向为零。
  步骤 4 的 Pass 分别输出这两份距离，具体约定见下表。

GGX 发现射线从原始接收点出发，区间为 `[RayBias, MaxDistance]`，方向和 PDF 均不因 shadowOriginOffset 改变。
调用者保证 `0 < RayBias < MaxDistance`。区间以外的光源事件没有竞争 GGX PDF，光源侧保留完整权重。
解析矩形灯逐个重建并调用修正起点后的遮挡查询；不按最近场景命中截断枚举。

自发光网格的镜面估计需要额外统一两种技术的可见性：GGX 只能发现原始射线上的最近三角形，
因此光源侧的 mesh specular 也检查原始接收点到发光点的遮挡，再与修正起点后的可见性相交。
GGX 侧最近命中已确认原始路径，随后只补修正起点的查询。两侧都保留现有 RayBias 终点排除，
属于当前数值偏移近似，不宣称严格无偏。Diffuse 保留原有修正阴影策略；矩形灯不受此 mesh 发现限制。
这会增加 mesh specular 光源样本的可见性查询，两个方向样本不等于固定两次 ray query。

步骤 3 单独完成时未修改 `Lighting/DirectLighting.hlsl` 的调用和随机维度；新增入口由步骤 4 启用。
参考积分器的矩形灯能力标记保持原样。

## 步骤 4：Pass 与降噪信号

- 光源样本继续使用一组 Sobol 随机数，GGX 样本使用独立的随机维度组。
- 合并后保留现有两张去调制 RGB 输出，合成 Pass 的材质还原和预曝光处理不变。
- 将单个距离语义拆开：diffuse 先保留光源采样距离，specular 保存 GGX 方向查询的距离。
- GGX 方向命中普通表面时，距离可供后续反射跟踪使用，但 RGB 不增加间接光贡献。
- 距离不相加、不除 PDF；miss、无效方向、关闭投影和解析灯命中的距离约定需要明确。
- 更新 SceneTextures 的契约说明。实际 NRD 打包、范围处理及距离规则在接入 NRD 时对齐。
- `DirectLightingMaterialFactors` 的保护下限保持 `0.02`。

### 已完成的接线与输出约定

`Lighting/DirectLighting.hlsl` 改为调用 `EvaluateDirectLightingMIS`：

- 光源随机数保留 `SobolBurley4D(frameNumber, 0u, seed)`；GGX 使用 `SobolBurley2D(frameNumber, 1u, seed)`。
  两者使用不同的维度组和各自接口的 scramble，像素 seed 不随帧变化，帧号只推进序列。
- Diffuse RGB 仍仅来自光源样本；Specular RGB 为两个 MIS 估计项之和，不再除以 2。
- 两份 RGB 仍按 `DirectLightingMaterialFactors` 去调制，合成时还原材质并应用一次预曝光。
  `DirectLightingComposite.hlsl` 无需改变，也不读取距离通道。
- NormalRoughness.a 输出有效粗糙度 `max(saturate(GBuffer.Roughness), 0.045)`，
  对齐 BRDF 和 GGX 采样下限。法线、ViewZ、Motion 的空间与编码规则保持原样。
- 保留现有五个 MRT 和全部资源绑定，C++ Pass 不需要增加纹理或常量。

距离均从原始接收点计算，写入各自纹理的 alpha；不乘 MIS、不除 PDF、不除材质因子，也不把两项距离相加。
输出沿用 `min(distance, 65504)` 的范围约定；无效 GBuffer 或 lightingReady 为零时，两份 alpha 都为零。

| 信号 / 情形 | alpha 约定 |
|---|---|
| Diffuse，有效有限光源样本 | 接收点到采样灯面点或点光位置的距离；被遮挡、关闭投影均保留 |
| Diffuse，无效或零响应光源样本 | 0 |
| Diffuse，方向光 | MaxDistance，输出封顶为 65504 |
| Specular，GGX 方向被拒绝 | 0；光源侧仍可产生 specular，距离不回退到光源样本 |
| Specular，最近场景三角形命中 | 命中距离；普通表面仍不产生 GI |
| Specular，同时命中可见且非零发光的矩形灯 | 取场景距离与这些矩形灯距离的最小值；不截断后续灯光枚举 |
| Specular，关闭投影的矩形灯位于场景命中后方 | 灯仍贡献 RGB，距离保留更近的场景命中 |
| Specular，没有场景或有效矩形灯命中 | MaxDistance，输出封顶为 65504 |
| Specular，BRDF 镜面响应为零 | 保留 GGX 场景命中 / miss 距离，不额外查询矩形灯 |

Specular 的 RGB 混合了两个样本，而距离只取自 GGX 方向，是当前引擎的原始辅助信号，
不是整份混合 RGB 的唯一光程。尤其关闭投影灯可越过场景几何贡献 RGB，距离仍描述最近事件。
接入 NRD 时仍需核对所选降噪器的距离语义、normal/roughness 打包、hit-distance 归一化和范围处理。

步骤 4 完成后实时画面开始使用双样本估计。预期低粗糙度矩形灯高光更容易被采到，
但 diffuse 仍只有一个光源样本，阴影和漫反射噪声不会因此自动消失；尚无 NRD 或新的时间累积。

## 用户验证时的重点

1. 第一步：原混合采样使用相同随机输入时，方向与混合 PDF 的行为保持一致。
   检查纯 GGX PDF 和混合 PDF 的区别，失败样本不重试。
2. 第二步：矩形边界、单面发光、遮挡前后关系、多个解析灯重叠与 CastShadow 开关。
3. 第三步：单独检查未经过 TAA 的 diffuse / specular；增加 GGX 样本后不能出现系统性增亮。
   检查点光、矩形灯、自发光网格，以及多个光源的选择 PMF。
4. 第四步：低粗糙度高光、有效 GBuffer 粗糙度及距离信号。区分采样噪声、Specular AA 和 TAA 的影响。
5. 矩形灯用面光源求交，自发光网格用场景几何求交；避免同一个物理发光体被重复注册为两种来源。

## 参考

- [PBRT：Direct Lighting](https://pbr-book.org/3ed-2018/Light_Transport_I_Surface_Reflection/Direct_Lighting)
- [PBRT：A Better Path Tracer](https://pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer)
- 现有 VNDF 实现及其原始资料链接：`Engine/Asset/Shaders/Lib/Sample/BRDFSampling.hlsli`。
