# I11 GPU-Driven 基础　实现方案

路线图基础设施 I11 的落地计划，排在 P4 与 P5 之间。总览见 `TODO_RenderPipelineRoadmap.md`。

I11 做五件事：**RHI 的 indirect 抽象重做**、**渲染图里的间接参数**、**buffer 跨队列同步按原生语义纠正**、
**几何数据放进共享 buffer**（租约，和 Binding 系统同一个形状）、**compute 视锥剔除**。
做完之后，一个 pass 对一个视图画场景物体是一次 indirect 调用，画哪些由 GPU 决定。

本文是草案。D1（范围）、D2（RHI 的 indirect 抽象）、D3（能力位）、D4（几何的共享 buffer）、D6（剔除的输入）、
D7（剔除的输出）、D9（渲染图的入口）、D12（buffer 的跨队列同步）已由用户确认，D8（压缩）也已确认。D5、D11 已撤销；D10 已撤销，改为一段与 I5 的关系的说明。决策项没有待确认的了。
现状一节来自 2026-10-04 / 05 读代码，没有运行验证，写的是动手之前的样子。D12 的原生语义来自 2026-10-05 查的规范原文。

---

## 状态

| 步骤 | 内容 | 依赖 | 状态 |
|---|---|---|---|
| 0 | RHI：indirect 抽象重做 + 能力位 | — | 完成（2026-10-05），见下 |
| 1 | 渲染图：间接参数的访问角色、`DispatchIndirect`、图内 buffer 的首个用例 | 0 | 完成（2026-10-05），见下 |
| 2 | buffer 跨队列同步的纠正（D12） | — | 完成（2026-10-06），见下 |
| 3 | 几何租约 + `g_Geometries`，draw 改用共享 buffer 里的偏移（仍走 CPU 提交） | 2 | 未开始 |
| 4 | CPU 填参数的 indirect draw（不剔除） | 0、1、3 | 未开始 |
| 5 | compute 视锥剔除，带 count buffer | 4 | 未开始 |

0 / 1 与 2 / 3 互不依赖。4 把"indirect 画得对"和"剔除算得对"分开验证，所以不跳过。

**步骤 0 跑过什么、没跑过什么**

- 全量 Debug 编译通过。
- `IndirectDraw` 示例由用户看过画面：indexed indirect draw，带与不带 count buffer 两种。它同时是"设备初始化时创建
  3 个 command signature"的第一次实际执行。
- 没有执行过的路径：不带索引的 indirect draw、indirect dispatch（留给步骤 1 的测试 pass）、记录里 `firstInstance`
  不为 0（留给步骤 4）。
- `RenderSystem` 里"三项能力齐全才能运行"的检查没有触发过：DX12 三项恒为真，用户决定不为它强行关能力位来测。
- Vulkan `PhysicalDevice` 加的两个特性查询没有编译过（Vulkan 后端不参与构建）。

**步骤 1 跑过什么、没跑过什么**

- 全量 Debug 编译通过；`SparkRenderTest` 89 个用例通过，其中三个是新加的（见 §四 步骤 1）。
- `IndirectDispatch` 示例由用户看过画面，我在 debug layer 下跑过 300 帧以上、无断言无报错。它是这些东西的第一次
  实际执行：图内 transient buffer、buffer 的 UAV view 与 `.Bind`、shader 里的 `RWStructuredBuffer`、
  buffer 从"compute 写"到"间接参数读"的屏障、`IndirectArguments` / `DispatchIndirect` 与 lowering、
  RHI 的 indirect dispatch。引擎代码没有因此改动。
- 没有执行过的：`RenderScope::IndirectArguments`（没有能用它的调用，步骤 4 才有）；buffer 的只读 `.Bind`
  （示例里绑的是可写的）；跨队列的 buffer 访问（示例全在图形队列）；GPU-based validation（只开了 debug layer）。
- 屏障本身没有单元测试：`SparkRenderTest` 不创建设备，而屏障编译要读真实 buffer 对象的状态。

**步骤 2 跑过什么、没跑过什么**

- 全量 Debug 编译通过；`SparkRenderTest` 89 个用例通过，没有新增用例。
- 我在 debug layer 下各跑 10 秒、只看日志，无断言无报错：
  - `DrawCube`：mesh 的 VB / IB 已是共享模式，走的是"上传不发屏障、图形队列按 fence 等、之后队列内一条屏障"。
    `InstanceIDBuffer` 仍是独占，走原来的路径。
  - `IndirectDispatch`，`kArgsOnComputeQueue` 打开：`ArgsPass` 在 Compute 队列写参数 buffer，`PatternPass` 在图形队列
    把它当间接参数读。临时日志确认走到了"共享 buffer 换队列"的新分支（日志已删）。改动前的代码也这样跑过一遍，
    旧的"释放 + 接手"路径同样无报错，说明 Compute 队列上的 pass 本身能跑。
  - `IndirectDispatch`，开关关：与步骤 1 相同的路径。
- 画面：2.1（上传与静态路径、mesh 改共享模式）之后用户检查过，没有问题。`kArgsOnComputeQueue` 打开时的画面没有看过。
- 没有执行过的：`CompileExternalWaits` 里 buffer 的分支（引擎里没有导入的 buffer，只有导入的 image）；
  运行中流式加入物体（我没有跑）；copy 队列写一段、图形队列同时读同一个 buffer 的另一段（步骤 3 有了租约才出现）；
  GPU-based validation（它是 `ValidationLayer.h` 里的编译期常量，Debug 只开 debug layer）。

---

## 一、为什么排在 P5 之前

P5 在功能上不需要 indirect。排在它前面是因为 P5 的两项会把 indirect 依赖的形状定死：

- **I5 / I10 决定 batch 的 key。** 一次 indirect 调用内不能换 PSO，也不能换顶点 / 索引缓冲（D2）。所以 batch key 是
  "PSO 变体 × 几何所在的 buffer 对象"，后一半由本计划的 D4 定。先做 I5 / I10，key 里就没有这一维。
- **Translucency 是第一个要排序的 pass。** 它从后往前排，batch 不再是查询的副产品，不走 indirect。先有真实的
  indirect 路径，P5 才能把它明确设计成例外（D9）。

---

## 二、现状

**RHI**

- 有一套从 Atom 移植的 indirect 代码：`IndirectBufferSignature` / `IndirectBufferLayout` / `IndirectBufferView` /
  `IndirectBufferWriter`、`DrawType::Indirect`、`DispatchType::Indirect`、DX12 的 `CommandList::ExecuteIndirect`。
  Render 层没有任何消费者。
- 记录格式允许带 VB view / IB view / root constants（`IndirectBufferLayout.h:26-28`），只有 DX12 支持。
  能力分级 `IndirectCommandTiers` 被注释掉了（`DeviceFeatures.h:57`）。
- 两处实现问题：`CommandList.cpp:884` 的 count buffer 偏移没有加 `MemoryView` 的偏移（参数 buffer 那行加了）；
  DX12 `IndirectBufferSignature::ShutdownInternal` 没有把 command signature 送进延迟释放队列。
- 能力位已有三个：`m_indirectDrawCountBufferSupported`、`m_indirectDispatchCountBufferSupported`、
  `m_indirectDrawStartInstanceLocationSupported`，DX12 全填 true（`Device.cpp:159-161`）。Vulkan 后端只有
  `PhysicalDevice`，查了 `drawIndirectCount`（`PhysicalDevice.cpp:177`）。
- 屏障一侧是齐的：`BufferBindFlags::Indirect`、`AccessFlags::IndirectRead`、`AttachmentUsage::Indirect`、
  `AttachmentStage::DrawIndirect`，DX12 的转换也在（`Conversions.cpp:543`、`:623`）。

**渲染图**

- Scope / item 模型。item 是什么由组件决定（`RenderGraphExecuter.cpp:46` 的 `SubmitItem`）。
- compute Scope 只有直接的 `Dispatch(x, y, z)`。Scope 上有 `ReadBuffer` / `ReadWriteBuffer` / `WriteBuffer`，
  `RenderPassScopes` 有 `CreateBuffer`，但 **`Render/Feature/` 下没有任何 pass 调用它们**——图内 buffer 这条路还没有用例。
- 场景物体靠 `s.Accepts<Tags>()` 选中。`CompileScopeSubmitRanges` 对每个视图把 `CollectDrawItems<Tags>` 的结果追加进
  submit list，`CollectDrawItems` 不看视图（`PassScopes.h:23`）。**现在没有任何剔除**，每个视图画全部物体。
- 消费者只有三个：DepthPre 与 GBuffer（`MainViewTag` × `OpaqueTag`）、Shadow（`ShadowViewTag` × `ShadowCasterTag`）。
- root constant 是 Scope 级的（`ScopeRootConstants`），`viewIndex` 一项由 executer 在每个视图句柄处重写。

**几何与实例**

- **每个世界实体一对独立的 VB / IB**（`MeshSystem.cpp:137` 的 `MeshVB_<entity>`），偏移恒为 0。两个实体引用同一个
  primitive 也各传一份。
- 编译后的顶点布局是固定的：Position / Normal / Tangent / TexCoord0，48 字节（`ModelAssetCompiler.cpp:192`）；索引是
  `UINT32`（`ModelAssetLoader.cpp:462`）。
- 渲染层已经按"buffer 的 handle + 区间"描述顶点流：`GeometrySpec` 的每个流是（handle，字节偏移，字节数，stride），
  偏移现在恒为 0；所有 draw 的 ID 流引用的是同一个 buffer 实体。
- 实例身份走"ID 流 slot1 + `StartInstanceLocation`"（`GeometrySpec.h:56-74`），`StartInstanceLocation` 就是
  `g_Instances` 的槽位。
- `InstanceData` 208 字节，有 12 字节 padding，没有包围盒，也不知道自己的几何是哪一个。
- `g_Instances` 是稳定槽位，**有空洞且空洞不清零**（`GlobalBuffer.h:25-27`）。
- 资产里每个 primitive 有局部 AABB（`ModelAsset.h:79`），运行时没有传到渲染侧。
- `g_Views` 一行有 `m_viewProjection`（`ViewData.h:17`）。Shadow 的视图也在 `g_Views` 里（`ShadowPass` 绑
  `ViewBindingTag`）。

**buffer 的分配、上传与跨队列同步**

- `BufferPool` 每个 buffer 调一次 `CreateResource3`（`BufferPool.cpp:103`），D3D12MA 把它们放进同一个 heap 块：
  内存是共用的，但各自是独立的 `ID3D12Resource`。
- `BufferDescriptor::m_sharedQueueMask` 只有一位是独占（有一个归属队列），多位是共享。mesh 的 VB / IB 原来填的是
  只有图形一位，步骤 2 改成了图形与 copy 两位；`InstanceIDBuffer` 仍是只有图形一位。走上传的 buffer 只有这三种。
- 上传走 copy 队列（`AsyncUploadSystem`）。待上传的数据是实体上的一个 `PendingBufferUpload` 组件，
  一个实体同一时刻只能挂一段。
- `RHI::Buffer` 上记一份状态，其中一项是"当前所有者队列"（`ResourceState.h:24`）。上传时 copy 队列对整个 buffer 发
  接手 / 释放屏障；之后图形队列等上传的 fence，再发一条跨队列屏障接回来。步骤 2 之前独占与共享的 buffer 都这么走，
  之后只有独占的这么走（见 D12）。

---

## 三、决策记录

### D1　范围　✅ 已定

做：indirect draw / dispatch 的机制、让几何数据落在同一个 buffer 对象里（D4）、compute 视锥剔除（带 count buffer）。

不做：HZB 遮挡剔除（`HZBFurthest` 仍然没有读者）、meshlet、GPU 侧 LOD 选择、双阶段剔除。它们留在路线图 §五原来的
触发条件上。

### D2　RHI 的 indirect 记录固定为三种，取消 signature / layout 对象　✅ 已定

按"跟随更严格的后端"，记录里只能有 draw / dispatch 参数。Vulkan 的 `vkCmdDraw*Indirect*` 只认这个；记录里换
VB / IB / push constant 要靠 device-generated commands 扩展。`TODO_DrawItemShapePlan.md` §二已经得出这个结论。

**不支持记录里换 VB / IB / push constant，也不接 `VK_EXT_device_generated_commands`（已定）。** 那个扩展是给数据
没有统一的引擎用的：缓冲不共享、逐 draw 数据不走下标、shader 不分 batch 也能一次调用画完。引擎本来就朝统一的方向走——
逐 draw 数据已经靠实例下标查 `g_Instances`，几何由 D4 统一。而且扩展是可选的，没有它的设备仍要能画，统一的工作
省不掉，接它只是多养一条路；它的"记录里换 shader"在 DX12 上也没有对应物。以后真要加是加法：一个能力位加一种新记录，
不改已有的。

这样一来三种记录在两个后端上**逐字段相同**：

| RHI 记录 | 字段 | 大小 | DX12 | Vulkan |
|---|---|---|---|---|
| `DrawIndirectCommand` | vertexCount、instanceCount、firstVertex、firstInstance | 16B | `D3D12_DRAW_ARGUMENTS` | `VkDrawIndirectCommand` |
| `DrawIndexedIndirectCommand` | indexCount、instanceCount、firstIndex、vertexOffset(int)、firstInstance | 20B | `D3D12_DRAW_INDEXED_ARGUMENTS` | `VkDrawIndexedIndirectCommand` |
| `DispatchIndirectCommand` | x、y、z | 12B | `D3D12_DISPATCH_ARGUMENTS` | `VkDispatchIndirectCommand` |

记录是固定的 POD，就不需要一个描述"记录里有什么"的对象：

- **删除** `IndirectBufferSignature`、`IndirectBufferLayout`、`IndirectBufferView`、`IndirectBufferWriter`、
  `Factory::CreateIndirectBufferSignature`、DX12 的对象池、`IndirectCommandTiers`。
- **`IndirectArguments` 改为** `{ buffer, byteOffset, maxCount, countBuffer, countByteOffset }`。
  draw 的 indirect 分成 `DrawType::Indirect` 与 `DrawType::IndexedIndirect` 两种，对应两个 API 各自的两个入口
  （原来"是否 indexed"要到 signature 的 layout 里查）。
- **stride 固定为记录大小，不是参数**（用户 2026-10-05 定）。可变 stride 的用途是在记录旁边夹带每次绘制的数据，
  本计划不这么做（实例身份走 `firstInstance`）。以后需要时再加回字段。
- **DX12 后端内部**每种记录一个 `ID3D12CommandSignature`，共 3 个，随 Device 创建，不带 root signature
  （没有改绑定的参数就不需要）。Vulkan 没有对应对象，stride 是调用参数，到时传记录的大小。
- **dispatch indirect 只有一条命令，没有 count。** `vkCmdDispatchIndirect` 就是这个形状，`DispatchIndirect` 因此只有
  buffer 与偏移，不复用 `IndirectArguments`。
- **不做 DispatchRays indirect**：路线图决策 3 只用 inline ray query。
- 逐 draw 的数据走"实例下标查 buffer"，不进记录。root constant 保持 Scope 级，与
  `TODO_RenderGraphItemPlan.md` 里"multi-draw-indirect 不能逐 draw 改 push constant"一致。

顺带消掉现状里的两处实现问题（那两段代码整个被替换）。

### D3　能力位按 Vulkan 的可选特性拆　✅ 已定

Vulkan 里这三项都是可选的，DX12 全部支持：

| 能力 | Vulkan | 用在 |
|---|---|---|
| 一次调用多条记录 | `multiDrawIndirect` | 所有 indirect draw |
| 记录里 firstInstance 非 0 | `drawIndirectFirstInstance` | 实例身份：`firstInstance` 就是 `g_Instances` 的槽位 |
| count buffer | `drawIndirectCount`（1.2） | 压缩后的参数（D8） |

`DeviceFeatures` 里对应 `m_indirectMultiDrawSupported`（新增）、`m_indirectDrawStartInstanceLocationSupported`、
`m_indirectDrawCountBufferSupported`。删掉了 `m_indirectDispatchCountBufferSupported`（D2 里 dispatch 没有 count）、
注释掉的 tier，以及没有任何读写方的 `m_indirectDrawSupport`（单条记录的 indirect draw 在两个 API 里都是必备的）。

**三项都是运行的硬性要求**（用户 2026-10-05 定）：缺任何一项的设备不支持运行。不为它们保留不压缩的形式（D8），
也不靠 CPU 提交路径兜底。CPU 路径仍然存在，但用途里没有"回退设备"这一条（D9）。

**检查在 `RenderSystem` 的初始化里**，拿到设备之后：缺任何一项就打错误日志（点名哪一项）并返回失败，和它前后的
其他初始化检查同一个写法。能力位由 RHI 如实报告，拒绝运行是渲染层的决定；直接用 RHI 的示例不经过这里。

### D4　几何数据放进共享 buffer：租约，和 Binding 系统同一个形状　✅ 已定

> 2026-10-06：这一项与步骤 3 正在按 `TODO_BufferPoolPlan.md` 重新讨论（由 `BufferPool` 在一个原生 buffer 内分配，
> 上层不再自己分配与回收）。那份计划定下来之前，下面的内容保持原样，定下来之后改写。

记录里不能换 VB / IB，所以一个 batch 要合成一次调用，它的 draw 必须共用缓冲，靠 `firstIndex` / `vertexOffset` 区分。

要共用的是 **buffer 对象**，不只是内存。现在各个 mesh 的 buffer 共用的是 heap，各自仍是独立的原生对象（§二）。
VB / IB 的绑定单位是 buffer 对象（Vulkan 是 `VkBuffer` + 偏移，DX12 是一个资源的地址范围），两个独立对象即使在
heap 里相邻也合不成一次调用。

**方向（已定）**

- **它是又一个 Binding 系统。** 和 `g_Instances`、`g_Materials` 一样：都是往 GPU 送大量同一类数据、放进一个大 buffer。
  系统扫描带 Mesh 数据的实体，统一分配，给它挂上租约。
- **共享 buffer 是这个系统持有的一个普通资源实体**，实体和原生对象一对一。
- **Mesh 拿到的是租约**（共享 buffer 里的一段），不是一个属于自己的 buffer。租约归还靠它自己析构，同 `SlotRef`。
  每份租约是 RHIContext 里的一个实体，世界侧照旧持有它的 `RHIHandle`（见下"区间实体与分段上传"）。
- **Mesh 只填申请信息。** 它不知道自己和谁共用一块，也不知道会被 direct 还是 indirect 画。
- **RHI 保持两个 API 的原生语义。** 一个 `RHI::Buffer` 就是一个原生的 buffer 对象；`BufferPool` 只管分配策略，
  不为这件事加任何东西。

仓库里已有的"只用资源的一部分"都是这个形状：资源是一个实体、由系统持有，使用方持有寻址数据——`g_Instances` /
`g_Materials` 的槽位、阴影 atlas 的 tile（`View::m_rect`）、ShadowMask 的 slice 与通道、image 的 view（资源实体上的 cache）。
`GeometrySpec` 的顶点流也已经是"handle + 区间"。

**否掉的做法**

- 渲染层另立"几何池"，Mesh 持有池的句柄：把分配做成了一个新概念。
- `BufferPool` 在 buffer 对象内再分配：把使用场景带进了 `BufferPool`，还要把资源状态从 `RHI::Buffer` 挪到底层对象上。
- 多个实体各带一个指向同一 `RHI::Buffer` 的 `Components::Buffer` 与各自的静态 attachment：它们看起来都是资源，
  实体与原生对象变成多对一，"每个对象只做一次"的事要按对象去重。定下来的做法（见下）区间实体带的是另一个组件。
- 区间实体引用"持有共享 buffer 的实体"：实体引用实体，不要。
- 把上传请求做成列表组件挂在共享 buffer 的实体上：每一段的 fence 与就绪状态没有地方放。

**和现有 Binding 系统不同、需要解决的地方**

- **变长。** mesh 大小各不相同，租的是连续的一串而不是一个槽位。空闲表要支持切分与相邻合并；mesh 数量是千这个量级，
  按偏移排序的空闲表加首次适配就够。
- **回收后不能马上复用。** 已提交、GPU 还没执行完的帧可能还在画那段数据。归还的区间先等 `m_frameCountMax` 帧再回到
  空闲表，与现有释放队列的延迟一致。`SlotPool` 不需要这一步，因为实例数据每帧整块重写。
- **不是每帧整块重传。** 现有 Binding 系统是 host 可见的 buffer、每帧重传；几何数据大、只传一次、要放显存。
  上传按 D12：copy 队列直接写租约的那一段。
- **就绪跟着租约走。** 一段在上传的 fence 完成后才算就绪，就绪了才出 `DrawItem`。
- **只有上传会写这个 buffer**（D12 的"一个写者"）。以后 compute 写的动态几何（蒙皮输出）要另开一个 buffer。
- **不缩容，也不整理碎片。** 总空闲够、但没有一段连续的够用时，第一版按溢出处理。

**按元素分配（已定）**

顶点放一个数组（元素是 48 字节的顶点），索引放另一个数组（元素是 `uint32`），是两个 buffer。mesh 租的是"连续的
一串元素"，一个 mesh 两份租约：一串顶点、一串索引。租约的起点直接就是记录里的 `vertexOffset` / `firstIndex`，
不需要对齐处理；`SlotPool` 只是从"租一个"推广到"租一串"。以后每种顶点布局（vertex factory）一个顶点数组，
索引数组共用。没有选按字节分配：那样可以合成一个 buffer，但偏移要按 stride / 4 对齐才能换算成记录里的单位。

**容量分三版放开（已定）**

显存是创建时就占用的，定大了浪费、定小了不够。后一版不推翻前一版：

| 版本 | 做法 | 共享 buffer 的个数 | 归属 |
|---|---|---|---|
| 一 | 固定容量，一个可配的常量；溢出时报错并丢弃，同 `GlobalBuffer` | 1 | 本计划 |
| 二 | 容量不够时搬迁扩容：开一对更大的，把旧内容拷过去，切换 | 1 | 本计划之后 |
| 三 | 长到上限后开第二个；超过阈值的大 mesh 独占一个 | 大于 1 | I5 做出多 bucket 之后 |

- 本计划只做第一版。它和现有 Binding 系统完全一致，不需要新机制。
- 第二版新增三件事：旧 buffer 的内容拷到新 buffer；切换时重烘所有 `DrawItem`（租约存的是偏移，不用变）；旧 buffer 等
  在途的帧用完再释放。按 D12，拷贝期间图形队列照常读旧 buffer。
- 第三版才让 bucket 大于 1（所在 buffer 是 batch key 的一部分）。
- 没有选"一堆固定大小的小页"：它直接增加 buffer 的个数，而减少 buffer 切换正是做这件事的目的。
- 这是成熟做法。Bevy 的网格分配器是同一个形状：顶点与索引各自一套共享 buffer，从 1 MiB 起、满了重新分配并拷贝
  （每次 1.5 倍）、到 512 MiB 上限后再开新的、超过 256 MiB 的 mesh 独占一个，分配按批提交、建议每帧最多一批
  （bevy PR #14257 与 `SlabAllocatorSettings` 的文档，2026-10-05 核对）。
- 没有选稀疏资源（预留大 buffer、按需绑定物理内存）：在 Vulkan 上是可选特性。

**区间实体与分段上传（已定）**

一个资源会有多个上传，是以后的主要路径（image 的子资源上传是同一件事）。做法：**谁要上传，上传组件就打在谁身上。**

- 每一段是 RHIContext 里的一个实体，带一个**区间组件**：`Ptr<RHI::Buffer>` + 字节偏移 + 字节数。引用的是 RHI 对象，
  不是别的实体。`RHI::DrawItem` 里的 view 本来就是这样持有 buffer 的。
- 共享 buffer 本身仍是几何系统持有的普通资源实体，带 `Components::Buffer` 与静态 attachment，和原生对象一对一。
  区间实体不带这些，按实体遍历资源的系统看不到它们。
- 和现在单个 buffer 是同一个状态机，只有"物化"不同：

  | | 现在 | 分段 |
  |---|---|---|
  | 申请 | 实体带 `PendingBufferInit` | 实体带一个"要一段"的申请 |
  | 物化 | `RHIResourceSystem` 建 buffer，挂 `Components::Buffer` | 几何系统在固定的那一处分配，挂区间组件 |
  | 上传 | `PendingBufferUpload` 写进自己的 buffer | `PendingBufferUpload` 写进区间指向的那一段 |
  | 提交后 | 实体上盖 `PendingSync` | 同样盖在这个实体上，就是这一段的 fence |
  | 就绪 | 没有 `UploadPendingTag` | 没有 `UploadPendingTag`，且 fence 已完成 |

- 上传系统的规则：**上传到这个实体所描述的范围；自己拥有整个 buffer 的实体，范围就是整个 buffer。** 现有行为是特例。
- 区间组件定义在 RHI 的组件层（上传系统在 RHI 层），只是数据，描述的是原生 API 每个绑定调用都在用的
  "buffer + 偏移 + 大小"。
- 拿 handle 取 buffer 的地方（烘 `DrawItem`、上传、就绪判断）走同一个解析函数，返回 buffer、基偏移、字节数。
- 就绪判断要看 fence：共享 buffer 一直是稳定状态，渲染图不会替某一段去等。
- 第二版搬迁扩容时，几何系统把所有区间组件里的指针换成新 buffer，与重烘 `DrawItem` 是同一趟遍历。
- image 以后用同一个形状：实体持有 `Ptr<RHI::Image>` 加子资源范围，`PendingImageUpload` 打在它上面。这次不做。

**`g_Geometries` 表**（稳定槽位，`GlobalBuffer`）：每个几何一行，`{ firstIndex, indexCount, vertexOffset, bucket,
局部 AABB 的中心与半长 }`，前三项来自租约。`InstanceData` 用 padding 加一个 `m_geometryIndex`，仍是 208 字节。
它是同一个系统的另一个槽位，也是 I9（P7 命中点着色）要的"几何记录表"的起点。

**去重不在本计划内。** 现在每个世界实体各传一份几何（`MeshSystem.cpp:137`）。以后 Mesh 会像 Material 一样放进
独立的 Context 当实体，世界实体共享的是它的句柄，自然去重。

D5 已撤销：实例身份的做法不变，不构成一项决策，内容并入 D8。后面的编号不动。

### D6　剔除直接遍历 `g_Instances`，靠记录里的 mask 跳过空洞　✅ 已定

`g_Instances` 是稳定槽位，有空洞且不清零，shader 遍历 `[0, Size())` 会碰到已经删掉的物体。
`TODO_GlobalBufferUploadPlan.md` §六 为这种情况预留的做法是补一个 GPU 侧的有效性信号，这里落的就是它。

- `InstanceData` 用 padding 加 `m_drawMask`：每一位是一个分类（对应 `OpaqueTag`、`ShadowCasterTag`），
  **0 表示不画**。连同 D4 的 `m_geometryIndex`，仍是 208 字节。
- 剔除 shader 的第 i 个线程读 `g_Instances[i]`：mask 不含当前集合的位就退出；否则按 `m_geometryIndex` 读
  `g_Geometries` 拿包围盒与 draw 参数。引用只有一跳，和实例 → 材质同形。
- 写成 0 的三种情况：
  - **槽位被释放。** `SlotPool::Free` 只记下 id（`HandlePool.h:42` 的约定），`GlobalBuffer::Update` 在分配新槽位
    之前排空，把 staging 里那一条清零。staging 整块上传，不需要别的通知途径。
  - **实体带 `DeadTag` 但还没销毁。** 编码的 view 排除它（`GlobalBuffer.h:83`），它还握着槽位，旧记录要清掉。
  - **几何还没就绪**（D4：这一段的 fence 未完成）。
- mask 由 `InstanceBindingSystem` 编码时写。分类要和 CPU 路径打 tag 用同一个来源，否则两条路径画的集合会不一样。
- tag 与位的对应定义在 D7 的共享头里。只走 CPU 路径的分类（以后的半透明）不占位。
- 回收延迟不需要：一帧内剔除与绘制读的是同一份副本。跨帧的 GPU 状态出现时再加
  （同 `TODO_GlobalBufferUploadPlan.md` §六）。

代价：空洞白占线程。高水位只增不减，卸载大量物体后仍按峰值派发，每个空线程读一个字段就退出。
每个视图都要把整张表过一遍。

否掉的做法：另建一个每帧重建的稠密清单 `g_SceneDraws { instanceIndex, drawMask }`。它多一个 buffer、多一个每帧
O(N) 的循环、多一层引用（清单 → 实例 → 几何）。引擎里一个世界实体就是一个槽位、一次绘制，实例表本身就是绘制表。

### D7　剔除的输出按（draw 集合，视图）分段　✅ 已定

分段本身不是选择：一次 indirect 调用里不能换视图常量与渲染目标，所以每个视图一段；不同 pass 画的集合不同，
所以每个集合一份。

- **draw 集合集中定义**在一个共享头里（暂名 `Feature/SceneDraws/SceneDraws.h`，同 `SceneTextures.h` 的先例）：
  `MainOpaque` = `MainViewTag` × `OpaqueTag`，`ShadowCasters` = `ShadowViewTag` × `ShadowCasterTag`。
  一个集合知道自己的视图类型、分类 tag 与它在 `m_drawMask` 里的位、参数 buffer 与 count buffer 的名字。
- **DepthPre 与 GBuffer 共用 `MainOpaque` 的结果**：剔一次，两个 pass 读同一段。可见性是（物体，视图）的属性，
  与 pass 无关；而且 GBuffer 的深度测试是 `Equal`，它依赖 DepthPre 画的是同一批物体。
- **每个集合两个图内 transient buffer**：参数 buffer 是"视图数 × N"条记录，N 是 `g_Instances` 的高水位
  `Size()`（D6：不另外数每个集合有多少 draw），第 k 个视图的段从 `k × N` 开始；count buffer 每个视图一个 `uint`。
  容量向上取到 2 的幂，免得 transient 池每帧换 buffer。
- **k 是视图在 `CollectViews<ViewTag>` 里的枚举序。** 剔除 pass 与绘制 pass 问同一个函数，否则会读到别的视图的段。
- **段的布局只在一个函数里算**：给定视图序号，返回参数的字节偏移、最多几条、count 的字节偏移。剔除 pass
  （告诉 shader 往哪写）、lowering（填 indirect 的 `DrawItem`）、清零 count 的那一步都问它，谁也不自己算 `k × N`。
- **每个（集合，视图）一个 compute Scope**，`viewIndex` 与段的起点走 `.Constant`，沿用 P4 D2 的做法。
- **视锥平面在 shader 里从 `g_Views[viewIndex].m_viewProjection` 提取**，就是光栅化用的那个矩阵，`ViewData` 不加字段。
  局部 AABB 经 `Model` 变到世界空间（中心乘矩阵，半长乘矩阵的绝对值）再对六个面测试。reversed-Z 加无限远平面时
  有一个面是退化的，要跳过。
- 剔除 pass 放在帧结构的 Scene Update 段，各 binding system 上传之后、ShadowPass 之前，对应路线图里
  `GPU Culling` 那一行。

已知开销：Shadow 的视图数上限是 `kShadowViewCapacity`，参数 buffer 按"活跃视图数 × 高水位 × 20B"分配。
它不能靠"GPU 先计数、再按前缀和排紧"来缩小：每个视图的段从哪里开始，是 CPU 录制 indirect 调用时填的偏移
（DX12 与 Vulkan 都是），GPU 算出来的起点 CPU 拿不到。真成为问题时要另想办法，现在不做。

### D8　输出总是压缩：原子追加 + count buffer　✅ 已定

压缩的是输出（参数 buffer），不是输入。空洞、不属于本集合的槽位、被剔掉的物体，对应的线程都不写记录；
可见的线程对 count buffer 里本视图的那个 `uint` 做 `InterlockedAdd`，拿到序号后把记录写到"段的起点 + 序号"。
跑完之后那个 `uint` 就是可见条数，indirect 调用以 `maxCount` = 高水位、count 来自 buffer 执行。计数与写记录在
同一次派发里完成，不需要单独的计数 pass。

**没有不压缩的形式。** 不压缩（每个槽位原位写一条，不画的 `instanceCount = 0`）只在设备没有 count buffer 能力时
才有意义，而那类设备不支持运行（D3）。

**压缩不需要额外的表。** 实例身份沿用现在的做法：ID 流 slot1，记录里的 `firstInstance` 就是 `g_Instances` 的槽位，
DX12 与 Vulkan 读逐实例顶点流时都会加上它。记录自带身份，压缩只是拷贝记录，不需要另一张"第几条记录 → 哪个实例"
的表，shader 不用改。

count buffer 每帧要清零。没有 buffer 的 clear 操作，由剔除 pass 的第一个 Scope 写 0。

风险：原子追加的顺序不保证跨帧稳定，完全共面重叠的两个表面在 GBuffer 里（深度 `Equal`，后写的赢）可能逐帧换人。
这本来就是内容上的 z-fighting，但现在的 CPU 顺序是稳定的，所以要实测。实测有问题时的补救是保序的压缩
（先写每个槽位可见与否，再做前缀和，最后按位置写），多两到三次派发。

### D9　渲染图的入口　✅ 已定

新入口都挨着现有的同类放，pass 选场景物体的写法不变。

- **访问角色**：`RenderScope` / `ComputeScope` 加 `IndirectArguments(name)`，对应
  `AttachmentUsage::Indirect` + `AccessFlags::IndirectRead` + `AttachmentStage::DrawIndirect`。RHI 侧都有，只缺入口。
  count buffer 也按这个角色读（DX12 要求它在 indirect argument 状态，Vulkan 要求 indirect 用途位）。
  它返回 `Attachment`：可以接 `.From`，不能 `.Bind`（不是 shader 访问）。写这个 buffer 与把它当间接参数读是冲突的
  访问，不能在同一个 Scope 里：写参数的 pass 与按参数调用的 pass 因此必须分开。
- **`ComputeScope::DispatchIndirect(arguments, byteOffset)`**：挨着 `Dispatch`，产生一个 indirect 的 `DispatchItem`。
  `arguments` 是本 Scope 的 `IndirectArguments(name)` 返回的那次访问，不是名字（同 `RenderScope::Resolve` 收
  render target 的先例）。buffer 里放的是**线程组数**，不是 `Dispatch` 收的线程数，写参数的 shader 自己除以
  `[numthreads]`。本计划内没有消费者，用户决定一并做；由示例 `IndirectDispatch` 验证。
- **item 的 buffer 指针由 lowering 填。** item 在声明时建，那时 buffer 还没有后备，所以 item 上带一个组件记着它用
  哪次访问（`ItemIndirectArguments`），`CompileItemIndirectArguments` 在生成提交列表之前把后备 buffer 填进去。
  分配 transient 资源排在编译的最前面，所以此时后备一定已经就位。
- **buffer 的 `.Bind`**（步骤 1 补的，原来只支持 image）：绑定之前要先用 `.View(...)` 给出元素大小与个数，buffer
  没有"整个资源"这样的默认 view。buffer 的 `.BindIndex` 没有做，目前没有需要它的地方。
- **场景 draw 的声明不变**：仍是 `s.Accepts<OpaqueTag>()`，三个 pass 的代码不改。pass 不知道自己被 direct 还是
  indirect 画。"视图类型 + tag"确定是哪个集合（D7）：视图类型来自 pass 的 `RendersView<ViewTag>()`，
  `Accepts<Tags>()` 在模板里顺带记下这些 tag 在 `m_drawMask` 里的位。某个 tag 没有位，这次选择就只走 CPU 路径。
- **走哪条路由 builder 按开关决定**（能力不足的设备不会运行到这里，D3）：GPU 路径下 builder 替 Scope 加上参数 buffer 与 count buffer 的
  `IndirectArguments` 访问，pass 不声明它们。这部分步骤 1 没有做，留到步骤 4；它调的会是
  `s.IndirectArguments(name)` 底下的同一个函数。
- **CPU 提交路径一直在，不随 GPU 路径做完而清理。** 走它的有：Scope 自己声明的 item
  （全屏三角形、Skybox 这类 `NoInstanceBinding` 的 draw）；P5 的 Translucency（要排序）；运行时开关关掉的时候
  （§五的验证靠两条路对照）。实体上持久的 `RHI::DrawItem` 因此保留，步骤 3 之后它的 view 指向共享 buffer。
  GPU 路径打开时，被 indirect 画的物体的 `DrawItem` 是闲置的；让它们不生成是以后的小优化。
- **lowering**：`CompileScopeSubmitRanges` 对 GPU 路径的集合，在每个视图句柄之后追加一个 per-frame 的
  item 实体，带 indirect 的 `RHI::DrawItem`（VB / IB = 几何所在的 buffer 对象 + ID 流；参数 = 该视图的段）。
  **executer 不改**：`SubmitItem` → `CommandList::Submit(DrawItem)` 已经是这个形状。

### D10　与 I5（多 PSO）的关系　已撤销，改为说明

D10 原来想为 I5 预留"段按（视图，bucket）划分"的形状。它讨论的几乎全是 I5 的设计，本计划里不构成决策，
也**不为多 PSO 预留任何东西**：不给段加 bucket 这一维，item 上不带 PSO。I5 设计时再仔细考虑。

**多 PSO 对 indirect 的影响**：一次调用里不能换 PSO，也不能换顶点 / 索引缓冲。一个 pass 有 V 个变体之后，每个视图
要 V 次调用，各读自己的一段（bucket）。bucket 实际是"PSO 变体 × 几何所在的 buffer × 索引格式"。

**接缝在提交列表上，两条路径共用。** 提交列表是扁平的句柄序列，视图句柄是其中"切换状态"的标记
（`RenderGraphExecuter.cpp:381`）。多 PSO 给两条路径带来同一个变化，视图下面多一层换 PSO 的标记：

```
今天      普通 Draw：[视图] item item item …          Indirect：[视图] item
I5 之后   普通 Draw：[视图] [bucket] item item [bucket] item …
          Indirect： [视图] [bucket] item      [bucket] item …
```

普通 Draw 要靠收集时分组或排序把同一个 bucket 的物体排到一起；indirect 把分组交给剔除 shader，每个 bucket 后面
只有一个 item。

**已经和用户对过的方向**（2026-10-05），供 I5 设计时用：

- **每个材质一套 shader 必须支持。** 所以 bucket 不是固定的小集合，是各 pass 在运行期按活着的材质列出的；
  "材质 → bucket"的对应按 pass（深度与阴影要把大多数材质合并成几个 bucket，GBuffer 不能）。
  否掉的做法：把变体收成与 pass 无关的固定枚举（alpha 模式 × 单双面），它只在固定材质模型下成立。
- **可见性每个视图只算一次，各 pass 再按自己的 bucket 生成参数。** 不走"各剔一次"：GBuffer 依赖 DepthPre 画的是
  同一批物体（D7），加上遮挡剔除之后两次计算不再保证得出相同结果，可见性的开销也会变大。
  现在"直接写 draw 记录"是只有一段时的退化形式。
- **CPU 不回读 GPU 的结果。** 各 bucket 的起点与容量来自 CPU 上的统计（全场景里属于它的实例数），不来自剔除结果。
  代价是空调用：一个 bucket 在某个视图里一条都不可见时，仍要设一次 PSO、发一次条数为 0 的调用。
  每个视图的总容量不因分 bucket 而变大。

**留给 I5 的问题**：

- 一个材质在某个 pass 里的键怎么算（比如"它的 shader 会不会影响深度"在哪里声明）；
- 每个 pass 一份的"材质 → bucket"表由谁持有（它是按材质槽位寻址、要上传的数组，形状同 `StagedArrayBuffer`，
  但数量随 pass 而定）；
- 容量的统计放在哪。讨论过的一种做法：实例系统维护与 pass 无关的"每个（材质，几何组）有几个实例"，物体增删时
  加减，各 pass 自己折算；
- 新变体第一次出现时现建 PSO 的卡顿（CPU 路径同样有）。

D11 已撤销：CPU 提交路径保留不构成一项决策，没有需要权衡的另一边，内容并入 D9。后面的编号不动。

### D12　buffer 的跨队列同步按原生语义纠正　✅ 已定；步骤 2 已实现

**原则**：RHI 把两个 API 原生支持的能力暴露出来，确实需要的机制才加（执行过程中记录每个资源的状态是需要的），
不为一个场景创造新概念。

**原生语义**（查自 Enhanced Barriers 规范、D3D12 的多队列同步文档、Vulkan 规范的同步一章）

- 屏障只在一个队列内部起作用。Vulkan：屏障在"提交到同一个队列的、它之前与之后的命令"之间定义依赖。
  DX12：作用范围是一次 `ExecuteCommandLists`。
- 队列之间的先后只靠 fence / semaphore。
- 能跨队列持续存在的资源属性只有两个：纹理的 layout，以及 Vulkan 独占模式下的队列族所有权。buffer 没有 layout。
- DX12 的 buffer 始终允许"多个读者和一个写者同时访问，只要写者不改读者正在读的字节"；同一时刻只能有一个队列写；
  写完的字节要等 fence 之后别的队列才能读。

所以对 buffer：

| 情况 | 原生需要什么 |
|---|---|
| 同一队列内，有依赖的两次访问 | 屏障 |
| 跨队列，访问的是同一批字节 | fence；Vulkan 独占模式另加一对所有权屏障 |
| 跨队列，访问的字节不相交 | 什么都不需要 |

**偏差**：步骤 2 之前的代码把"独占模式"的做法（释放 / 接手一对屏障）用在了所有 buffer 上，也没有区分"字节不相交"的情况。
它对独占的 buffer 是对的。

**纠正**（步骤 2 已做）

| 位置 | 改之前 | 改之后 |
|---|---|---|
| 上传前后的屏障（`AsyncUploadSystem` 的 `ProcessBatch`、`SubmitBatch`） | 每个目标 buffer 发接手和释放两条 | 共享模式的 buffer 两条都不发；独占的保持 |
| 上传前等读者（`SubmitBatch`） | 实体上的 `PendingSync` 没完成就推迟 | 不变；新的区间实体自己没有 `PendingSync`，所以写不相交的段时不会等 |
| 上传后盖 `PendingSync`（`SubmitBatch`） | 盖在带上传组件的实体上，它就是 buffer 实体 | 不变；按段上传时那个实体是区间实体（D4），fence 自然落在这一段上 |
| 静态 buffer 要不要等（`CompileStaticResourceBarriers`） | 状态里的队列不是自己才等 | 共享模式：身上有 fence 且没完成就等，判断放在"已是稳定状态"的提前返回之前；独占的保持 |
| 静态 buffer 的屏障（同上） | 一条跨队列屏障 | 代码没有改：共享模式的状态不再被翻到 Copy，生成出来的自然是队列内的一条 |
| 导入的 buffer 被某个队列首次使用（`CompileExternalWaits`） | 状态里的队列不是自己才等 | 共享模式：身上有 fence 且没完成就等；独占的保持 |
| 渲染图里 buffer 换队列（`CompileBufferAccess`） | 等 fence，再发释放 / 接手一对屏障 | 共享模式：只记跨队列等待，两边都不发屏障；独占的保持（导入的独占 buffer 本来就不允许换队列） |

不动的：DX12 后端的跨队列屏障实现（留给独占模式）；image 的所有路径——image 上传确实两样都要，fence 管先后，
屏障管 layout。

**等待的判断为什么要改**（定 D12 时漏掉的）：原来"要不要等"看的是 buffer 状态里的队列，上传的屏障把它翻成 Copy，
图形队列看到不是自己才去等。共享模式不发屏障后它不再变，照旧判断的话图形队列不会等上传。所以共享的 buffer 改看
`PendingSync`。原表把 `CompileExternalWaits` 列为"不动"，同样是错的。

**共享的 buffer 不区分"是不是本队列留下的"**：有 fence 且没完成就等。

- 等自己队列的 fence 是合法的，那次 signal 在队列里排在前面，等待一到就通过。代价是执行器为每个等待多拆一次提交。
- 现在这个代价不存在：引擎里没有导入的 buffer；静态 buffer 帧末不会被渲染图重新盖章（那段按 `BackingBuffer` 遍历，
  静态 buffer 没有这个组件），身上只可能是上传的 fence。
- 出现"每帧都用的导入共享 buffer"时再区分。那时的做法是在 `PendingSync` 里记下 signal 它的队列对象、使用方比较队列；
  记对象而不是类别，因为上传系统的 copy 队列与渲染图的 copy 队列类别相同、对象不同。
- 试过又撤掉的：比较 fence 的地址是不是渲染图给本队列的那一个。它是从 fence 反推队列，依赖"只有渲染图用这组 fence"
  的约定。

**换队列时两边都不发屏障的依据**（Enhanced Barriers 规范 Barrier-Free Access 一节，2026-10-06 重读原文）：buffer 在
一次 `ExecuteCommandLists` 里的第一次访问可以不带屏障；执行器在每次等 fence 之前都先把已录的命令提交掉，所以一个队列
等完之后的访问一定是新一次提交里的第一次访问。图内创建的 buffer 掩码默认是 `All`，这条规则对它们全都生效。

按段上传不发屏障之后，buffer 的状态（发屏障时才更新）一直停在图形队列的顶点 / 索引读，渲染图每帧看到的是稳定状态。

**使用方的契约**（原生 API 同样不替你保证）：同一时刻只有一个队列写这个 buffer；读的字节与正在写的字节不相交；
写完的字节等 fence 之后才读。几何 buffer 由租约保证后两条（D4）。

**对几何上传的含义**：共享 buffer 用共享模式创建（`m_sharedQueueMask` 带图形与 copy 两位）；copy 队列直接往租约的
那一段拷贝，不发屏障；那一段在 fence 完成后就绪。

**没有覆盖的**

- 同一个队列隔着别的队列、对同一个 buffer 连续两帧写（如 compute 每帧写、图形每帧读）：严格按 Vulkan，compute 自己的
  两次写之间需要一条队列内的屏障，而一份状态填不出"上一次在本队列的访问"。现有代码同样没有覆盖，也没有这样的用例，
  这次不处理。
- DX12 规范有一句"对同一资源的连续写必须用屏障刷新"。一批上传里往同一个 buffer 拷多段是否算，读不出确定答案；
  按惯例这是合法的常规用法。步骤 2 没有碰到这个情况：一批里一个 buffer 只有一次拷贝，数据跨 staging 包时拆出的几次
  拷贝分在不同的提交里。步骤 3 按段上传时才会出现，到时用 debug layer 与 GPU-based validation 确认。

---

## 四、步骤

### 0　RHI

1. 按 D2 删旧类型，加三种记录的 POD 与新的 `IndirectArguments`。
2. DX12：Device 上每种记录一个 command signature；`CommandList` 的 draw / dispatch indirect 路径重写。
3. D3 的能力位；DX12 全填 true，Vulkan 的 `PhysicalDevice` 把三项都查出来；`RenderSystem` 初始化时检查三项齐全。
4. 在 `SandBox/Program/RHI/` 新增示例 `IndirectDraw`：CPU 填参数的 indexed indirect draw，不经过渲染图。
   6 个方块各一条记录；`kUseCountBuffer` 控制带不带 count buffer。

### 1　渲染图

1. buffer 的 `.Bind`：builder 接受 buffer 访问，lowering 从资源的 view 缓存取 buffer view 填进 pass 的绑定。
   原计划以为只要核对，实际是 builder 与 lowering 都只支持 image，要补实现。
2. `IndirectArguments(name)` 角色（两种 Scope）与 `ComputeScope::DispatchIndirect`，以及 lowering 的
   `CompileItemIndirectArguments`。
3. 示例 `IndirectDispatch`（`SandBox/Program/RenderGraph/`）：`ArgsPass` 用 compute 把三个线程组数写进图内 buffer，
   `PatternPass` 先直接派发铺一层暗图案、再按那个 buffer 间接派发画亮的，`PresentPass` 显示。
4. `SparkRenderTest` 三个用例：把 buffer 当间接参数读的 pass 排在写它的 pass 之后（顺带补上 `ResolveGraph` 处理
   buffer 访问的测试，夹具原来只造 image 的访问）；`Indirect` 用途换算成 `IndirectRead`；写一个 buffer 与把它当
   间接参数读是冲突的访问。原计划的"UAV 写 → IndirectRead 的屏障"做不成单元测试，由示例在 debug layer 下验证。

### 2　buffer 跨队列同步的纠正

1. 上传与静态路径：`AsyncUploadSystem` 对共享模式的 buffer 不发上传前后的屏障；`CompileStaticResourceBarriers`
   对它们按 fence 决定等不等。mesh 的 VB / IB 改成共享模式（图形与 copy 两位），否则没有 buffer 走到新路径——
   走上传的三种 buffer 原来全是独占的；`InstanceIDBuffer` 留独占，两条路径各有一个真实的 buffer 在跑。
   新增 `RHI::IsExclusiveQueueMask`，替掉上传系统与编译器里各自手写的判断。
2. 图内与导入的 buffer：`CompileBufferAccess` 对换队列的共享 buffer 只记跨队列等待；`CompileExternalWaits` 对共享的
   buffer 按 fence 决定等不等。示例 `IndirectDispatch` 加常量 `kArgsOnComputeQueue`（默认关），打开后 `ArgsPass` 跑在
   Compute 队列，是"一个队列写、另一个队列读同一个图内 buffer"的用例。
3. `PendingSync` 的协议注释补上共享 buffer 的规则。

与原计划的出入：原计划是"按 D12 的表改五处"，实际表里有两处要补（见 D12"等待的判断为什么要改"）；原计划以为换队列
那一处验证不了，实际可以用示例覆盖。

### 3　几何租约

1. 区间组件与解析函数；`AsyncUploadSystem` 按区间找目标与基偏移；就绪判断加上 fence。
2. 几何的 Binding 系统：固定容量的共享 buffer（顶点、索引各一个）、变长的租约分配、延迟回收。
3. `g_Geometries`、`InstanceData::m_geometryIndex`，HLSL 镜像同步。
4. `MeshSystem` 改为只提交申请；`DrawItemRouter` 烘 `DrawItem` 时 view 指向共享 buffer，租约换算成
   `firstIndex` / `vertexOffset`。
5. 仍走 CPU 提交。画面应与改动前完全一致。

### 4　indirect draw，CPU 填参数

1. `SceneDraws.h`（集合，以及 tag 与 mask 位的对应）；`InstanceData::m_drawMask`，HLSL 镜像同步；
   释放的槽位、带 `DeadTag` 的实体、几何未就绪的实例写 0（D6）。
2. 参数 buffer 先由 CPU 每帧上传（每个集合一份，所有视图读同一段，无 count buffer）。
3. `Accepts<Tags>()` 记下 mask 位、builder 的路径选择、lowering 的 indirect item。
4. 加运行时开关。三个消费者（DepthPre、GBuffer、Shadow）的代码不改，开关打开后它们走 GPU 路径。

### 5　compute 视锥剔除

1. `InstanceCulling` pass 与 shader，按 D7 输出；参数 buffer 的生产者从 CPU 换成它。
2. D8 的压缩（原子追加 + count buffer），以及每帧清零 count 的那个 Scope。
3. 更新路线图：帧结构里 `GPU Culling` 一行、I11 的状态。

---

## 五、验证

按仓库的分工，画面由用户确认，我只保证能跑、测试通过、validation 无报错。

- **步骤 0**：`IndirectDraw` 不带 count buffer 时 6 个方块都出现，带 count buffer（值为 3）时只出现上面一排；
  D3D12 debug layer 无报错。`firstInstance` 不为 0、不带索引的 indirect draw、indirect dispatch 这个示例不覆盖。
- **步骤 1**：`SparkRenderTest` 新用例通过；示例 `IndirectDispatch` 的画面是整窗的暗图案加左边一条宽度来回变化的
  亮带，debug layer 无报错。
- **步骤 2**：画面与改动前一致；加载场景、流式加入物体时 debug layer 无报错；`IndirectDispatch` 的
  `kArgsOnComputeQueue` 开关两边画面一致、debug layer 无报错。GPU-based validation 没有跑。实际跑了什么见"状态"一节。
- **步骤 3**：画面与改动前一致；抓帧里所有 mesh 的 VB / IB 落在共享 buffer 上；删掉实体后那一段被回收、再加载不增长；
  运行中加载新 mesh 的那几帧，已有物体照常画、validation 无报错。
- **步骤 4**：开关两边画面一致；抓帧里 DepthPre / GBuffer 各一次 `ExecuteIndirect`，Shadow 每个视图一次。
- **步骤 5**：
  - 开关两边画面一致，包括阴影——**阴影视图要用自己的视锥剔**，主相机转开后投影者不能消失；
  - 物体跨过屏幕边缘时不闪、不提前消失；
  - 删掉物体后它立刻不再被画，之后在同一个槽位加载别的物体也正常（D6 的空洞）；
  - 抓帧里 count 的值等于该视图实际画出的 draw 数；
  - 完全共面的两个表面不逐帧换人（D8 的风险）；
  - GPU-based validation 无报错。

**验证不到的**：

- **收益。** 现在没有上万实例的场景。需要数字的话，另做一个把同一个 mesh 铺成 N×N 的压力场景。
- **Vulkan。** 没有 Vulkan 后端：D3 的"能力不足时拒绝运行"在 DX12 上不会触发（用户决定不强行关能力位来测）；
  D12 里独占 / 共享两种模式的区别在 DX12 上没有可观察的差异。

---

## 六、未决

- **Mesh 怎么表达"我要的是一段几何数据"（D4）。** 它不认识渲染侧的几何系统，申请组件放在哪一层、长什么样，
  留到实现时定。
- **共享 buffer 第一版的容量取多少（D4）。** 没有数据，先按现有场景的实际用量取一个宽裕的值。
- **D12"没有覆盖的"两条。** 实现细节已在步骤 2 定下，见 D12。
- **`PendingSync` 按用途拆开。** 2026-10-06 与用户对过方向，没有定，也不在步骤 2 内。
  - 现状：资源实体上一个 `{fence, value}` 槽位，后盖的覆盖先盖的，同时承担三种关系——

    | 关系 | 谁盖 | 谁读 | 怎么用 |
    |---|---|---|---|
    | 上传 → 首次使用 | 上传系统 | 渲染图 | GPU 上等（`queue.Wait`） |
    | 使用 → 再次上传 | 渲染图帧末 | 上传系统 | CPU 上查，没完成就推迟 |
    | 渲染图内部跨帧、跨队列 | 渲染图帧末 | 渲染图下一帧 | GPU 上等 |

  - 读代码看到的迹象：覆盖是否安全靠注释里的推理保证（`RenderGraph.cpp` 帧末给静态资源盖章的那段）；
    `Component.h` 的协议写"使用方等完后移除"，实际没有任何地方移除；"是谁"在资源的状态里、由上传线程发屏障时写，
    "何时完成"在 `PendingSync` 里、由主线程提交时盖，两处不同步（独占路径上渲染图编译若先于上传线程，那一帧不会等上传；
    只是读出来的，没有观察到）；盖章靠每个提交方自觉，静态资源因为不知道哪些被用了只能每帧全盖，逐资源的信息等于一个全局值。
  - 方向：不往这个槽位里加信息，按用途拆成三件各自更简单的事。
    - 上传 → 首次使用：改成 CPU 上判断就绪，上传的 fence 完成前资源不参与渲染。渲染队列不再在 GPU 上等 copy 队列，
      上面那个不同步也随之消失。D4 给租约定的就是这条规则，这里是把它推广到所有上传的资源。代价是资源晚一两帧出现。
    - 使用 → 再次上传：用一个全局的帧 fence（"最后一次可能用到它的那一帧跑完了没有"），与延迟释放队列同一个思路。
    - 渲染图内部：留在渲染图自己的组件里，生产方与使用方都是它自己，不需要跨系统的协议。
  - 没想清楚的：`PendingSync` 的使用方没有全部读过（`UIProcessFeature` 里有一处）；image 上传后还要一条屏障转 layout，
    它怎么配合就绪判断。
  - 时机：步骤 3 第一小步要写"就绪判断加上 fence"，到那时定它只对租约生效，还是作为所有上传资源的统一规则。
- **`m_drawMask` 的来源（D6）。** 分类 tag 现在打在 RHIContext 的 `GeometrySpec` 实体上，mask 是
  `InstanceBindingSystem` 遍历世界实体时写的；两边怎么共用一个分类来源、几何就绪怎么传到实例编码，留到实现时定。
- **`DeadTag` 到销毁之间隔几帧（D6）。** 没有查。它决定"带 `DeadTag` 的实体清零"这一步是必需的还是只是保险。
- **16 位索引。** 现在资产都是 `UINT32`。一次调用只有一种索引格式，以后引入 16 位索引的话它是 bucket 的又一维。
- **每视图一个 Scope 还是一次二维 dispatch（D7）。** 阴影 tile 多的时候后者省 dispatch，但要一张"参与的视图下标"表。
- **多个 MainView。** 设计上按视图分段，天然支持；但 `MainOpaque` 的消费者（DepthPre / GBuffer）之外的 pass 目前只
  处理第一个 MainView，与 P3 / P4 的遗留相同。
- **多 PSO（I5）时怎么分 bucket。** 不在本计划内，已对过的方向与留下的问题见 D10 的说明。

---

## 七、改动清单（草案）

| 步骤 | 文件 |
|---|---|
| 0 | 删 `RHI/Resource/Buffer/IndirectBuffer{Signature,Layout,View,Writer}.*`、DX12 `Resource/Buffer/IndirectBufferSignature.*`；`RHI/Command/IndirectArguments.h`、`DrawArguments.h`、`DispatchItem.h`；`RHI/Factory.h`、`ID3D12Factory.{h,cpp}`；DX12 `Command/CommandList.{h,cpp}`、`Device/Device.{h,cpp}`；`RHI/Device/DeviceFeatures.h`；Vulkan `PhysicalDevice.{h,cpp}`；两个 CMake；新建 `RHI/Command/IndirectCommands.h`、`SandBox/Program/RHI/IndirectDraw.cpp`，`SandBox/Program/CMakeLists.txt` |
| 1 | `RenderGraph/PassScopes.{h,cpp}`、`RenderGraphBuilder.{h,cpp}`、`RenderGraphCompiler.{h,cpp}`、`RenderGraph.cpp`、`RenderGraphExecuter.cpp`（注释）；`Pass/Component/ScopeComponents.h`（`ItemIndirectArguments`）；新建 `SandBox/Program/RenderGraph/IndirectDispatchFeature.{h,cpp}`、`SandBox/Asset/Shader/IndirectDispatch{Args,Pattern}.hlsl`，`SandBox/Program/CMakeLists.txt`；`Test/Render/RenderGraphResolveTest.cpp`、新建 `Test/Render/BufferAccessTest.cpp`，`Test/Render/CMakeLists.txt` |
| 2 | `RHI/System/AsyncUploadSystem.{h,cpp}`；`RHI/HardwareQueue.h`（`IsExclusiveQueueMask`）；`RHI/Component/Component.h`（注释）；`Render/RenderGraph/RenderGraphCompiler.{h,cpp}`（静态 buffer 的等待、导入 buffer 的等待、逐 Scope 的 buffer 访问）；`Feature/Mesh/MeshSystem.cpp`（VB / IB 的掩码）；`SandBox/Program/RenderGraph/IndirectDispatchFeature.cpp`（`kArgsOnComputeQueue`） |
| 3 | `RHI/Component/Component.h`（区间组件）、`RHI/ResourceBuilder.h`（解析函数）、`RHI/System/AsyncUploadSystem.cpp`（目标与基偏移）、`RHI/System/RHIResourceSystem.cpp`（关闭时清理区间组件）；`Render/RenderGraph/RenderGraphUtils.h`（就绪判断）；新建 `Render/Binding/Geometry/`（系统、租约、变长分配、`g_Geometries`）；`Feature/Mesh/Components.h`、`MeshSystem.cpp`；`Render/Drawable/MeshGeometryComposer.cpp`、`DrawItemRouter.cpp`；`Binding/Instance/InstanceData.h` 与 `InstanceData.hlsli`。`RHI::Buffer` 与 `BufferPool` 不动 |
| 4 | 新建 `Render/Feature/SceneDraws/SceneDraws.{h,cpp}`；`Binding/SlotPool.h`、`GlobalBuffer.h`（排空释放的槽位并清零）、`Binding/Instance/InstanceData.h` 与 `InstanceData.hlsli`（`m_drawMask`）、`InstanceBindingSystem.cpp`；`PassScopes.h`（`Accepts<Tags>()` 记下 mask 位）、`RenderGraphBuilder.{h,cpp}`（路径选择）；`RenderGraphCompiler.cpp`（lowering）。三个 pass 的文件不动 |
| 5 | 新建 `Render/Feature/InstanceCulling/InstanceCullingPass.{h,cpp}`、`Shaders/InstanceCulling/InstanceCulling.hlsl`；`RenderSystem.cpp` 的注册；`TODO_RenderPipelineRoadmap.md` |

---

## 关联文档

- `TODO_RenderPipelineRoadmap.md` —— 总览；I11 在 §三，帧结构里的 `GPU Culling` 一行
- `TODO_DrawItemShapePlan.md` —— §二（记录里能放什么）、§五（per-batch PSO）、§六（M / V）、§七（变体索引不放实体上）
- `TODO_MultiViewPlan.md` —— §三·五：DrawList 按视图分、list 内按 PSO 分段
- `TODO_RenderGraphItemPlan.md` —— Scope / item 模型；逐 draw 数据走索引查 buffer
- `TODO_GlobalBufferUploadPlan.md` —— §六 预留的"GPU 侧遍历空洞的有效性信号"由 D6 落地；D4 的租约是它的变长版本
- `TODO_PerDrawPSOVariant.md` —— I5；D10 的说明里记着它设计时要用的方向
