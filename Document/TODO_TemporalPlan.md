# 时序基础 + TAA 实施计划（路线图 P1）

## 背景与目标

路线图 `TODO_RenderPipelineRoadmap.md` 的 P1。管线目前没有任何跨帧数据：View 只存当前矩阵、InstanceData 只有当前
`m_model`、图里只有 transient 与 imported 资源。TAA、SSR、自动曝光、NRD 降噪、DDGI 都依赖跨帧数据流。

目标：建立跨帧数据流（上一帧 View / 实例变换、Velocity、实体持有的持久资源），在其上落地 TAA。另外完成两项画面
不变的前置重构：reversed-Z、Vertex Factory HLSL 契约。

---

## 状态

| 步骤 | 内容 | 状态 |
|---|---|---|
| 1 | reversed-Z | 已完成（画面已验证；含 `ShadowViewSystem` 近平面反推修复） |
| 2 | Vertex Factory / Material HLSL 契约 | 已完成 |
| 3 | View 时序数据 + ViewBindings | 已完成 |
| 4 | InstanceData `m_prevModel` | 已完成 |
| 5 | 渲染 / 输出分辨率分离 | 已完成（含输出视图，修复场景未对齐编辑器面板） |
| 6 | Velocity | 已实现，待验证 |
| 7 | 跨帧保留的图资源 | 已按"图资源每帧新建、存储池化"重写，尚无调用方，未验证 |
| 8 | `ITemporalUpscaler` + TAA | 未开始 |

---

## 核心决策

### 1. reversed-Z 从 P2 提前到本阶段第一步，所有 View 统一

TAA 取邻域最近深度、`ConvertFromDeviceZ` 等 UE shader 默认 reversed-Z，先写正向 Z 的 TAA 到 P2 还要返工。主相机与
阴影 View 使用同一约定：`Math::PerspectiveFov` / `OrthographicProjection` 是所有 View 共用的投影入口，在这一层
切换，避免两套深度约定并存。

### 2. 不采用 translated world（相机相对坐标）

当前没有大世界需求。ViewBindings 新字段按 UE 命名（`g_` 前缀 + UE 名），将来切换只改矩阵构造方式。

### 3. Velocity 写在 GBufferPass 的 MRT，所有物体都写

不区分静态 / 运动物体。未被几何覆盖的像素（天空）不写入，由单独的 Velocity Resolve Pass 用深度 +
`g_ClipToPrevClip` 补全相机运动，产出每个像素都有值的 Velocity。TAA 与以后的 DLAA / DLSS / FSR / XeSS 都读补全后的
结果（外部 upscaler 要求全屏运动向量，不会自己重建）。"静态物体不写、由深度重建"的优化留到有需求时。

### 4. Jitter 与 TAA 开关联动

TAA 关闭时不施加 jitter，画面与现在一致。

### 5. history 由图的池化图提供，不复用 `ImagePerFrame`

`ImagePerFrame` 按 `frameIndex`（swap chain 图像索引）轮换，`frameCountMax = 3` 时取到的是三帧前的内容，语义不是
"上一帧"。机制见步骤 7。

---

## 一、reversed-Z

**改动点：**

- `Core/Math/MathUtils.h`：`PerspectiveFov` / `OrthographicProjection` 交换 near / far 映射（near → 1，far → 0）。
- `DepthPrePass`、`ShadowPass`：深度测试 `Less → Greater`，清除值 `1 → 0`；`ShadowPass` 的 slope-scaled bias 取反。
- `SkyboxPass`：`LessEqual → GreaterEqual`；`Skybox.hlsl` 远平面 z `1 → 0`。
- `LightingPass`：全屏三角形 z `1 → 0`，测试 `Greater → Less`（`Lighting.hlsl` VS 与 C++ 渲染状态同步）。
- 阴影采样：`ShadowSampling.hlsli` 中 `depthBias` 的方向、比较采样器的比较函数、"清除深度读作无阴影"的判断。
- CPU 侧依赖深度值的代码（如 `ShadowViewSystem` 方向光 ortho 包围盒拟合）逐一核对。

无需改动：`ReconstructWorldPos`（经 `g_InvViewProj` 反投影，自动适配）、`Frustum::FromViewProjection`（近远平面
都提取）。

bias 只改符号，`TODO_ShadowOptimizePlan.md` §三 的 bias 量纲待办不在本步处理。

**验证**：画面逐像素一致；阴影无新增 acne / peter-panning；DX12 validation 零警告。

---

## 二、Vertex Factory / Material HLSL 契约

契约定义见路线图 §二「Vertex Factory 契约」。本步只定形，不建组合机制。

**改动点：**

命名按 UE 语义、去掉 `F` 前缀。

- `Shaders/Material/MaterialParameters.hlsli`：三方共享的数据类型 `MaterialVertexParameters`、
  `MaterialPixelParameters`、`PixelMaterialInputs`（法线可为切线空间或世界空间，`NormalIsTangentSpace` 区分）。
- `Shaders/VertexFactory/LocalVertexFactory.hlsli`：`VertexFactoryInput` / `PositionOnlyVertexFactoryInput` /
  `VertexFactoryIntermediates` / `VertexFactoryInterpolantsVSToPS`，函数 `GetVertexFactoryIntermediates`、
  `VertexFactoryGetWorldPosition`（两种输入共用 `TransformLocalToWorld`）、`VertexFactoryGetPreviousWorldPosition`
  （暂时返回当前位置，步骤 4 接上）、`GetMaterialVertexParameters`、`VertexFactoryGetInterpolantsVSToPS`、
  `GetMaterialPixelParameters`。
- `Shaders/Material/MaterialTemplate.hlsli`：`CalcPixelMaterialInputs`（GBuffer PS 原有材质逻辑，法线贴图输出切线
  空间法线）、`CalcMaterialParameters`（求值材质并解出世界法线）、`GetMaterialWorldPositionOffset`（返回 0）、
  `GetMaterialMask`（`clip()` 输入）。采样器以参数传入，不占寄存器。
- `GBuffer.hlsl`、`DepthOnly.hlsl` 改为模板，直接 include 上述文件（不做 include 重定向）；GBuffer 内 `EncodeGBuffer`
  写现有 4 个 MRT。两者都施加 WPO，保持深度对称。

**注意**：GBufferPass 以 `Equal` 对 DepthPre 的 SceneDepth 做深度测试，要求两者 VS 算出逐位相同的深度。两个模板的
位置计算必须走同一个函数路径并保持 `precise`，否则 GBuffer 会大面积丢像素。

**验证**：画面逐像素一致。

---

## 三、View 时序数据 + ViewBindings

**前置（已单独提交）**：引擎时钟 `FrameTime`（帧序号、游戏时间 / 真实时间及上一帧值、double 累加、游戏 dt 截断
0.1 s），由 `OnTick(const FrameTime&)` 广播。

**`View` 组件**：`m_viewToClip` 保持无 jitter（剔除、阴影近平面反推读它）；新增 `m_jitter`（NDC 偏移）与
`m_bufferSize`（`m_rect` 所属目标的像素尺寸，由生产者填：主视图 = 渲染尺寸，阴影视图 = atlas）；
`GetJitteredWorldToClip()` 经 `Math::JitterProjection`。

**`ViewHistory`（opt-in，集中维护）**：生产者给需要时序数据的 View 加上无效的 `ViewHistory`（目前只有
`CameraViewSystem` 的主视图）；`ViewBindingSystem` 每帧对它"无效则用当前填充 → 编码 → 写回当前"。没有它的
View（阴影）上一帧编码为当前帧。`ViewHistoryResetTag` 置无效（镜头切换、瞬移）。

**Jitter**：`CameraViewSystem` 用 Halton(2,3) 8 点（序号 `frameNumber % 8 + 1`），`[-0.5,0.5)` 像素换算 NDC；
开关 `RenderSystem::m_temporalJitterEnabled`，默认关，TAA 就位后打开。

**ViewBindings**（`ViewBindingSystem.cpp` 按名写入；`Math::DeviceZToViewZParams` / `ConvertFromDeviceZ` 与
HLSL 同名函数互为镜像）：

| 字段 | 内容 |
|---|---|
| `g_ViewProjection` / `g_InvViewProj` | 改为带 jitter（光栅化与从像素位置重建都用它） |
| `g_ViewProjectionNoAA` | 当前无 jitter world → clip |
| `g_PrevViewProjection` | 上一帧无 jitter world → clip |
| `g_ClipToPrevClip` | 当前无 jitter clip → 上一帧 clip |
| `g_TemporalAAJitter` | NDC，xy 当前、zw 上一帧 |
| `g_ViewSizeAndInvSize` / `g_BufferSizeAndInvSize` | 视口矩形 / 所属目标的像素尺寸 |
| `g_InvDeviceZToViewZ` + `ConvertFromDeviceZ()` | (m22, m32, 是否透视, 0)，透视与正交都支持 |
| `g_FrameNumber` / `g_GameTime` / `g_PrevGameTime` / `g_DeltaTime` | 来自 `FrameTime` |

`ViewBindingsReflect.hlsl` 的 dummy VS 引用了每个成员，否则被优化掉、反射不到。

时间字段在 `SceneConstants`（space0）里有一份同源副本 `g_SceneFrameNumber` 等，供只绑场景组的
shader 使用。语义上时间属于场景而非视图，view 组这份是为绑定便利保留的副本；两份都由
`FrameTime` 写入，永远一致。前缀是必须的：cbuffer 成员共享一个全局命名空间，同名会在同时
include 两个头的 shader 里重定义。

**验证**：相机静止时 `g_ClipToPrevClip` 为单位阵；TAA 关闭时画面不变。

---

## 四、InstanceData `m_prevModel`

**改动点：**

- `InstanceData` 144B → 208B，`PrevModel` 紧跟 `Model`，同步 `InstanceData.hlsli` 与 `static_assert`。
- 世界实体上新增 `InstanceHistory{ m_model }`，与 `ViewHistory` 对称。`InstanceData` 是单向输出，
  不从 staging 回读上一帧的值。
- `InstanceBindingSystem::Update`：编码时 `prev` 取 `InstanceHistory`，没有则等于当前矩阵；编码后把当前矩阵
  滚动写回，持有 slot 但还没有历史的实体补上组件。新物体首帧速度为 0，`GlobalBuffer` 不需要知道 slot 是否新分配。
- `LocalVertexFactory.hlsli` 的 `VertexFactoryGetPreviousWorldPosition` 改用 `PrevModel`。

传送（transform 跳变）不在本步处理；需要时删除实体的 `InstanceHistory` 即可。

**验证**：静止物体 `PrevModel == Model`；物体停止移动后的下一帧两者恢复相等；新生成物体首帧相等。

---

## 五、渲染 / 输出分辨率分离

| 尺寸 | 含义 | UE 对应 |
|---|---|---|
| renderSize | 场景内部渲染分辨率，= outputSize × 缩放比例 | `View.ViewRect` |
| outputSize | 场景最终显示的分辨率：编辑器为 Scene View 面板，独立运行为 swap chain | `View.UnscaledViewRect` |
| swap chain | 整个窗口（含 UI），与上两者独立 | ViewFamily render target |

**改动点：**

- `RenderSystem::OnTick`：原尺寸改名 `outputSize`；`renderSize = outputSize`，缩放比例以后接在这一行。
- `RenderGraph::ExecutePipeline` / `RenderGraphBuilder::Begin` 同时接收两者，builder 新增 `GetOutputSize()`。
- DepthPre、GBuffer、Lighting 继续用 `GetRenderSize()`；目前没有 `GetOutputSize()` 的消费者，第一个是步骤 8 的 TAA 输出。
- `CameraViewSystem::Update`：宽高比取 outputSize（缩放后 renderSize 逐轴取整会使比例漂移）；`m_bufferSize` 与 jitter
  取 renderSize，`g_ViewSizeAndInvSize` 因此为渲染尺寸。

**输出视图**：`View::m_rect` 只表示"该 view 所服务 pass 的目标中的比例"，`m_bufferSize` 必须等于目标尺寸（executer
校验）。主视图只服务渲染尺寸的目标；写 swap chain 的 Pass（Tonemap）渲染 `OutputViewTag`，由 `CameraViewSystem` 每帧拷贝
主视图并把 rect 设为面板在 swap chain 中的区域。`g_ViewRectMin` 给出像素原点，Tonemap 用 `SV_Position - g_ViewRectMin` 取像素。

**约束**：Tonemap 以 `SV_Position` 对输入做 1:1 `Load`。缩放比例非 100% 前，SceneColor 与 Tonemap 之间必须有放大分辨率的
Pass（TAAU，或 TAA 关闭时的空间放大），不在本阶段。

**验证**：画面不变；拖动 Scene View 面板改变大小时画面正常重建、无拉伸。

---

## 六、Velocity

**编码**（`Shaders/Lib/Velocity.hlsli`）：当前帧无 jitter NDC − 上一帧无 jitter NDC，上一帧位置 = `ndc - velocity`。
RG16F 存原始值，不压缩。

- 未写入：清除为 `kVelocityUnwritten = 65504`（float16 最大值），`GBufferPass` 的清除值与之对应。
- 真实值钳到 `±kVelocityMax = 1024`：只为保持有限且低于哨兵值；超过 ±2 已在屏幕外，钳制不会把屏幕外拉回屏幕内。
- 上一帧在相机后方（`prevClip.w <= 0`）：输出 `kVelocityMax`，即"屏幕外、无历史"。

**改动点：**

- `GBufferPass`：第 5 个目标 `Velocity`（R16G16_FLOAT）。
- `GBuffer.hlsl`：VS 输出 `g_ViewProjectionNoAA × 当前世界坐标` 与 `g_PrevViewProjection × 上一帧世界坐标` 两个插值量，PS 在
  透视除法后相减。上一帧世界坐标 = `VertexFactoryGetPreviousWorldPosition` + 在该位置求值的 WPO（WPO 目前没有时间输入）。
- **`VelocityResolvePass`**（GBuffer 之后的全屏 Pass，`MainViewTag`）：读 `Velocity` 与 `SceneDepth`；未写入像素用
  `(uv*2-1 - jitter, depth)` 经 `g_ClipToPrevClip` 算相机运动，输出每像素有值的 `ResolvedVelocity`（R16G16_FLOAT）。
  所有时序消费方只读它。
- 调试可视化没有单独做，直接用 RenderDoc 查看 `Velocity` / `ResolvedVelocity`。

**验证**：相机平移时静态物体方向一致；运动物体呈独立数值；天空在 `Velocity` 中为 65504、在 `ResolvedVelocity`
中与相邻静态物体方向连续；相机与物体都静止时全图为 0。

---

## 七、跨帧保留的图资源（I0）

**形态：借鉴 UE 的提取（extraction）。图资源每帧新建，只有存储跨帧。** 帧内所有资源都是普通资源；帧末把需要的
资源的存储提取出去，下一帧作为导入资源读回。TAA 历史、上一帧 SceneColor（SSR/SSGI）、上一帧深度与 HZB 都是同一个机制。

第一版实现把"跨帧的存储"做成了图资源实体本身（每个名字一对实体，帧末交换 tag），与生产者声明的瞬态实体是两个身份，
编译期只能全量遍历 attachment 把 `m_image` 改指过去；另有"生产者必须先声明才能挂上 pair"、帧末无条件轮换等补丁。
根源都是把"帧内身份"与"跨帧存储"放在了同一个实体上，已按下文重写。

### 声明：由读取者提出

```cpp
builder.ReadPreviousImageAttachment<PassTag>("SceneColor", bind);
// Execute 期
if (IsPreviousFrameMissing<PassTag>(rhiCtx, slot)) { /* 历史权重取 0，用 select 而不是乘法 */ }
```

一个调用表达两件事：导入上一帧的该名字资源，并要求本帧的同名资源在帧末被提取。名字必须在本帧已声明（与所有 `Read*`
一致），因此读取时已知本帧描述符。创建者不需要知道谁读它的上一帧。

### 身份：附件标识带帧偏移

`(名字, 版本, 帧偏移)`。上一帧的读取身份为 `(A, 0, 1)`，不登记 `m_latestVersions`，与本帧 `A` 的使用记录互不相干，
建图时不产生边。帧偏移目前只有 0 / 1。

### 存储：池化图（`PooledImageTag` 实体）

RenderGraph 持有一个 `ImagePool`（committed，释放按 `frameCountMax` 延迟），池化图从这里分配。池就是一组实体，
状态全部用组件表达：

| 组件 | 挂在 | 含义 |
|---|---|---|
| `PooledImageTag` | 池化实体 | 一张物理图；另有 `Image`、`ImageDescriptor`、`BackingImage`。**一个实体终身对应一张图**，`BackingImage` 与 view cache 永不过期 |
| `PooledImageActiveTag` | 池化实体 | 本帧已被取用（缺失时的替身，或提取目标），帧末清除 |
| `PreviousFrameOf{ name }` | 池化实体 | 内容是上一帧的 `name` |
| `ExtractedImage{ pooledImage }` | 本帧瞬态资源实体 | 帧末提取。Build 挂上，Compile 填入 |
| `PreviousFrameTag` / `PreviousFrameMissingTag` | 读上一帧的 attachment | 上一帧读取 / 没有上一帧内容 |

池化实体不挂 `ResourceName`：它作为导入资源进入图，若带名字会被裸名字 `Read` 经 `FindImportedResourceByName` 解析到。

空闲 = `PooledImageTag` 且不带 `PreviousFrameOf`、`PooledImageActiveTag`。取用（`AcquirePooledImage`）先找描述符
相同的空闲实体，没有才分配。

### 流程

| 阶段 | 做什么 |
|---|---|
| Build `ReadPrevious("A")` | 本帧 `A` 的描述符补 `ShaderRead`，挂 `ExtractedImage`。找 `PreviousFrameOf{A}`：描述符不符则摘掉它的 `PreviousFrameOf`；找不到就取一张空闲图作替身，当场挂 `PreviousFrameOf{A}`（同帧其他读取方共用）并标 missing。以 `(A, 0, 1)` 导入 |
| Compile `CompileTransientResources` | 照常按名字把 `A` 的 attachment 接到 `A` 的瞬态实体；带 `ExtractedImage` 的跳过生命期累加，不进 transient 池、不参与别名 |
| Compile `CompileExtractedImages` | 为每个 `ExtractedImage` 取一张空闲池化图作写入目标，其图写入 `A` 的 `BackingImage` |
| 帧末 `ExtractImages`（在 executer `End` 销毁瞬态实体之前） | ① 销毁本帧既无 `PreviousFrameOf` 也未 Active 的池化实体 ② 清除全部 `PreviousFrameOf` ③ 每个提取目标挂 `PreviousFrameOf{A}` ④ 清除全部 Active |

没有拷贝，也没有引用重写。稳定状态每个被提取的名字两张图：帧末一张成为 `PreviousFrameOf`，另一张回到空闲，下一帧
立刻被取为写入目标——乒乓是池复用的结果，不是结构。替身判定：真正的上一帧图永远不是 Active，找到的 `PreviousFrameOf`
若是 Active，就是本帧更早读取方取的替身。

资源状态天然连续：本帧 `A` 与下一帧的导入实体是两个图资源，但背后是同一个 `RHI::Image`，首次访问的屏障都从
`Image::GetResourceState()` 起算。

### 回收

"一整帧没被碰过"即回收（上表 ①），不需要闲置帧数参数：稳定状态下空闲图下一帧必被取走，留下一整帧的只可能是描述符
已过期（窗口缩放）或读取方已停用。GPU 安全由 `ImagePool` 的延迟释放保证。读取方停一帧：其图保留一帧，回来时被复用、
历史无效；停两帧以上则释放。

### 失效

| 级别 | 触发 | 处理 |
|---|---|---|
| 资源级 | 首次读取、描述符变化、读取方恢复 | 读取的 attachment 带 `PreviousFrameMissingTag`，执行期 `IsPreviousFrameMissing` 查询 |
| 视图级 | 镜头切换（`ViewHistoryResetTag`） | ViewBindings 增加 `g_CameraCut`，逐 view 生效（未做） |

视图级不能让整张图失效：多个 view 共享同一张图，各占一块 rect，左半屏切镜头时右半屏的历史仍然有效。逐 view 执行的地方是
shader，所以标志放在 ViewBindings 里，消费者写成 `historyWeight = g_CameraCut ? 0 : historyWeight`。

`ViewHistoryResetTag` 的存活期改为整帧：由 `RenderSystem::OnTick` 在 `ExecutePipeline` 之后统一移除。目前还没有生产者，
来源在世界层（切换相机、瞬移、加载场景）。

### 暂不处理

- 池化图的描述符只取生产者声明 + `ShaderRead`，队列掩码不随实际使用累加；跨帧、跨队列的 fence 等待（`PendingSync`）
  只覆盖 `ImportedTag`。在历史资源进入 async compute 之前补上。
- 回看多帧：帧偏移 > 1。
- 多个 view 各自拥有不同尺寸的独立目标时，附件身份还要再加一个 view 维度，与 builder 目前只有一个 `renderSize` 是同一个
  限制，届时一起改。共享目标的左右分屏不需要这一步。
- 替身图在 Build 期分配 RHI 资源。从 Pass 看它就是一个已有的导入资源，分配是池内细节。

### 当前状态

机制已落地，Debug 全量构建通过。**还没有任何 Pass 调用**，整条路径一次没跑过。

**验证**：等第一个消费者（TAA）接上后一起验证 —— 窗口反复缩放、TAA 反复开关后池化图数量回到稳定值、无泄漏、无
validation 报错；首帧与缩放后一帧 `IsPreviousFrameMissing` 为真；RenderDoc 中两张池化图逐帧交替。

---

## 八、`ITemporalUpscaler` + TAA

**接口**：输入 SceneColor、SceneDepth、`ResolvedVelocity`、上一帧 history，渲染尺寸与输出尺寸；输出 TAA 后的 SceneColor 与
本帧 history。插在 Skybox 之后、Tonemap 之前，Tonemap 改读其输出。

**TAA 实现**：按 UE4 `TemporalAA.usf` 的算法结构移植，不逐 include 照搬：

- 3×3 邻域取最近深度处的 `ResolvedVelocity`（膨胀属于 TAA 自身，不放进 Resolve，外部 upscaler 自己做）。
- Catmull-Rom 采样 history；重投影落在屏幕外则丢弃 history。
- YCoCg 空间邻域 min/max 裁剪 history。
- 亮度加权混合抑制闪烁；混合系数随速度调整响应。
- history 无效时直接输出当前帧。

**Pass 形态**：UE4 TAA 的输出即 history，因此只写本帧 history 一张图，Tonemap 直接读它，不额外建输出 RT。先做成
全屏图形 Pass（MRT 单目标），不把 compute pass 进图（I3，P3）提前。

**开关**：临时放在 RenderSystem 层，P3 的 PostProcessSettings 就位后迁移；与 jitter 联动。

**验证**：

- 静止相机下几何边缘无锯齿、无抖动。
- 快速平移 / 旋转无明显拖影；运动物体边缘无残影。
- 镜头切换、窗口缩放时历史正确重置，无一帧错误画面。
- TAA 关闭时画面与步骤 1–7 完成后一致。
- DX12 validation 零警告。

---

## 依赖关系

```
1 reversed-Z ─────────────────────────────────────────┐
2 VF 契约 ──► 4 m_prevModel ──┐                       │
3 View 时序 ──────────────────┼──► 6 Velocity ────────┤
5 分辨率分离 ─────────────────┼───────────────────────┼──► 8 TAA
7 持久资源 ───────────────────┴───────────────────────┘
```

建议顺序：1、2（纯重构、画面不变）→ 3、4、5 → 6 → 7 → 8。

---

## 未决

- **多 View 下的隔离**：编辑器多视口时，每个主 View 需要各自的 history 与 Velocity；图的 attachment 是全局名，
  需要确认 per-view Pass 循环下这些资源怎么区分。本阶段先只支持单主 View。
- **Velocity 编码**：精度、"未写入"保留值的具体形式。
- **镜头切换的触发来源**：编辑器切换相机、打开场景之外，gameplay 传送怎么通知。
- **游戏时钟来源**：确认引擎现有时钟是否区分游戏时间与真实时间。
