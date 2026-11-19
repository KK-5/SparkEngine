# NRD 接入方案（初稿）

路线图 P6 第 5 项（NRD 原生接入）的落地计划。总览见 `TODO_RenderPipelineRoadmap.md`，其决策 4 定的是
"不走 NRDIntegration/NRI，按 `GetInstanceDesc` / `GetComputeDispatches` 走自己的 RHI 与 RenderGraph"。

本文是初稿：**NRD 的形状和引擎的缺口已经用源码核实**，工程量、参数与调优在实施时再逐条确认。文中标
"待确认"的条目是有意留白的，不要在开工前当成结论。

---

## 状态

| 步骤 | 内容 | 状态 |
|---|---|---|
| 0 | 方案调研（NRD 源码 + 引擎源码） | ✅ 完成，结论见 §一 |
| 1 | 引擎：per-Scope 的 bindings 来源 | 已完成（`52173081`） |
| 2 | 引擎：detached pipeline（layout + PSO） | 未开始 |
| 3 | 引擎：本地与共享 bindings 隔离 | 已完成（见 D5，`52173081`） |
| 3a | 引擎：按字段提交运行时常量 | 方案已定，见 D4 |
| 4 | 信号层（打包、归一化、MV、范围） | 未开始 |
| 5 | NRD 系统（instance / pool / settings / 生命周期） | 未开始 |
| 6 | 合成与开关（降噪 / 直通） | 未开始 |
| 7 | 构建与依赖（可选 CMake 依赖） | 未开始 |

1~3a 是引擎前置工作，4~6 是 NRD 接入本身，7 只影响构建。

---

## 一、结论

- **一条 dispatch 对应一个 Scope；一条 pipeline 对应一个 PSO；一份 layout 覆盖全部 pipeline。**
- **per-Scope bindings 已落地。** 接下来补齐外部 layout/PSO 的接入、按 group 数提交 dispatch，
  以及供接入层按字段提交运行时常量的入口，见 §三和 D4。
- 不做 bindless，也不改 NRD 源码：NRD 的 shader 按固定寄存器取图（没有索引层），改它属于修改 SDK 本体，
  许可证不授权（见 §五）。
- 其余都是机械工作（detached pipeline）与参数工作（信号、设置、生命周期）。

一句话的形状：

```
一个 compute pass（无 shader）
  └ N 个 Scope，每个 Scope = 一条 NRD dispatch
       PSO       ← pipelines[dispatch.pipelineIndex]
       bindings  ← 该 Scope 自己的 ShaderBindings（同一份 layout）
       常量      ← 接入层按字段拆解 dispatch.constantBufferData，再逐项 .Constant(...)
       grid      ← dispatch.gridWidth / gridHeight
```

---

## 二、NRD 的形状（源码核实）

来源：`Include/NRD.h`、`Include/NRDDescs.h`、`Include/NRDSettings.h`、`Source/InstanceImpl.cpp`、
`Source/Reblur.cpp`、`Source/Relax.cpp`、`Shaders/*.resources.hlsli`、`Source/Denoisers/*.hpp`。

### 2.1 对象与 dispatch

```
instance（一个 view 一份）
 └ denoiser（按 identifier 注册）
    └ pipeline[]（创建时注册，按字节码指针去重）
       └ dispatch（每帧由 GetComputeDispatches 生成，数量随设置变）
```

公共的 `nrd::DispatchDesc` 只有四样东西：

| 字段 | 内容 |
|---|---|
| `pipelineIndex` | 索引 `InstanceDesc::pipelines[]`，每项是一份 DXIL + `resourceRanges` |
| `resources[]` | 扁平的 `ResourceDesc{descriptorType, type, indexInPool}`；先 SRV 段、后 UAV 段，寄存器从 `resourcesBaseRegisterIndex`（=0）连续 |
| `constantBufferData/size` | 该 dispatch 的常量字节（instance 内 arena 的切片）；`MatchesPreviousDispatch` 只是省一次上传的提示 |
| `gridWidth/gridHeight` | 已经是 group 数（NRD 已按各自 `[numthreads]` 换算） |

`InstanceDesc` 给出：两个 space（常量+采样器 / 资源）、CB 寄存器、2 个 static sampler（`NEAREST_CLAMP` s0、
`LINEAR_CLAMP` s1）、入口点 `NRD_CS_MAIN`、permanent/transient 池的 `TextureDesc` 列表、
`descriptorPoolDesc`（每 set 最多 SRV/UAV 数）、`constantBufferMaxDataSize`。

三种不属于降噪链的 dispatch：

- **Clear**：`accumulationMode == CLEAR_AND_RESTART` 时注入到列表最前，每个被写过的资源一条（单 UAV），
  grid 用 rect 或池尺寸。
- **Split screen**：`splitScreen > 0` 时在链尾。
- **Validation**：`enableValidation` 时在最后，写 `OUT_VALIDATION`。

NRD **不做**任何状态转换、屏障、常量上传、池分配。

### 2.2 REBLUR_DIFFUSE_SPECULAR

应用侧要提供 5 张输入 + 2 张输出（+ 可选 validation）：

| 资源 | 格式 | 方向 |
|---|---|---|
| `IN_DIFF/SPEC_RADIANCE_HITDIST` | RGBA16F（.xyz 辐射度，.w 归一化 hitT） | 读，**同时被写成 scratch** |
| `IN_NORMAL_ROUGHNESS` | RGBA8+ / 按编码 | 只读 |
| `IN_VIEWZ` | R16F+ | 只读 |
| `IN_MV` | RG16F+ | 读，**且被改写**（存上一帧 MV） |
| `OUT_DIFF/SPEC_RADIANCE_HITDIST` | RGBA16F（.w = 遮挡或 history 长度） | 写，链中反复读写 |
| `OUT_VALIDATION` | RGBA8 | 可选 |

默认设置（prepass 半径非 0、`maxStabilizedFrameNum=63`）每帧 **7 条**：

| # | 名称 | 读 | 写 |
|---|---|---|---|
| 1 | Classify tiles | `IN_VIEWZ` | `TILES`（transient，R8_UNORM，1/16） |
| 2 | Pre-pass | TILES, NR, VIEWZ, `IN_DIFF`, `IN_SPEC` | `OUT_DIFF`, `OUT_SPEC`, `SPEC_HITDIST_FOR_TRACKING`(t) |
| 3 | Temporal accumulation | TILES, NR, VIEWZ, `IN_MV`, `PREV_VIEWZ`*, `PREV_NR`*, `PREV_INTERNAL`*, `OUT_DIFF`, `OUT_SPEC`, `DIFF/SPEC_HISTORY`*, `DIFF/SPEC_FAST_HISTORY`*, `SPEC_HITDIST`*(ping) | `DATA1`(RG8), `DIFF/SPEC_TMP2`(t), `DIFF/SPEC_FAST_HISTORY`(t), `SPEC_HITDIST`*(pong), `DATA2`(R32_UINT) |
| 4 | History fix | TILES, NR, `DATA1`, VIEWZ, `DIFF/SPEC_TMP2`(t), `DIFF/SPEC_FAST_HISTORY`(t), `SPEC_HITDIST`*(pong) | `OUT_DIFF`, `OUT_SPEC`, `DIFF/SPEC_FAST_HISTORY`* |
| 5 | Blur | TILES, NR, VIEWZ, `DATA1`, `OUT_DIFF`, `OUT_SPEC` | `PREV_VIEWZ`*, `DIFF/SPEC_TMP2`(t) |
| 6 | Post-blur | TILES, NR, `DATA1`, `PREV_VIEWZ`*, `DIFF/SPEC_TMP2`(t) | `PREV_NR`*, `DIFF/SPEC_HISTORY`* |
| 7 | Temporal stabilization | TILES, NR, `PREV_VIEWZ`*, `DATA1`, `DATA2`, `SPEC_HITDIST`*(pong), `DIFF/SPEC_HISTORY`*, `DIFF/SPEC_LUMA_STABILIZED`*(ping) | **`IN_MV`（u0）**, `PREV_INTERNAL`*, `OUT_DIFF`, `OUT_SPEC`, `DIFF/SPEC_LUMA_STABILIZED`*(pong) |

`*` = permanent 池，`(t)` = transient 池，`ping/pong` 每帧由 `UpdatePingPong` 交换 `indexInPool`。

要点：

- **`OUT_DIFF`/`OUT_SPEC` 是链中的 scratch**（`DIFF_TEMP1 = OUT_DIFF_...`），Pre-pass 与 History fix 都写它，
  最后一次写才是结果。所以它必须同时能当 SRV 和 UAV。
- **`IN_MV` 是 inout**：Temporal stabilization 里是 `RWTexture2D<float4>`（u0，`NRD_RESOURCE_IN`），
  NRD 会写回自己 patch 过的 MV。
- 池内容（实例创建时定死）：permanent 13 张（PREV_VIEWZ R32F、PREV_NR 按编码、PREV_INTERNAL R16_UINT、
  DIFF/SPEC_HISTORY RGBA16F、DIFF/SPEC_FAST_HISTORY R16F、DIFF/SPEC_STABILIZED ping/pong R16F、
  SPEC_HITDIST ping/pong R16F）；transient 8 张（DATA1 RG8、DATA2 R32U、SPEC_HITDIST R16F、
  DIFF/SPEC_TMP2 RGBA16F、DIFF/SPEC_FAST_HISTORY R16F、TILES R8U@1/16）。
- 注册 22 个 pipeline 排列（ClassifyTiles 1、HD recon 4、PrePass 2、TA 8、HistoryFix 1、Blur 1、
  PostBlur 2、Stabilization 1、SplitScreen 1、Validation 1），每帧只用 7 条；Clear 另加 2 个。
- 设置改变链：prepass 两个半径都为 0 → 去掉 Pre-pass（6 条）；`maxStabilizedFrameNum=0` → 去掉 Stabilization
  （6 条，Post-blur 换排列直接写 `OUT_*`）。

### 2.3 RELAX_DIFFUSE_SPECULAR

默认 10 条：ClassifyTiles、Pre-pass、Temporal accumulation、History fix、History clamping、A-trous × 5。
（`enableAntiFirefly` 默认 false，所以没有 Copy / Anti-firefly。）

A-trous 有 5 个 binding variant（SMEM / 读 PING 写 PONG / 读 PONG 写 PING / 读 PING 写 OUT / 读 PONG 写 OUT），
5 次迭代的 `passIndex` 序列是 **0,1,2,1,4**：同一个 pipeline 会出现两次，**且不相邻**（中间隔着另一个 variant）。
8 次迭代时 v1、v2 各出现 3 次，对应 `.hpp` 里的 `maxRepeatNum = 3`。

结论：**pass ↔ pipeline 不是 pass ↔ dispatch**。这是"一个 dispatch 一个 Pass"做不到的原因。

### 2.4 布局差异

22 条 pipeline 只有 10 种 `(SRV, UAV)` 形状；骨架完全一致（2 个 space、`b0` CBV、`s0/s1`、SRV 从 t0、
UAV 从 u0 起）。但**同一寄存器在不同 pass 里语义不同**（例如 `t2` 在 TA 是当前 viewZ、在 Stabilization 是
上一帧 viewZ、在 Split screen 是 diff 辐射度）。所以布局只描述"t_i 是一个 SRV"，语义由 dispatch 的
`resources[]` 按位置决定，不需要换布局。

NRD 两种布局都支持（`DescriptorPoolDesc` 注释）：共享一份并集 layout（推荐），或每 pipeline 独立 layout。

坑：`descriptorPoolDesc.perSet*` 是 `PrepareDesc` 按"唯一 pass 名的**首次**出现"统计的，Post-blur 两个排列
是 3/6 UAV，不能直接信这个数；要最大值得自己遍历 `pipelines[].resourceRanges` 求和。

---

## 三、引擎现状与缺口（源码核实）

### 3.1 已有的

- **本地 bindings 由 Scope 持有**：图从 pass layout 为每个本地 space 创建一个 `ShaderBindings`，
  由 `ScopeBinding` 持有。实际执行的指针与共享组一起放进 `ScopeState.m_bindings[]`。
- **bindings 编译不是缺口**：`CompileShaderInputs` 遍历 `ScopeState` 的指针，挂在哪儿就编哪儿
  （`RenderGraph/RenderGraphCompiler.cpp`）。
- **执行器已支持 PSO 切换**：`ItemPipelineState` 支持 per-item 切换，执行器对 item 类型无关；不透明钩子里也可以
  直接 `SetPipelineState`（`RenderGraph/RenderGraphExecuter.cpp`、`RHI/Command/CommandList.h`）。
- **CB 已支持逐字段写入**：图按字段填充 `ShaderBindings`，RHI 按寄存器和字节偏移组装 CB。
  现有 Scope `.Constant(name, value)` 接收静态类型，外部字节块的逐字段适配见 D4。
- **屏障在 Scope 边界**：图按 Scope 生成前后屏障，1 dispatch = 1 Scope 时是免费的。
- **输出清屏**：RHI 有 `ClearUnorderedAccess`。
- **space 是 layout 内的索引**，不是全局命名空间。`BuildPassPipelineLayout` 只会把 pass 自己的
  `kPerPassSpaceId` 与 `FindSharedGroupLayout(spaceId)` 里的共享 space 放进一个 layout
  （`Pass/PassBuilder.cpp`）；`Binding/BindingSpaces.h` 也写明 standalone shader 自己编号 space。

### 3.2 缺的

**外部 compute pipeline 的接入。** RHI 已能从 DXIL 创建 PSO，RenderGraph 仍需接收不经 ShaderAsset 反射构建
的 pass layout，并让 Scope 使用预建的 PSO。NRD 在自己的 layout 中编号，参见 D1–D3。

**按 group 数提交 dispatch。** `ComputeScope::Dispatch(threadCount)` 要按
pass 的 `PassThreadGroupSize` 换算 group 数，而该组件只在 pass 有 compute shader 时由反射填；
`RenderGraphBuilder::AddScopeDispatch` 会直接断言 "Pass dispatches without a compute shader"
(`RenderGraph/RenderGraphBuilder.cpp`)。NRD 的 grid 本身就是 group 数，所以两条路都可以：

- `.Execute` 钩子里手工 `Submit(DispatchItem{DispatchDirect{grid, 1, 1, 1, 1, 1}})`（DX12 只算
  `ceil(total/threadsPerGroup)`，等价于直接给 group 数）；
- 或者给 pass 一个 group size 的来源（例如 builder 的 `.ThreadGroupSize(x, y)`），让 `.Dispatch` 可用。

第一版走第一条更省事；如果后面确认要"1 Scope = 1 dispatch 且由 item 表达"，再把第二条补上。

**运行时单字段常量。** 保持现有按名声明的方式，补充由接入层提供单个字段字节的重载。SDK 的常量布局映射
由 NRD 接入层完成，见 D4。

### 3.3 为什么不改 NRD 源码（bindless）

NRD 的 shader 按固定寄存器取图，没有"索引"这一层：`NRD_INPUT(Texture2D, float4, gIn_X, t, 1, ...)` 里
寄存器号是编译期字面量，类型逐槽不同（float/float4/uint），也没有 index 参数。`InstanceDesc` 的模型是
"base register + ranges"，即一张连续的表。

要改成 bindless 需要：改 `NRD.hlsli` 的资源声明宏、给每个 pass 的常量加一组 index、按类型分组、把 shader
model 提到 6.6、重编全部排列 —— 等于 fork，且许可证不允许（§五）。收益还是负的：一张表指针（8 字节）比
18 个 index（72 字节）更便宜。

引擎现有的 bindless（`ResourceDescriptorHeap[g_Scope.xxxIndex]`，见 SceneDownsample/Bloom/GTAO/HZB）适合的是
"同一个 shader 访问无界集合"，NRD 是每 pass 固定小集合，用表更自然。

---

## 四、方案

### D1　一个 pass + N 个 Scope；每条 dispatch 一个 Scope

目标形状：无 `ShaderAsset` 的 `SPARK_COMPUTE_PASS` 接收 NRD 系统构建的 layout 和 PSO。
`Build` 按当帧的 dispatch 列表开 N 个 Scope，每个 Scope 声明附件和常量，图按 layout 创建并填充 bindings，
屏障由图的 Scope 边界给出。

无 shader 的 pass 拿不到 `PassThreadGroupSize`，所以第一版的 dispatch 由 pass 的 `.Execute` 钩子记录：
钩子按 Scope 调用一次（`ExecuteWork::m_scopeIndex` 指认是哪条，`m_itemHandles` 为空），在里面
`SetPipelineState(psos[pipelineIndex])` → 绑该 Scope 的 bindings → `Submit(DispatchDirect{grid, 1, 1, 1, 1, 1})`
（见 §3.2）。如果后面改成"每条 dispatch 一个 item"的正规表达，再补 group size 的来源。

不采用"一条 dispatch 一个 pass"：pass 身份是编译期类型（`SPARK_PASS_TAG`），而 RELAX 的 A-trous 会让同一
pipeline 在一帧里 dispatch 多次（§2.3）；且排列数（22~27）会把 pass 集合撑成"注册全部、每帧激活少数"。

### D2　layout：一份并集，手工按 `InstanceDesc` 构建

- space A（常量+采样器）：`b0` CBV 由 D4 的字段描述按字节偏移组成，另有 `s0/s1` static sampler。
  初始化时记录 `constantBufferMaxDataSize`，用来核对 SDK 输出的常量块。
- space B（资源）：一个 SRV 数组 + 一个 UAV 数组，长度为所有 pipeline 的最大值。
  数量按 `pipelines[].resourceRanges` 求，不直接用 `descriptorPoolDesc.perSet*`（§2.4 的坑）。
- 布局由 NRD 系统在 device 初始化后手工建，不经过 `BuildPassPipelineLayout`；`AddShaderInputDescriptors` /
  `AddStaticSamplerDescriptor` / `CreatePipelineLayoutDescriptor` 都是公开 API。
- 待确认：单个超集 layout 是否被所有 pipeline 接受（预期接受：DX12 多余 range 合法，Vulkan 要求 shader 用到的
  是子集，也成立）。开工第一步先打印 `GetInstanceDesc()` 的 `pipelinesNum`、每个 pipeline 的 ranges、
  `perSetTexturesMaxNum`、`constantBufferMaxDataSize` 存档。

### D3　PSO：每 pipeline 一个，共享同一份 layout

- `Factory::CreateShaderStageFunction(Compute)` + `SetByteCode(pipelines[i].computeShaderDXIL)` + `Finalize`。
- `PipelineStateDescriptorForDispatch` + 同一份 layout descriptor，每个 `pipelineIndex` 一个 PSO。
- 存放：NRD 系统自己的 detached 容器（不挂 pass 组件，`PassBuilder::Finalize` 只认 `ShaderAsset`）。
- 注意：引擎每个 PSO 内部各建一个 `PipelineLayout`，切 PSO 会 `SetComputeRootSignature` 并清空已绑定的
  space，所以**每条 dispatch 都要重绑**它的表与 CBV。

### D4　bindings 与常量：按 Scope 声明，常量逐字段传入

- NRD 的 Build 按 `resources[]` 和当前 pipeline 的 ranges 声明 SRV/UAV 附件并绑定对应的数组槽位。
  图为每个 Scope 的本地 space 创建 `ShaderBindings`，由 `ScopeBinding` 持有到帧末。
  lowering 将声明的视图和常量填入这些对象，再通过 `ScopeState` 统一编译和使用。
- 初始化时在 NRD 接入层构建字段表。先确认实际 SDK 的 DXIL 能否提取常量反射，再复用
  `ShaderAssetCompiler` 的反射逻辑获取字段名、CB 寄存器和 space、字节偏移、大小及元素步长。
  若发行物缺反射，需从同一版本、同一编译配置取得字段元数据。并集 layout 按字段建立
  `ShaderInputConstantDescriptor`，合并前验证各 PSO 的字段偏移、尺寸和步长相容。
- Build 按当前 pipeline 的字段表从 `DispatchDesc::constantBufferData` 取值。先用
  `constantBufferDataSize` 检查每个字段的读范围，再逐项调用 `.Constant(name, bytes)`。
  `bytes` 是一个字段的字节 span。若字段在 HLSL CB 中有填充，接入层先按元素去掉填充，再交给现有字段
  写入逻辑恢复其 CB 步长。没有常量数据的 dispatch 省略这一步。
- `.Constant(name, bytes)` 是拟新增的运行时单字段重载，沿用已有按名校验与拷贝语义。原有
  `.Constant(name, value)` 仍可直接传矩阵、向量和标量。Scope 在 Build 时复制每个字段，Compile 无需保留
  SDK 的指针。同一个寄存器内的字段最终仍组装成一份 GPU CB。
- 保留 `ScopeConstants` 的 8 条内嵌预留和单条 64 B 上限。REBLUR 4.18 的共享 CB 约有 81 个字段，
  单个 `float4x4` 占 64 B。实际字段超过 8 条时使用 `eastl::fixed_vector` 已有的堆扩展。
- SDK 的字段表和字节解析留在 NRD 接入层。自研或替换降噪器时，Scope、PSO 选择、SRV/UAV 声明、
  逐字段常量和图屏障都可以复用，替换 dispatch 生成和算法自己的历史管理即可。

### D5　权限边界

- Render 层的 `PassPipelineLayout.m_sharedSpaces` 记录哪些 space 由共享池提供。NRD 使用手工 layout，
  共享列表留空，两个 space 都由 Scope 持有，编号可以和引擎共享 space 重合。
- `CompileScopeBindings` 只向 `ScopeBinding` 持有的对象写值。Direct 共享资源从资源发布的
  `Shader*Usage` 独立填入共享池，Scope 只声明访问并沿用发布的视图。因此 RHI 无需保存共享归属。

### D6　图可见性、屏障、清屏

- 只把**终端输出**（denoised diffuse/specular）与要读的 4~5 张信号 import 进图；pool 与中间纹理全部私有，
  不声明为附件。
- dispatch 之间手发 UAV/状态屏障（`QueueBarrier` + `FlushBarriers`）。**不要**对手动改过状态的图内资源再手发，
  否则与 `ImageStateTracker` 失步；需要跨 dispatch 传递的中间结果全部留在私有池里。
- 终端输出在首个 dispatch 前清 0（`ClearUnorderedAccess`）：NRD 不写 `denoisingRange` 之外的像素。

### D7　池与 ping-pong

- 按 `InstanceDesc::permanentPool/transientPool` 的 `TextureDesc{format, downsampleFactor}` 分配 RHI image，
  由 view 实体持有（`RHIComponents.h` 的 pooled/imported 机制可复用），每帧 import 进图（如果被声明）。
- 每帧按 `DispatchDesc.resources` 解析：`type` 是 `IN_*`/`OUT_*` 用引擎自己的视图表；
  `PERMANENT_POOL`/`TRANSIENT_POOL` 用 `pool[indexInPool]`。
- `indexInPool` 每帧会因 ping-pong 变化，**不能缓存**。

### D8　生命周期

- NRD 没有 resize：分辨率或 rect 变化 → 销毁重建 instance + 池，首帧用 `CLEAR_AND_RESTART`。
- history 复位：`ViewData::m_temporalHistoryValid`（相机切换/重置）→ `AccumulationMode::RESTART`；
  重建/首帧 → `CLEAR_AND_RESTART`，并执行注入的 Clear dispatch。
- `frameIndex` 严格 +1；pass 被跳过（未启用）时不要推进，也不要让 `m_CommonSettings` 的帧号与之一致性断裂。
- 多视图：instance 与池 per view；只有 MainView 跑 NRD。

### D9　信号层（只列改动点，细节见 `TODO_DirectLightingMISPlan.md`）

- hitT 归一化：`REBLUR_FrontEnd_GetNormHitDist`，参数与 CPU 的 `ReblurHitDistanceParameters` 同源。
- normal/roughness：`NRD_FrontEnd_PackNormalAndRoughness`，编码与库编译宏一致；roughness 要 linear
  （引擎的 perceptual 与 alpha 是平方关系，用 `NRD_ROUGHNESS_ENCODING=SQRT_LINEAR` 最省事）。
- radiance：`REBLUR_FrontEnd_PackRadianceAndNormHitDist(..., sanitize)`；输出用
  `REBLUR_BackEnd_UnpackRadianceAndNormHitDist` 解包（`.w` 不是 radiance）。
- MV：天空/未写 velocity 的哨兵值改 0；2.5D（`.z = viewZprev - viewZ`）作为后续增强，GBuffer 的 velocity
  已有该通道。
- `denoisingRange` 作为 view 常量，同时用于夹 `settings.MaxDistance` 与 sky 的 viewZ。
- 合成：读解包后的 radiance，还原材质因子，pre-exposure 仍只做一次。

---

## 五、依赖与构建

NRD 作为**可选外部依赖**，不进仓库；使用者自己构建。缺省关闭，渲染走直通。

- CMake 变量（名字待定，示例 `SPARK_NRD_SDK_DIR`）：指向 `_NRD_SDK`（`Include/` + `Lib/` + `Shaders/`）。
  找不到则 `SPARK_WITH_NRD=0`，`#include <NRD.h>` 收在一个 wrapper 头里，其余代码不依赖它的 include 路径。
- 消费侧需要：三个头文件、库、**Shaders 目录**（含构建期生成的 `NRDConfig.hlsli`），后者要加进 DXC 的
  include 路径（给 DirectLighting 里的打包函数用）。
- 构建 NRD 的一方需要：CMake ≥ 3.30、VS 2022 x64、git submodule（至少 **ShaderMake** 与 **MathLib**）、DXC。
  建议 `NRD_STATIC_LIBRARY=ON`、`NRD_NRI=OFF`、`NRD_EMBEDS_SPIRV_SHADERS=OFF`、`NRD_EMBEDS_DXBC_SHADERS=OFF`
  （只做 DX12 的话）。
- **一致性**：`NRD_NORMAL_ENCODING` / `NRD_ROUGHNESS_ENCODING` / 两个 space 号必须与引擎 shader 编译时用的
  `NRDConfig.hlsli` 是同一份；启动时用 `GetLibraryDesc()` 校验并断言，同时断言 `NRD_VERSION_MAJOR/MINOR`
  在已验证范围内（当前 4.18）。
- **许可证**：不把 SDK 源码/二进制提交进仓库（§4(e)：不能把 SDK 置于开源许可之下；若不提交，SparkEngine
  自身开源也可以只依赖用户提供）。分发使用 NRD 的**产物**时按 §6.1(c) 在致谢页署名并显示 NVIDIA Marks，
  Mark 使用需事前书面批准；拿不准按 LICENSE §19 的邮箱问一次。商用前确认（路线图第七节已列）。

---

## 六、阶段与验收

| 阶段 | 做什么 | 验收 |
|---|---|---|
| 1 | per-Scope bindings、group 数提交与运行时逐字段常量 | 一个测试 pass 的两个 Scope 分别绑定不同图、写不同目标；每个 Scope 按字段传入合计超过 64 B 的不同常量并验证结果；本地声明无法改写共享对象 |
| 2 | detached pipeline + 并集 layout | 建出全部 NRD PSO；跑一条 Clear dispatch，内容变 0 |
| 3 | NRD 系统 + 信号层最小改动 | REBLUR_DIFFUSE_SPECULAR 出图：静止画面收敛、无闪烁；关闭 NRD 时与现在逐像素一致（直通） |
| 4 | 合成 + 开关 | 编辑器可切降噪/直通；切换与 resize 不产生残留历史（画面无拖影） |
| 5 | 质量与参数 | 2.5D MV、validation 调试、antilag/预通道参数调优；相机运动下的拖影可接受 |

阶段 3 之前不要动合成 pass 的输入，保证"关掉 NRD 就是今天的效果"始终成立。

---

## 七、风险与未决

- **并集 layout 的可行性**：需要一个超集 root signature 被全部 pipeline 接受。开工第一件事是把
  `GetInstanceDesc()` 的全量信息打印出来存档，再定 D2 的数组长度与 CB 大小。
- **`descriptorPoolDesc.perSet*` 是启发式**（§2.4），不要直接当上限用。
- **Scope 内屏障**：图只发 Scope 边界的屏障。如果 NRD 的相邻 dispatch 需要更细的状态（同一张图既读又写），
  手发是否足够、`ImageStateTracker` 会不会失步，需要在阶段 2 实测。
- **编辑器视口的 rect/origin**：NRD 的 `inputRectOrigin/outputRectOrigin` 默认不支持
  （`NRD_SUPPORTS_VIEWPORT_OFFSET=0` 时必须为 0）。要么把 NRD 资源做成视口大小，要么打开这个编译开关。
- **性能**：每条 dispatch 一份 bindings 意味着每帧 N 次 descriptor 表更新 + N 次常量上传；REBLUR 是 7~10 条，
  预计可忽略，但要在阶段 4 量一次（尤其 + 切换 PSO 导致的重绑）。
- **常量布局**：REBLUR 4.18 的共享 CB 按源码计算约 896 B，字段总量大但每个矩阵只有 64 B。
  初始化时存档实际 SDK 的 `constantBufferMaxDataSize` 和字段偏移，确认共用 layout 的排列相容。
- **参数默认值**：第一版全部用 NRD 默认设置（`{}` 构造），不要先调参。

---

## 八、关联文档

- `TODO_RenderPipelineRoadmap.md` —— P6 与决策 4
- `TODO_DirectLightingMISPlan.md` —— 直接光的信号现状（距离语义、去调制）
- `TODO_RenderGraphItemPlan.md` —— pass/Scope 模型与"图在 pass 内部是瞎的"这条已接受的代价
- `TODO_TemporalHistoryValidationPlan.md` —— 跨帧历史与复位事件
- `TODO_BindingModelPlan.md` / `TODO_AttachmentBindingPlan.md` —— space 分层与绑定模型

外部资料（版本 4.18）：

- `NRD/Include/NRDDescs.h`、`NRDSettings.h` —— 资源枚举、`CommonSettings`、各降噪器设置
- `NRD/Source/InstanceImpl.cpp` —— dispatch 列表如何生成、池、ping-pong、Clear 注入
- `NRD/Source/Reblur.cpp`、`Source/Denoisers/Reblur_DiffuseSpecular.hpp` —— REBLUR 的链与每 pass 的读写
- `NRD/Source/Relax.cpp` —— A-trous 的 binding variant 与重复 dispatch
- `NRD/Shaders/*.resources.hlsli` —— 每个 pass 的常量结构、sampler、寄存器列表
- [`NRD/Shaders/REBLUR_Config.hlsli`](https://raw.githubusercontent.com/NVIDIA-RTX/NRD/v4.18.0/Shaders/REBLUR_Config.hlsli#L107) —— REBLUR 共享常量字段
- `NRD/LICENSE.txt` —— 修改授权（§1(b)/§4(b)）、开源限制（§4(e)）、致谢（§6.1(c)）
