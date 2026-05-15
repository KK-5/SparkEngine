# Import 资源异步上传 → Render Graph Barrier 集成方案

## 背景

`AsyncUploadSystem` 在 Copy 队列上完成资源初始化（staging → copy → signal fence），资源实体上挂 `UploadSubmitted{m_fenceValue, m_uploadFence}` 标记"GPU 上传已提交、等待完成"。

Render Graph 在 `CompileImageBarriers` / `CompileBufferBarriers` 中为 import 资源编译 barrier 时，存在两个 gap：

1. **srcQueue 不正确**：首 pass 遇到 import 资源时 `srcQueue = dstQueue`，但资源刚从 Copy 队列的 copy 操作出来，真实的 srcQueue 是 Copy，barrier 缺少跨队列所有权转移
2. **UploadSubmitted 被 PollCompletions 提前清理**：编译阶段需要 `UploadSubmitted` 提供 barrier 来源信息，但 `PollCompletions` 在 `OnFrameBegin` 就会删除它

方案目标：让 Render Graph Compiler 在 barrier 编译时消费 `UploadSubmitted`，生成正确的 CopyDst(Copy) → m_initial(首 pass 队列) 屏障，并统一同步/异步场景。

---

## 设计

### 统一用户合约

**规则：用户必须确保 import 时资源 GPU 侧已就绪。Render Graph 负责 barrier 和队列所有权转移。**

| 场景 | 用法 | 谁负责等待 |
|---|---|---|
| 同步 | `FlushUploadPackets()` 后 import | 用户（CPU 阻塞） |
| 异步 | CPU 检查 `UploadSubmitted` fence 完成后 import | 用户（PollCompletions 或自行 check） |
| 未就绪 | 不 import，跳过 pass 或用 fallback | 用户（Build 阶段决策） |

Render Graph 侧不再感知"资源是否还在上传中"，不需要注入外部 fence wait（`Wait(fence, V)`）。用户保证 import 进来的资源 fence 已完成，compiler 只需发出正确的 barrier 完成 Copy→firstPassQueue 的队列所有权转移。

### 数据流

```
AsyncUploadSystem::ProcessBatch
  → Signal(m_uploadFence, V)
  → entity 挂 UploadSubmitted{m_fenceValue=V, m_srcQueue=Copy, m_srcState=CopyDst}

用户侧（Build 阶段）：
  if (fence 就绪):
      builder.ImportImageAttachment(name, bind)   // 正常 import

RenderGraphCompiler::CompileResourceBarriers（首 pass 遇此资源）：
  if (entity 有 UploadSubmitted):
      srcQueue = UploadSubmitted.m_srcQueue     // Copy
      srcState = UploadSubmitted.m_srcState     // CopyDst
      dstQueue = firstPassQueue
      dstState = ImportedResourceState.m_initial
      → 生成 barrier: CopyDst(Copy) → m_initial(firstPassQueue)
      → 标记 UploadSubmitted 已消费

Compiler::End():
  清理已消费的 UploadSubmitted

PollCompletions:
  兜底清理：fence 已完成但未被消费的 UploadSubmitted（资源本帧未 import）
```

### UploadSubmitted 扩展

`Component.h` — 增加 barrier 来源信息：

```cpp
struct UploadSubmitted
{
    uint64_t                m_fenceValue  = 0;
    Fence*                  m_uploadFence = nullptr;
    // Barrier source: state the Copy queue leaves the resource in.
    RHI::ResourceState      m_srcState    {RHI::AttachmentUsage::CopyDst, RHI::AttachmentAccess::Write};
    RHI::HardwareQueueClass m_srcQueue    {RHI::HardwareQueueClass::Copy};
};
```

`m_srcState` / `m_srcQueue` 描述 upload 完成后资源的物理状态——Copy 队列的 copy 操作总是以 CopyDst 结束，这是确定性的。`m_srcStage`（Copy 操作的 pipeline stage）暂不加入——`ResourceStateTracker::m_lastStage` 默认为 `AttachmentStage::Any`，对首 pass barrier 而言保守但正确。未来可加入以提升 barrier 精度。

### PollCompletions 职责调整

**当前**：fence 完成即删除 `UploadSubmitted`。

**改为**：兜底清理 stragglers——只删除 fence 已完成但 compiler 未消费的 `UploadSubmitted`（资源本帧未被 import，没有 pass 引用它）。正常情况下 compiler 消费后自己删除，PollCompletions 看不到。

```cpp
// PollCompletions: 兜底清理
view.each([&](handle, submitted) {
    if (submitted.m_fenceValue <= completed) {
        toRemove.push_back(handle);  // 本帧未 import，安全清理
    }
});
```

### CompileImageBarriers / CompileBufferBarriers 首 pass 路径

当前（`RenderGraphCompiler.cpp:430`）：

```cpp
else  // first use
{
    srcQueue = dstQueue;
}
```

改为：

```cpp
else  // first use
{
    if (context.Has<UploadSubmitted>(resource))
    {
        auto& us = context.Get<UploadSubmitted>(resource);
        // 用 upload 侧的真实物理状态覆盖
        tracker.m_current = us.m_srcState;
        srcQueue = us.m_srcQueue;
        consumedUploads.push_back(resource);  // 标记待清理
    }
    else
    {
        srcQueue = m_import.m_initialQueue;  // 普通 import: 尊重用户声明的初始队列
    }
}
```

`tracker.m_current` 被覆盖为 CopyDst，`tracker.m_lastStage` 保持默认值 `AttachmentStage::Any`（保守但正确）。后续 barrier 构造逻辑自然生成 CopyDst→m_initial 的 barrier。

**Release 侧必须显式跳过**：`tracker.m_lastPass == NullPass` 时，现有代码的 `if (srcQueue != dstQueue) { tryGet<PassBarriers>(NullPass) → else → Add<PassBarriers>(NullPass, ...) }` 路径会导致 crash。上传的 fence 已完成，源队列工作已结束，acquire 侧的 pre-barrier 足以完成所有权转移，不需要 release。应在 release 分支加上 `tracker.m_lastPass != NullPass` 守卫。

同样的守卫也修复了普通 import 的潜在问题——如果用户设 `m_initialQueue != firstPassQueue`，之前同样会 crash。合并修复：

```cpp
if (srcQueue != dstQueue)
```
改为：
```cpp
if (srcQueue != dstQueue && tracker.m_lastPass != NullPass)
```

### Compiler::End() 清理

```cpp
for (RHIHandle resource : consumedUploads) {
    context.Remove<UploadSubmitted>(resource);
}
```

### SubmitBatch 处理重复 UploadSubmitted

正常路径不会重复——同一实体一次 upload 完成前不会有第二次 upload。但为安全，也为了 UploadSubmitted 跨帧存活的场景（资源导入后可能跨帧使用），`SubmitBatch` 改为 `AddOrReplace`：

```cpp
ctx.AddOrReplace<UploadSubmitted>(handle, UploadSubmitted{...});
```

---

## 变更范围

| 文件 | 变更 |
|---|---|
| `RHI/Component/Component.h` | `UploadSubmitted` 加 `m_srcState` / `m_srcQueue` |
| `RHI/System/AsyncUploadSystem.cpp` | `SubmitBatch`：`UploadSubmitted` 填充 srcState/srcQueue；`PollCompletions`：兜底清理 stragglers；`Add` → `AddOrReplace` |
| `Render/RenderGraph/RenderGraphCompiler.cpp` | `CompileImageBarriers` / `CompileBufferBarriers` 首 pass 消费 `UploadSubmitted`；`Compiler::End()` 清理已消费的 `UploadSubmitted` |

---

## TODO 列表

| # | 任务 | 文件 | 说明 |
|---|---|---|---|
| 1 | `UploadSubmitted` 增加 `m_srcState` / `m_srcQueue` 字段 | `Component.h` | 描述 upload 完成后的物理状态，带默认值{CopyDst, Copy} |
| 2 | `SubmitBatch` 填充 srcState/srcQueue，改用 `AddOrReplace` | `AsyncUploadSystem.cpp` | 写 `UploadSubmitted` 时带上 srcState{CopyDst,Write} srcQueue{Copy} |
| 3 | `PollCompletions` 改为兜底清理模式 | `AsyncUploadSystem.cpp` | 只删 fence 已完成且未被 compiler 消费的 straggler `UploadSubmitted` |
| 4 | `CompileImageBarriers` 首 pass 消费 `UploadSubmitted` | `RenderGraphCompiler.cpp:398` | 检测 `UploadSubmitted`，覆盖 `tracker.m_current` 和 `srcQueue`，收集到 consumed 列表 |
| 5 | `CompileBufferBarriers` 首 pass 消费 `UploadSubmitted` | `RenderGraphCompiler.cpp:316` | 同 #4，对称逻辑 |
| 6 | 修复 release 侧守卫：`srcQueue != dstQueue` → `srcQueue != dstQueue && tracker.m_lastPass != NullPass` | `RenderGraphCompiler.cpp:464,326` | 阻止首 pass（无 producer）时向 NullPass 添加 PassBarriers，同时修复普通 import 跨队列的潜在 crash |
| 7 | `Compiler::End()` 清理已消费的 `UploadSubmitted` | `RenderGraphCompiler.cpp:108` | 遍历 consumed 列表，`Remove<UploadSubmitted>` |
| 8 | 修正普通 import 首 pass 的 `srcQueue`：`dstQueue` → `ImportedResourceState.m_initialQueue` | `RenderGraphCompiler.cpp:432` | 无 `UploadSubmitted` 的首 use 也应尊重用户声明的初始队列 |

---

## 与其他 TODO 的关系

- [TODO_AsyncUpload_RemainingIssues.md](TODO_AsyncUpload_RemainingIssues.md) — 本方案是其延伸，解决 upload→barrier 集成问题
- [TODO_DataDrivenRHI.md](TODO_DataDrivenRHI.md) — 数据驱动 RHI 的主推进路径，本方案是其 imported 资源通道的补全
