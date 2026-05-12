# AsyncUploadSystem / RHIResourceSystem 审查残余问题

来自一次代码审查的修复进度跟踪。原始问题清单审查于 `AsyncUploadSystem.cpp` / `RHIResourceSystem.cpp` 实现完成时。

---

## 已完成

| # | 问题 | 修复方式 | 文件 |
|---|---|---|---|
| 1 | `m_uploadFence` 单调性 bug — 主线程与 upload 线程共用 Increment + 同 batch 内多次 Signal 倒序，违反 D3D12 队列 Signal 严格递增约束 | 引入 `m_packetFence`：外部契约（`m_uploadFence`）每 batch 只 Signal 一次；packet 轮转改用 `m_packetFence`（upload 线程私有，零竞争） | [AsyncUploadSystem.h](Engine/Code/RunTime/Feature/RHI/Upload/AsyncUploadSystem.h) / [AsyncUploadSystem.cpp](Engine/Code/RunTime/Feature/RHI/Upload/AsyncUploadSystem.cpp) |
| 3 | host-pool buffer 挂 `PendingBufferUpload` 被静默丢弃 | `SubmitBatch` 里识别 `Components::BufferPerFrame`，明确报错"请用 Map 直写"并清除 PendingX/UploadPendingTag | [AsyncUploadSystem.cpp:184-227](Engine/Code/RunTime/Feature/RHI/Upload/AsyncUploadSystem.cpp#L184-L227) |
| 4 | Image upload 缺 `D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT`（512）source offset 对齐 — D3D12 validation 直接报错 | `RHILimits.h` 加 `TexturePlacement = 512`；每个 image upload 进 staging 前 `AlignUp(packet->m_offset, TexturePlacement)`，对齐后溢出则 `SubmitFramePacket()` 轮转 | [RHILimits.h:85-87](Engine/Code/RunTime/Feature/RHI/RHILimits.h#L85-L87) / [AsyncUploadSystem.cpp:331-342](Engine/Code/RunTime/Feature/RHI/Upload/AsyncUploadSystem.cpp#L331-L342) |
| 5 | entity 没物化时 `UploadPendingTag` + `PendingX` 永远卡住，调用方无法释放 `m_data` | 与 #3 合并：缺 `Components::Buffer/Image` 一律 log + 清标签 | 同 #3 |
| 6 | `RHIResourceSystem` 物化失败重试风暴 — `InitBuffer`/`InitImage` 失败每帧重试 + 重复 LOG_ERROR | 失败时收集到 `toDestroy` 列表，循环外 `ctx.DestoryEntity(handle)` 彻底销毁 entity（连同 Descriptor 一起消失，下一帧不再扫到）；per-frame 路径用 `bool failed + break` 避免半填充组件 | [RHIResourceSystem.cpp](Engine/Code/RunTime/Feature/RHI/Resource/RHIResourceSystem.cpp) |

## 已抽离到独立方案

| # | 问题 | 方案文档 |
|---|---|---|
| 2 | CommandList 不是一等 RHI 资源 — 跨线程 `CommandListAllocator` thread-local pool 与 upload 线程模型不匹配，导致表面"内存泄漏"、实际是 cross-thread `Reset` 和 `Collect` 的 data race / D3D12 UB | [TODO_RHI_CommandList_FirstClass.md](TODO_RHI_CommandList_FirstClass.md) — 把 `RHI::CommandList` 提到一等公民、补 `RHI::CommandAllocator`、保留 pool 作便利层 |

---

## 待修复（按优先级）

### [ ] #7 Pool 选择策略把"绑定位"和"内存位"混在一起

**位置**：[RHIResourceSystem.cpp:79-83](Engine/Code/RunTime/Feature/RHI/Resource/RHIResourceSystem.cpp#L79-L83)

```cpp
bool isHost = (desc.m_bindFlags & (BufferBindFlags::CopyRead | BufferBindFlags::Constant))
              != BufferBindFlags::None;
return isHost ? m_hostBufferPool.get() : m_deviceBufferPool.get();
```

**问题**：
- `BufferBindFlags` 表达"GPU 如何访问"，跟"放在哪种 heap"是正交的；硬绑死之后调用方失去选择能力。
- 典型反例：**静态 CBV**（GPU 上传一次即常驻 device 内存），按当前规则被强行扔到 host pool 还每帧分配 N 份 PerFrame。
- `CopyRead` 也未必等于 host——一个用于 ReadBack 的 buffer 在 device heap 上配 `CopyRead` 也合理。

**建议方向**：
- 在 `BufferDescriptor` 加一个 hint 字段，或者新增一个 `BufferResidency { Device, HostPerFrame, HostUploadOnce }` 组件，pool 选择改为"按 hint 路由"。
- Image 路径目前只有单 device pool，暂时没此问题；未来加 attachment-only pool 时同样的设计原则。

**优先级**：中。当前的二元判断在 sandbox / TrianglePass 这种简单用例上恰好"碰对"，但一旦上 streaming / 静态 UBO 就会出错。

### [ ] #8 ViewHierarchy 反向链没人维护

**位置**：[Component.h:46-55](Engine/Code/RunTime/Feature/RHI/Component/Component.h#L46-L55)

```cpp
struct ViewHierarchy
{
    RHIHandle m_resource  {NullHandle};
    RHIHandle m_prevView  {NullHandle};
    RHIHandle m_nextView  {NullHandle};
};
struct ResourceHierarchy
{
    RHIHandle m_firstView {NullHandle};
};
```

**问题**：`prevView` / `nextView` / `firstView` 字段定义了，但 `RHIResourceSystem::CreateBufferViews/CreateImageViews` 创建 view 时只读 `m_resource`，**从不维护**链表。

**未来需要这条链的场景**：
- resize / recreate 资源时找出所有依赖它的 view 一并失效
- entity 销毁时连带销毁所有 view
- 调试 / inspector 列出"这个 buffer 有哪些 view"

**建议方向**：
- 要么现在补上链表维护逻辑（CreateView 时头插，DestroyView 时摘链）
- 要么先把这两个字段删掉、避免给阅读者错误暗示，等真有用例再加

**优先级**：中-低。当前没人需要这条链，但留着会让代码读者产生"已经实现了"的错觉。

### [ ] #9 AsyncUploadSystem Descriptor 无入口

**位置**：[AsyncUploadSystem.h:27-31](Engine/Code/RunTime/Feature/RHI/Upload/AsyncUploadSystem.h#L27-L31)

```cpp
struct Descriptor
{
    size_t   m_stagingSizeInBytes = 16 * 1024 * 1024;
    uint32_t m_frameCount         = Limits::Device::FrameCountMax;
};
```

**问题**：
- `m_descriptor` 成员永远是默认值——`Init` / `InitInternal` 没有重载接收 `Descriptor` 参数。
- `m_frameCount` 字段死代码：实际 packet 数量从 `device->GetDescriptor().m_frameCountMax` 取（[AsyncUploadSystem.cpp:61](Engine/Code/RunTime/Feature/RHI/Upload/AsyncUploadSystem.cpp#L61)）。

**建议方向**：
- 加一个 `Init(const Descriptor&)` 重载，或者通过 service-level 配置传入。
- 删 `m_frameCount` 字段；内部直接读 device descriptor。
- 16MB staging 默认在 streaming 大贴图场景偏小，需要可调。

**优先级**：低。当前默认值能跑，但调优需求来时这是阻塞点。

### [ ] #10 pendingBatches 无背压

**位置**：[AsyncUploadSystem.cpp:227-230](Engine/Code/RunTime/Feature/RHI/Upload/AsyncUploadSystem.cpp#L227-L230)

```cpp
{
    std::lock_guard lk(m_mutex);
    m_pendingBatches.push_back(eastl::move(batch));
}
m_cv.notify_one();
```

**问题**：主线程每帧 `OnFrameBegin` 无条件 push batch。如果 upload 线程处理速度跟不上（大数据 + copy queue 拥塞），队列无限增长，CPU 内存随之膨胀。

**建议方向**：
- 给 `m_pendingBatches` 设一个上限（比如 `FrameCountMax * 2` 个 batch）。
- 超过上限时：
  - 简单做法：主线程在 push 之前 `cv.wait` 等队列降下来——退化成同步上传
  - 复杂做法：主线程**保留** PendingX 标签不清掉，等下帧再试，相当于背压回到调用方
- 加监控：超过软上限时 `LOG_WARN` 提醒。

**优先级**：低。当前用例（每帧少量小 buffer / 偶尔贴图）远低于阈值；streaming 上来后必须处理。

### [ ] #11 同帧物化 → 同帧上传的顺序耦合脆弱

**问题**：当前 `OnFrameBegin` handler 顺序：

```
RHIResourceSystem::OnFrameBegin (物化 Components::Buffer)
  ↓
AsyncUploadSystem::OnFrameBegin (SubmitBatch 要求 Components::Buffer 已经存在)
```

调用方在同一帧 `Add<BufferDescriptor> + Add<PendingBufferUpload>` 依赖这个顺序。一旦未来：
- 把 `RHIResourceSystem` 拆成 worker 异步物化
- 改到 `OnFrameCompileBegin` 阶段
- 加入"materialization in progress"状态

这个隐式契约就会崩。

**建议方向**：

`AsyncUploadSystem::SubmitBatch` 里识别"还没物化但物化路径在跑"的情况，silent skip（不像 #3 那样清标签），让物化先完成。需要新增 `PendingMaterialization {}` 之类的标签——由 `RHIResourceSystem` 在开始物化 entity 时贴上、完成或失败后移除。

也可以更激进：上传系统完全不假设资源已物化，而是订阅"物化完成"事件，事件驱动地把 entity 加进 batch。

**优先级**：低。当前顺序契约清晰、写明在 TODO_DataDrivenRHI.md 里，短期不会被违反。但属于设计层面的"暗约束"，最好将来用显式机制替代。

---

## 小问题（style / 注释 / 边界）

### [ ] #12 `PendingBufferUpload::m_data` 生命周期注释不准确

**位置**：[Component.h:67-71](Engine/Code/RunTime/Feature/RHI/Component/Component.h#L67-L71)

```cpp
// CPU source data for a buffer upload. Caller guarantees m_data is valid
// until UploadSubmitted is removed (or FlushAndWait returns).
```

**问题**：实际上"`Add<PendingBufferUpload>` 之后、`SubmitBatch` 运行之前"那一段时间也必须存活，注释只提 `UploadSubmitted` 不准确。

**修法**：改为"直到 `PendingBufferUpload` **和** `UploadSubmitted` 都从 entity 上消失"。Image 路径同改。

### [ ] #13 packet 轮转 `WaitOnCpu` 不带值，过度保守

**位置**：[AsyncUploadSystem.cpp:291-294](Engine/Code/RunTime/Feature/RHI/Upload/AsyncUploadSystem.cpp#L291-L294)

```cpp
if (packet->m_fenceValue > m_packetFence->GetCompletedValue())
{
    m_packetFence->WaitOnCpu();
}
```

`WaitOnCpu()` 等的是 fence 当前 pending（也就是最新 Increment 的值），而我们需要的只是等 `packet->m_fenceValue`。不影响正确性（更保守的等待），但浪费时间。

**修法**：给 `RHI::Fence` 加 `WaitOnCpuValue(uint64_t)` API（DX12 已有 `DX12Fence::Wait(event, value)` 内部支持），改用之。

### [ ] #14 `ResourceName` 没传给 D3D12 `SetName`

**问题**：`RHIResourceSystem` 物化 buffer/image 时不会调 `ID3D12Object::SetName(...)`，PIX / RenderDoc / D3D12 debug layer 报错时看到的是空名字，debug 体验差。

**修法**：物化成功后，如果 entity 有 `ResourceName` 组件，调用 backend 的 `SetDebugName`。需要在 `RHI::Buffer/Image` 上加一个虚函数，DX12 backend 转 wide string 后调 `SetName`。

### [ ] #15 `CopyBufferDescriptor` 把 `m_destinationOffset` / `m_size` 截断到 `uint32_t`

**位置**：[AsyncUploadSystem.cpp:300-301](Engine/Code/RunTime/Feature/RHI/Upload/AsyncUploadSystem.cpp#L300-L301)

```cpp
copyDesc.m_destinationOffset = static_cast<uint32_t>(upload.m_destinationOffset);
copyDesc.m_size              = static_cast<uint32_t>(upload.m_dataSize);
```

**问题**：>4GB buffer 在静默截断。

**修法**：`CopyBufferDescriptor` 这两个字段类型从 `uint32_t` 改为 `uint64_t`。改动会冒到 RHI Copy 路径上，需要核对 DX12 backend `CopyBufferRegion` 的实参类型（D3D12 API 本身是 UINT64）。

---

## 优先级总览

```
高（功能性 bug，会真崩 / 报错）：
  ✓ #1 fence 单调性             —— 已修
  ✓ #4 image source offset 对齐  —— 已修
  ✓ #3+#5 upload 卡死 / 静默丢弃  —— 已修
  ✓ #6 物化失败重试风暴           —— 已修
  ↗ #2 CommandList 一等公民       —— 已抽离独立方案

中（设计层面，影响扩展性）：
  ☐ #7 Pool 选择策略
  ☐ #8 ViewHierarchy 链表
  ☐ #11 物化 / 上传顺序耦合

低（边界条件 / debug 体验 / 调优入口）：
  ☐ #9 Descriptor 入口
  ☐ #10 pendingBatches 背压
  ☐ #14 ResourceName → SetName
  ☐ #15 64-bit copy offset
  ☐ #12 注释勘误
  ☐ #13 WaitOnCpu 加 value 版本
```

---

## 与其他 TODO 的关系

- 主推进路径在 [TODO_DataDrivenRHI.md](TODO_DataDrivenRHI.md)，本文档只是审查残余。
- [TODO_RHI_CommandList_FirstClass.md](TODO_RHI_CommandList_FirstClass.md) 是 #2 的独立方案，落地后会让 AsyncUploadSystem 本身的代码进一步简化（ProcessBatch 里 `factory->CreateCommandList(...)` 调用没了，FramePacket 自己持 CL）。
- #7 / #9 都会触发 `BufferDescriptor` schema 的演进，最好放到同一个 PR 里做。
