# 公共命中点着色

入口：`engine://Shaders/Lib/RayTracing/RayTracingSurface.hlsli`。
供 ReferencePathTracer、RT 反射及 GI 共用；不依赖 GBuffer、相机、曝光或积分器。

## 数据链

| 信息 | 来源 |
| --- | --- |
| 实例槽位 | TLAS 的 instance ID；不是 TLAS 数组下标 |
| 三角形、重心坐标、射线参数、正背面 | ray query 的 committed hit |
| 模型矩阵、法线矩阵、几何和材质槽位 | `g_Instances[hit.InstanceIndex]` |
| 顶点起点、索引起点、索引字节数 | `g_Geometries[instance.GeometryIndex]` |
| 三角形位置、法线、切线、UV0 | `g_MeshGeometry`，几何池的整段 raw SRV |
| PBR 参数与五种贴图的 bindless 索引 | `g_Materials[instance.MaterialIndex]` |

几何池的标准顶点为 48 字节，与 `Resource::StandardVertex` 一致。
`FirstIndex` 和 `VertexOffset` 是元素偏移。`IndexByteStride` 为 0/2/4，分别表示
无索引、UINT16、UINT32。UINT16 允许起点只有两字节对齐。

## Shader 调用

```hlsl
#include <Shaders/Lib/RayTracing/RayTracingSurface.hlsli>

RayTracingHit hit;
if (TraceClosestOpaqueTriangle(ray, 0xff, hit))
{
    RayTracingSurface surface = LoadRayTracingSurface(ray, hit);
    RayTracingMaterial material = EvaluateRayTracingMaterial(surface, samplerState, mipLevel);
    // 材质输出包括 WorldNormal、DiffuseColor、SpecularColor、Inputs.EmissiveColor 等。
    // EvaluateRayTracingBRDF 返回 BRDFResult，Diffuse / Specular 均包含 NoL；
    // 尚未乘光源亮度或可见性，需要总值时显式将两项相加。
}
```

- `TraceClosestOpaqueTriangle` 返回最近三角形；miss 时不可调用重建函数。
- `TraceOpaqueVisibility` 查询调用者给定的射线段是否无遮挡，可提前结束求交。
- 世界位置为 `Origin + T * Direction`。只有方向归一化时 T 才是世界距离。
- 几何法线面向入射射线，供起点偏移和几何半球检查使用；着色法线独立保留。
- `WorldTriangleNormal` 始终保留变换后的索引绕序方向，不随入射射线翻转。
- 重建支持非均匀缩放和镜像变换。两侧均可求交、着色；单面剔除策略由后续调用者决定。
- 贴图读取显式使用 `SampleLevel`；调用者指定 LOD，尚未估算 ray cone 或 ray differentials。
  光栅化入口仍使用隐式导数，材质因子、通道解释和法线贴图求值与 RT 共用。
- 输出在线性、未预曝光的尺度下。AO 作为材质数据返回，不乘入追踪光照。
  粗糙度保留原值；BRDF 求值入口采用现有解析 BRDF 的 0.045 下限。

## Pass 与 RenderGraph 契约

无需声明组：shader 反射到 space3/4/6 时，图按 space 找到对应的共享组并绑定。

每个执行 Scope 必须导入并声明以下读取，不能只绑定 SRV：

| 资源 tag | 读取方式 |
| --- | --- |
| `RayTracingSceneTag` | `ReadAccelerationStructure` |
| `InstancePoolTag` | `ReadBuffer` |
| `GeometryPoolTag` | `ReadBuffer` |
| `MeshGeometryPoolTag` | `ReadBuffer` |
| `MaterialPoolTag` | `ReadBuffer` |

render pass 的共享读取还需指定 shader stage；compute pass 的 stage 固定。
材质纹理以 ImportSharedSet<MaterialTextureTag> 读取，上传承接由这些读取完成。
只做可见性查询时，仅需 `RayTracing.hlsli`、AS binding 和 AS 读取。

## 当前范围

支持引擎标准格式的静态三角形网格、每个 BLAS 单个几何，以及当前 glTF metallic-roughness
材质。暂不提供 alpha-test 候选过滤、透明/折射、变形几何或多几何 BLAS 的解码。
`FORCE_OPAQUE` 会把透明和裁剪材质当作实体；Opacity 返回值不会改变求交结果。
材质的单/双面标志尚未进入 GPU 记录，因此当前按两侧表面重建。

本模块提供求交、表面重建、材质与 BRDF 求值。光源采样、BRDF 采样/PDF、MIS、
路径吞吐量、累积与输出仍属于下一步参考积分器的工作。

## 公共三角形与任意表面访问

`Lib/Geometry/MeshGeometry.hlsli` 提供标准顶点、索引和三角形读取。
`Lib/Geometry/GeometrySurface.hlsli` 提供世界空间三角形、面积及重心坐标插值；
不依赖 TLAS 或命中记录，供后续光源表面采样复用。

`RayTracingSurface` 是公共 `GeometrySurface` 的别名；原有命中点入口继续保留，
内部调用公共插值后恢复射线命中位置，并按入射方向定向表面。

## 解析矩形灯与光源命中

`Lib/Geometry/Rectangle.hlsli` 提供纯解析矩形求交，不访问场景或加速结构。
`WorldRectangle` 使用正交的单位 Normal / Right 及正半尺寸，`IntersectRectangle`
返回射线参数、灯面坐标与正背面；矩形边界及射线区间边界包含在内。

`Lib/Sample/LightIntersection.hlsli` 将矩形灯适配到几何接口，并分开候选枚举与命中枚举：

- `IntersectRectLight`：只使用传入的 LightData，并按现有单面发光策略拒绝背面。
- `FindNextRectLightCandidate`：遍历当前场景矩形灯，是以后替换灯光搜索结构的位置。
- `FindNextRectLightIntersection`：返回 `LightSampleLocation` 和射线参数；每条新射线从游标 0 开始。

`RayTracingLighting.hlsli` 的 `QueryNextVisibleRectLight` 复用光源重建和可见性查询。
射线起点必须是原始接收点，方向必须归一化；修正起点只用于随后重新瞄准灯面点的遮挡射线。
解析灯互不遮挡，因此要遍历所有命中。不能按场景命中提前截断，否则关闭投影的灯会被错误遮挡。

自发光网格先使用已有最近三角形查询，再调用 `ReconstructEmissiveLightSample`。
普通表面没有光照贡献，但场景命中距离仍由调用者持有；不继续计算间接反弹。

这些查询接口供下述实时双样本估计使用，不改变参考积分器的矩形灯 MIS 能力标记。
分步计划见 `Document/TODO_DirectLightingMISPlan.md`。

## 实时双样本直接光

`Lib/RayTracing/RayTracingDirectLighting.hlsli` 提供 `EvaluateDirectLightingMIS`：
调用者传入一组独立的 float4 光源随机数和 float2 GGX 随机数，每种技术各一个样本。
Diffuse 仅由光源样本估计；Specular 合并光源与纯 GGX VNDF 样本，使用 power heuristic，结果不除以 2。
返回线性 RGB 以及光源采样距离、GGX 最近命中距离两份原始元数据。

GGX 只计算发光网格和解析矩形灯，不计算普通表面的反射光或天空光。
解析灯分别使用自己的光源 PDF，保留重叠灯及关闭投影灯的贡献；delta 灯的光源侧权重为 1。
自发光网格的镜面估计两侧均要求原始路径可见及修正起点后的路径可见，确保最近网格发现与光源采样匹配。
几何发现和 PDF 使用原始接收点；数值偏移仍沿用现有 RayBias 近似。

DirectLighting Pass 已调用新入口。光源使用 SobolBurley4D 的维度组 0，GGX 使用 SobolBurley2D
的维度组 1；同一像素的 seed 保持稳定，帧号推进序列。Diffuse alpha 保留光源采样距离；
Specular alpha 取 GGX 最近场景三角形及可见非零发光矩形灯的最近距离。
关闭投影灯仍可在几何后方贡献 RGB，但不会覆盖更近的场景距离；普通表面命中只提供距离。
光源无效 / GGX 方向被拒绝时各自距离为 0，miss / 无限距离输出封顶为 65504。
距离不参与 MIS 或材质去调制，合成只读取 RGB。NormalRoughness 的粗糙度下限为 0.045，
与实际 BRDF/GGX 采样一致。完整信号契约见 `Feature/SceneTextures/SceneTextures.h`。
这些仍是引擎原始辅助信号，NRD 接入需要另外匹配 SDK 的打包及距离语义。
