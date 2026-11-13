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
    // EvaluateRayTracingBRDF 返回 BRDF * NoL；尚未乘光源亮度或可见性。
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

使用完整模块的 Pass 声明：

```cpp
.Binds<InstanceBindingTag, GeometryBindingTag, MaterialBindingTag>()
```

每个执行 Scope 必须导入并声明以下读取，不能只绑定 SRV：

| 资源 tag | 读取方式 |
| --- | --- |
| `RayTracingSceneTag` | `ReadAccelerationStructure` |
| `InstancePoolTag` | `ReadBuffer` |
| `GeometryPoolTag` | `ReadBuffer` |
| `MeshGeometryPoolTag` | `ReadBuffer` |
| `MaterialPoolTag` | `ReadBuffer` |

render pass 的共享读取还需指定 shader stage；compute pass 的 stage 固定。
MaterialBindingTag 沿用现有的材质纹理上传 acquire 机制。
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
