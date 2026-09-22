# 渲染管线实现总览与路线图

## 背景与目标

延迟管线已跑通并完成结构对齐：Shadow → DepthPre → GBuffer(+Velocity) → VelocityResolve → ShadowProjection →
Lights → IndirectDiffuse → Reflections → Skybox → TemporalAA → Tonemap → UI。本文档规划从这里走到一条功能完整的
**光栅化 + ray query 混合管线**。

**对齐 UE 是硬目标**，理由是以后能把 UE 的渲染算法原样抄进来。对齐分三层，缺一层抄算法时就要写转换层：

1. **帧结构** —— Pass 的顺序与职责边界对应 `FDeferredShadingSceneRenderer::Render` 与 `AddPostProcessingPasses`。
2. **数据契约** —— GBuffer 编码对应 `FGBufferData`，View 参数对应 `FViewUniformShaderParameters` 的子集，
   SceneColor 使用 PreExposure 语义，深度使用 reversed-Z。
3. **插件点** —— 按信号降噪（`IScreenSpaceDenoiser`）、时域上采样（`ITemporalUpscaler`）、GI 方法选择
   （`DynamicGlobalIlluminationMethod`）。

不是每个功能都要实现，但**结构和接缝先对**，之后加功能是加法。

本文档是总览。每个阶段开工时拆出独立的 `TODO_*Plan.md`，本文档只更新状态和指向。

---

## 状态

| 阶段 | 内容 | 状态 |
|---|---|---|
| P1 | 时序基础 + TAA（含提前的 reversed-Z） | **已完成**，见 `TODO_TemporalPlan.md`（`ITemporalUpscaler` 抽象推迟到第二个实现） |
| P2 | 结构对齐（GBuffer / PreExposure / 光照拆分 / ShadowMask） | **已完成**，见 `TODO_StructureAlignPlan.md`（reversed-Z 已提前到 P1 完成） |
| P3 | 后处理主干（自动曝光 / Bloom / Tonemap） | 计划已定，见 `TODO_PostProcessPlan.md` |
| P4 | 屏幕空间效果（HZB / GTAO / Contact Shadow / SSR） | 未开始 |
| P5 | 透明物体（BlendMode / Translucency / Fog） | 未开始 |
| P6 | 光追阴影 / RTAO + NRD | 未开始 |
| P7 | 命中点着色 | 未开始 |
| P8 | DDGI | 未开始 |
| P9 | DOF / MotionBlur / 其余 | 未开始 |

---

## 核心决策

### 1. 可见性与光照解耦：信号纹理

光照 Pass 不直接采样阴影图、不自己算 AO，只读取**屏幕空间信号纹理**。每种信号的来源可替换（光栅 / 屏幕空间 /
光追），在管线搭建期按设备能力和设置选择：

| 信号 | 消费方 | 来源 |
|---|---|---|
| `ShadowMask`（每灯一个通道，4 灯打包一个 array slice） | Lights | ShadowProjection（光栅 atlas）/ RayTracingShadows |
| `AmbientOcclusion` | IndirectDiffuse、Reflections | GTAO / RTAO |
| `DiffuseIndirect` | IndirectDiffuse | 无（天光 SH/IBL）/ DDGI / 以后 SSGI、Lumen |
| `Reflections` | Reflections | 预滤波 cube / SSR / 以后 RT 反射 |

对应 UE 的 `ScreenShadowMaskTexture`、`RenderDiffuseIndirectAndAmbientOcclusion`、
`RenderDeferredReflectionsAndSkyLighting`。

### 2. 跨帧持久资源由实体持有，不硬编码在 View 上

TAA history、自动曝光、上一帧 SceneColor 属于 View；DDGI 的 irradiance / distance 纹理属于**探针体积实体**。
机制统一为：任意实体持有持久 image → 帧初 Import 进图 → 帧末交换/保留。对应 UE 的 `FSceneViewState` +
`RegisterExternalTexture` / `QueueTextureExtraction`。

### 3. 光追只用 inline ray query

阴影、AO、DDGI 探针更新都在 CS 里用 `RayQuery` 完成，不引入 RT PSO / SBT / hit shader。命中点材质在 uber
shader 里求值（材质是"参数 + bindless 纹理"的单一 PBR 模型，成立）。RT pipeline 推迟到出现需要多样化材质代码的
场景（shader graph 材质、高质量 RT 反射）时再加——它和 ray query 共用加速结构，是纯加法。

### 4. 降噪用 NRD，原生接入

NRD（SIGMA 阴影、REBLUR occlusion AO，以后 REBLUR/RELAX 做反射与 GI）跨厂商、纯 compute shader。
**不走 NRDIntegration/NRI**，而是按 `GetInstanceDesc` / `GetComputeDispatches` 走自己的 RHI 与 RenderGraph：
permanent pool → 持久资源（决策 2），transient pool → 图的 transient，dispatch 列表 → 一个 CustomPipeline
compute pass。封装在 `IScreenSpaceDenoiser` 形状的接口之后。

OIDN 只用于将来的烘焙/路径追踪预览；DLSS RR / FSR Ray Regeneration 若要支持，接在 `ITemporalUpscaler` 位置，
并让对应信号的降噪器可配为直通。

### 5. RHI 光追接口按 Vulkan 语义设计

遵循 CLAUDE.md "从写下起对所有后端正确"：加速结构是独立对象与独立绑定类型（不是 buffer SRV），BLAS 输入缓冲
在创建时声明用途，compaction 大小延迟一帧回读。细节见 §三 I7。

### 6. 数据契约照搬 UE 命名

新增的 View 参数、GBuffer 通道、shader 函数尽量沿用 UE 的名字（`PreExposure`、`ClipToPrevClip`、
`ShadingModelID`、`ConvertFromDeviceZ`……），抄 shader 时减少改名。

---

## 一、目标帧结构

`✅` 已有　`◐` 已有但需调整　`☐` 缺失　`—` 预留位置，暂不排期

```
                                             UE 对应                                   阶段
── Scene Update ─────────────────────────────────────────────────────────────────────────
✅ Camera / Shadow / View / Scene / Material / Instance bindings                         
☐  RayTracingScene Update (BLAS build/compact, TLAS)   FRayTracingScene               P6
☐  DDGI Update (trace → blend → border → relocate)     (RTXGI 插件)                    P8
—  GPU Culling (compute cull → indirect draw)          GPUScene culling
—  WaterInfo (俯视渲染水面高度/深度/流速)                WaterZone
── Geometry ─────────────────────────────────────────────────────────────────────────────
✅ ShadowPass (atlas)                                   RenderShadowDepthMaps
◐  DepthPrePass → SceneDepth                           PrePass                    ✅rev-Z P5(Masked)
☐  HZB                                                 BuildHZB                        P4
—  DBuffer Decals / CustomDepth
✅ GBufferPass → Normal/Surface/BaseColor + Vel + Color BasePass                       (D 待第二着色模型)
── Lighting ─────────────────────────────────────────────────────────────────────────────
☐  AmbientOcclusion → AmbientOcclusion                 GTAO / RTAO                     P4 P6
◐  ShadowProjection / RTShadows → ShadowMask[light]    RenderShadowProjections     ✅光栅 P6
✅ Lights (一个全屏 draw 循环所有灯)                    RenderLights
◐  IndirectDiffuse (sky/IBL | DDGI) × AO               RenderDiffuseIndirectAndAO   ✅IBL P4 P8
◐  Reflections (SSR ∪ prefiltered cube) × EnvBRDF      RenderDeferredReflections... ✅cube P4
✅ Skybox                                              Sky / SkyAtmosphere
—  VolumetricCloud                                     RenderVolumetricCloud
—  SingleLayerWater (折射水下 SceneColor + 吸收散射)     RenderSingleLayerWater
—  VolumetricFog (froxel) → HeightFog / Translucency   ComputeVolumetricFog
☐  HeightFog                                           RenderFog                       P5
── Translucency ─────────────────────────────────────────────────────────────────────────
☐  Translucency (BeforeDOF) → SceneColor               RenderTranslucency              P5
☐  Translucency (AfterDOF) → SeparateTranslucency      (separate translucency)         P5
☐  Distortion                                          RenderDistortion                P5
── PostProcess ──────────────────────────────────────────────────────────────────────────
☐  DOF                                                 DiaphragmDOF                    P9
☐  Composite AfterDOF translucency                                                     P5
✅ TemporalAA（以后 TSR/DLSS/FSR 走同一位置）           ITemporalUpscaler
☐  MotionBlur                                          MotionBlur                      P9
☐  SceneDownsample                                     FSceneDownsampleChain           P3
☐  Histogram → EyeAdaptation                           EyeAdaptation                   P3
☐  Bloom                                               Bloom                           P3
◐  Tonemap (AgX + Look + bloom + exposure)              Tonemap                         P3
—  FXAA                                                FXAA                            不做
✅ UI
```

现有 Pass 名保留（DepthPrePass / GBufferPass 等），对齐的是职责和数据，不强求改名。

---

## 二、数据契约

### ViewBindings 新增（P1）

| 字段 | UE 名 | 用途 |
|---|---|---|
| 上一帧 ViewProj / View | `PrevViewToClip`、`PrevTranslatedWorldToView` 等 | Velocity、SSR、NRD |
| `ClipToPrevClip` | 同 | 静态物体用深度重建 Velocity |
| 当前/上一帧 jitter | `TemporalAAJitter`（xy 当前，zw 上一帧） | TAA |
| 无 jitter 的投影 | `ViewToClipNoAA` | Velocity、UI 类不抖动的绘制 |
| 渲染区域尺寸 | `ViewSizeAndInvSize`、`BufferSizeAndInvSize` | 所有屏幕空间 Pass |
| 深度线性化参数 | `InvDeviceZToWorldZTransform` | `ConvertFromDeviceZ`、NRD viewZ |
| `PreExposure` | 同 | 所有写 SceneColor 的 shader |
| 帧序号 | `FrameNumber`、`StateFrameIndexMod8` | 噪声序列、TAA |
| 相机位置（当前/上一帧） | `WorldCameraOrigin`、`PrevWorldCameraOrigin` | |

### InstanceData 新增（P1）

`m_prevModel`（上一帧 object → world），静态网格 vertex factory 算上一帧位置用。144B → 208B，同步改
`InstanceData.hlsli` 与 `static_assert`。

### Vertex Factory 契约（P1 定 Velocity 部分，P5 随 I5 定其余部分）

对应 UE 的 `FVertexFactory`：Pass × Material × VertexFactory 组合出 shader 排列。VS 侧"几何从哪来、怎么变形"
由 vertex factory 负责，Pass 和材质不关心。目的是所有权边界：新几何不改任何 Pass，新 Pass（含用户 Pass）不关心
有哪些几何。

- **Pass shader 是模板**，只调用 factory 函数（`VertexFactoryGet*`、`GetMaterialPixelParameters` 等）与材质函数
  （`CalcMaterialParameters`、`GetMaterialWorldPositionOffset`、`GetMaterialMask`）。命名按 UE 语义、不带 `F` 前缀。
  材质侧现在是固定 uber 实现，以后材质图生成同一组函数。
- **VS 同时输出当前与上一帧的 clip 位置**。Velocity 由此得出，不由 Pass 用 `m_prevModel` 统一计算——波浪、地形
  LOD 形变、蒙皮的上一帧位置只有 factory 自己知道。
- **factory 私有数据走 bindless 索引 + per-instance 结构化数据**，不新增 SRG 槽位，守住"同一 pass 共享
  PipelineLayout"的约束（`TODO_PerDrawPSOVariant.md`）。
- **factory 函数写成不依赖 VS 专属语义的纯函数**，以后可从 Mesh Shader 调用（MS 是 factory 的另一种执行后端，不替代
  factory）。可考虑按 (instanceId, vertexId) 从缓冲读顶点，与 P7 命中点还原顶点属性共用。
- **用户 Pass 两种模式**：Mesh 模式只给 PS，VS 由 factory 注入，对所有几何成立；Raw 模式自带 VS + InputLayout，
  只接受指定 factory。
- 路线图内只有静态网格一个 factory；地形、水面、蒙皮、粒子是第二用例（§六）。P1 只定 HLSL 契约、把 GBuffer /
  DepthOnly 重构为模板；组合机制（include 重定向、排列 key 进 `ShaderDescriptor::Hash()`、排列过滤 / 缓存 /
  异步编译回退、排列 layout 须为 Pass layout 子集的校验）到第二个 factory 或开放用户 mesh pass 时再建。

### GBuffer 布局（P2）

已落地。名字用语义名而非 UE 的字母，`GBufferData` 的**字段名**与 `FGBufferData` 保持一致——那才是抄 shader 时
被引用的东西。

| MRT | 本引擎 | 内容 | 格式 | UE 对应 |
|---|---|---|---|---|
| 0 | SceneColor | 自发光；光照 Pass 往上 additive | RGBA16F | SceneColor |
| 1 | GBufferNormal | 世界法线 `N*0.5+0.5`、a 预留 | R10G10B10A2 | GBufferA |
| 2 | GBufferSurface | Metallic、Specular、Roughness、ShadingModelID | RGBA8 | GBufferB |
| 3 | GBufferBaseColor | BaseColor、GenericAO | RGBA8_SRGB | GBufferC |
| 4 | Velocity | 屏幕空间运动向量 | RG16F | Velocity |
| 5 | —— | CustomData，等第二个着色模型 | RGBA8 | GBufferD |

SceneColor 的创建者是 GBufferPass。解码集中在 `Lib/DeferredShadingCommon.hlsli`（对应
`DeferredShadingCommon.ush`）：`GetGBufferData()` 与不取深度的 `DecodeGBufferData()`，纹理作为参数传入，
所有消费方（Lights / IndirectDiffuse / Reflections / 以后 SSR、GTAO）共用。

### 深度（P1）

reversed-Z，主相机与阴影 View 统一：DepthPre / Shadow `Less → Greater`、清除值 `1 → 0`、LightingPass 全屏三角形改放
z=0 并用 `Less`。UE 的 `ConvertFromDeviceZ` 及 TAA、大量屏幕空间 shader 默认此约定，所以提前到 P1、先于 TAA。
改动清单见 `TODO_TemporalPlan.md` §一。

---

## 三、基础设施清单

各阶段引用这里的编号。

| 编号 | 内容 | 首个用例 | 阶段 |
|---|---|---|---|
| I0 | 实体持有的持久图资源（决策 2） | TAA history | ✅ P1 |
| I1 | View 时序参数 + jitter 序列 + `m_prevModel` | Velocity / TAA | ✅ P1 |
| I2 | 渲染分辨率 / 输出分辨率分离，支持半分辨率 Pass | TAA（100% 缩放，只立接缝） | ✅ P1（缩放固定 100%） |
| I3 | Compute pass 进图（ReadWrite attachment 已支持，尚无用例） | Histogram / Bloom | P3 |
| I4 | per-subresource barrier（IBL 计划里记录的欠账） | HZB 逐 mip 生成 | P4 |
| I5 | 按材质 PSO 变体 + BlendMode（见 `TODO_PerDrawPSOVariant.md`），变体范围包含 VS 与 InputLayout（为 I10 留位） | Masked | P5 |
| I6 | View 上的 PostProcessSettings（对应 `FinalPostProcessSettings`） | 曝光 / Bloom 参数 | P3 |
| I7 | RHI 光追：加速结构对象与绑定类型、BLAS 输入缓冲用途位、build/update/compaction、实例结构、能力检测、SM6.5 / `SPV_KHR_ray_query` 编译 | RT 阴影 | P6 |
| I8 | 光追场景：BLAS 跟随 mesh 几何生命周期、TLAS 每帧更新、TLAS InstanceID = InstanceBinding slot；BLAS 的输入不限于静态顶点缓冲，可以是 compute 生成的缓冲（动态 BLAS，对应 `FRayTracingDynamicGeometryUpdate`） | RT 阴影 | P6 |
| I9 | 几何记录表（instance → 顶点/索引缓冲 bindless 索引、属性布局）+ 命中点着色库 | 主光线调试视图 | P7 |
| I10 | Vertex Factory（§二契约）：一个 Pass 内按 factory 选 VS 与 InputLayout | 静态网格（接缝），地形/蒙皮（第二用例） | ✅ HLSL 契约 P1；组合机制 P5 |

I7 的现状：`DeviceFeatures::m_rayTracing` 已有，`BufferBindFlags` 已有 AS / ShaderTable / Scratch，**缺 BLAS 构建输入
的用途位**（Vulkan 要求顶点/索引缓冲创建时带 `ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY` +
`BUFFER_DEVICE_ADDRESS`）。

---

## 四、路线图

### 依赖关系

```
P1 时序基础 ──┬──► P3 后处理主干
              │
P2 结构对齐 ──┼──► P4 屏幕空间 ──┐
              │                  ├──► P6 光追阴影/AO ──► P7 命中点着色 ──► P8 DDGI
              ├──► P5 透明 ──────┘（P7 的 alpha test 依赖 P5 的 BlendMode）
              │
              └──► P9 DOF / MotionBlur（依赖 P1 Velocity、P5 AfterDOF 桶）
```

P1、P2 互不依赖，可并行。P3 / P4 / P5 之间互不依赖。

### P1 时序基础 + TAA　✅ 已完成

详细计划与实现细节：`TODO_TemporalPlan.md`。

1. reversed-Z（§二「深度」）。
2. Vertex Factory / Material HLSL 契约，GBuffer / DepthOnly 重构为模板。
3. I1：View 时序数据、Halton jitter、ViewBindings 时序字段。
4. I1：`m_prevModel`，静态网格 `VertexFactoryGetPreviousWorldPosition` 用它实现。
5. I2：builder 区分渲染尺寸与输出尺寸（缩放先固定 100%）。
6. Velocity：GBufferPass 写 Velocity MRT，所有物体都写；未写入像素（天空）由 TAA 用深度 + `ClipToPrevClip` 重建。
7. I0：持久资源机制，View 持有 TAA history。
8. `ITemporalUpscaler` 接口 + TAA 实现（移植 UE4 `TemporalAA.usf`）。插在 Tonemap 之前（HDR 域）。

**验证**：已确认静止边缘无锯齿无跳动、运动无明显拖影、细高光经 specular AA 后不再闪烁。RenderDoc 下池化图逐帧
交替、窗口反复缩放后池化图数量回到稳定值、DX12 validation 零警告仍待确认。

**带入后续阶段的遗留**（详见 `TODO_TemporalPlan.md` 步骤 7 / 8）：

- `ITemporalUpscaler` 未抽象——只有一个实现时形状只能靠猜，等 TAAU 或 DLAA/DLSS 成为第二个实现时再抽（P9）。
- 镜头切换失效 `g_CameraCut` 未做，且尚无 `ViewHistoryResetTag` 的产生者（世界层切相机 / 瞬移 / 加载场景）。
- 单主 View 限制：history、Velocity、pooled image 的附件身份都还没有 view 维度，多视口时一起改（`TODO_MultiViewPlan.md`）。
- 池化图描述符不累加队列掩码、跨队列 fence 只覆盖 `ImportedTag`——历史资源进 async compute 前补上。
- 关闭顺序：`RenderGraph` 的 `ImagePool` 先于引用其图的 SRG 销毁，现有临时补丁。正解是按 Init 逆序关闭 + 关闭前
  等 GPU 空闲，独立处理。
- TAA 是全屏 PS；I3（compute pass 进图）落地后改 CS。

### P2 结构对齐　✅ 已完成

详细计划与决策记录：`TODO_StructureAlignPlan.md`。

1. GBuffer 按 §二布局重排，Emissive 改写 SceneColor，`GetGBufferData()` 统一解码，加 `ShadingModelID`
   （先只有 DefaultLit）与 Specular。
2. PreExposure：所有写 SceneColor 的 shader 乘 `View.PreExposure`，Tonemap 除回。P3 之前固定为 1。
3. LightingPass 拆为 Lights / IndirectDiffuse / Reflections 三个 Pass，三者 additive 叠进同一张 SceneColor。
   AO 只用材质自带的 `GBufferAO`，屏幕空间 AO 的接入整体留给 P4。
4. 阴影拆分：ShadowProjection 把 atlas 投影成 `ShadowMask`（4 灯打包一个 array slice），Lights 仍是一个全屏
   draw 循环所有灯。逐灯绘制不做（`TODO_StructureAlignPlan.md` D5）。
   前置：RHI 补 `m_layerCount` 写入与从 VS 写 `SV_RenderTargetArrayIndex` 的能力位。

**验证**：每步人工看画面确认。DX12 validation 零警告仍待确认。

**带入后续阶段的遗留**：

- **共享绑定组的 space0 布局缺陷**——Pass 的描述符表偏移来自它自己的反射，而共享组按组的顺序写描述符，
  只引用部分 space0 的 shader 会让空缺之后的槽位整体错位。现用 `SceneBindings.hlsli` 里的
  `SpaceZeroKeepAlive()` 绕过（每个 shader 强行引用全部 space0 资源）。正解是让组拥有的 space 由组描述布局，
  改点在 `PassBuilder.h` 的 `BuildPipelineLayoutFromShaders` 加一处初始化顺序调整，不动 PSO 与 DX12 后端。
- `SkyboxPass` 只引用 `g_EnvIntensity`、不引用任何 space0 SRV，**疑似同一问题但未验证**。测法：改天空盒组件
  的 intensity，看天空亮度跟不跟。
- ShadowProjection 是全屏 draw，**没有按灯包围盒收缩**。等 P4/P5 的光源剔除到位后自然补上。
- 灯光组件的"阴影方式"字段没加，`ShadowViewSystem` 仍为所有投影灯分配 atlas tile。P6 接 RT 阴影时要做。
- 屏幕空间 AO 的声明与 `AmbientOcclusion` 信号纹理不存在，P4 与 GTAO 一起建。

### P3 后处理主干

详细计划：`TODO_PostProcessPlan.md`。

1. I6：PostProcessSettings 挂 View，取代 `View::m_exposure`。
2. I3：SceneDownsample 链，引擎第一个 compute pass（每级独立纹理，不依赖 I4）。
3. Histogram + EyeAdaptation（直方图取百分位，不用平均亮度），1×1 结果走 I0 持久。
4. Bloom：Jimenez dual-filter，复用降采样链（不用 UE4 高斯，理由见计划 D4）。
5. Tonemap 换 **AgX + Look**，合入 bloom 与自动曝光。分级即 Look 的 ASC CDL，内联不建 CombineLUTs。
6. `PreExposure` 接自动曝光，含 TAA 的 `PreExposureCorrection`。

不做 FXAA（TAA 关闭时的低配路径，我们没有这个场景）。色调曲线不用 UE 的 `FilmToneMap`：它在链条最末端，
下游无消费者，不属于要对齐的帧结构 / 数据契约 / 插件点三层中的任何一层，而 AgX 解掉了 ACES 系的色相偏移。

**验证**：亮灯进画面时曝光不被拽走（直方图相对平均亮度的唯一理由）；Bloom 只作用于高亮；PreExposure 生效
后画面与它固定为 1 时一致。

### P4 屏幕空间效果

1. I4 + HZB（closest / furthest 两套 mip 链）。
2. GTAO → `AmbientOcclusion`，接入 IndirectDiffuse / Reflections。
3. Contact Shadow：在 Lights 中对单灯追屏幕空间短射线，乘进 ShadowMask。
4. SSR：HZB 步进 + 上一帧 SceneColor（View 持久资源）重投影，与预滤波 cube 混合。

**验证**：AO 只压暗间接光、不影响直接光；SSR 在屏幕外/遮挡处平滑回退到 cube。

### P5 透明物体

1. I5：按材质 PSO 变体，变体维度按"材质 × vertex factory"设计（I10，factory 维度先只有静态网格）。材质加 `BlendMode`（Opaque / Masked / Translucent / Additive / Modulate /
   AlphaComposite）、`TranslucencyPass`（BeforeDOF / AfterDOF）。
2. Masked：DepthPrePass / ShadowPass / GBufferPass 带 PS 做 clip。
3. 分类：`DrawItemPersistencePlan` §八 所说的"加一个分类 tag + 一条映射 + 一个 pass"。
4. Translucency（SurfaceForwardShading）：前向着色库与延迟共用 BRDF，逐灯循环 + 采样 shadow atlas + IBL；按
   视图深度从后往前排序。BeforeDOF 直接写 SceneColor；AfterDOF 写 SeparateTranslucency，DOF 之后合成（P9 之前
   在 TAA 前合成）。
5. HeightFog：不透明走全屏 Pass，透明在前向着色里自己算。
6. Distortion：累积偏移到 distortion buffer，再对 SceneColor 副本做折射。

LightGrid（分簇光源）只留接缝：前向着色的灯光遍历封装成函数，以后替换内部实现。

**验证**：半透明叠加顺序正确；透明物体受阴影和 IBL；AfterDOF 桶开 DOF 后不被模糊（P9 后验证）。

### P6 光追阴影 / RTAO + NRD

1. I7 RHI 光追（DX12 实现 + Vulkan 语义正确的接口）。
2. I8 光追场景：BLAS 在 mesh 几何就绪时构建、下一帧 compaction；TLAS 在 InstanceBindingSystem 之后更新，作为
   buffer 资源进图并绑定到 SceneBindings。BLAS 构建接口只接收顶点/索引缓冲句柄，不假设来自静态网格（动态 BLAS
   的接缝），实现先只覆盖静态网格。
3. RT 阴影 → `ShadowMask`：按光源角尺寸做锥形采样，输出 SIGMA 所需的半影/遮挡距离（`SIGMA_FrontEnd_PackPenumbra`）。
4. RTAO → `AmbientOcclusion`：输出归一化命中距离。
5. NRD 原生接入（决策 4）：SIGMA、REBLUR_DIFFUSE_OCCLUSION，封装在降噪器接口后。
6. 回退：设备不支持光追时，两个信号自动选光栅 / GTAO 来源。

**验证**：RT 阴影与光栅阴影在同一场景下对比形状一致、接触处更硬、远处更软；降噪后静止无闪烁；关闭光追能力位时回退正常。

### P7 命中点着色

1. I9 几何记录表：TLAS InstanceID → instance slot → 几何记录（顶点/索引 bindless 索引、步长、属性偏移）；
   用 `GeometryIndex()` 区分 BLAS 内子几何。固定顶点属性布局契约，HLSL 端按契约从原始缓冲读取。
2. 命中点着色库（`RayTracingLighting.hlsli`）：插值 UV/法线 → uber 材质求值 → 直接光（每条光线按重要性选一盏灯
   + 阴影光线）→ 天空/IBL（未命中）。
3. Masked 在 ray query 中处理：`CANDIDATE_NON_OPAQUE_TRIANGLE` 分支采样 alpha。
4. 调试视图：全屏每像素一条主光线，显示命中点 albedo / 法线。

**验证**：调试视图与 GBuffer 的 albedo / 法线逐像素对比一致（仅采样 mip 差异）。

### P8 DDGI

1. 探针体积组件（World 侧：包围盒、间距、每探针光线数），Render 侧 binding system。
2. 持久探针纹理（irradiance / distance 八面体图、probe data）由体积实体持有（I0）。
3. 更新链：trace（ray query，命中点走 P7 着色库，采样上一帧探针得到多次弹射）→ blend（hysteresis）→ border 更新
   → relocation / classification。
4. 采样库：8 探针三线性 + Chebyshev 可见性 + 背面权重。
5. 接入 IndirectDiffuse（GI 方法：None / DDGI），× AmbientOcclusion；Translucency 与 Reflections 未命中回退也采样。

**验证**：单色墙的颜色溢出；关灯后间接光按 hysteresis 收敛；探针在墙内时 relocation 生效、无漏光。

### P9 其余

- DOF（Diaphragm DOF）、MotionBlur（依赖 Velocity）。
- `ITemporalUpscaler` 的其他实现：TSR、DLSS、FSR（需要 I2 的非 100% 缩放）。
- RT 反射（REBLUR_SPECULAR；需要多样化材质时再评估 RT pipeline）。

---

## 五、明确不做 / 推迟

| 项 | 原因 / 触发条件 |
|---|---|
| RT pipeline / SBT / hit shader | 出现 shader graph 材质或 RT 反射质量要求时 |
| Lumen | 复杂度过高；DDGI 占住 GI 方法位置，以后作为另一个选项加入 |
| TSR | 先 TAA；接口同为 `ITemporalUpscaler` |
| DLSS RR / FSR Ray Regeneration | 仅 NVIDIA / AMD 新卡，混合管线收益有限 |
| OIDN | 非实时，留给烘焙 / 路径追踪预览 |
| MSAA | 与延迟管线不兼容 |
| DBuffer Decals / CustomDepth | 帧结构中留位置，不排期 |
| LightGrid 分簇光源 | 灯多到逐灯循环成为瓶颈时。届时直接做分簇，不经过逐灯光体积 + stencil 那一步 |
| GPU 剔除 + indirect draw | 植被等实例数上万、每 Drawable 一个 DrawItem 撑不住时 |

---

## 六、路线图之后的功能落点

路线图完成后，这些功能的**着色侧**都是在管线上长肉；**几何侧**集中依赖 I10 Vertex Factory，其余依赖已有接缝。

| 功能 | 新增 vertex factory | 帧结构位置 | 依赖的接缝 |
|---|---|---|---|
| 地形 | 四叉树 CDLOD，VS 采样高度图 | BasePass（DefaultLit） | I10、I5（层混合材质）、I8 动态 BLAS、资源驻留（高度图/weightmap 流式） |
| 植被 / 草 | 实例化 | BasePass、Masked | GPU 剔除 + indirect draw、I5 Masked |
| 水体 | 围绕相机的四叉树网格 + Gerstner / FFT 位移 | SingleLayerWater（新 ShadingModel） | I10、I0（WaterInfo、交互波纹）、I6（水下后处理）、多 View（平面反射）、P4 SSR |
| 蒙皮网格 | GPU 蒙皮 | 同静态网格 | I10、I8 动态 BLAS |
| GPU 粒子 | compute 模拟结果 | 不透明 / 透明桶 | I10、I3、I0 |
| 体积雾 | — | VolumetricFog | I3、I0（froxel 历史）、P5 透明采样 |
| 体积云 / 大气 | — | VolumetricCloud | I3、I0（重投影历史） |
| 贴花 | — | DBuffer Decals | — |

---

## 七、未决

- ~~Translated world space~~：已定，暂不采用，ViewBindings 字段按 UE 命名（`TODO_TemporalPlan.md` 核心决策 2）。
- ~~Velocity 写入位置~~：已定，GBufferPass MRT、所有物体都写（`TODO_TemporalPlan.md` 核心决策 3）。
- ~~GBufferA 法线编码~~：已定，`R10G10B10A2` 直存 `N*0.5+0.5`（`TODO_StructureAlignPlan.md` D1）。
  **复查点在 P4**：做 SSR 时用低粗糙度大平面实测，出条带就换八面体——只换 `EncodeNormal` / `DecodeNormal` 两个
  函数体，无调用点改动。
- **ShadowMask 的容量与开销**：现在是 16 盏投影灯封顶（4 slice × 4 灯），1080p 31.6 MiB、4K 127 MiB，且投影是
  全屏 draw 不带收缩。上限触及或 4K 成为目标时，靠分簇把槽位从全局灯号改为簇内灯号（`O(N)` → `O(K)`）。
- **共享绑定组的布局权威**：见 P2 遗留第一条。这是 P2 唯一没修掉的结构性问题。
- **NRD 许可证**：商用前确认条款。
- **Vulkan 后端的光追实现时机**：I7 接口必须 Vulkan 语义正确，Vulkan 实现是否与 DX12 同步落地。

---

## 关联文档

- `TODO_StructureAlignPlan.md` —— P2
- `TODO_PostProcessPlan.md` —— P3
- `TODO_PerDrawPSOVariant.md` —— I5
- `TODO_DrawItemPersistencePlan.md` §八 —— P5 透明分类
- `TODO_ShadowOptimizePlan.md` —— P2 阴影拆分时注意其中 bias 量纲的待办
- `TODO_MultiViewPlan.md` —— I0 的 View 持有者、per-view Pass 循环
- `TODO_IBLPlan.md` —— I4 per-subresource barrier 欠账的出处
