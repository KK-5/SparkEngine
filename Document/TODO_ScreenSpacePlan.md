# P4 屏幕空间效果　实现方案

路线图 P4 的落地计划。总览与阶段依赖见 `TODO_RenderPipelineRoadmap.md`。

P4 做四件事：**HZB**、**GTAO**（`AmbientOcclusion` 信号）、**Contact Shadow**、**SSR**（`Reflections` 信号的屏幕空间来源）。
开工前要先补两项基础设施：compute pass 访问 View、子资源屏障（I4）。

本文是草案，决策多为"倾向"，待逐条确认。

---

## 状态

| 步骤 | 内容 | 依赖 | 状态 |
|---|---|---|---|
| 0a | compute pass 访问 View（space1） | — | ✅ 完成 |
| 0b | I4：子资源屏障 | — | 未开始 |
| 1 | HZB（closest / furthest，一张带 mip 的纹理） | 0b | 未开始 |
| 2 | GTAO → `AmbientOcclusion`，接入 IndirectDiffuse / Reflections | 0a（见 D2） | 未开始 |
| 3 | Contact Shadow，乘进 ShadowMask | — | 未开始 |
| 4 | SSR → 与预滤波 cube 混合 | 0a、1 | 未开始 |

0a、0b 互不依赖；3 不依赖任何前置，可以最先做来热身。

---

## 决策记录

### D1　实现来源：宽松许可证的开源实现，结构对齐 UE　⬜ 倾向，待确认

路线图的硬目标是对齐 UE，理由是"以后能把 UE 的渲染算法原样抄进来"。但 UE 的 shader 源码受 Unreal Engine EULA
约束，拿到非 UE 引擎里用是受限的（以 EULA 条款为准）。P3 的 AgX 走的是另一条路：移植 MIT / Apache 的现成实现，
效果好、许可证干净。P4 沿用这条路：

| 效果 | 候选实现 | 许可证 |
|---|---|---|
| GTAO | Intel XeGTAO | MIT（已核实） |
| HZB | 自写（逻辑简单：逐 mip 2×2 取 min/max）；备选 FidelityFX SPD | SPD：MIT |
| SSR | FidelityFX SSSR 的层级步进函数（只取步进，不取随机采样与降噪） | MIT（FidelityFX SDK） |
| Contact Shadow | 自写（UE 式的短射线步进，逻辑简单）；备选 Bend Studio 的 Screen Space Shadows | Bend：据介绍 Apache-2.0，开工时核实 |

**对齐 UE 的三层不受影响**：帧结构（pass 的位置与职责）、数据契约（信号纹理、View 参数命名）、插件点都照旧对齐；
变的只是 shader 的来源。移植时文件头写明出处与许可证，同 `Shaders/Lib/AgX.hlsli`。

### D2　compute pass 访问 View：Build 里选视图，`.Binds<ViewBindingTag>()`　✅ 已定

由 `TODO_BindingModelPlan.md` 定下：视图数据是 space1 的全局表 `g_Views`，shader 用 `GetView(g_Scope.viewIndex)` 读。
compute pass 不渲视图，没有 `RendersView`：pass 声明 `.Binds<ViewBindingTag>()`，Build 自己选视图，用
`TryGetViewIndex` 取它的下标，`.Constant("viewIndex", ...)` 写进 Scope；视图还没有槽位时跳过这个 Scope。

XeGTAO 本身用自己的常量结构（由 CPU 按投影矩阵填），可以照搬；它要的矩阵也能直接从 `GetView()` 读。顺带解锁 P1 的
遗留"TAA 改 CS"。

### D3　HZB：一张带 mip 的 `R32_FLOAT` 纹理，逐 mip 一个 Scope　⬜ 倾向，待确认

- **必须是一张带 mip 的纹理**，不能像降采样链那样每级独立：SSR 步进时在 shader 里按距离运行时选 mip。所以依赖 I4。
- **closest 与 furthest 两张**：reversed-Z 下 closest = 2×2 取 max，furthest = 取 min。SSR 步进用 closest（不漏掉遮挡），
  furthest 留给以后的遮挡剔除。同一个 pass 一起写，多一张图的代价很小。只做 closest 也行，等剔除时再加 furthest。
- **mip 0 为半分辨率向上取到 2 的幂**，每级严格减半，没有奇数尺寸的对齐问题；采样时 UV 按比例缩放。
- **格式 `R32_FLOAT`**：Vulkan 强制支持的 storage 格式里有它，没有 `R16_SFLOAT`（见 D4）。
- **生成方式**：先每 mip 一个 Scope（第 j 个 Scope 读 mip j-1、写 mip j），逻辑最简单；需要时再优化为一次 dispatch 用
  groupshared 生成 4 级。不用 SPD 单 pass：它要 buffer 原子计数器（buffer 的 `.Bind` 还没做）与 globallycoherent，且只是
  绕开 I4，而 I4 本来就是欠账（`TODO_IBLPlan.md`）。

### D4　storage 格式只用 Vulkan 强制支持的　✅ 已定（沿用 P3 D10 的规则）

Vulkan 保证能作 storage image 写的格式：`R8G8B8A8` 的 UNORM / SNORM / UINT / SINT，`R16G16B16A16` 的 UINT / SINT /
SFLOAT，`R32` / `R32G32` / `R32G32B32A32` 的 UINT / SINT / SFLOAT。**`R8_UNORM`、`R16_SFLOAT`、`R11G11B10` 都不在内**，
要靠可选的 `shaderStorageImageExtendedFormats`。

对 P4 的影响：

| 数据 | 移植源的格式 | 我们用 |
|---|---|---|
| HZB | — | `R32_FLOAT` |
| XeGTAO 的 viewspace 深度 mip 链 | `R16_FLOAT` | `R32_FLOAT` |
| XeGTAO 的 AO | 单通道 8 位 | `R32_FLOAT`；带 bent normal 时 `R8G8B8A8_UNORM`（XeGTAO 本来就这样打包） |
| SSR 结果 | — | `R16G16B16A16_FLOAT`（alpha 为置信度） |

### D5　参数归属按 P3 D11 的划分　⬜ 倾向，待确认

| 参数 | 性质 | 放在 |
|---|---|---|
| AO 强度、半径 | 风格（UE 放在 PostProcessVolume） | Volume 上的 `AmbientOcclusionComponent`，presence 即开关 |
| SSR 强度、最大粗糙度 | 风格（同上） | Volume 上的 `ScreenSpaceReflectionComponent` |
| 接触阴影长度 | 灯的属性（UE 放在灯组件上） | `LightComponent` 加字段，0 即关 |
| 各效果的质量档位 | 画质 | 画质设置的归宿未定（P3 D11），先写成常量 |

### D6　信号的可选接入与命名　⬜ 倾向，待确认

- **可选输入沿用 Bloom 的做法**：消费方（IndirectDiffuse / Reflections）按常量里的权重或开关走一个全 draw 一致的分支，
  信号不存在时不读纹理（Vulkan 读空描述符要靠可选特性）。
- **信号名集中定义**：同 `PostProcessResources.h`，新信号（`AmbientOcclusion`、`ScreenSpaceReflections`、`HZBClosest`
  …）的名字、尺寸与"这帧有没有"的判断放在一个共享头里，pass 不互相 include。可以借这个机会建 `SceneTextures.h`（对应
  UE 的 `FSceneTextures`），现有 pass 里散落的 `"SceneDepth"`、`"GBufferNormal"` 等字符串以后逐步迁进来。

### D7　Contact Shadow 放在 ShadowProjection 里　⬜ 倾向，待确认

ShadowMask 是四灯打包的 RGBA8 array slice，由 ShadowProjection 的 PS 写。放在它里面，对每盏灯沿光方向在屏幕空间走
一段短射线、比对 SceneDepth，结果直接乘进这盏灯的通道：

- 不新增 pass，PS 能拿到 View；
- 不需要对打包的 RGBA8 做 UAV 读改写（RGBA8 的 typed UAV load 在 DX12 是可选能力，Vulkan 的无格式读也是）。

代价：没有阴影槽位的灯（超出预算，或以后设为"无阴影"的灯）拿不到接触阴影；UE 对这类灯在光照 pass 里补做，我们等
需要时再补。Bend 的 wavefront 方案质量与效率更好，但它是按灯的独立 compute pass，作为以后的升级。

### D8　SSR：每像素一条光线，沿 closest HZB 层级步进，取上一帧颜色　⬜ 倾向，待确认

- 只追粗糙度低于上限的像素（上限来自 D5 的组件），每像素一条镜面方向的光线，不做随机采样，噪声交给 TAA。
- 层级步进移植 FidelityFX SSSR 的步进函数；命中后用命中点的 velocity 重投影，采样上一帧的 `TemporalAA`（即
  `ReadPrevious`，HDR、已含 PreExposure）。
- 置信度：屏幕边缘、命中背面、粗糙度接近上限处淡出；写进结果的 alpha。
- Reflections pass 里 `lerp(cube, ssr.rgb, ssr.a)` 后再乘 EnvBRDF，屏幕外与未命中平滑回退到 cube。
- 随机多光线加降噪（SSSR 全套或 NRD REBLUR）以后做：SSSR 全套要 tile 分类、间接 dispatch 与结构化 buffer，基础设施还没有。

---

## 一、前置

### 0a　compute pass 访问 View　✅

按 D2 做完（`TODO_BindingModelPlan.md` 步骤 4）：

- `ComputePassBuilder` 加 `Binds<>()`，写进 `PassCapabilities`（同 `RenderPassBuilder`）；没有 `RendersView`，
  执行器不按视图展开 compute 的提交区间。
- `TryGetViewIndex(rhiCtx, view, out)`（`Binding/View/ViewBinding.h`）给 Build 取视图下标。
- 多视图：每个视图一个 Scope，各自 `.Constant("viewIndex", ...)`，dispatch 尺寸按各视图的区域给。"每个视图读写不同的
  资源"仍是 `TODO_MultiViewPlan.md` 的范围。
- 每个 Scope 的根常量字段必须全部写过，漏写即断言（`RenderGraphBuilder::EndPass`），`viewIndex` 忘了写不会静默读到
  槽位 0。

**验证**：SandBox 的 ComputePass，pattern pass 的尺寸改从 `GetView().viewSizeAndInvSize` 读。TAA 改 CS 作为第二个用例
（可选）。

### 0b　I4 子资源屏障

即 RenderGraphItemPlan 第 18 条。现状与要改的：

| 层 | 现状 | 要改 |
|---|---|---|
| RHI `ImageBarrier` | 无子资源范围，整张图 | 加 `ImageSubresourceRange`（mip / array 区间） |
| DX12 | 转换屏障写死 `D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES`；但 `Image` 已按子资源记录状态（`m_subresourceState`） | 范围展开成逐子资源的屏障，更新对应子资源的状态 |
| Vulkan | —— | `VkImageSubresourceRange` 原生支持 |
| `ResourceStateTracker` | 一个资源一个状态 | 一组子资源区间各自的状态；首次触碰时整张图一个区间 |
| `CompileScopeBarriers` | 同一 Scope 里同一资源的 attachment 合并成一个访问；读写冲突即断言 | 按子资源区间合并；区间不重叠的读与写可以共存（HZB 的读 j-1、写 j），重叠才报错 |
| 视图 | `ImageViewDescriptor` 已有 mip / array 区间，谓词 `OverlapsSubResource` 已写好 | 用上 |

- 跨队列 release / acquire 也要带范围；这条路径本来就没有运行时覆盖，先保证正确，验证等第一个 async compute 用例。
- **补单元测试**：屏障编译目前没有测试，I4 让它的逻辑明显变复杂。在 `SparkRenderTest` 里对 tracker 与合并规则写用例
  （整图 → 部分 → 整图、相邻 Scope 读写不同 mip、同 Scope 重叠报错）。

**验证**：单元测试；HZB 做完后在 GPU-based validation 下跑（bindless 访问 CPU 端查不到，同 P3）。

---

## 二、HZB

```
SceneDepth ──► HZBPass（compute，逐 mip 一个 Scope）──► HZBClosest（R32_FLOAT，mip 链）
                                                    └► HZBFurthest（可选，同上）
```

- Scope 0 读 SceneDepth（`R32_FLOAT` 视图），写 mip 0：半分辨率取到 2 的幂后，每个 mip 0 纹素覆盖的深度范围不一定正好是
  2×2 个像素，按覆盖范围取 max / min，越界处 clamp。
- Scope j（j ≥ 1）读 mip j-1、写 mip j，2×2 取 max / min。
- 级数、尺寸、名字放在共享头里（D6），SSR 从那里取。

**验证**：RenderDoc 看各 mip；GPU-based validation 无报错。

## 三、GTAO

移植 XeGTAO（MIT）。它是三个 compute pass：

```
SceneDepth ──► PrefilterDepths ──► viewspace 深度 mip 链（5 级，一次 dispatch 写完）
GBufferNormal ─┐                         │
               └──────► MainPass ◄───────┘ ──► AO（+ 边缘信息）──► Denoise ──► AmbientOcclusion
```

- **PrefilterDepths 一次写 5 个 mip**：同一 Scope 里对同一资源的 5 个 mip 各声明一个 `Write`（不同的 mip 视图，各自
  `.BindIndex`）。全部是写，整张图处于 UAV 状态即可，**不依赖 I4**。
- **法线**：XeGTAO 要 viewspace 法线；我们有世界空间的 `GBufferNormal`，在 MainPass 里乘 View 矩阵转换（要 View，见 D2）。
- **常量**：XeGTAO 的常量由 CPU 按投影矩阵填（它自带 C++ 头文件）。开工时要核对它对 reversed-Z 与矩阵行列约定的假设。
- **时域**：XeGTAO 按帧序号换噪声，靠 TAA 累积；自带 5×5 深度感知的空间降噪。不另做时域滤波。
- **格式**按 D4 调整：深度 mip 链 `R32_FLOAT`，AO `R32_FLOAT`。
- **接入**：IndirectDiffuse 与 Reflections 里 AO = `GBufferAO × AmbientOcclusion`（UE 的组合方式开工时核对）。反射目前直接
  乘 AO（`Reflections.hlsl` 的注释说明了原因），专门的镜面遮蔽等 bent normal 再说。
- 全分辨率（XeGTAO 的 High 档就是全分辨率）。

**验证**：AO 只压暗间接光、不影响直接光（路线图）；角落与接触处变暗、平面上无噪点；关掉组件后画面与现在一致。

## 四、Contact Shadow

按 D7 放在 ShadowProjection 的 PS 里：

- 对这个 slice 的每盏灯：从像素的世界位置沿光方向走长度 L（灯组件上的"接触阴影长度"）的射线，投到屏幕上等距采样 N 步
  （8~16），比对 SceneDepth，有遮挡即把这盏灯的通道乘上遮挡因子。深度比较加厚度容差，避免把背后的表面当成遮挡。
- 每像素的步进起点加抖动，由 TAA 平滑。
- 长度为 0 的灯跳过（全 draw 一致的循环内判断，按灯取值）。

**验证**：长度为 0 时画面与现在一致；小物体与地面接触处出现阴影、阴影贴图的分辨率在这里不再是瓶颈；不出现浮空的误遮挡。

## 五、SSR

按 D8：

```
HZBClosest ─┐
SceneDepth ─┼─► SSRPass（compute，读 View）──► ScreenSpaceReflections（RGBA16F，a = 置信度）
GBuffer ────┤                                          │
TemporalAA（上一帧）┘                                   ▼
                                               Reflections：lerp(cube, ssr, a) × EnvBRDF
```

- 数据契约顺带复查：路线图「未决」要求在这里用低粗糙度的大平面检查 `GBufferNormal` 的 R10G10B10A2 编码是否出条带，
  出了就换八面体编码（只改 `EncodeNormal` / `DecodeNormal`）。
- 上一帧颜色是 TAA 之后的 HDR；PreExposure 现在恒为 1，曝光分支到来时要按 `PreExposure(N) / PreExposure(N-1)` 校正。

**验证**：路线图的"屏幕外 / 遮挡处平滑回退到 cube"；相机移动时反射不拖影、不闪；粗糙度上限处无明显断层。

---

## 六、改动清单（草案）

| 步骤 | 文件 |
|---|---|
| 0a | `Pass/ComputePass.h`（`Binds`）；`Binding/View/ViewBinding.h`（`TryGetViewIndex`）；`RenderGraphBuilder.cpp`（根常量漏写检查） |
| 0b | `RHI/Resource/ResourceState.h`（`ImageBarrier` 带范围）；DX12 `CommandListBase` / `Image`；`RHIComponents.h` 的 tracker；`RenderGraphCompiler.cpp` 的合并与屏障；`SparkRenderTest` 新用例 |
| 1 | `Render/Feature/HZB/HZBPass.{h,cpp}`、`Shaders/HZB/HZB.hlsl`；共享头（D6） |
| 2 | `Shaders/Lib/XeGTAO.hlsli`（移植）与三个 CS；`Render/Feature/AmbientOcclusion/`；世界侧 `Feature/AmbientOcclusion/`；`IndirectDiffuse.hlsl`、`Reflections.hlsl` |
| 3 | `ShadowProjection.hlsl`；`LightComponent` 加字段、`LightData` 打包 |
| 4 | `Render/Feature/ScreenSpaceReflections/`、`Shaders/SSR/`；世界侧组件；`Reflections.hlsl` |

---

## 七、未决

- **D1~D3、D5~D8 待确认。**
- AO 与材质 AO 的组合方式（乘 / 取 min），开工时对照 UE。
- 多视图：HZB、GTAO、SSR 目前都只处理第一个 MainView，与 P3 相同。
- XeGTAO 的常量对 reversed-Z 与矩阵约定的假设。
- 是否这次建 `SceneTextures.h` 并迁移现有字符串（D6）。

---

## 关联文档

- `TODO_RenderPipelineRoadmap.md` —— 总览，P4 在 §四
- `TODO_RenderGraphItemPlan.md` —— I4 是其第 18 条；"多 Scope × 多视图"是其未决项
- `TODO_PostProcessPlan.md` —— D10（storage 格式）、D11（参数归属）、`PostProcessResources.h` 的先例
- `TODO_IBLPlan.md` —— I4 欠账的出处
- `TODO_MultiViewPlan.md` —— 多视图下的 dispatch 尺寸与资源身份
