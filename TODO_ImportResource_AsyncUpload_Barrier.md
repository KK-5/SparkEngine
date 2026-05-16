# Upload 资源 → Render Graph Barrier 集成方案

## 当前架构（与旧方案的区别）

旧方案依赖的概念现在已变更：

| 旧概念 | 当前状态 |
|---|---|
| `UploadSubmitted` (统一) | 已拆为 `BufferUploadSubmitted` / `ImageUploadSubmitted`，含预构 `m_acquireBarrier` |
| `ImportedResourceState` (声明态) | 已删除。初始状态从 `Resource::GetResourceState()` 读取运行时态 |
| `PollCompletions` | 已删除。CPU 不再主动 poll |
| `CompileFinalTransitionBarrier` | 已删除。帧末归位不在本轮范围 |

核心变更：`ResourceState` 新增 `m_queue` / `m_stage`，`PendingSync` 取代 `PollCompletions`。upload 完成后 `ProcessBatch` 的 release barrier 已经把 `Resource::m_resourceState` 设为 `{Uninitialized, Unknown, Copy, Any}`，并且 entity 上挂了 `PendingSync { m_uploadFence, V }`。

**所以 `BufferUploadSubmitted` / `ImageUploadSubmitted` 是冗余的**——它携带的 `m_acquireBarrier` 可以由 barrier compiler 现场构造（从 `ResourceState` 取 srcQueue/srcStage），它携带的 `m_uploadFence` / `m_fenceValue` 就是 `PendingSync` 的内容。

## 问题

`CompileBufferBarriers` / `CompileImageBarriers` first-touch 路径：

```cpp
init.m_current = GetResourceInitialState(resource, context); // m_queue = Copy (upload 后)
init.m_current.m_queue = dstQueue;  // ← 立即覆盖！丢失了"资源在 Copy 上"
```

这导致 upload 后的资源 first touch 时 `srcQueue == dstQueue`，不生成跨队列 acquire barrier，资源直接从 COMMON 被当作 Graphics 队列上的 Uninitialized 处理。

## 方案

### 删除 `BufferUploadSubmitted` / `ImageUploadSubmitted`

它们的职责被拆分到：
- `PendingSync` — 跨系统 fence 句柄交换（`m_fence`, `m_fenceValue`）
- `ResourceState::m_queue` — barrier 的 srcQueue
- `ResourceState::m_stage` — barrier 的 srcStage

### 修正 CompileBufferBarriers / CompileImageBarriers first-touch

```cpp
if (!context.Has<ResourceStateTracker>(resource))
{
    ResourceStateTracker init;
    init.m_current = GetResourceInitialState(resource, context);
    // 不再覆盖 m_queue。fresh resource 默认 m_queue=Graphics (构造默认值),
    // upload 后 m_queue=Copy (release barrier 写入)。覆盖会丢失真实的 srcQueue。

    // consume PendingSync: 记录 fence wait, 摘掉组件
    if (init.m_current.m_queue != dstQueue)
    {
        if (auto* sync = context.TryGet<PendingSync>(resource))
        {
            RecordExternalFenceWait(pass, *sync);  // 见下文
            context.Remove<PendingSync>(resource);
        }
    }

    context.Add<ResourceStateTracker>(resource, init);
}
```

### 跨队列 release 侧加 NullPass 守卫

```cpp
// 旧: if (srcQueue != dstQueue)
// 新: first touch 时 tracker.m_lastPass == NullPass, 没有 producer 可挂 release.
//     此时 release 已在 Copy 队列的 ProcessBatch 中 emit 过, acquire 侧只需 pre-barrier.
if (srcQueue != dstQueue && tracker.m_lastPass != NullPass)
{
    // push release onto previous pass
}
```

### 外部 fence wait 传递到 executer

新增组件 `PassExternalFenceWaits`（`PassComponents.h`）：

```cpp
struct PassExternalFenceWaits
{
    eastl::vector<FenceWait> m_waits;
};
```

Compiler first-touch 消费 `PendingSync` 时写入：

```cpp
auto& waits = passContext.GetOrAdd<PassExternalFenceWaits>(pass);
waits.m_waits.push_back({ sync.m_fence, sync.m_fenceValue });
```

Executer 在 `Execute` 的 `item.m_itemIndex == 0` 分支中，**在 `ExecutePreBarriers` 之前** emit：

```cpp
if (item.m_itemIndex == 0)
{
    if (auto* extWaits = passContext.TryGet<PassExternalFenceWaits>(item.m_pass))
    {
        for (const auto& w : extWaits->m_waits)
            cmdList->QueueWait(*w.m_fence, w.m_value);
    }
    ExecutePreBarriers(cmdList, item.m_pass, passContext);
}
```

### 清理

- `Component.h` 删除 `BufferUploadSubmitted` 和 `ImageUploadSubmitted`
- `AsyncUploadSystem.cpp` 的 `SubmitBatch` 不再 Add 这两个组件（当前已经是错的——它根本没 Add 它们，只 Add `PendingSync`）
- `RenderGraphExecuter::End()` 清理 `PassExternalFenceWaits`

## 变更清单

| # | 文件 | 变更 |
|---|---|---|
| 1 | `RHI/Component/Component.h` | 删除 `BufferUploadSubmitted`、`ImageUploadSubmitted` |
| 2 | `Render/RenderGraph/RenderGraphCompiler.cpp` | `CompileBufferBarriers` first-touch: 去掉 `m_queue = dstQueue` 覆盖；消费 `PendingSync` → 写 `PassExternalFenceWaits`；release 侧加 `NullPass` 守卫 |
| 3 | `Render/RenderGraph/RenderGraphCompiler.cpp` | `CompileImageBarriers` 同上 |
| 4 | `Render/Pass/Component/PassComponents.h` | 新增 `PassExternalFenceWaits`；复用 `RHI::FenceWait`（挪到 RHI 层或直接 inline `{Fence*, uint64_t}`） |
| 5 | `Render/RenderGraph/RenderGraphExecuter.cpp` | `Execute`: 在 `ExecutePreBarriers` 前 emit external fence waits |
| 6 | `Render/RenderGraph/RenderGraphExecuter.cpp` | `End()`: 清理 `PassExternalFenceWaits` |

## 与其他 TODO 的关系

- 本方案完成后，`TODO_CrossSystemResourceSync.md` 的 RG consumer 步骤落地
- `TODO_AsyncUpload_RemainingIssues.md` — 独立推进
- `TODO_DataDrivenRHI.md` — T5 acquire 侧通过本方案完成
