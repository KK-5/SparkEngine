# DX12 后端切换到 Enhanced Barriers　实现方案

DX12 后端的屏障从 legacy 资源状态（`D3D12_RESOURCE_STATES` + `ResourceBarrier`）换成 Enhanced Barriers
（`ID3D12GraphicsCommandList7::Barrier`：layout / access / sync 三轴，子资源范围原生）。

决策 D1~D6 均已确认。

---

## 起因

- **RHI 按 Vulkan 设计，legacy 只是勉强对上**。RHI 的 `ImageBarrier` / `BufferBarrier` 给出源 / 目标的访问、阶段、队列；
  legacy 路径经 `ConvertImageState` / `ConvertBufferState` 压成一个资源状态，阶段信息基本丢掉（只剩 PS / 非 PS 的 SRV 之分）。
  Enhanced Barriers 的 `SyncBefore/After`、`AccessBefore/After`、`LayoutBefore/After` 与之几乎一一对应。
- **aliasing 是特殊路径**：RHI 的 `DeviceMemoryBarrier` 在 DX12 走 `D3D12_RESOURCE_ALIASING_BARRIER`，与其后的转换屏障分开。
- **copy 队列的纹理只能是 COMMON**：`ConvertImageState` 里为此特判（注释写明"待切到 Enhanced Barriers 后整类消失"），
  靠 copy 引擎的隐式状态提升完成传输。
- **子资源屏障（`TODO_SubresourceBarrierPlan.md`）**：legacy 要把范围展开成逐子资源的屏障（其 D5）；Enhanced Barriers
  原生支持范围。先切换，I4 就不必写这段展开。

---

## 一、现状

| 位置 | 现状 |
|---|---|
| `DX12.h` | `ID3D12GraphicsCommandListX = ID3D12GraphicsCommandList4` |
| `Conversions.cpp` | `ConvertAccessToStates`：RHI 访问位 → `D3D12_RESOURCE_STATES`；`ConvertImageState` 对 copy 队列固定返回 COMMON |
| `CommandList::QueueBarrier`（Image / Buffer） | 同队列：一次转换；跨队列：release 转到 COMMON、acquire 从 COMMON 转出；执行时写 `Resource` 的状态记录 |
| `CommandList::QueueBarrier(DeviceMemoryBarrier)` | `D3D12_RESOURCE_ALIASING_BARRIER` |
| `CommandListBase` | `BarrierOp` 队列：转换屏障写死 `ALL_SUBRESOURCES`；源 == 目标且为 UAV 时发 UAV 屏障；`FlushBarriers` 按 `MultisampleState` 分批提交 |
| 资源创建 | `ImagePool` / `BufferPool` / `TransientResourcePool` / `ConstantBufferContext` 经 D3D12MA（2.0.1）`CreateResource` / `CreateAliasingResource`，给初始资源状态 |
| 功能检查 | `Device.cpp` 查 SM 6.6 与资源绑定 tier（不满足只警告） |

Windows SDK 10.0.26100 的 `d3d12.h` 已含 `ID3D12GraphicsCommandList7`、`D3D12_TEXTURE_BARRIER`、`D3D12_OPTIONS12`，不引入
Agility SDK 的头文件即可编译。

---

## 决策记录

### D1　不保留 legacy 回退，全部替换　✅ 已定

DX12 后端的屏障全部换成 Enhanced Barriers，legacy 的资源状态转换（`Convert*State`、`ResourceBarrier`、aliasing 屏障）整体
删除。设备不支持即初始化失败并报错。不保留回退：否则要同时维护两套转换逻辑，I4 的逐子资源展开也得照写。

运行时是否可用取决于系统自带的 D3D12 与驱动（`D3D12_OPTIONS12::EnhancedBarriersSupported`）。SM 6.6 与 Enhanced Barriers
进入系统自带运行时的时间不同，后者更晚，所以"已要求 SM 6.6"推不出"系统自带支持"。要支持更老的系统版本时，引入 Agility
SDK 只是随程序附带 `D3D12Core.dll` 并导出两个符号。

### D2　layout 不进 RHI，各后端由访问与队列推导；DX12 用队列专用 layout　✅ 已定

RHI 的 `ResourceState`（访问、队列、阶段）不变，I4 的 `ImageSubresourceStates` 也只存它。两家的 layout 集合不对齐
（Vulkan 的存储图像只能 `GENERAL`、没有 resolve 专用 layout、copy 队列不受限、不分队列；DX12 反之），所以不在 RHI 抽象
layout，各后端从（访问, 队列）确定性地推出自己的 layout，源、目标各推一次。DX12 的推导：

| RHI 访问 | layout |
|---|---|
| `None`（空闲，内容保留） | `COMMON`（access `NO_ACCESS`、sync `NONE`） |
| `Undefined`（内容不保留，见 D3） | `UNDEFINED`（只作 LayoutBefore） |
| `ShaderSampledRead` | `*_SHADER_RESOURCE` |
| `ShaderStorageRead` / `Write` | `*_UNORDERED_ACCESS` |
| `ColorAttachmentRead` / `Write` | `RENDER_TARGET` |
| `DepthStencilWrite` | `DEPTH_STENCIL_WRITE` |
| `DepthStencilRead`（单独） | `DEPTH_STENCIL_READ`（不允许 SRV 访问；深度测试兼采样属下面的多种只读组合） |
| `TransferRead` / `TransferWrite` | `*_COPY_SOURCE` / `*_COPY_DEST` |
| `ResolveRead` / `ResolveWrite` | `RESOLVE_SOURCE` / `RESOLVE_DEST` |
| `ShadingRateRead` | `SHADING_RATE_SOURCE` |
| 多种只读组合 | `*_GENERIC_READ` |
| `Present` | `PRESENT` |
| copy 队列上的任何访问 | `COMMON`（copy 队列的纹理只支持 COMMON，访问位照填，不再靠隐式提升） |

API 把 layout 交给应用，是因为驱动在录制时不知道资源的前一个 layout（命令列表并行、乱序录制，驱动不跟踪状态），且同一用途
常有多个合法 layout 可选；本引擎在 RHI / 渲染图里跟踪了状态，推导即是这层的策略。推导不到的只有：让资源停在一个 layout
跨多次用途以省转换（如 Vulkan 的 `GENERAL`），属性能优化，需要时加资源级提示；推导的输入可能要扩充——plane / aspect
（I4 按子资源记录已覆盖）、格式（Vulkan 描述符写入时固定了 `imageLayout`，深度图的采样 layout 须与用途组合无关）。

`*_` 按队列取 `DIRECT_QUEUE_` / `COMPUTE_QUEUE_`：这几个 layout 都有通用版（direct、compute 队列都可用）和队列专用版，
一律用专用版，驱动可按该引擎保留压缩。通用版的长处是两个队列可同时读、跨队列不必转换，但跨队列同步无论如何省不掉
（Vulkan 要求队列所有权转移），省下的只是一次转换，不值得放弃压缩。唯一例外是 D5 跨队列中转用的 `COMMON`，必须是通用版。

访问位 → `D3D12_BARRIER_ACCESS`、`AttachmentStage` → `D3D12_BARRIER_SYNC` 基本逐位对应。各组合与 layout 的兼容性（例如
`DEPTH_STENCIL_READ` 是否允许 SRV 访问、`GENERIC_READ` 覆盖哪些访问）开工时对照规范逐条核实。

### D3　取消 `DeviceMemoryBarrier`，aliasing 记进资源的初始状态　✅ 已定

Enhanced Barriers 没有 aliasing 屏障，且同一次 `Barrier()` 里的屏障互不排序，前一占用者的同步只能写在新资源自己的屏障上：
`SyncBefore` = 前一占用者最后的阶段、`AccessBefore = NO_ACCESS`、`LayoutBefore = UNDEFINED`，渲染目标 / 深度模板再带
`D3D12_TEXTURE_BARRIER_FLAG_DISCARD`。Vulkan 同样可以只用一个 `oldLayout = UNDEFINED` 的图像屏障表达。

屏障的源一侧统一定义为**这块内存上一次的访问**：无 aliasing 时即资源自己上一次的访问，aliasing 首次使用时是前一占用者的。
"内容不保留"单独表达，不能借 `None`——`None` 是"当前无访问"（Vulkan `VK_ACCESS_NONE`），空闲资源（如上传完的纹理）内容
必须保留。于是 `AccessFlags` 加 `Undefined`，与 `Present` 同属借访问位表达的状态：

- 可与读写位并存（表示前一占用者的访问），不与 `Present` 并存；只作源，作目标 ASSERT。
- DX12 纹理：`LayoutBefore = UNDEFINED`、`AccessBefore = NO_ACCESS`、`SyncBefore` 由阶段推出，RT / DS 带 `DISCARD`。
  其余访问位是前一占用者的，可能属另一类资源（buffer 与纹理共用堆），纹理 / buffer 屏障表达不了，另发一个 global barrier
  （前一占用者的阶段、访问 → 本次的阶段、访问）等待并刷出它们；buffer 无 layout，只发这个 global barrier。
  Vulkan：`oldLayout = UNDEFINED`，`srcAccessMask` 取其余访问位（写后写需要内存依赖）。
- 它进得了资源的状态记录：transient 池放置资源时把初始状态设为 `{Undefined | 前一占用者的访问, 队列, 前一占用者的阶段}`
  （无前一占用者为 `{Undefined, 队列, 无阶段}`），编译器经 `GetResourceInitialState` 读到，首次使用的屏障源一侧即是它，
  不再有 aliasing 专用路径。`Discard` 时池记下资源最后的访问与阶段（现只记阶段）。
- 删除：`PreAliasingBarrier` 组件及执行器里对它的执行、`GetAliasingBarrier`、`DeviceMemoryBarrier` / `BarrierResourceType` /
  `MakeDeviceMemoryBarrier`、`CommandList::QueueBarrier(DeviceMemoryBarrier)`。

没有访问的一侧也不带阶段（同 Present）：`ResourceState` 的默认阶段、`ConvertToPresent`、`AsyncUploadSystem` 的 release 目标
阶段改为 `Uninitialized`。静态导入图像以 Clear 加载时，源由 `None` 改为 `Undefined | 原读写位`。

transient 池的 `TransientAllocationFence` 加 `m_queue` / `m_access`；前一占用者与新资源首次使用同队列时才把它的访问、阶段
写进初始状态，跨队列由调用方的 fence 排序。

### D4　升级 D3D12MA，创建时直接给初始 layout　✅ 已定

D3D12MA 从 2.0.1 升到 3.2.0（2026-06-05），资源一律经 `CreateResource3` / `CreateAliasingResource2` 以初始 layout 创建，
后端不再出现 `D3D12_RESOURCE_STATES`。两个接口需 `ID3D12Device10`（SDK 26100 头文件已含，运行时随步骤 1 一并检查）。
仓库只用到 `CreateResource` / `CreateAliasingResource` / `AllocateMemory`，均未受 3.0 的不兼容改动影响；3.0 起默认把小 buffer
建成 committed（`ALLOCATOR_FLAG_DONT_PREFER_SMALL_BUFFERS_COMMITTED` 可关），有问题再关。

| 资源 | 初始 layout |
|---|---|
| buffer（各池） | `UNDEFINED`（API 要求） |
| transient 放置纹理 | `UNDEFINED`，初始状态为 `Undefined`（D3） |
| 其余纹理（`ImagePool`、transient 的 committed 回退） | `COMMON`，各队列（含 copy 队列上传）都可直接用 |

`ImagePool` 新建资源的记录是 `None`，按 D2 即 `COMMON`，与创建时的 layout 一致。

### D5　跨队列：release 转到 COMMON，acquire 从 COMMON 转出　✅ 沿用现状

Enhanced Barriers 没有队列所有权转移；`DIRECT_QUEUE_*` 一类 layout 只能在 direct 队列用，COMMON 各队列都可用。现有
"release → COMMON、acquire ← COMMON"的做法与之相容，`QueueBarrier` 里不写 release 一侧状态记录的约定也不变。

### D6　buffer 屏障　✅ 由 API 决定

buffer 没有 layout，只有 sync 与 access；范围须为整个 buffer（`Offset = 0`、`Size = UINT64_MAX`），与 I4 的"buffer 只按整个
资源"一致。upload / readback 堆上的 buffer 仍不发屏障。

### D7　交换链的 Present 转换随最后一个用到它的 Scope 录制；交换链最后一次访问须在 graphics　✅ 已定

原先 Present 转换在帧末单独提交一个只含屏障的命令列表，每帧触发一次 #1356。转换本身省不掉（Present 要求 `COMMON`，写入时是
`DIRECT_QUEUE_RENDER_TARGET`），单独成列表却不必：编译器在交换链的最后一个 attachment 上挂 `PostImageBarrier`（目标
`{Present, Graphics, Uninitialized}`），与最后一个 pass 录在同一列表，排在帧末 Signal 之前。

约束：交换链最后一次访问须在 graphics，编译器 ASSERT。DX12 的交换链只能绑 direct 队列，Present 恒在 graphics；非 graphics
写交换链的场景（异步 compute 直接合成）另加一个 graphics 上的展示 pass 即可。当前交换链只有 `Color` 绑定，本就只能在 graphics 写。
Vulkan 后端据此须为交换链选支持 present 的 graphics 队列族，找不到即报错，不在后端补所有权转移。

### D8　图像不跨帧换队列，先以 ASSERT 约束　✅ 已定

acquire 一侧的 `LayoutBefore = COMMON` 假定有配对的 release；跨帧换队列时没有 release，图像还停在上一队列推出的 layout 上。
DX12 的屏障两侧 layout 都须与执行队列兼容：`DIRECT_QUEUE_*` 只在 direct 用，copy 队列只认 `COMMON` 且不做 layout 转换，
所以目标队列无法自行从实际 layout 转出，须由旧队列转出或帧边界约定。Vulkan 的 layout 与队列无关、跨队列资源为 CONCURRENT，
目标队列可由上次访问推出 `oldLayout` 直接转换，无此问题。

目前无场景触发（渲染图的 pass 全在 graphics；上传目标都是新建图像），先约束：

- 编译器首次触及图像时，初始状态的队列与本帧使用队列不同，且初始状态不是 `None`、不含 `Undefined`、不在 copy 队列，即 ASSERT。
  copy 队列留下的图像处于各队列都认的 layout（上传结束也有 release）。
- `AsyncUploadSystem` 的上传目标须为 `None` 或已在 copy 队列；往用过的图像里重新上传改为上传到新图像。buffer 无 layout，不受限。

出现用例（如异步 compute 生成、下一帧 graphics 读的 history）时的方向：DX12 按 `m_sharedQueueMask` 推导 layout，声明含 Compute
的图像用通用 layout（`SHADER_RESOURCE`、`UNORDERED_ACCESS` 等，direct / compute 都认），direct 与 compute 间换队列无需 release、
无需预知下一帧；进 copy 队列仍须 `COMMON`，由上述 ASSERT 守住。前提是 `m_sharedQueueMask` 默认值由 `All` 改为 `Graphics`，
否则所有图像（含 transient）都会失去专用 layout，Vulkan 也会全部走 CONCURRENT。

### D9　transient 图像不在 copy 队列上首次使用　✅ 已定

copy 队列只认 `COMMON`，不做 layout 转换，也不能 `DISCARD` / Clear。placed 图像以 `UNDEFINED` 创建，在 copy 队列上转不出来；
原先 `Undefined` 源在 copy 队列走普通路径，得到 `NO_ACCESS` + 前一占用者的 sync + `COMMON`，并跳过了刷前一占用者访问的 global
barrier，是同步漏洞。需求几乎不存在，直接约束：编译器首次触及 transient 图像时使用队列为 copy 即 ASSERT；DX12 后端的图像屏障
源含 `Undefined` 且在 copy 队列同样 ASSERT。transient buffer 不受限（无 layout，只发 global barrier）。

---

## 二、要改的

- `DX12.h`：`ID3D12GraphicsCommandListX` 换成 `ID3D12GraphicsCommandList7`。
- `Device.cpp`：查 `D3D12_OPTIONS12::EnhancedBarriersSupported`，按 D1 处理不支持的情况。
- `Conversions`：`ConvertAccessToStates` / `ConvertImageState` / `ConvertBufferState` 换成 layout（D2）、access、sync 三个转换。
- `CommandListBase`：`BarrierOp` 改为收 `D3D12_TEXTURE_BARRIER` / `D3D12_BUFFER_BARRIER` / `D3D12_GLOBAL_BARRIER`，`FlushBarriers`
  按类型分组调 `Barrier()`；UAV 屏障即 layout 不变、access 为 UAV 的屏障。`MultisampleState` 的分批保留。
- `CommandList::QueueBarrier`：三个重载按新类型改写；纹理屏障带子资源范围（I4 落地前恒为整图）。
- aliasing：按 D3，RHI（`AccessFlags.h`、`ResourceState.h`、`CommandList`、`TransientResourcePool`）与渲染图（编译器、
  执行器、`RHIComponents.h`）同步改。
- 资源创建：按 D4，`3rdParty/D3D12MA` 换成 3.2.0，`ImagePool` / `BufferPool` / `TransientResourcePool` /
  `ConstantBufferContext` 改用 `CreateResource3` / `CreateAliasingResource2`。
- sync 只由阶段推导；唯一例外是 resolve 访问补 `SYNC_RESOLVE`（Vulkan 的 resolve 属 `COLOR_ATTACHMENT_OUTPUT`，D3D12 单列）。
  阶段与队列、访问不匹配一律 ASSERT，不在后端补救。交换链 Present 转换的 `m_dstStage` 改为 `Uninitialized`（Present 后无阶段）。
- `AsyncUploadSystem` 的上传与移交、静态资源屏障：随转换函数改动，逻辑不变。
- Present 转换：按 D7，`CompileScopeBarriers` 末尾给交换链挂 `PostImageBarrier`，删掉 `RenderGraph::SubmitSwapChainPresentTransition`。

---

## 三、步骤（草案）

| 步骤 | 内容 |
|---|---|
| 1 ✅ | 运行时检查：`Device::InitFeatures` 查 `OPTIONS12` 与 `ID3D12Device10` 并记日志；本机两项均支持。改为不支持即初始化失败（D1）随步骤 3 一起做 |
| 2 ✅ | D3D12MA 换成 3.2.0，调用不变，确认构建与画面无变化 |
| 3 ✅ | 转换函数（D2）、`CommandListBase` 的屏障队列、`QueueBarrier` 改写，命令列表换 `List7`；资源改以初始 layout 创建（D4）；aliasing 记进初始状态（D3），删掉 `DeviceMemoryBarrier` 一路。首轮运行暴露 `None` 兼任"丢弃"的错误，D3 由屏障上的 bool 改为 `AccessFlags::Undefined`，原步骤 4 并入。7 个示例与编辑器运行无 debug layer 错误；画面待人工确认 |
| 3b | Present 转换随最后一个 Scope 录制（D7），消除 #1356；跨帧换队列的 ASSERT（D8）；transient 图像不上 copy 队列的 ASSERT（D9）。debug layer 与 GPU-based validation 下编辑器与 7 个示例均无报错，画面已确认 |
| 4 | 删除 legacy 残留：`Image::m_subresourceState` 一族与 SwapChain 对它的调用（与 I4 步骤 1 一起） |

I4 的步骤 1（`ImageSubresourceStates`、按子资源记录状态）与后端无关，可与本文并行；I4 的步骤 2 放在本文之后，D5 改为依赖
原生范围。

**验证**：debug layer 与 GPU-based validation 无报错；主视图、阴影、TAA、Bloom、UI、异步上传（copy 队列 → graphics）、
交换链 Present 各路径画面不变。legacy 下若有依赖隐式状态提升 / 衰减的地方，会在这里以验证错误的形式暴露。

---

## 四、未决

- D2 各访问组合与 layout 的兼容性：已由 debug layer 纠正两处（`DEPTH_STENCIL_READ` 不允许 SRV；resolve 访问的 sync 只能是
  `RESOLVE`，每个 sync 位都须与访问兼容），其余组合随验证覆盖。
- ~~**#1356 警告**~~：已由 D7 解决。只含屏障的命令列表触发，唯一来源是单独提交的交换链 Present 转换（编辑器 20 秒内 1818 条
  全部落在它的 `ExecuteCommands` 内）。
- ~~**`RouteDebugMessagesToLog`**~~：保留。`Device.cpp` 的 `ID3D12InfoQueue1` 回调把 debug layer 的警告 / 错误转进引擎日志，
  只在开启验证时注册。
- ~~**跨帧换队列的 `LayoutBefore`**~~：由 D8 约束；出现用例时按 D8 的方向做。
- ~~**copy 队列上的 `Undefined`**~~：由 D9 约束。
- **aliasing 的 global barrier 可精确化**（暂缓，待性能分析）：现以 global barrier 刷前一占用者的访问（D3）。若性能分析显示有开销，可由池在初始
  状态之外附上前一资源，DX12 改为对它发一个以 `NO_ACCESS` 结束的屏障；同一批次内的多个 global barrier 也可在 `FlushBarriers`
  里按位合并。预估完整管线每帧 15~35 条，与 legacy 的 aliasing 屏障数相同。
- ~~`TransientResourcePoolStats::m_aliasingBarrierCount`~~：已删除（从未被填写，随 aliasing 屏障的删除已无对应概念）。

---

## 关联文档

- `TODO_SubresourceBarrierPlan.md` —— I4；其 D5（DX12 按范围展开）由本文取代
- `TODO_RenderGraphItemPlan.md` —— 渲染图的屏障编译
- `TODO_AsyncUpload_RemainingIssues.md` —— copy 队列上传与移交
