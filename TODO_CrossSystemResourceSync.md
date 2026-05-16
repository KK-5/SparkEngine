# 跨系统资源同步重构

## 总览

把"资源同步"从**资源持 fence**的形态，改造成**系统间通过 ECS 组件交换 fence 句柄**的形态。让 AsyncUploadSystem、RenderGraph、未来的 Readback / Streaming 等系统用同一套握手协议交换资源所有权。

核心问题——目前 `ImportedResourceState` 是一份**静态声明契约**（owner 注册时给一次），被错误当成"此刻资源在什么状态"用。一旦资源被多个系统轮流持有（典型：AsyncUploadSystem 先上传、RenderGraph 后使用、可能下次又被 re-upload），静态契约无法表达"此刻它在哪、欠谁一次 wait"。

## 背景：问题如何暴露的

AsyncUploadSystem 的 pre-copy barrier 当前实现：

```cpp
// ProcessBatch 开头
BufferBarrier pre = ConvertToCopyWrite(*upload.m_targetBuffer);
pre.m_srcQueue = HardwareQueueClass::Copy;
pre.m_dstQueue = HardwareQueueClass::Copy;
```

`srcQueue = Copy` 隐含了"资源此刻没有被任何其它 queue 持有"——只对**首次上传到新创建资源**成立。re-upload 场景（资源被 Graphics queue 用过、要再传一次）下：

- 资源被 Graphics 队列持有，pre-copy 假设是 Copy 持有，barrier 错误
- AsyncUploadSystem 没有等 Graphics 上次使用完的 fence wait 机制，跟 Graphics race

更深层的问题是**多写者下声明态 vs 观察态的分裂**：`ImportedResourceState.m_initial` 描述的是 owner 声明的初始落点，不是资源此刻真实在哪。RenderGraph 单写者时两者等价；引入 AsyncUploadSystem 后破。

## 设计：三层分离

| 层 | 形式 | 谁写 | 谁读 |
|---|---|---|---|
| **状态**（资源此刻在哪、是什么） | `Resource::m_resourceState` + `Resource::m_lastQueue` | barrier emit 路径（`SetResourceState`） | `MakeXxxBarrier` 等 helper、RG first-touch tracker seed |
| **进度**（资源此刻欠谁一次 wait） | `PendingSync { Fence*, uint64_t value }` 组件 | 任何 producer 系统提交后无条件 AddOrReplace | 跨 queue consumer 读 + 摘 |
| **声明**（owner 对帧末归位的约定） | `ImportedResourceState { m_final, m_finalStage, m_finalQueue }` | owner 注册时 Add | RG `CompileFinalTransitionBarrier` |

三轴正交：资源不持 fence、系统不替资源管 fence。Resource 答"我在哪"，PendingSync 答"等什么"，ImportedResourceState 答"帧末归位"。

## 协议

### `PendingSync` 语义

```cpp
namespace Spark::RHI
{
    //! Universal sync handshake. Present on a resource entity when a producer
    //! has submitted work touching it but no cross-queue consumer has yet
    //! absorbed the fence. Receiver protocol: if the next user is on a
    //! different queue than the resource's m_lastQueue, read this component,
    //! emit queue.Wait(fence, value), remove the component, then emit the
    //! cross-queue acquire barrier.
    //!
    //! Multiple fences may stamp the same resource across time, each from a
    //! different system (uploadFence, RG graphicsFence, RG computeFence, ...).
    //! AddOrReplace is safe:
    //!   - Cross-queue overwrite: a consumer must have removed the prior
    //!     PendingSync before the new producer touched the resource. So the
    //!     overwritten value was already consumed.
    //!   - Same-queue overwrite: queue serial execution guarantees the new
    //!     fence's signal happens-after the old fence's signal on this queue.
    //!     Waiting on the new value implies the old work has completed.
    struct PendingSync
    {
        Fence*   m_fence      = nullptr;
        uint64_t m_fenceValue = 0;
    };
}
```

### Producer 步骤（提交结束时，**不分跨 queue / 同 queue**）

```cpp
queue.Signal(myFence, myValue);
for (resource in touchedResources)
{
    ctx.AddOrReplace<PendingSync>(resource, {&myFence, myValue});
}
```

**同 queue producer 也必须 stamp**——否则该次提交的进度信息丢失，下一个跨 queue 拾起者只能 wait 到更早的旧 fence value，错过当前 producer 的工作。

### Consumer 步骤（第一次摸该资源前）

```cpp
if (resource.m_lastQueue != myQueue)
{
    if (auto* sync = ctx.TryGet<PendingSync>(resource))
    {
        myQueue.Wait(*sync->m_fence, sync->m_fenceValue);
        ctx.Remove<PendingSync>(resource);
    }
    // emit acquire barrier (state + queue ownership transition)
}
// else: 同 queue,串行执行天然保证 happens-before,不读、不摘、不 wait
```

### 不变量

1. `Has<PendingSync>` ↔ "该资源最近一次写者尚未被跨 queue 拾起者吸收"
2. `Resource::m_lastQueue` 永远反映"此刻该资源住在哪个 queue 上"
3. 两者协同：`m_lastQueue` 决定要不要查 PendingSync；PendingSync 内容决定查到后等谁
4. AddOrReplace 永远正确——靠跨 queue 的"读后摘"和同 queue 的串行执行两条天然性质保证

## 跨队列模型选择

放弃严格 QFOT，走 **concurrent 语义**：

- 源 queue 的 release 全部转去 COMMON（DX12: 已是；Vulkan: 用 `VK_QUEUE_FAMILY_IGNORED`）
- 源 queue **不需要知道**下一个使用者在哪个 queue
- 目的 queue 的 acquire 由资源现状（`m_resourceState` + `m_lastQueue`）+ 自己想要的状态决定，自给自足

代价：移动端 TBDR 失去一些 cache / layout 优化。桌面 GPU 几乎无影响。需要 mobile target 时再考虑严格 QFOT 路径。

## 落地步骤

### Step 1：Resource 类扩展

- `Engine/Code/RunTime/Feature/RHI/Resource/Resource.h`
  - 加 `HardwareQueueClass m_lastQueue` 成员
  - `SetResourceState` 接口签名扩展为 `SetResourceState(ResourceState, HardwareQueueClass)` 或重载
  - `GetLastQueue()` 访问器

- `Engine/Code/RunTime/Feature/RHI/Command/CommandList.cpp`
  - `CommandList::SetResourceState(Resource&, ResourceState)` 内调用 `resource.SetResourceState(state, GetHardwareQueueClass())`

- `Engine/Code/RunTime/Feature/RHI/Backend/DX12/Command/CommandList.cpp`
  - `QueueBarrier` 内每条 `SetResourceState` 路径同步传 queue 信息（buffer / image / cross-queue release / cross-queue acquire 四处）

- `MakeBufferBarrier` / `MakeImageBarrier` (`ResourceState.cpp`)
  - 自动从 `resource.m_lastQueue` 填 `srcQueue`，caller 只需指定 `dstQueue`
  - 解决之前"每个 barrier caller 都要重复填 srcQueue 的不谐之处"

### Step 2：新增 PendingSync 组件

- `Engine/Code/RunTime/Feature/RHI/Component/Component.h`
  - 加 `struct PendingSync { Fence* m_fence; uint64_t m_fenceValue; }`
  - 删 `struct BufferUploadSubmitted`、`struct ImageUploadSubmitted`

### Step 3：ImportedResourceState 瘦身

- `Engine/Code/RunTime/Feature/RHI/Component/Component.h`
  - 删 `m_initial` / `m_initialStage` / `m_initialQueue`
  - 保留 `m_final` / `m_finalStage` / `m_finalQueue`
  - 注释更新："owner 的帧末归位约定，非资源此刻状态"

- `Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraphCompiler.cpp`
  - `GetImportedResourceInitialState` 删除——genesis 状态由资源出生默认值给（Uninitialized），不再走声明
  - first-touch 路径改为读 `Resource::m_resourceState` + `m_lastQueue`

- `Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraph.cpp`
  - `ImportSwapChain` 内 `ImportedResourceState` 初始化只填 `m_final*`

### Step 4：AsyncUploadSystem 改造

- `SubmitBatch`（main thread）
  - 不再读 `ImportedResourceState`（已删 m_initial*）
  - 不再构造预编好的 `m_acquireBarrier`（移到 consumer 自己构造）
  - 对每个上传目标，做 **consumer 步骤**：若 `m_lastQueue != Copy` 且 `Has<PendingSync>`，记录 fence wait 到 batch 待 upload thread 在 pre-barrier 前 emit；读完摘掉
  - 构造 pre-copy barrier 时 srcQueue 从 `target.m_lastQueue` 来（自动 via `MakeBufferBarrier`），不再硬编码 Copy
  - 构造 release barrier：dst = COMMON / Uninitialized，srcQueue=Copy, dstQueue=Copy（**不再预测 dstQueue**——concurrent 语义）
  - **在 SubmitBatch 末尾**做 **producer 步骤**：对每个上传目标 `AddOrReplace<PendingSync>` 携带 `{&m_uploadFence, batch.m_fenceValue}`。promissory 语义——fence 还没真正 signal 出去，consumer wait 时 GPU 自然 stall 等 upload thread 真正 signal

- `ProcessBatch`（upload thread）
  - Pre-barrier 阶段：从 batch 取出 fence wait 列表，emit `m_copyQueue->Wait(...)`
  - 之后流程：pre-copy barriers → memcpy + CopyItem → release barriers → Signal `m_uploadFence` 至 `batch.m_fenceValue`
  - **不再触碰 RHIContext**——所有 ECS 写入留在 main thread 的 SubmitBatch

- `Batch` 结构
  - 加 `eastl::vector<FenceWait> m_preFenceWaits`，承载 upload thread 要 emit 的 `queue.Wait` 列表
  - 删 `m_bufferReleaseBarriers` 中预构造的 `m_dstUsage = imported->m_initial` 字段路径——改为 dst = COMMON

### Step 5：RenderGraph 改造

- `RenderGraphCompiler::CompileBufferBarriers` / `CompileImageBarriers`
  - first-touch 分支：
    - `tracker.m_current = resource.GetResourceState()` （取观察态）
    - 推 srcQueue 从 `resource.m_lastQueue`
    - 若 srcQueue ≠ passQueue：consumer 步骤（读 PendingSync、记录 fence wait、摘组件）；wait 列表挂到一个新组件 `PassUploadAcquires`（per-pass）暂存
  - 其余路径（subsequent touch、cross-queue release/acquire pair）保持现有逻辑

- 新增 segment 级 `m_externalWaits`
  - `RenderGraphExecuter::BuildSegments` 时从该 segment 内每个 pass 的 `PassUploadAcquires` 收集（dedup）

- `RenderGraph::ExecutePipeline`
  - 每个 segment 在内部 cross-queue waits 之后,emit `m_externalWaits` 的 `queue.Wait`
  - **ExecutePipeline 末尾**：walk 本帧所有 touched imported 资源（通过 `ResourceStateTracker` 的 view），对每个 `AddOrReplace<PendingSync>` 携带 `{对应 queue 的 fence, latestSignaledValue}`

- `RenderGraphExecuter::End`
  - Clear `PassUploadAcquires`

### Step 6：清理与验证

- 删除 `RHIComponents.h` 中 `using RHI::ImportedResourceState;` 后未引用的别名
- 跑 build：cmake --preset windows-ninja-debug → cmake --build --preset ninja-debug
- 跑 ctest：确认 RHI / ECS 测试还过

## 落地后回归到主线

完成本 TODO 后：

- T5 acquire 侧（`TODO_DataDrivenRHI.md`）**自动完成**——它的本质就是 RG first-touch consumer 步骤
- T7 TrianglePass 可以无缝跑通——VB upload + RG 消费走的就是这套协议
- 未来 readback / streaming 系统直接复用 PendingSync，不需要再设计 sync 通道

## 待定 / 已知简化

- **多 producer 共享同一 queue**：目前 Copy queue 只有 AsyncUploadSystem 一个 producer。未来若 Readback 也用 Copy queue，需要约定它跟 AsyncUploadSystem 共用同一个 fence 对象（最简）或者扩展 PendingSync 为多 fence 列表（复杂）。**当前 YAGNI，先按一 queue 一 producer 推进**。
- **`Fence*` 生命周期**：PendingSync 持的是裸指针，依赖 owning system 比 imported 资源活得长。AsyncUploadSystem、RenderGraph 都是 engine-life，OK。文档化即可，不加 weak ref 这种重机制。
- **per-frame stamp 的开销**：RG 在 ExecutePipeline 末尾对每个 touched imported 资源 AddOrReplace 一次 PendingSync。entt sparse-set 写入很便宜，量级是每帧几十到几百次，可忽略。

## 与其他 TODO 的关系

- 替代 / 完成 [TODO_DataDrivenRHI.md](TODO_DataDrivenRHI.md) 的 T5 acquire 侧
- 推进路径仍以 [TODO_DataDrivenRHI.md](TODO_DataDrivenRHI.md) 为主：本 TODO 完成后回到 T6（SRG builder）→ T7（TrianglePass）→ T8（smoke test）
- [TODO_AsyncUpload_RemainingIssues.md](TODO_AsyncUpload_RemainingIssues.md) 中"Image upload 只支持 2D 单 subresource"、"frame index 应归属 Device"两个搁置问题与本 TODO 无关，独立推进
