# 提交路径统一:Draw / Copy / Dispatch 汇入同一条 arena

`TODO_DrawItemShapePlan.md` 落地后,draw 的提交路径已经收敛。copy 自成一套,dispatch 一个使用者都没有。
本文档记录三者汇入同一条提交路径的形态。

> **落地状态(2026-08-22)**:**骨架已实现并验证**(第三~六节)。**copy / compute 的接入暂缓**(第七节)
> ——两者都没有消费者:`CopyFrameBufferPass` 未接线,`SPARK_COMPUTE_PASS` 零使用者。骨架里已经留好入口,
> 真出现第一个使用者时接上即可。

**以已落地代码为准**,不引用任何文档里的将来方案。行号取自撰写时的 HEAD。

---

## 一、改造前的分歧

| | draw | copy |
|---|---|---|
| 上层配方 | `GeometrySpec`(按 handle) | `CopyRequest`(按 slot 名) |
| 解析发生在 | tick 相位,一处集中(`DrawItemRouter`) | **Compile 相位,每个 pass 各跑一遍** |
| 就绪门控 | `GeometryReadyToDerive` | **无**,slot 解析失败即 nullptr 照样提交 |
| DeadTag | collect 处 `Exclude<DeadTag>` | **两处 view 都没有** |
| 进执行器 arena | 是 | **否** |
| 状态建立 | 执行器在 batch 边界 | **pass 的 hook 里手写** |

dispatch 连配方都没有:`ComputePassBuilder` 存在(`Pass/PassBuilder.h:69`),但零使用者,且 `Finalize` 强制
要求调用者自备 Execute function。

---

## 二、前提:copy / dispatch 不能照搬 DrawItem 的生命周期

这是整个方案的出发点。

**DrawItem 能持久化,是因为它背后有一个世界实体。** 生命周期与网格资产一致,解析结果与帧无关(几何 buffer
不轮转),并且**可以被多个 pass 共享**。这三条支撑了 `GeometrySpec` → `DrawItem` 的「一次派生、级联 reap、
多 pass 认领」结构。

**copy / dispatch 三条一条都不占:**

1. 背后没有世界实体。它们是每帧的局部构造;真要持久化,生命周期上限也只能是 **pass**。
2. 解析结果与帧强绑定(swapchain 轮转、transient 资源每帧重建)。
3. **共享没有意义。** 一次 copy 属于且只属于一个 pass。

在这套代码里把东西做成实体只买到四样:级联 reap(需要依赖能独立死亡)、多对多认领、相位间传输、省一个容器。
copy / dispatch **只占第三条**。由此:

- **不需要 `CopyItemRouter`。** router 存在的唯一理由是多对多认领。
- **生产者直接打 PassTag 是对的,不是硬编码耦合。** 需要反向认领的只有 draw。
- **`RHI::CopyItem` 在场不代表任何事**,幂等锚点得另找(第七节)。

item 仍然做实体,理由只有一条:`ExecuteWork::m_itemHandles` 是 `eastl::span<const RHI::RHIHandle>`,
把值存进 pass 私有容器就得往 `ExecuteWork` 里塞 variant 或 (void\*, stride, count)。走句柄一个字段都不用加。

---

## 三、已落地:统一的提交 arena

`m_submitItems` 是 `eastl::vector<RHI::RHIHandle>`,三种类型混排。

### 类型擦除为什么安全

从 collect → 分批 → segment / group / work 切分 → `Execute` 的全部索引运算**没有一处解引用**。类型只在
pass 的 execute hook 里重新静态化,而 hook 由 builder 按 pass 种类装入。

支撑它的不变式是「一个 pass 只有一种提交类型」,**由构造保证,不需要运行时检查**:

1. 每个 builder 自己 `CreatePass()` 再打自己的种类 tag,一个 Pass 实体只出自一个 builder。
2. `PassFunctions::m_executeFunction` 是单个函数。
3. collect 是 `GetView<PassTag, ItemT>` 的 **join 查询**——错打了 tag 的另一类型实体根本不在结果里。

### arena 就是提交序

每个 view 的重放在 arena 里各占一段(`BuildPassSubmitTable` 每 view collect 一次),不做折叠:

```
原 submit item [1 2 3 4 5 A B a]
数字 = DrawItem,大写字母 = DispatchItem,小写字母 = CopyItem

 |-------------pass1-------------------------|----pass2--------|-----pass3-----------|-pass4--|--pass5-|
 |--view1----|--view2------|-----view3-------|
[1   2    5    1    2    5    1     2     5      3    4     5    1    2    3   4   5   A     B      a]
 |------------Group1-------------------------|---------------Group2------------------|-Group3-|-Group4-|
 |-------work1---|------work2--------|-work3-|------work4----------|------work5------|-work6--|-work7--|
```

代价只是**句柄多存几份**(4 字节量级),不是复制 item。买到三件事:

1. **单一索引空间**,`DrawBatch::m_drawBegin` 那套换算消失。
2. **`TODO_DrawItemShapePlan.md` §七.2 的警告自动解除**——per-view 排序在自己那段里做,不再和别的 view 冲突。
3. **per-view 剔除的前置条件**。剔除后每个 view 项集不同,共享一段在物理上表达不了。
   `TODO_MultiViewPlan.md` §八 的剔除位图落地时必须是这个形态。

### 五层区间

只有 **work 的边界允许落在段中间**,所以它必须能在任意切点重建状态。

| 层 | 类型 | 边界能落在哪 | 切分依据 |
|---|---|---|---|
| item | — | — | arena 元素,三种类型混排 |
| batch | `SubmitBatch` | batch 边界 | 状态变化(view / PSO 变体) |
| pass | `PassSubmitTable` | batch 的整数段 | pass 身份 |
| work | `ExecuteWorkItem` | **可落在 batch 中间、pass 中间** | 录制负载 |
| group | `ExecuteGroup` | work 的整数段,且 = pass 的整数段 | 提交预算 |

上两层由逻辑属性决定,下两层是为并行录制切的。

### 改名与删除

| 原 | 现 |
|---|---|
| `m_draws` / `m_drawBatches` | `m_submitItems` / `m_submitBatches` |
| `DrawBatch` | `SubmitBatch`(`m_drawBegin`、`m_variantId` 删除) |
| `PassDrawTable` | `PassSubmitTable` |
| `ExecuteWork::Item` | `ExecuteWorkItem`(顶层类型) |
| `ExecuteWork::m_drawHandles` | `m_itemHandles` |
| `ExecuteWork::m_pass` | **删除**(只写不读) |
| `kSingleVariantId` | **删除**(变体是切分依据,不是携带物) |
| `BuildDrawTables` / `BuildPassDrawTable` / `BuildDrawBatches` | `BuildSubmitTables` / `BuildPassSubmitTable` / `BuildSubmitBatches` |

---

## 四、已落地:`SubmitState`

pass 级(PSO、space0 / 2 / 4)与 view 级(space1、viewport)合并到一个维度,内联在 `SubmitBatch` 里。

### 准入判据:幂等

**在任意切点从零重新应用一遍,结果必须与不切完全相同。** 这直接来自 work 的定义——边界可以落在任何 batch
中间,而每个 work 是全新的 CommandList。

| 操作 | 幂等 | 进 `SubmitState` |
|---|---|---|
| `SetPipelineState` / `BindShaderInputs` / `SetViewport` / `SetScissor` | ✓ | **进** |
| barrier | ✗(是转换不是状态) | 留在 pass 级 |
| `BeginRenderPass` / `EndRenderPass` | ✗(带 loadOp 清屏) | 留在 pass 级 |

**这两样恰好就是阻塞 work 切分的东西,不是巧合**:不能幂等重放的操作才是切分的障碍。所以 `SubmitState`
装不下的,需要 suspend / resume 这类专门机制(第八节)。

空字段表示「不关心」,不表示「清除」——失效模式只可能是多设一次,永不漏设。这是 copy pass 的空 state 能夹在
两个满 state 中间的依据。

### 去重归 `CommandList`,上层不持游标

`ApplySubmitState` 每个 batch 无条件下发。三个理由:

1. **后端已经在做,而且更准。** 绑定逐 space 去重、root signature 按 layout 变化清缓存、VB / IB 按 hash、
   viewport / scissor 脏标记(`Backend/DX12/Command/CommandList.h:127-154`)。
2. **上层做会用错粒度,而且是后端语义上浮。** 「PSO 变 → 全部重绑」是错的——后端按 **pipeline layout** 判,
   共享 layout 时一次都不重绑。而 layout 失效规则是后端事实:DX12 换 root signature 清空全部 root parameter,
   Vulkan 的 descriptor set 在 layout 兼容时保持绑定。编码进 render 层,Vulkan 来时两边都不对。
3. **`CommandList` 的生命周期天然与状态作用域对齐。** `Reset` 全清 `m_state`,所以 work 切分、
   suspend / resume 之后的重建自动正确;上层游标却必须知道 work 边界在哪才能清零。

随之删除:`ExecuteBindPSO`、`ExecuteBindShared`、`ExecuteDrawListState`、`DrawList` 类型、
`boundList` / `boundPSO` 两个游标。

---

## 五、已落地:构建与执行

### `PassCapabilities` 条目不增

**判据:只有需要 pass 的编译期模板参数的操作才做类型擦除。** 构建 `SubmitState`、扇出 view、解析 viewport
全是普通组件读,属于执行器。

还是 5 条:`m_accepts` / `m_markSubmitItem` / `m_resolveSharedBindings` / `m_collectViews` /
`m_collectSubmitItems`。后者签名加了 `view` 参数——今天被实现忽略,但**先定死**,因为 per-view 剔除落地后
再改签名是扩散性改动。

`m_markSubmitItem` 只有 draw 用得上(router 反向认领);copy / dispatch 的生产者自己打 PassTag(第二节)。

### `BuildPassSubmitTable`

`BuildSubmitTables` 遍历**全部队列**。三种 pass 的差异塌成组件存在性判断,没有一处按种类分支:

```
caps = TryGet<PassCapabilities>;  无 → 早退

if (Has<RenderPassTag>)                       // 唯一的种类检查,只做前置守卫
    ASSERT(m_collectViews);
    ResolveTargetViewport 失败 → 早退         // 否则 draw 落在上一个 pass 的 viewport 上

emitBatch(view):
    view 非空且 view 绑定未就绪 → 跳过该 view
    collect → 空则不产出 batch
    SubmitState:有 PSO 才填绑定,是 render pass 才填 viewport
    push batch

m_collectViews 为空 → emitBatch(NullHandle)   // copy / 不渲染 view 的 compute
否则               → 每 view 一次
```

零 batch 的 pass 不写 `PassSubmitTable`,`BuildExecuteWorks` 的判空三元给它一个空 `ExecuteWorkItem`
——barrier 和 `BeginRenderPass` 靠它。

### `Execute`

```
for (item : work.m_items)
    itemIndex == 0 → ExecutePreBarriers + ExecuteBeginRenderPass
    for (b : [m_firstBatch, m_batchEnd))
        ApplySubmitState(cmdList, batch.m_state)      // 无条件,后端去重
        切 span → SetSubmitRange → hook
    m_firstBatch == m_batchEnd → 兜底调一次 hook      // 零 batch 的 pass
    itemIndex == itemCount - 1 → ExecuteEndRenderPass + ExecutePostBarriers
```

状态应用的时机因此从「pass 起始、`BeginRenderPass` 之前」变成「每 batch、`BeginRenderPass` 之后」。
两个后端都允许在 render pass 内设 PSO / root signature / viewport,barrier 仍先于 `BeginRenderPass`。

---

## 六、已落地:DX12 后端

**`SetPipelineState` 补去重**(`CommandList.cpp:111`)。`m_state.m_pipelineState` 此前只写不读——本方案把
PSO 设置提到 per-batch(view × variant),去重才成为必需。同一 PSO 指针意味着 topology、sample positions、
pipeline layout 全相同,早退等价。

**`DispatchItem::m_pipelineState` 删除**,与 `DrawItem` 对齐;`Submit(DispatchItem)` 不再自己设 PSO。
`m_rootConstants` / `m_rootConstantSize` 保留待扩充。

**`CommitViewportState` 的 `[TODO] remove dirty check` 关闭**——渲染层每个 batch 无条件设 viewport,靠它折叠。

---

## 七、暂缓:copy / compute 的接入

**两者都没有消费者**,所以骨架之外不再往下建。真出现使用者时,下面是接入方式。

### 幂等锚点在生产者手里

draw 用 `Exclude<RHI::DrawItem>` 做幂等过滤。copy / dispatch 用不了,因为它每帧刷新。锚点改由**生产者自己
持有句柄**——首次建实体并 `Add<PassTag>`,之后只刷标量,数量变少就打 `DeadTag` 并收缩自己的表。

collect 因此三种类型完全一致:`GetView<PassTag, ItemT>(Exclude<DeadTag>)`。

**pass 内的提交顺序不作保证。** entt 的 dense 数组是插入顺序,只在 erase 的 swap-and-pop 时局部错位。
今天没有用例依赖它:批量上传写不同目标;唯一严格有序的 mip 链每步之间要 barrier,而 barrier 是 pass 级的,
它本来就是 N 个 pass。真出现 pass 内有序需求,做法是 item 上挂排序键、collect 时排一次——与 draw 将来的
排序同形(`TODO_DrawItemShapePlan.md` §六),不是另起一套容器。

### 创建时机是 tick

决定 copy / dispatch **有几项、是哪几项** 的信息全部来自上层(mip 数、待上传数、UI 开关),**只有「这几项
指向哪些资源」来自图**。所以:tick 建实体、写标量;Compile 只写资源指针进已存在的组件。

这条同时保住了 Compile 的并行化前提——entt 里加 / 删组件是结构性变更,就地写不是。今天
`CompilePassCopyRequests` 那句 `AddOrReplace<RHI::CopyItem>`(`Pass/CopyPass.h:72`)就是违规的那一行。

(并行 Compile 还有一处独立障碍:`RenderGraphCompiler.cpp:1027-1030` 在 Compile 相位调
`GetOrCreateImageView`,那是往 RHIContext 里懒建实体。)

### compute 的两个人群

| 人群 | 例子 | 该走哪条路 |
|---|---|---|
| pass 级 dispatch | SSAO、bloom、一次性剔除 | 与 copy 同构 |
| 每对象 dispatch | 蒙皮、逐实例计算 | 与 draw 同构(`GeometrySpec` 那条路) |

第二类一条都没有。按 copy 的形状建 pass 级 dispatch,相位结构共用,将来第二类出现时换存储即可。
**不要现在为它设计。**

### copy pass 的 clear 没有声明式位置

`CopyFrameBufferPass.cpp:36` 声明的 `loadAction = Clear` 是死代码:
`CompileRenderPassBeginInfo`(`RenderGraphCompiler.cpp:1001-1006`)在 pass 没有 `RenderPassTag` 时直接
return。所以那个 pass 的 hook 里手写了 `COPY_DEST → RENDER_TARGET → 清 → COPY_DEST` 的往返。

**放宽那道门是不行的:两个后端都不允许 render pass 内部做 copy。** Vulkan 要求 `vkCmdCopyImage` 系列在
render pass instance 之外调用,DX12 的 render pass 同样限制了可录制的命令集合。

而清屏的往返是**状态要求本身的冲突**(`ClearRenderTargetView` 要 `RENDER_TARGET`,copy 要 `COPY_DEST`),
换个地方写也消不掉。

真出现「copy 到目标的一部分」时,正确做法多半是让**拥有整个目标的那个 pass** 声明 clear——它本来就是
render pass,走的是正规 loadOp 路径。这也正是 `TonemapPass` 能取代 `CopyFrameBufferPass` 的原因:
它画全屏三角形覆盖整个 swapchain,根本不需要清。

**Execute hook 这条逃生路径一直开着**,特殊需求可以走它,不必为此改 render graph 架构。

---

## 八、suspend / resume 的预留

**未实现,但映射关系已定死**,且全部落在已有字段上:

| `ExecuteWorkItem` 的位置 | Begin 侧 | End 侧 |
|---|---|---|
| `m_itemIndex == 0` | `Begin` | — |
| `0 < m_itemIndex` | `Resume` | — |
| `m_itemIndex < m_itemCount - 1` | — | `Suspend` |
| `m_itemIndex == m_itemCount - 1` | — | `End` |

`m_itemIndex` / `m_itemCount` 今天固定填 0 / 1。后端落地点也是现成的:`BeginRenderPass` 里的
`renderPassFlags`(`Backend/DX12/Command/CommandList.cpp:942`)加两个 `|=`
(`D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS` / `_RESUMING_PASS`);Vulkan 动态渲染对应
`VK_RENDERING_SUSPENDING_BIT` / `_RESUMING_BIT`。`RHI::RenderPassBeginInfo` 加一对 bool 即可。

**一条必须记下的约束:两个后端都要求 suspend / resume 对落在同一次提交内**,即**不能跨 Group 边界**
(Group 就是一次 `ExecuteCommandLists`)。(措辞按记忆写,动 RHI 前应对一遍两边的 spec。)

在此之前,切分器按 `RenderPassTag` 判定可不可切:copy / compute pass 现在就能自由拆,render pass 强制
`m_itemCount = 1`。

**状态重建不需要额外工作**:`Reset` 清空 `m_state`,resume 的 CommandList 上第一个 batch 的
`ApplySubmitState` 会全量下发。这是第四节第 3 条的直接推论。

---

## 九、已核对的事实

1. **`Submit` 的三个重载都带 submitIndex**(`RHI/Command/CommandList.h:89-95`),`SetSubmitRange` /
   `ValidateSubmitIndex` 对三种类型都是现成的。
2. **DX12 状态机完整**:绑定逐 space 去重(`CommandList.cpp:189` / `:238`)、root signature 按 layout 变化
   更新并清缓存(`:144-167`)、VB / IB 按 hash、topology / stencilRef、viewport / scissor / shadingRate
   脏标记、`Reset` 全清。
3. **`CopyFrameBufferPass::SetUp` 零调用点**——`TonemapPass` 直接导入 swapchain 取代了它。它的
   `CopyRequest` 生产者已随之删除;重新接线需要同时恢复一个生产者。
4. **`ComputePassBuilder` 已存在**(`Pass/PassBuilder.h:69`),但 `SPARK_COMPUTE_PASS` 零使用者,且
   `Finalize` 强制要求调用者自备 Execute function。
5. **`DispatchItem::m_rootConstants` / `m_rootConstantSize` 零读写**,保留待扩充。
6. **删 `DispatchItem::m_pipelineState` 是零成本的**:五处赋值(`EnvironmentBaker.cpp` 四处、
   `BRDFLutGen.cpp` 一处)每一处前面都已显式调过同一个 PSO 的 `SetPipelineState`,全是冗余。

---

## 十、不要踩的几条

1. **不要给 arena 加判别位,也不要在 collect 加类型校验。** 「一个 pass 一种提交类型」由构造保证(第三节)。
   真需要判别位,说明那条不变式已经被破坏了——该修的是那个 pass。

2. **不要在 render 层做状态去重。** 见第四节:粒度会错(应按 pipeline layout 而非 PSO)、是后端语义上浮、
   且上层游标必须知道 work 边界才能清零。**去重是 `CommandList` 的职责。**

3. **不要让 copy / dispatch 的 item 实体带上生命周期语义。** 它纯粹是「能进共享 arena 的一个句柄」,
   与 DrawItem 实体(背后有世界实体、能被多 pass 共享)**完全不是一回事**。否则下一个人会去给它加级联 reap
   ——那是对一个每帧刷新的东西做生命周期管理。

4. **不要往 `PassCapabilities` 加条目。** 判据见第五节:只有需要模板参数的才擦除。每个条目要保持原子。

5. **不要在 Compile 里做结构性变更。** 新增的 copy / dispatch 代码必须只做就地写。

6. **不要为「每对象 dispatch」提前设计。** 第七节:它一条都还没有。

7. **不要把 copy 的清屏表达成 loadOp。** 第七节:两个后端都不允许 render pass 内做 copy,这不是审美问题。
