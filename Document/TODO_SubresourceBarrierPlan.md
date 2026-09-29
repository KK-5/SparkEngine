# 子资源屏障（I4）　实现方案

`TODO_RenderGraphItemPlan.md` 第 18 条、`TODO_ScreenSpacePlan.md` 步骤 0b。P4 的 HZB 逐 mip 一个 Scope（读 mip j-1、写
mip j），需要同一张图的不同 mip 处在不同状态；欠账出处见 `TODO_IBLPlan.md`。

决策 D1~D6 均已确认。

---

## 一、现状

| 层 | 现状 | 位置 |
|---|---|---|
| RHI `ImageBarrier` | 没有子资源范围，整张图 | `RHI/Resource/ResourceState.h` |
| RHI `ImageSubresourceRange` | 已有：mip / array 的 [min, max] 与 aspect，默认即整图 | `RHI/Resource/Image/ImageSubResource.h` |
| 跨帧状态记录 | `RHI::Resource` 上**单个** `ResourceState`：`CommandList::QueueBarrier` 执行屏障时写，下一帧首次触碰时 `GetResourceInitialState` 读 | `Resource.h`；DX12 `CommandList.cpp` 的 `QueueBarrier` |
| DX12 转换屏障 | 写死 `D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES` | `CommandListBase::QueueTransitionBarrier` |
| DX12 `Image::m_subresourceState` | O3DE 留下的残留：建图时初始化，屏障路径不读。`GetSubresourceIndexByRange` 可用来算 D3D12 子资源下标 | `Backend/DX12/Resource/Image/Image.{h,cpp}` |
| `ResourceStateTracker` | 一个资源一个 `m_current` 加 `m_lastAttachment`，首次触碰时播种，帧末清空 | `Pass/Component/RHIComponents.h` |
| `Pre/Post*Barrier` | 每个 attachment 实体最多各一个 | 同上 |
| `CompileScopeBarriers` | 排序后同一 Scope 内同一资源的 attachment 相邻，合并成一个访问（`ScopeResourceAccess`）；有写且不是 UAV 读写即断言 | `RenderGraphCompiler.cpp` |
| 追踪器的其他读者 | 帧末给 imported 资源盖 `PendingSync` 用 `m_current.m_queue`；交换链 Present 转换用 `m_current` | `RenderGraph.cpp` |
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

**接口放在图像上，buffer 不变**：buffer 仍用 `Resource` 上的单个状态。图像改为按子资源存储，提供按范围读取的接口；保留
"整图状态"的读取接口给现有调用者（异步上传、`ResourceState.cpp` 的屏障辅助函数、静态资源屏障、各池子的重置），只在整图
一致时有效，不一致即断言。现有调用者都是整图访问，不用改。

**追踪器的另两个读者**：交换链的 Present 转换永远整图，不受影响。帧末 `PendingSync` 盖章按资源所在队列取 fence，一个资源
只挂一个 `PendingSync`；一张 imported 图像在帧末若各子资源停在不同队列上，选不出一个 fence，先断言不支持，等第一个
async compute 用例再设计。

**删掉 DX12 `Image::m_subresourceState`**：它是另一份按子资源的记录（存 D3D12 状态），从来不读，与 RHI 层的记录并存只会
误导。`GetSubresourceIndexByRange` 保留，D5 算 D3D12 子资源下标要用。

不在渲染图里补帧末整图屏障（UE 的 RDG 对 extracted 资源这么做）：要维持"哪些图会活到下一帧"这条约定，且覆盖不了不经过
渲染图的流式上传。

### D2　追踪器的表示：按子资源的数组，RHI 与渲染图共用一个类型　✅ 已定

一个 RHI 层的类型（暂名 `ImageSubresourceStates`）：每个子资源（mip × array 层 × aspect）一个 `ResourceState`，加"整图
一致"的快速路径——没被部分访问过的图像（现在的全部）只存一个状态，开销不变。发屏障时把源状态相同的连续子资源合并成一段
（O3DE 原本的做法，DX12 `Image.cpp` 里留有合并逻辑可参考）。HZB 约 11 个 mip，cube 6 面 × 若干 mip，数组都很小。

- RHI 图像上的跨帧记录（D1）与渲染图的帧内追踪器用同一个类型：追踪器首次触碰一张图时复制图像上的记录作为起点。一套
  逻辑、一套单测（D6）。
- 放在 RHI 层没有问题：内容只有 mip、层、aspect 与 `ResourceState`，不涉及渲染层的概念。

不用区间列表（范围, 状态）：更省内存，但部分转换后的集合不再是矩形，拆分与合并的逻辑复杂得多。

### D3　`Pre/PostImageBarrier` 改为 `fixed_vector`，仍挂在 attachment 上　✅ 已定

一个 attachment 只经一个 view 引用资源，但一个 view 可以覆盖一段子资源；段内源状态不一致，一个屏障就不够——RHI 的
`ImageBarrier` 与 Vulkan 的 `VkImageMemoryBarrier` 都只有一个源状态。源状态必须来自编译期的追踪器，不能交给后端从图像的
运行时记录里查：各队列的录制顺序不等于帧内执行顺序，Vulkan 还要源阶段与源访问。

- **Pre**：个数 = 范围内源状态不同的连续段数。现在都是 1；第一个用户 HZB → SSR 就是 2——SSR 用覆盖整条 mip 链的 SRV
  读，而 HZB 生成完时 mip 0..n-2 停在 shader 读、最后一级停在 UAV 写。上限是范围内的子资源数；只取部分 mip 又跨多个
  layer 时，D3D12 子资源下标不连续，要按 layer 分开合并。
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

1. **累加**：建一个与图像同形的请求表，每个 attachment 把访问与阶段按位或进范围内的每个子资源；同一子资源上出现读写冲突
   （UAV 读写除外）即断言。
2. **对比**：请求表与追踪器逐子资源比较，源、目标状态都相同的连续子资源合成一段，每段一个屏障，挂在组内第一个
   attachment 的 Pre 列表上。
3. **更新**：追踪器里这些子资源改为请求的状态。

2、3 就是 `ImageSubresourceStates` 本来要提供的"应用新状态 → 转换列表"，输入由"一个范围一个状态"换成请求表；D4 只多了
第 1 步的累加。"重叠的合并、不重叠的分开"由此自动成立。

**快速路径**：组内只有一个整图 attachment 且追踪器整图一致（现在的全部情况）时，跳过累加与逐子资源比较，照现在发一个
整图屏障，开销与行为不变。

### D5　DX12 用 Enhanced Barriers 的原生子资源范围　✅ 已定

DX12 后端先切到 Enhanced Barriers（`TODO_EnhancedBarriersPlan.md`），`D3D12_TEXTURE_BARRIER` 的子资源范围（mip、array 层、
plane 各自的起点与数量）直接由 `ImageBarrier::m_range` 填，不再展开成逐子资源的屏障。深度模板的两个 plane 由 aspect 换算
成 plane 范围。

### D6　可测性：抽出纯逻辑单测　✅ 已定

`CompileScopeBarriers` 依赖 `BackingImage` 等真实 RHI 对象，直接测很难。抽出两块纯逻辑单独测：

- `ImageSubresourceStates`（D2）："对一个范围应用新状态 → 需要发出的转换列表"；
- D4 的组内累加：独立函数，输入是组内各 attachment 的（范围、访问、阶段）与图像形状，不留在 `CompileScopeBarriers` 的
  匿名命名空间里。

落地：

- **测试放在 `SparkRenderTest`**：它链接 SparkRender，SparkRHI 的头文件可间接用到，不新建 RHI 测试工程。
- **`ImageSubresourceStates` 用 mip 数、层数、aspect 构造**，不接收 `RHI::Image`：测试不需要设备，RHI 图像在内部用自己的
  描述符构造它。
- **读写冲突由返回值报告**，编译器据此 `ASSERT`：不用 death test 就能测"重叠的读写被检出"。

用例：整图 → 部分 → 整图；相邻 Scope 读写不同 mip；同 Scope 重叠读写报冲突；深度模板的 plane。

---

## 三、步骤（草案）

| 步骤 | 内容 |
|---|---|
| 1 | RHI：`ImageSubresourceStates` 及单测（D2、D6）；图像按子资源记录状态、整图读取接口（D1）；删 DX12 `m_subresourceState` |
| 2 | RHI：`ImageBarrier::m_range`；DX12 填 Enhanced Barriers 的子资源范围（D5）；`QueueBarrier` 按范围更新图像的记录。在 `TODO_EnhancedBarriersPlan.md` 之后 |
| 3 | 渲染图：追踪器改用 `ImageSubresourceStates`；按范围合并（D4）及单测；`Pre/PostImageBarrier` 改列表（D3）；`PendingSync` 的混合队列断言 |

**验证**：单元测试；现有画面不变、GPU-based validation 无报错（整图路径一个比特不变）；HZB 做完后在 GPU-based
validation 下跑部分范围的路径。

---

## 关联文档

- `TODO_ScreenSpacePlan.md` —— 步骤 0b；HZB（D3）是第一个用户
- `TODO_RenderGraphItemPlan.md` —— 第 18 条；Scope 模型与逐 Scope 的屏障
- `TODO_IBLPlan.md` —— I4 欠账的出处
- `TODO_EnhancedBarriersPlan.md` —— DX12 后端的屏障 API 切换，步骤 2 的前置
