# 子资源屏障（I4）　实现方案

`TODO_RenderGraphItemPlan.md` 第 18 条、`TODO_ScreenSpacePlan.md` 步骤 0b。P4 的 HZB 逐 mip 一个 Scope（读 mip j-1、写
mip j），需要同一张图的不同 mip 处在不同状态；欠账出处见 `TODO_IBLPlan.md`。

决策 D1~D7 均已确认。

---

## 一、现状

| 层 | 现状 | 位置 |
|---|---|---|
| RHI `ImageBarrier` | 带 `ImageSubresourceRange m_range`，默认整图；还没有调用者传部分范围 | `RHI/Resource/ResourceState.h` |
| RHI `ImageSubresourceRange` | 已有：mip / array 的 [min, max] 与 aspect，默认即整图 | `RHI/Resource/Image/ImageSubResource.h` |
| 跨帧状态记录 | buffer 上单个 `ResourceState`，图像上按子资源的 `ImageSubresourceStates`：`CommandList::QueueBarrier` 执行屏障时按屏障的范围写，下一帧首次触碰时 `GetResourceInitialState` 读整图状态。transient 资源由池放置时写成 `Undefined \| 前一占用者的访问` | `Buffer.h` / `Image.h`；DX12 `CommandList.cpp` 的 `QueueBarrier` |
| DX12 纹理屏障 | Enhanced Barriers 的 `D3D12_TEXTURE_BARRIER`，子资源范围由 `ImageBarrier::m_range` 换算（`ConvertBarrierSubresourceRange`） | DX12 `CommandList.cpp` 的 `QueueBarrier` |
| `ResourceStateTracker` | 一个资源一个 `m_current` 加 `m_lastAttachment`，首次触碰时播种，帧末清空 | `Pass/Component/RHIComponents.h` |
| `Pre/Post*Barrier` | 每个 attachment 实体最多各一个 | 同上 |
| `CompileScopeBarriers` | 排序后同一 Scope 内同一资源的 attachment 相邻，合并成一个访问（`ScopeResourceAccess`）；有写且不是 UAV 读写即断言 | `RenderGraphCompiler.cpp` |
| 首次触碰 | 播种时读 `m_current.m_queue`：跨帧换队列的断言、transient 图像不上 Copy 队列的断言、`FindExternalWait` | `CompileScopeResourceBarrier` |
| 追踪器的其他读者 | 帧末给 imported 资源盖 `PendingSync` 用 `m_current.m_queue`（`RenderGraph.cpp`）；交换链 Present 转换读 `m_current`，挂在 `m_lastAttachment` 的 `PostImageBarrier` 上（`CompileScopeBarriers` 末尾） | 见左 |
| 视图 | `ImageViewDescriptor` 已有 mip / array 区间，谓词 `OverlapsSubResource` 已写好 | `RHI/Resource/Image/ImageViewDescriptor.h` |
| 测试 | `SparkRenderTest` 没有屏障编译的用例 | `Engine/Code/Test/Render/` |

---

## 二、要改的

- `ImageBarrier` 加 `ImageSubresourceRange m_range`，默认整图，现有调用处不变。Vulkan 直接对应 `VkImageSubresourceRange`。
- RHI 图像的跨帧状态记录与渲染图的追踪器都按子资源记录，共用一个类型（D1、D2）。
- `CompileScopeBarriers` 按子资源范围分组（D4），源状态不同的子资源段各发一个屏障。
- `Pre/PostImageBarrier` 改为列表（D3）。
- DX12 填 Enhanced Barriers 的子资源范围（D5）。
- 跨队列 release / acquire 带上范围。这条路径本来就没有运行时覆盖，先保证正确，验证等第一个 async compute 用例。

**不变**：静态资源的屏障（`CompileStaticResourceBarriers`）、交换链的 Present 转换、buffer 屏障，都仍按整个资源。

---

## 决策记录

### D1　跨帧的状态记录：RHI 图像按子资源记录　✅ 已定

`QueueBarrier` 按范围更新图像上的记录，下一帧首次触碰时追踪器从它播种。

- **结构上即正确**：图像以什么状态组合结束这一帧都行，渲染图不必知道哪些图会活到下一帧，也不必为此补屏障。D3D12 与
  Vulkan 本来就按子资源跟踪状态与 layout。
- **纹理流式加载要用**：按 mip 逐级上传时，一张图的部分 mip 已转到 shader 读、其余还停在上传后的状态，且这些图不经过
  渲染图的帧末处理。

**状态从 `Resource` 下放到 `Buffer` 与 `Image`**：`Get/SetResourceState` 不是虚函数，状态留在基类而图像另存一份，经
`Resource&` 读到的就是没人维护的那份。buffer 仍是单个状态，只是挪到 `Buffer` 上；图像改为按子资源存储，提供按范围读取的
接口，并保留"整图状态"的读取接口给现有调用者（异步上传、`ResourceState.cpp` 的屏障辅助函数、静态资源屏障、各池子的
重置），只在整图一致时有效，不一致即断言。现有调用者都是整图访问，且手里都是具体类型，不用改。

不把 buffer 当作"只有一个子资源的资源"统一到基类（legacy D3D12 时代的做法，那时两者同是一种屏障）：Vulkan 与 Enhanced
Barriers 的 buffer 屏障没有 layout、没有子资源范围，仓库里的屏障、组件、编译器也已经是两条路径。

**追踪器的另两个读者**：交换链的 Present 转换永远整图，不受影响。帧末 `PendingSync` 盖章按资源所在队列取 fence，一个资源
只挂一个 `PendingSync`；一张 imported 图像在帧末若各子资源停在不同队列上，选不出一个 fence，先断言不支持，等第一个
async compute 用例再设计。

**删掉 DX12 `Image::m_subresourceState` 一族**：它是另一份按子资源的记录（存 D3D12 状态），从来不读，与 RHI 层的记录并存
只会误导。`GetSubresourceIndexByRange` 一并删：D5 用原生范围，不需要子资源下标。

不在渲染图里补帧末整图屏障（UE 的 RDG 对 extracted 资源这么做）：要维持"哪些图会活到下一帧"这条约定，且覆盖不了不经过
渲染图的流式上传。

### D2　追踪器的表示：按子资源的数组，RHI 与渲染图共用一个类型　✅ 已定

一个 RHI 层的类型（暂名 `ImageSubresourceStates`）：每个子资源（mip × array 层 × aspect）一个 `ResourceState`，加"整图
一致"的快速路径——没被部分访问过的图像（现在的全部）只存一个状态，开销不变。HZB 约 11 个 mip，cube 6 面 × 若干 mip，
数组都很小。

- RHI 图像上的跨帧记录（D1）与渲染图的帧内追踪器用同一个类型：追踪器首次触碰一张图时复制图像上的记录作为起点。一套
  逻辑、一套单测（D6）。
- 放在 RHI 层没有问题：内容只有 mip、层、aspect 与 `ResourceState`，不涉及渲染层的概念。
- **按矩形合并**：发屏障时把源状态相同的子资源合并成 mip × 层 × aspect 的矩形，一个矩形一个 `ImageSubresourceRange`。
  `D3D12_BARRIER_SUBRESOURCE_RANGE` 与 `VkImageSubresourceRange` 都是矩形；按子资源下标的连续段合并（O3DE 原本的做法）
  在多层图像上会多发——cube 的"前几级一个状态、最后一级另一个"按下标是 12 段，按矩形是 2 段。做法：每层先合并连续
  mip，再并分段相同的相邻层，最后并两个 aspect。
- **回到一致即折叠**：部分写入后若所有子资源状态相同，收回成单个状态，否则这张图以后每帧都走逐子资源比较。
- **aspect 维度保留**：深度模板图的 depth、stencil 各自记录。DX12 的两个 plane 本就独立；Vulkan 须开启
  `separateDepthStencilLayouts`（1.2 的可选特性），否则屏障必须同时带两个 aspect。Vulkan 后端要求该特性，不支持即初始化
  失败。去掉这一维会让"深度只读采样 + 模板写"无法表达。
- **3D 纹理的 depth slice 不是子资源**：两家 API 都是每个 mip 一个子资源。由视图换算范围时忽略
  `ImageViewDescriptor::m_depthSliceMin/Max`；同一 Scope 里读写同一 mip 的不同 depth slice 算冲突。
- **范围先归一化再比较**：`HighestSliceIndex` 与 `ImageAspectFlags::All` 按图像的 mip 数、层数、格式换算成实际值，
  "是否整图"据此判断。

不用区间列表（范围, 状态）：更省内存，但部分转换后的集合不再是矩形，拆分与合并的逻辑复杂得多。

不把子资源做成 entity：它是稠密、定形、随图像同生同灭的数组，操作都是按范围读写与相邻合并；引用它的（attachment、
视图）引用的是范围；跨帧记录在 `RHI::Image` 上，由只拿得到 `Image*` 的后端与异步上传写。同视图收进 `ImageViewCache`。

### D3　`Pre/PostImageBarrier` 改为 `fixed_vector`，仍挂在 attachment 上　✅ 已定

一个 attachment 只经一个 view 引用资源，但一个 view 可以覆盖一段子资源；段内源状态不一致，一个屏障就不够——RHI 的
`ImageBarrier` 与 Vulkan 的 `VkImageMemoryBarrier` 都只有一个源状态。源状态必须来自编译期的追踪器，不能交给后端从图像的
运行时记录里查：各队列的录制顺序不等于帧内执行顺序，Vulkan 还要源阶段与源访问。

- **Pre**：个数 = 范围内源状态不同的矩形数（D2）。现在都是 1；第一个用户 HZB → SSR 就是 2——SSR 用覆盖整条 mip 链的
  SRV 读，而 HZB 生成完时 mip 0..n-2 停在 shader 读、最后一级停在 UAV 写。上限是范围内的子资源数。
- **Post**（跨队列 release）：一个 attachment 写的一段子资源，被不同队列上的 Scope 分别接走时才会多于一个，现在所有 pass
  都在 Graphics 队列。

两者都改为 `fixed_vector`，内联容量 2，常见情况不分配；执行器的 `QueueScopePreBarriers` / `QueueScopePostBarriers`
遍历列表。

以后若屏障数量或归属需要调整，可以把屏障做成每帧实体，带一个指向 Scope 的链接组件，排序后按 Scope 记 Pre / Post 区间，
同 `ScopeAttachment` / `ScopeItem` 与 `RecordScopeRanges`；归属随之从 attachment 移到 Scope。

### D4　同一 Scope 内按范围分组　✅ 由 API 决定

- 范围重叠的 attachment 合并访问，冲突即断言（同现在）；
- 范围不重叠的各自发屏障。HZB 的"读 mip j-1、写 mip j"属于此类，DX12 与 Vulkan 都允许，前提是视图限定在对应 mip 上，
  `ImageViewDescriptor` 本来就是这样。

范围里的 `HighestSliceIndex`（到最后一级）用 `BackingImage` 的描述符换算成实际的 mip 数与层数。

**实现**：只处理"同一 Scope 里同一资源"的那一小组 attachment（排序后本来就相邻，现在的线性扫描就是这么分组的），不遍历
整个 Scope。组内不做几何上的范围合并（两个矩形的并集不一定还是矩形），直接复用 D2 的按子资源数组：

1. **累加**：建一个与图像同形的请求表，每个 attachment 把访问与阶段按位或进范围内的每个子资源。
2. **对比**：请求表与追踪器逐子资源比较，源、目标状态都相同的连续子资源合成一段，每段一个屏障，挂在组内第一个
   attachment 的 Pre 列表上。
3. **更新**：追踪器里这些子资源改为请求的状态。

2、3 就是 `ImageSubresourceStates` 本来要提供的"应用新状态 → 转换列表"，输入由"一个范围一个状态"换成请求表；D4 只多了
第 1 步的累加。"重叠的合并、不重叠的分开"由此自动成立。

**不单独写快速路径**：请求表与追踪器都是 `ImageSubresourceStates`，整图一致时它的 `Set` / `GetSpans` 直接返回，不分配、
不遍历，结果是一个整图屏障，与加范围之前相同。只留一条路径，少一处可能不一致的地方。

**声明的合法性在 Build 阶段拦住**，不流入编译：Scope 由 pass 显式 `Close()`，`RenderGraphBuilder::CloseScope` 对这一个
Scope 的 attachment 两两比较，同一资源、范围重叠（`ImageViewDescriptor::OverlapsSubResource`，buffer 视为整体）且访问冲突
（`RHI::HasAccessConflict`：有写且不是恰好 UAV 读写）即断言。不在声明的那一刻查，因为 `.View()` 是声明之后才收窄范围的。
两两比较与按并集判断等价。编译器因此只处理合法输入，累加只做按位或。

一个 pass 同一时刻只开一个 Scope：`OpenScope` 要求上一个已 `Close()`，往 Scope 上加东西要求它正开着，`EndPass` 要求都已
关闭。builder 因此只记当前 Scope 及其 attachment，原先按 pass 攒的 Scope 列表与"未设 stage 的 attachment"列表都不需要了，
按 Scope 的校验（attachment 非空、root constant 齐全、stage 已设）一并挪到 `CloseScope`。

**深度模板 attachment 必须覆盖图像的全部 aspect**：DSV 同时绑定两个 plane，视图只含 Depth 时按视图取范围会漏掉 stencil
plane。同样在 `CloseScope` 断言拦住，不自动扩展。

**首次触碰**：两条断言（跨帧换队列、图像不能以 Undefined 首次用在 Copy 队列）原先只在建追踪器时对单个
`m_current.m_queue` 做一次，改为按转换做：源队列与本队列不同、且源队列这一帧没有可挂 release 的 attachment（D7）时，
这段子资源就是从上一帧带过来的。

**`ExternalWait` 从屏障编译里拆出**（`CompileExternalWaits`，独立一步）：它只需要资源自己的记录与 `PendingSync`，不需要
追踪器。规则由"资源（或子资源）第一次被碰"改为按（资源，队列）：一个 imported 资源被别的队列留着、fence 未到时，每个
队列上第一个碰它的 Scope 都等。这是原规则的超集，不依赖子资源状态；多出来的是先在队列 A 碰过、后又在队列 B 碰的情况，
B 会多等一次已到或将到的 fence。资源被留在哪个队列取自它的记录，图像取被访问过的子资源所在的队列（必须同一个，同 D1）。
这条路径没有运行覆盖：编辑器默认场景与各 sample 里没有被别的队列留着的 imported 资源。

**Undefined 的子资源不发 release，但仍要等**：它没有内容要交接，只在目标队列发 acquire 一半（带 discard）。跨队列等待
照记——transient 图像的 mip 0 首次在 Graphics 上用、mip 1 之后首次在 Compute 上用时，前一占用者的同步是经 Graphics 上
那个 Scope 传递的（它的屏障带 global barrier 等过前一占用者），而后端的 acquire 一半不发 global barrier，Compute 不等它
就和前一占用者之间没有任何同步。这条路径没有运行覆盖。

**transient 图像的首次触碰可以是部分范围**：HZB 的第一个 Scope 只写 mip 0，其余 mip 仍是 `Undefined | 前一占用者`，各自
首次触碰时再发自己的 discard 与等待前一占用者的 global barrier。结果正确，每张图多出约 mip 数条 global barrier（分属
不同 Scope，`FlushBarriers` 里合并不掉）；要省可在第一个屏障之后把追踪器里其余子资源降为不带前一占用者的 `Undefined`
——前一占用者已被等过。先不做。

### D5　DX12 用 Enhanced Barriers 的原生子资源范围　✅ 已定

DX12 后端先切到 Enhanced Barriers（`TODO_EnhancedBarriersPlan.md`），`D3D12_TEXTURE_BARRIER` 的子资源范围（mip、array 层、
plane 各自的起点与数量）直接由 `ImageBarrier::m_range` 填，不再展开成逐子资源的屏障。深度模板的两个 plane 由 aspect 换算
成 plane 范围。

- 整图仍填 `0xFFFFFFFF`，与加范围之前逐比特相同。
- 范围含图像的全部 aspect 时 plane 数取格式的（`GetFormatPlaneCount`）：NV12 / P010 这类 planar 格式在 RHI 里是一个 Color
  aspect，在 D3D12 里是多个 plane。

### D6　可测性：抽出纯逻辑单测　✅ 已定

`CompileScopeBarriers` 依赖 `BackingImage` 等真实 RHI 对象，直接测很难。抽出两块纯逻辑单独测：

- `ImageSubresourceStates`（D2）："对一个范围应用新状态 → 需要发出的转换列表"；
- D4 的组内累加：独立函数，输入是组内各 attachment 的（范围、访问、阶段）与图像形状，不留在 `CompileScopeBarriers` 的
  匿名命名空间里。

落地：

- **测试放在 `SparkRenderTest`**：它链接 SparkRender，SparkRHI 的头文件可间接用到，不新建 RHI 测试工程。
- **`ImageSubresourceStates` 用 mip 数、层数、aspect 构造**，不接收 `RHI::Image`：测试不需要设备，RHI 图像在内部用自己的
  描述符构造它。
- **读写冲突的规则是一个纯函数**（`RHI::HasAccessConflict`），builder 据此 `ASSERT`：不用 death test 就能测规则本身。

用例：整图 → 部分 → 整图（回到一致后折叠）；相邻 Scope 读写不同 mip；同 Scope 重叠读写报冲突；深度模板的 plane；
多层图像按矩形合并；范围归一化。

### D7　追踪器的 `m_lastAttachment` 按队列记　✅ 已定

现在一个资源一个 `m_lastAttachment`，跨队列 release 与 `RecordCrossQueueWait` 都挂在它上面。部分范围之后，各子资源最后
一次访问可以在不同 Scope、不同队列，单个值选不出 release 该录在哪。

改为每个队列一个：该队列上最后碰过这个资源的 attachment。源队列为 Q 的一段，release 挂到 Q 的那个 attachment 上——它排在
Q 上所有更早的访问之后，所以正确；等待的生产者也取它的 Scope。交换链的 Present 转换取 Graphics 的那个。

不按子资源记：`ImageSubresourceStates` 只存 `ResourceState`（D2），再存一份同形的 attachment 数组，换来的只是 release
能早几个 Scope 录制。

只对图像：追踪器按类型拆成 `BufferStateTracker` 与 `ImageStateTracker`，buffer 只有一个状态，源队列唯一，
`m_lastAttachment` 仍是单个。

---

## 三、步骤（草案）

| 步骤 | 内容 |
|---|---|
| 1 ✅ | RHI：`ImageSubresourceStates` 及单测（D2、D6）；图像按子资源记录状态、整图读取接口（D1）；删 DX12 `m_subresourceState` 一族（即 `TODO_EnhancedBarriersPlan.md` 的步骤 4）。还没有调用者传部分范围，运行行为不变 |
| 2 ✅ | RHI：`ImageBarrier::m_range`；DX12 填 Enhanced Barriers 的子资源范围（D5）；`QueueBarrier` 按范围更新图像的记录。还没有调用者传部分范围；部分范围的路径用一次临时实验在 debug layer 下跑过（`EnvironmentBaker` 的 cube mip 循环逐 mip 发屏障，再按 `GetSpans` 分两段转到拷贝读），深度模板只转一个 plane 的路径没有跑过 |
| 3 ✅ | 渲染图：追踪器改用 `ImageSubresourceStates`；按范围合并（D4）及单测；`Pre/PostImageBarrier` 改列表（D3）；`m_lastAttachment` 按队列（D7）；首次触碰的判断按子资源；`PendingSync` 的混合队列断言。还没有 pass 声明部分视图；部分范围的路径用一次临时实验在 debug layer 下跑过（ComputePass sample 里一条 3 级的 mip 链，每级一个 Scope 读上一级、写本级，之后整链读一次：编出的屏障逐级正确，最后一次读分成 mip 0..1 与 mip 2 两个）。跨队列的部分范围、深度模板分 plane、`CompileExternalWaits` 真正加上等待的分支都没有跑过 |

**验证**：单元测试；现有画面不变、GPU-based validation 无报错（整图路径一个比特不变）；HZB 做完后在 GPU-based
validation 下跑部分范围的路径。

---

## 关联文档

- `TODO_ScreenSpacePlan.md` —— 步骤 0b；HZB（D3）是第一个用户
- `TODO_RenderGraphItemPlan.md` —— 第 18 条；Scope 模型与逐 Scope 的屏障
- `TODO_IBLPlan.md` —— I4 欠账的出处
- `TODO_EnhancedBarriersPlan.md` —— DX12 后端的屏障 API 切换，步骤 2 的前置
