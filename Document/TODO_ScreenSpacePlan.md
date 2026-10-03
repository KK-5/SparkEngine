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
| 0b | I4：子资源屏障 | — | ✅ 完成 |
| 1 | HZB（closest / furthest，各一张带 mip 的纹理） | 0b | ✅ 完成 |
| 2 | GTAO → `AmbientOcclusion`，接入 IndirectDiffuse / Reflections | 0a（见 D2） | 已实现；颗粒闪烁暂缓到 P6（§三） |
| 3 | Contact Shadow，乘进 ShadowMask | — | 未开始 |
| 4 | SSR → 与预滤波 cube 混合 | 0a、1、`TODO_RenderGraphResolvePlan.md` 步骤 3 | D8 已定；渲染图的记录与解析分离已完成，可以开工（§五） |

0a、0b 互不依赖；3 不依赖任何前置。顺序上先做 4，3 不急。

---

## 决策记录

### D1　实现来源：宽松许可证的开源实现，结构对齐 UE　✅ 已定（GTAO 已按此移植；SSR 用 SSSR 的步进，许可证开工时核实；Contact Shadow 的来源开工时再核实）

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

### D3　HZB：带 mip 的 `R32_FLOAT` 纹理，逐 mip 一个 Scope　✅ 已定

- **必须是一张带 mip 的纹理**，不能像降采样链那样每级独立：SSR 步进时在 shader 里按距离运行时选 mip。所以依赖 I4。
- **closest 与 furthest 两张都做**：reversed-Z 下 closest = 2×2 取 max，furthest = 取 min。SSR 步进用 closest（不漏掉
  遮挡），furthest 留给以后的遮挡剔除，现在没有读者。同一个 Scope 一起写。
- **布局同 UE**：mip 0 的一个纹素正好盖 2×2 个屏幕像素，每级严格减半，所以每一级都是精确的 2×2，没有奇数尺寸的对齐
  问题。为此各边取"半分辨率向上取到 2 的幂"（1920×1080 → 1024×1024），链比屏幕宽：屏幕只占它的一角，屏幕 UV 乘
  `UvFactor = renderSize / (2 × mip0Size)` 得到 HZB 的 UV。屏幕之外的纹素重复屏幕边缘的深度（读入时坐标 clamp），
  对上面各级的 max / min 都没有影响。
- **级数到 1×1**：`log2(mip 0 的长边) + 1`，1080p 是 11 级。
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

### D5　参数归属按 P3 D11 的划分　⬜ AO、SSR 两行已定，接触阴影待确认

| 参数 | 性质 | 放在 |
|---|---|---|
| AO 强度、半径 | 风格（UE 放在 PostProcessVolume） | ✅ Volume 上的 `AmbientOcclusionComponent`（相机上的覆盖 Volume，同 Bloom）；强度为 0 即关 |
| SSR 强度、最大粗糙度、质量档 | 风格（同上）；质量档同 AO 的例外 | ✅ Volume 上的 `ScreenSpaceReflectionComponent`；强度为 0 即关；质量档 `Low` / `Medium` / `High` 由后端映射成最大步数 |
| 接触阴影长度 | 灯的属性（UE 放在灯组件上） | `LightComponent` 加字段，0 即关 |
| 各效果的质量档位 | 画质 | 画质设置的归宿未定（P3 D11），先写成常量。**例外**：AO 的质量档（`Low` / `Medium` / `High`）放在 `AmbientOcclusionComponent` 上，由 `CameraViewSystem` 映射成 3 / 6 / 9 个 slice；画质设置定下来之后再决定是否并入 |

### D6　信号的可选接入与命名　✅ 已定

- **可选输入沿用 Bloom 的做法**：消费方（IndirectDiffuse / Reflections）按常量里的权重或开关走一个全 draw 一致的分支，
  信号不存在时不读纹理（Vulkan 读空描述符要靠可选特性）。"这帧有没有"由生产者与消费者问同一个函数
  （`SceneTextures::AmbientOcclusion::FindView`），否则消费者会去读一个这帧没声明的资源。
- **信号名集中定义**：同 `PostProcessResources.h`，新信号（`AmbientOcclusion`、`ScreenSpaceReflections`、
  `HZBClosest` …）的名字、尺寸与"这帧有没有"的判断放在一个共享头里，pass 不互相 include。这个头是
  `Feature/SceneTextures/SceneTextures.h`（对应 UE 的 `FSceneTextures`），随 HZB 建立，现有 HZB 与 AmbientOcclusion；
  现有 pass 里散落的 `"SceneDepth"`、`"GBufferNormal"` 等字符串另起一次提交迁进来。

### D7　Contact Shadow 放在 ShadowProjection 里　⬜ 倾向，待确认

ShadowMask 是四灯打包的 RGBA8 array slice，由 ShadowProjection 的 PS 写。放在它里面，对每盏灯沿光方向在屏幕空间走
一段短射线、比对 SceneDepth，结果直接乘进这盏灯的通道：

- 不新增 pass，PS 能拿到 View；
- 不需要对打包的 RGBA8 做 UAV 读改写（RGBA8 的 typed UAV load 在 DX12 是可选能力，Vulkan 的无格式读也是）。

代价：没有阴影槽位的灯（超出预算，或以后设为"无阴影"的灯）拿不到接触阴影；UE 对这类灯在光照 pass 里补做，我们等
需要时再补。Bend 的 wavefront 方案质量与效率更好，但它是按灯的独立 compute pass，作为以后的升级。

### D8　SSR：每像素一条镜面光线，沿 closest HZB 层级步进，取上一帧颜色　✅ 已定

- **定位**：SSR 不会被以后的光追与探针取代，而是混合方案的第一层（同 UE5 Lumen 的 screen traces）：先在屏幕空间追，
  未命中的交给下一层。现在下一层是预滤波 cube，P8 之后是 DDGI 探针，以后的 RT 反射是 ray query。它便宜，命中点
  的光照这一帧已经算好，看到的几何与画面一致，也是不支持光追的设备上唯一的动态反射。
- **光线**：只追粗糙度低于上限的像素（上限来自 D5 的组件，默认 0.3~0.4），每像素一条镜面方向的光线，不做随机采样，
  上限附近淡出到 cube。粗糙表面的反射偏清晰，以此换没有噪声：GTAO 已经说明噪声全交给 TAA 在这里不够。按粗糙度
  读上一帧颜色的模糊 mip 是以后的升级；随机多光线加降噪等 P6 的 NRD（REBLUR_SPECULAR）。
- **步进**：移植 FidelityFX SSSR 的层级步进函数（许可证开工时核实）。HZB 的 mip 0 是半分辨率（D3），而 SSSR 的步进
  假定 mip 0 是全分辨率深度：步进在 HZB 的 UV 空间里做（射线乘 `UvFactor`，mip 0 的尺寸当作它的 screen size），命中点
  只精确到 2×2 像素，之后再对全分辨率的 SceneDepth 核一次。
- **步进函数是独立的库**（`Shaders/Lib/ScreenTrace.hlsli`），不写死在 SSR 里：以后的 RT 反射、SSGI 都先做一次屏幕空间
  追踪，Contact Shadow 也是一种短距离的屏幕追踪。
- **厚度**：深度图只有最前一层，光线落在表面之后、固定厚度以内才算命中。厚度先是常量。
- **颜色**：命中后用命中点的 velocity 重投影，采样上一帧的 `TemporalAA`（`ReadPreviousImage`，HDR、已含 PreExposure）。
  要求视图开着 TAA，没有就不做 SSR。这一帧的 TemporalAA 在 Reflections 之后才声明，而现在的 `ReadPreviousImage` 要求
  先声明，见 §五。
- **分辨率**：全分辨率追踪，量过耗时再考虑半分辨率。
- **置信度**（结果的 alpha）：屏幕边缘、朝向相机的光线、命中背面、粗糙度接近上限、上一帧缺失时淡出。全是常量。
  alpha 也是以后混合 RT 反射时的权重：置信度低的交给光追，接口不变。
- **合成**：Reflections pass 里 `lerp(cube, ssr.rgb, ssr.a)` 后再乘 EnvBRDF，屏幕外与未命中平滑回退到 cube。GTAO 的
  镜面遮蔽只乘在 cube 那部分：SSR 是追到的结果，已经含遮挡（UE 的做法开工时核对）。
- **HZB 跳过**：HZB 成为 SSR 的输入后，按"这帧有没有 SSR"生成，判断与 SSR 共用 `SceneTextures` 里的 `FindView`（D6）。

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

### 0b　I4 子资源屏障　✅

按 `TODO_SubresourceBarrierPlan.md` 做完，决策与每一步跑过、没跑过的路径都记在那里。下面是开工前的草案，其中 DX12 一行
已被 Enhanced Barriers 的原生子资源范围取代（`TODO_EnhancedBarriersPlan.md`）。

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

- Scope 0 读 SceneDepth（`R32_FLOAT` 视图），写两条链的 mip 0：纹素 (x, y) 取像素 (2x..2x+1, 2y..2y+1) 的 max / min，
  坐标 clamp 到 SceneDepth 的范围内（D3）。
- Scope j（j ≥ 1）读两条链的 mip j-1、写 mip j，2×2 取 max / min。读与写各用只含一级 mip 的视图，按 heap 下标访问。
- 一个 shader 管所有级：输入、输出各两个下标，Scope 0 的两个输入下标都指向 SceneDepth（声明两次读，一次读只能绑一个
  下标）。
- 名字、级数、各级尺寸、`UvFactor` 在 `SceneTextures.h` 的 `SceneTextures::HZB` 里（D6），SSR 从那里取。
- 排在 VelocityResolve 之后。每帧都生成：现在还没有读者，等 SSR 的组件出现后按"这帧有没有读者"跳过。
- 假定主视图铺满渲染目标（同 SceneDownsample，断言）；SceneDepth 是多重采样时不支持。

**验证**：尺寸函数的单元测试（`SceneTexturesTest.cpp`）；debug layer 下与 GPU-based validation 下各跑过，无断言、
无报错——这也是 I4 部分范围路径的第一次真实运行；两条链在 RenderDoc 里看过。还没有读者用到它。最初那两次运行停在
编辑器的欢迎页上，没有加载场景（管线在跑，SceneDepth 是空的）；之后做 GTAO 时在 `Scene.scene` 与 `Room.scene` 里又
跑过，含 GPU-based validation 一次。

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
- **时域**：XeGTAO 按帧序号换噪声，靠 TAA 累积；不另做时域滤波（做过一版，已去掉，见下）。视图没有 TAA 时噪声不随帧变。
- **格式**按 D4 调整：深度 mip 链、降噪前的 AO、边缘信息、最终 AO 都是 `R32_FLOAT`。
- 全分辨率，每侧固定 3 步。slice 数由组件的质量档决定：`Low` / `Medium` / `High` 对应 3 / 6 / 9（3 与 9 是 XeGTAO 的
  High 与 Ultra 档），默认 `Low`（D5）。组件只存档位，具体参数由 `CameraViewSystem` 映射。降噪固定一遍（XeGTAO 的
  sharp），写 `AmbientOcclusion` 并应用强度。

**移植**（`Shaders/Lib/XeGTAO.hlsli`，文件头有 MIT 声明与改动清单）：

- 源码的 `XeGTAO.h` / `XeGTAO.hlsli` / `vaGTAO.hlsl` 合成一个 HLSL 文件；C++ 那一半（`GTAOUpdateConstants`）不要，
  常量在 shader 里由 `ViewData` 与 Scope 的根常量填（`AmbientOcclusion/GTAOCommon.hlsli`）。
- **约定不用改算法**：引擎的 view space 是左手系（x 右、y 上、z 向前），与 XeGTAO 的一致，法线乘 `view.view` 即可。
  reversed-Z 已经在投影矩阵的 m22、m32 里：`ConvertFromDeviceZ` 的透视分支 `m32 / (z - m22)` 改写成它的
  `mul / (add - z)` 形式，即 `DepthUnpackConsts = (-m32, m22)`。只支持透视投影（断言）。
- **不打包**：源码把 AO 存成 `R8_UINT`、边缘存成 `R8_UNORM`，都不是 Vulkan 必须支持的 storage 格式。改成 float 纹理后
  AO 不再需要 `XE_GTAO_OCCLUSION_TERM_SCALE` 那套 UNORM 打包；边缘仍是 2 bit × 4 的打包值，只是存在 float 里。
- **写入都做越界检查**：dispatch 按整组线程覆盖，prefilter 的一个线程写 2×2 个像素、denoise 的一个线程写 2 个像素，
  奇数尺寸时会越界。D3D12 上越界写是空操作，Vulkan 上不是。
- 只留源码的默认路径：自动调参得到的常量、不做 thin-occluder 补偿。bent normal、从深度生成法线、调试可视化不移植。

**编织状纹路**（实现后发现；原因是 Hilbert 序号在 GPU 上算错，已改写并确认）：

- 现象：AO 上有一层横竖短条，成 16×16 像素的块，最终画面里也看得见；任何场景都有，关掉 TAA 后纹路静止不动。
- **原因**：噪声是"Hilbert 曲线序号驱动 R2 序列"。源码的 `HilbertIndex` 在循环里原地交换 `posX`、`posY`，这段代码
  在 GPU 上（RTX 5070 Ti；是 DXC 还是驱动的问题没有查）有一次交换丢了 `posX`，结果 64×64 的噪声块里有四分之一
  （曲线上第 0、4、8、12 个 16×16 的块）的序号只取决于 `posY`，噪声在这些块里成了条纹。条纹沿一个方向 16 像素不变，
  3×3 的降噪和 TAA 都去不掉。
- **怎么定位的**：从 RenderDoc 导出同一帧的 `GTAODepth`、`GBufferNormal`、`GTAOWorkingTerm`（DDS），在 CPU 上逐行
  重写主 pass 跑同一帧。CPU 的结果是均匀的细颗粒（与 XeGTAO README 的图一致），GPU 的是条纹块；按噪声块里的位置
  分块比较，12 个块的相关系数是 0.87~0.91，上述 4 个块只有 0.48~0.59；把 CPU 版在"第二层、只交换不翻转"时改成
  `posX = posY`，整体相关系数从 0.81 升到 0.93。深度链同时验过：5 级 mip 全部落在上一级 2×2 的范围内。
- **改法**：`XeGTAO_HilbertIndex` 改写成不改动 `posX`、`posY` 的形式，层与层之间的交换与翻转记成两个 bit。在 CPU 上
  对 4096×4096 的每个像素与源码的函数逐一比对，结果相同。改写后重新导出 `GTAOWorkingTerm`：64×64 的噪声块里 16 个小块都
  不再成条纹（每块的方差落在行均值 / 列均值里的比例，改前有 4 块是 0.99，改后全部在 0.02~0.13），画面上纹路消失。
- **之前几个判断的更正**：下面这些都是在序号算错的前提下得出的，结论不能再用。
  - "它是 XeGTAO 单帧输出本来就有的噪声""Denoise 去不掉它是因为核太小"：XeGTAO 正确的单帧噪声是细颗粒，3×3 的核
    对它有效。在导出的那一帧上，CPU 版正确噪声降噪一遍后没有纹路，只剩均匀的细颗粒。
  - "引擎的 TAA 因为历史裁剪累积不掉它"：没有依据。条纹在 3×3 邻域里几乎一致，任何带邻域裁剪的 TAA 都累积不掉；
    正确的细颗粒能不能被我们的 TAA 累积掉，没有验证过。
  - 给屏幕空间半径设上限、降噪改两遍、把原因归到车内机位半径太大：都没有碰到原因。前两处已回退。

**剩下的颗粒与闪烁**（序号改对之后；暂缓到 P6，见末条）：

- 现象：纹路没有了，降噪后剩均匀的细颗粒。TAA 能收敛掉大部分，但颗粒逐帧在变，画面上能看到闪烁。
- 在导出的那一帧（车内机位）上用 CPU 版量每帧噪声的幅度：同一帧算两次、只换噪声序号，降噪后相减，场景本身的明暗
  相消，剩下的就是噪声（RMS，AO 取值 0~1；降噪没有算边缘权重，深度边缘附近实际会高一些）：

  | 每像素采样 | 仪表台（AO 均值 0.69）降噪 1 / 2 / 3 遍 | 遮挡带（均值 0.15）降噪 1 / 2 / 3 遍 |
  |---|---|---|
  | 3 slice × 3 步（现状，降噪 1 遍） | **0.044** / 0.025 / 0.018 | **0.032** / 0.018 / 0.013 |
  | 6 slice × 3 步 | 0.027 / 0.015 / 0.010 | 0.015 / 0.009 / 0.006 |
  | 9 slice × 3 步（XeGTAO 的 Ultra） | 0.019 / 0.010 / 0.007 | 0.011 / 0.006 / 0.004 |
  | 3 slice × 6 步 | 0.028 / 0.016 / 0.012（均值变成 0.55） | 0.012 / 0.007 / 0.005（均值 0.08） |

  加 slice 只降噪声、不改结果；加步数会找到更多遮挡，AO 整体变暗，是另一回事。
- 可选的做法：降噪加到 2~3 遍（XeGTAO 的 medium / soft，多一两个便宜的 pass，AO 的细节略糊）；slice 加到 6 或 9
  （主 pass 的耗时成 2~3 倍）；两者相乘。AO 自己的时域累积能到相近的幅度，但带来重投影失效、屏幕边界分界、运动
  物体拖尾，能不引入就不引入。
- 当时的做法：不加时域累积；slice 数与降噪遍数做成组件上的参数，在车内机位上对比着取值。
- 第一次对比无效：检视面板的枚举下拉框把"第几项"当成枚举值写回去，只有从 0 连续编号的枚举才对。选 9 个 slice 写进去
  的是 2（非法值，落回 3），选 3 遍降噪写进去的是 2 遍，所以当时比的其实是 3 × 1 遍与 3 × 2 遍。问题出在那两个枚举把
  参数值当枚举值，现在组件上的枚举都从 0 连续编号，具体数值由后端映射（AO 的质量档、TAA 的 `Jitter Samples` 都改了）。
  `Editor/UI/Private/FieldWidgets.cpp` 的下拉框也改成按反射的枚举值找选中项、写回枚举值，与序列化按名字找枚举值一致。
- 在导出的两遍降噪输出上量：第二遍把 1 像素的细颗粒压到 43%，但平坦处几个像素宽的斑块只降到 60%（仍是该处 AO 的
  7% 左右），观感上差别很小。降噪遍数是个弱旋钮，已收回成固定一遍。
- 编辑器里对比的结果：slice 数的效果远大于降噪遍数，9 个 slice 时闪烁少了很多，但没有完全消除。
- 噪声与半径（3 slice、降噪 1 遍，仪表台，距相机约 1 m）：半径 0.5 / 0.25 / 0.1 时每帧噪声 0.044 / 0.020 / 0.012，屏幕上
  的半径约 860 / 430 / 170 像素。这个机位的噪声主要来自半径在屏幕上太大，但缩小半径同时改了 AO 的效果。
- 没有量的：经过 TAA 之后的闪烁幅度（只知道它随每帧噪声的幅度变）。
- **暂缓**：路线图 P6 的 RTAO + NRD 是 AO 的主路径，GTAO 届时是不支持光追时的回退。先用质量档加 TAA，P6 之后再看回退
  路径的闪烁能不能接受；不能的话再给 GTAO 加自己的时域滤波（按累积帧数决定空间回退、运动时历史更快让位，见下一节
  要处理的几点）。GTAO 输出的是遮蔽值而不是命中距离，不适合直接交给 NRD。

**AO 自己的时域累积**（做过，已去掉）：

- 在纹路的原因还没找到时加过第四个 pass（`GTAOTemporal.hlsl`）：按 `ResolvedVelocity` 重投影上一帧的 AO，用上一帧的
  深度链判断历史是否还是同一个表面（相差 2% 以内），按每像素的累积帧数混合（`1 / 帧数`，10 帧封顶）。
- 车内机位上的表现：静止时纹路基本消失但能看见收敛的过程，收敛后仍有轻微纹路；相机一动纹路重现，并在上一帧的屏幕
  边界投到本帧的位置上留下横平竖直的明暗分界（界外没有历史，界内是前几帧视角下的 AO）。
- 它补的是算错的噪声，所以在改写 Hilbert 序号的同时去掉了，先看 TAA 自己能不能把正确的噪声收敛掉。若不够再加回来，
  届时要处理的：运动时让历史更快让位；速度缓冲没有深度分量，沿视线方向运动的物体会被判为失效；历史没有邻域裁剪，
  运动物体离开后的 AO 拖尾。

**强度与组合**：

- 强度在 Denoise 的最后一步应用：`lerp(1, ao, intensity)`，消费方拿到的已经是调过的值。
- 与材质 AO **相乘**（UE 的做法）：`ao = GBufferAO × AmbientOcclusion`。
- IndirectDiffuse 用 `AOMultiBounce(BaseColor, ao)`（GTAO 论文的多次反弹近似，UE 也用）。
- Reflections 用 `SpecularOcclusion(NoV, roughness, ao)`，取 Frostbite 课程讲义 v3 的形式，指数 `exp2(-16α - 1)`：
  粗糙表面趋于 `ao`，光滑且正对视线时接近不遮蔽。**这与 UE 不同**：UE 的 `pow(NoV + ao, roughness²)` 是讲义早期的
  写法，指数随粗糙度增大，镜面反而拿到完整的 `ao`。等 bent normal 再换成更准确的镜面遮蔽。
- 后两条在没有屏幕空间 AO 时也生效（只有材质 AO），所以关掉组件后的画面与改动前不完全一致。

**限制**：只处理第一个 MainView，且它要铺满渲染目标（断言）；渲染目标任一边小于 16 像素时这一帧不做（深度链要 5 级
mip）；法线取自带法线贴图的 `GBufferNormal`，细节法线会在 AO 里留下纹理状的起伏。

**验证**：`Scene.scene` 与 `Room.scene` 里跑过：debug layer 下开 / 关各一次，GPU-based validation 下开一次，带 AO 改窗口
大小四次（含奇数尺寸），都无断言、无报错。把 AO 单独显示出来看过：开阔的平面是 1，凹槽、接缝、檐下变暗；天空盒不受
影响。这些都是在默认机位上看的，那里 AO 只占几小块，**不能用来判断画面质量**（编织状纹路就是这样漏掉的）。场景文件里
给 Volume 挂 `Ambient Occlusion` 组件的路径跑过一次。

改写 Hilbert 序号、去掉时域累积之后：RenderTest 60 个通过，编辑器启动时 shader 编译无报错；车内机位上纹路消失，
重新导出的 `GTAOWorkingTerm` 分块检查通过。slice 数与降噪遍数做成参数之后：`Scene.scene` 里 debug layer 下跑过
9 × 3 遍（带改窗口大小三次，含奇数尺寸）、6 × 2 遍、3 × 1 遍，无断言、无报错；编辑器里调过 slice 数，确认传到了 shader。
**已知问题**：上面的颗粒闪烁（暂缓到 P6）。

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
- **先决：在生产者之前读上一帧**。`RenderGraphBuilder::AddPreviousFrameAttachment` 要求这一帧已有 pass 把这个名字声明成
  transient image（取它的描述符、给它加 ShaderRead 并标记保留到下一帧），否则断言。TAA 读自己的历史是先声明后读，
  满足；SSR 排在 Reflections 之前，TemporalAA 这一帧还没声明。由 `TODO_RenderGraphResolvePlan.md` 解决：build 只记录、
  全部声明完再统一解析，SSR 依赖它的步骤 3（顺带补上读后写的依赖边、执行顺序确定、pass 可以插到管线中间）。
- 上一帧颜色的读取与命中点重投影写成通用的函数，SSGI 以后复用。

**验证**：场景由用户搭（低粗糙度的大平面加几个物体，能俯视的机位），效果由用户截图与 RenderDoc 导出。路线图的"屏幕外 /
遮挡处平滑回退到 cube"；相机移动时反射不拖影、不闪；粗糙度上限处无明显断层。

---

## 六、改动清单（草案）

| 步骤 | 文件 |
|---|---|
| 0a | `Pass/ComputePass.h`（`Binds`）；`Binding/View/ViewBinding.h`（`TryGetViewIndex`）；`RenderGraphBuilder.cpp`（根常量漏写检查） |
| 0b | `RHI/Resource/ResourceState.h`（`ImageBarrier` 带范围）；DX12 `CommandListBase` / `Image`；`RHIComponents.h` 的 tracker；`RenderGraphCompiler.cpp` 的合并与屏障；`SparkRenderTest` 新用例 |
| 1 | `Render/Feature/HZB/HZBPass.{h,cpp}`、`Shaders/HZB/HZB.hlsl`；`Render/Feature/SceneTextures/SceneTextures.{h,cpp}`（D6）；`SparkRenderTest` 的 `SceneTexturesTest.cpp` |
| 2 | `Shaders/Lib/XeGTAO.hlsli`（移植）、`Shaders/AmbientOcclusion/` 的三个 CS 与 `GTAOCommon.hlsli`、`Shaders/Lib/AmbientOcclusion.hlsli`（多次反弹、镜面遮蔽）；`Render/Feature/AmbientOcclusion/AmbientOcclusionPass.{h,cpp}`；世界侧 `Feature/AmbientOcclusion/`；`CameraViewSystem`（`ViewAmbientOcclusion`）；`SceneTextures`；`IndirectDiffuse` / `Reflections` 的 pass 与 shader |
| 3 | `ShadowProjection.hlsl`；`LightComponent` 加字段、`LightData` 打包 |
| 4 | 渲染图的上一帧读取（`RenderGraphBuilder`，§五）；`Shaders/Lib/ScreenTrace.hlsli`（步进库）；`Render/Feature/ScreenSpaceReflections/`、`Shaders/SSR/`；世界侧 `Feature/ScreenSpaceReflection/`；`CameraViewSystem`；`SceneTextures`；`HZBPass`（按有无读者跳过）；`Reflections` 的 pass 与 shader |

---

## 七、未决

- **D5 的接触阴影一行、D7 待确认。** Contact Shadow 不急，先做 SSR。
- 渲染图在生产者之前读上一帧（§五）：见 `TODO_RenderGraphResolvePlan.md`，在 SSR 之前做。
- SSGI 不在 P4：它和 SSR 共用步进与上一帧颜色的读取，但难点是每像素一两条随机光线的降噪，以及与天光、GTAO 的合成。
  等 P6 接入 NRD 后用 REBLUR_DIFFUSE 降噪再做，到时一并看 GTAO 回退路径的闪烁。
- GTAO 的颗粒闪烁暂缓到 P6：RTAO + NRD 做完后再决定回退路径要不要自己的时域滤波（§三）。
- 镜面遮蔽用 Frostbite v3 的形式而不是 UE 的（§三），要不要改回与 UE 一致。
- 多视图：HZB、GTAO、SSR 目前都只处理第一个 MainView，与 P3 相同。
- 把现有 pass 里的 `"SceneDepth"`、`"GBufferNormal"` 等字符串迁进 `SceneTextures.h`（D6）。

---

## 关联文档

- `TODO_RenderPipelineRoadmap.md` —— 总览，P4 在 §四
- `TODO_RenderGraphItemPlan.md` —— I4 是其第 18 条；"多 Scope × 多视图"是其未决项
- `TODO_PostProcessPlan.md` —— D10（storage 格式）、D11（参数归属）、`PostProcessResources.h` 的先例
- `TODO_IBLPlan.md` —— I4 欠账的出处
- `TODO_RenderGraphResolvePlan.md` —— SSR 的先决条件：渲染图的记录与解析分离
- `TODO_MultiViewPlan.md` —— 多视图下的 dispatch 尺寸与资源身份
