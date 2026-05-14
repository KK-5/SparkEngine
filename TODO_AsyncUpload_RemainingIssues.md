# AsyncUploadSystem / RHIResourceSystem 审查残余问题

来自一次代码审查的修复进度跟踪。原始问题清单审查于 `AsyncUploadSystem.cpp` / `RHIResourceSystem.cpp` 实现完成时。

---

## 已完成

| # | 问题 | 修复方式 | 文件 |
|---|---|---|---|
| 1 | `m_uploadFence` 单调性 bug — 主线程与 upload 线程共用 Increment + 同 batch 内多次 Signal 倒序 | 引入 `m_packetFence`：外部契约每 batch 只 Signal 一次；packet 轮转用 upload 线程私有 fence | AsyncUploadSystem.h/cpp |
| 2 | CommandList 不是一等 RHI 资源 — cross-thread data race | 抽离到独立方案 [TODO_RHI_CommandList_FirstClass.md](TODO_RHI_CommandList_FirstClass.md) | — |
| 3 | host-pool buffer 挂 `PendingBufferUpload` 被静默丢弃 | 识别 `Components::BufferPerFrame`，报错"请用 Map"并清除标签 | AsyncUploadSystem.cpp |
| 4 | Image upload 缺 `D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT`（512）对齐 | `RHILimits.h` 加 `TexturePlacement`；image upload 进 staging 前 AlignUp | RHILimits.h / AsyncUploadSystem.cpp |
| 5 | entity 没物化时 UploadPendingTag + PendingX 永远卡住 | 与 #3 合并：缺 Components::Buffer/Image 一律 log + 清标签 | AsyncUploadSystem.cpp |
| 6 | RHIResourceSystem 物化失败重试风暴 | 失败时收集到 toDestroy，循环外 DestroyEntity | RHIResourceSystem.cpp |
| 7 | Pool 选择策略把"绑定位"和"内存位"混在一起 | `PendingBufferInit`/`PendingImageInit` 显式控制 `HeapMemoryLevel` + `HostMemoryAccess`；`SelectBufferPool`/`SelectImagePool` 按 placement 路由 | Component.h / RHIResourceSystem.cpp |
| 8 | ViewHierarchy 反向链没人维护 | `LinkViewToResource` 头插维护 prevView/nextView/firstView 链表 | RHIResourceSystem.cpp |
| 9 | AsyncUploadSystem Descriptor 无入口 / m_frameCount 死代码 | 删 `m_frameCount`；加 `AsyncUploadSystem(const Descriptor&)` 构造函数 | AsyncUploadSystem.h |
| 10 | pendingBatches 无背压 | 非问题 — 每个 Batch 只有元数据指针，真正的大数据在调用方 | — |
| 11 | 同帧物化 → 同帧上传的顺序耦合脆弱 | `SubmitBatch` 中检查 `Has<PendingBufferInit/PendingImageInit>` 静默跳过，物化完成后下帧自动重试 | AsyncUploadSystem.cpp |
| 12 | `PendingBufferUpload::m_data` 生命周期注释不准确 | 改为 "直到 BOTH PendingX AND UploadSubmitted 从 entity 消失" | Component.h |
| 13 | packet 轮转 WaitOnCpu 不带值，过度保守 | 改为 per-packet fence — 每个 FramePacket 有自己的 fence，Wait 精确等自己的 value | AsyncUploadSystem.h/cpp |
| 14 | ResourceName 没传给 D3D12 SetName | RHIResourceSystem 在 Init 前 SetName → DX12 BufferPool/ImagePool 在 CreateResource 后 GetName → MultiByteToWideChar → allocation->SetName() | RHIResourceSystem.cpp / DX12 BufferPool.cpp / ImagePool.cpp |
| 15 | CopyBufferDescriptor m_destinationOffset/m_size 截断到 uint32_t | 改为 `uint64_t` | CopyItem.h / AsyncUploadSystem.cpp |

---

## 与其他 TODO 的关系

- 主推进路径在 [TODO_DataDrivenRHI.md](TODO_DataDrivenRHI.md)。
- [TODO_RHI_CommandList_FirstClass.md](TODO_RHI_CommandList_FirstClass.md) 是 #2 的独立方案。
