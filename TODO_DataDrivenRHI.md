# 数据驱动 RHI 资源管理改造方案

## 总览

把引擎的 RHI 资源生命周期（创建 / 上传 / 释放）从命令式改造为**完全数据驱动**——外层 Feature/Asset 只往 entity 上挂"声明组件"，由专门系统自动识别、物化、上传。这套机制跟现有的 SRG `RHIUpdateTag` + `RenderGraphCompiler::CompileShaderResources` 是同一种范式，把它推广到 imported 资源域。

四套数据驱动通道，形态完全同构：

```
[Imported 资源]              [Transient 资源]            [SRG 数据更新]            [资源上传]
ImportedTag + Descriptor     TransientTag + Descriptor    RHIUpdateTag + SRG entity  UploadPendingTag + PendingUpload
        │                            │                            │                          │
        ▼                            ▼                            ▼                          ▼
RHIResourceSystem            RG::CompileTransientResources  RG::CompileShaderResources  AsyncUploadSystem
(OnFrameBegin)               (CompilePhase)                 (CompilePhase)              (OnFrameBegin, after RHIResourceSystem)
```

---

## 已完成事项

### PassBuilder / PSO Compiler / SRG 基础（前两个 session 工作）

1. **PSO 相关组件** — `Pass/Component/PassComponents.h`：`CustomPipelinePassTag`、`SinkPassTag`、`PassPipelineState`、`PassCompiledPSO`、`PassPSODirtyTag`
2. **PassBuilder 链式 API** — `Pass/PassBuilder.h`：`RenderPassBuilder<PassTag>` / `ComputePassBuilder<PassTag>` + `SPARK_RENDER_PASS` / `SPARK_COMPUTE_PASS` 宏 + `.Finalize()` 集中校验
3. **RenderSystem::BuildPipeline 迁移** — UI Pass 改走 `SPARK_RENDER_PASS(...).CustomPipeline()...Finalize()`
4. **RenderGraphCompiler::CompilePipelineStates** — ECS view + `Exclude<CustomPipelinePassTag>`，缓存命中跳过，调 `factory->CreatePipelineState()`
5. **ShaderResourceLayout 内部优化** — `ConstantsLayout` 改为 lazy 创建
6. **ShaderResource Entity 模型** — `ShaderResourceTag` / `ShaderResource` / `BackingShaderResource` / `ShaderResourceLayout` / `RHIUpdateTag` 组件；`PassShaderResources` 持 slot→entity 映射
7. **RHIUpdateTag 消费者 + RG 编译流程接入** — `RenderGraphCompiler::CompileShaderResources` 批量 flush dirty SRG
8. **PipelineLayoutDescriptor 构建（PSO Compiler 内）** — 从 `PassShaderResources` 推导
9. **PassExecuteContext 注入架构** — `RenderSystem` 不再硬引用 `Pipeline`，外部可堆栈注入
10. **RenderGraphExecuter 绑定 PSO + SRG** — `ExecuteWork::Item` 缓存 PSO / SRG 句柄
11. **TrianglePass 骨架** —— SandBox 文件已创建，方法仍是 TODO stub

### 本次 session 完成

12. **RHIContext 下沉到 RHI 层**
    - 文件位置：`Engine/Code/RunTime/Feature/Render/Pass/RHIHandle.h` + `RHIContext.h` → `Engine/Code/RunTime/Feature/RHI/Context/RHIHandle.h` + `RHIContext.h`
    - 命名空间：`Spark::Render::{RHIHandle, NullHandle, RHIContext, RHIExecuteContext}` → `Spark::RHI::...`
    - Render 层兼容：`Pass/Component/RHIComponents.h` 顶部 `using RHI::...` 五个别名，Render 命名空间下原有代码无需改动
    - 影响范围：~13 个文件 include 路径更新，编译干净

13. **RHIInterface 持有 RHIContext + Device**
    - `RHIInterface` 加 `protected: RHIContext m_context;` + `Ptr<Device> m_device;`
    - `RHIInterface::InitInternal()` push m_context；`ShutdownInternal()` reset m_device + pop m_context
    - 继承策略：**子类显式 chain 父类**。Init 时子类**末尾**调 `RHIInterface::InitInternal()`（context push 在 backend ready 之后）；Shutdown 时子类**开头**调 `RHIInterface::ShutdownInternal()`（device/context unwind 在 backend teardown 之前）
    - `DX12::RHISystem::InitInternal/ShutdownInternal` 显式 chain 到 base
    - Device 用户驱动构造：`EnumeratePhysicalDevices()` + `InitDevice(PhysicalDevice&, DeviceDescriptor&)` —— 用户挑物理设备 + 填 descriptor，结果交付给 RHIInterface 持有
    - `RenderSystem::m_rhiContext` 字段已删除，Push/Pop 调用已删除

14. **RHIResourceSystem 实现（T3）**
    - 文件：`Engine/Code/RunTime/Feature/RHI/System/RHIResourceSystem.{h,cpp}`
    - 身份：`ISystem` + `FrameEventBus::Handler`（不需要 Service）
    - **六个 pool**（按"绑定位 + 内存位"两轴正交细分）：
      - `m_devicePlacedBufferPool` — Device heap, VB/IB/UAV/CBV/SRV/Indirect/Predication (suballocated)
      - `m_deviceCommittedBufferPool` — Device heap, RayTracing AS/SBT/Scratch (oversized escape hatch)
      - `m_hostUploadPlacedBufferPool` — Host heap + Write, CopyRead/Constant（per-frame cbuffer 走这里）
      - `m_hostReadbackPlacedBufferPool` — Host heap + Read, CopyWrite
      - `m_deviceImagePool` — Device heap, RT/DS/SRV/UAV
      - `m_hostReadbackImagePool` — Host heap + Read, CopyWrite（截图等）
    - **声明组件不是裸 descriptor**：调用方挂 `PendingBufferInit { descriptor, heapLevel, hostAccess }` / `PendingImageInit { descriptor, heapLevel, hostAccess }`，由 `SelectBufferPool` / `SelectImagePool` 按 placement 路由到对应池
    - 五个处理方法：`CreateBuffers`、`CreateImages`、`ProcessBufferMaps`、`CreateBufferViews`、`CreateImageViews`
    - PerFrame 策略：实体上挂 `PerFrameTag` → 物化为 `Components::BufferPerFrame` / `Components::ImagePerFrame`（FrameCountMax 份）；无 tag → 单份 `Components::Buffer` / `Components::Image`
    - `PendingBufferMap`：Host buffer 的同步写路径（Map → memcpy → Unmap）。PerFrame 时只写当前帧的 slot
    - View 创建：通过 `ViewHierarchy::m_resource` 查底层 resource entity，按单帧/PerFrame 物化为 `Components::BufferView` / `Components::BufferViewPerFrame`，并维护 `ResourceHierarchy::m_firstView` 头插链表
    - 失败处理：`InitBuffer/InitImage/View::Init` 失败 → 实体收集到 `toDestroy`，循环外统一 `DestoryEntity`，避免重试风暴
    - Host+Write image 无对应 pool（`SelectImagePool` 返回 nullptr，实体直接销毁并报错）
    - ResourceName：`SetName` 在 `pool->InitBuffer/InitImage` 之前调用 → DX12 backend `CreateResource` 后读取并 `MultiByteToWideChar` → `allocation->SetName`
    - FrameCountMax 取自 `device.GetDescriptor().m_frameCountMax`（不硬编码 `Limits::Device::FrameCountMax`）
    - 幂等：物化后 entity 已有 `Components::Buffer/Image`，下次循环靠 `Exclude<...>` 跳过
    - 引擎注册：`SparkEngine::SetUp()` 中在 DX12::RHISystem→RenderSystem 之后、AsyncUploadSystem 之前 Init
    - **Backing\* bridge 已由 T4b 完成**：`BackingBuffer/BackingImage/BackingBufferView/BackingImageView` 在 `RenderGraphBuilder::Import*Attachment` 路径懒挂（单帧 set-once，PerFrame `AddOrReplace`），不在 RHIResourceSystem 物化路径里写

15. **Upload 组件 + AsyncUploadSystem 实现（T4）**
    - 组件位置：`Engine/Code/RunTime/Feature/RHI/Component/Component.h`（`Spark::RHI` 命名空间）
    - 状态机：`UploadPendingTag` + `PendingBufferUpload`/`PendingImageUpload` → （SubmitBatch 处理后，移除 upload 组件，挂 `BufferUploadSubmitted`/`ImageUploadSubmitted` 含 cross-queue acquire barrier）→ （RG executer 在 pass 首次使用资源且 fence 就绪时，emit acquire barrier + fence wait on graphics queue，移除该组件）
    - **跨队列 barrier 管道**（完整的 copy→graphics 所有权转移）：
      - **Pre-copy**（upload 线程 ProcessBatch 开头）：`ConvertToCopyWrite` → intra-copy-queue transition（target → Copy/Write@Copy）
      - **Copy**（upload 线程）：memcpy staging → `CopyItem` on copy queue
      - **Release**（upload 线程 ProcessBatch 结尾）：cross-queue release（Copy/Write@Copy → COMMON），DX12 backend 看到 `srcQueue==dstQueue` 对 release 侧是 `target→COMMON`
      - **Acquire**（RG executer，per-pass，未来实现）：cross-queue acquire（COMMON → `m_initial@m_initialQueue`），executer 检查 `m_fenceValue ≤ completed` 后 emit paired `Wait(fence)` + acquire barrier，移除 `BufferUploadSubmitted`/`ImageUploadSubmitted`
    - **`ImportedResourceState` 硬要求**：每个走 staging upload 的资源 entity 必须挂 `ImportedResourceState`（声明 post-upload rest state）。SubmitBatch 从它构造 release/acquire barrier pair。没有则报错 + 跳过
    - **`m_data` 非 owning**：`PendingBufferUpload`/`PendingImageUpload` 中 `m_data` 为 `const void*`，调用方保证从挂组件到 `BufferUploadSubmitted`/`ImageUploadSubmitted` 清除前数据存活
    - `BufferUploadSubmitted`：`{ m_fenceValue, m_uploadFence, m_acquireBarrier }`，acquire barrier 由 SubmitBatch 构造（跟 release barrier 相同结构，镜像到 entity 上供 executer 后续 emit）
    - `ImageUploadSubmitted`：同上，携带 `ImageBarrier m_acquireBarrier`
    - `PerFrame` 组件位置：`Spark::RHI::Components` 子命名空间（`BufferPerFrame`、`ImagePerFrame`、`BufferViewPerFrame`、`ImageViewPerFrame`），使用 `FrameArray<Ptr<T>>`
    - 文件：`Engine/Code/RunTime/Feature/RHI/System/AsyncUploadSystem.{h,cpp}`
    - 身份：`ISystem` + `FrameEventBus::Handler`（不需要 Service）
    - 架构：专用 copy queue + staging pool（Host heap + Write, CopyRead）+ timeline fence + 后台 upload 线程
    - Staging ring：`FrameCountMax` 个 `FramePacket`，每个含 `Ptr<Buffer>` staging + `Ptr<CommandRecorder>` + **per-packet `Ptr<Fence>`**（隔离 packet 轮转 wait，避免跨 packet 过度等待）
    - **两套 fence**：
      - `m_uploadFence`（外部契约）：每个 batch 在结尾 Signal 一次；初始化为 `FenceState::Signaled`（pending=0，避免空 shutdown 时 FlushUploadPackets hang）；**`m_pendingValue` 由 upload 线程独占写入**（通过 `CommandQueue::Signal` 的 `SetPendingValue`），主线程通过私有计数器 `m_batchFenceValue` 分配 batch fence 值
      - `packet.m_fence`（私有）：packet 轮转时 wait/signal，跟 batch fence 完全解耦
    - `OnFrameBegin`（主线程）：直接 `SubmitBatch`（不再有 `PollCompletions` —— submitted entity 上的 `BufferUploadSubmitted`/`ImageUploadSubmitted` 由 RG executer 消费并 emit acquire barrier，CPU 侧不主动 poll）
    - SubmitBatch 防御逻辑：
      - `Components::Buffer/Image` 不存在 + `PendingBufferInit/PendingImageInit` 还在 → 静默跳过（同帧物化未完成，下帧重试）
      - `Components::Buffer/Image` 不存在 + 物化标也不在 → 报错 + 清标签（异常实体）
      - `Components::BufferPerFrame` 或 `Components::ImagePerFrame` → 报错"用 PendingBufferMap 或直接写，不要走 staging"+ 清标签
      - `ImportedResourceState` 缺失 → 报错 + 清标签（无法构造 cross-queue barrier pair）
    - Upload 线程（ProcessBatch）：dequeue → Reset recorder → pre-copy barriers（ConvertToCopyWrite per target）+ FlushBarriers → memcpy + CopyItem → release barriers（从 `batch.m_bufferReleaseBarriers`/`m_imageReleaseBarriers`）+ FlushBarriers → Close + ExecuteCommands + `m_uploadFence.Signal(batch.m_fenceValue)` + per-packet fence Signal
    - `Batch` 结构含 `m_bufferReleaseBarriers` / `m_imageReleaseBarriers`：主线程 SubmitBatch 阶段从 `ImportedResourceState` 构造，upload 线程在 copy 完成后 emit
    - 规范：所有 Init 调用检查 `ResultCode`，if/for 单行加大括号
    - 引擎注册：`SparkEngine::SetUp()` 中在 RHIResourceSystem 之后 Init
    - 残余 TODO 见 [TODO_AsyncUpload_RemainingIssues.md](TODO_AsyncUpload_RemainingIssues.md)

---

## 核心设计：两个新 System

### A. RHIResourceSystem（声明式资源物化）

**职责**：扫 RHIContext 中"已声明但未物化"的 entity，自动创建对应的 RHI 资源对象。

**位置**：`Engine/Code/RunTime/Feature/RHI/Resource/RHIResourceSystem.{h,cpp}`，归 RHI 层

**身份**：`ISystem` + `Service<RHIResourceSystem>::Handler` + `FrameEventBus::Handler`

**外层（Feature / Asset 加载）声明姿势**：

```cpp
// Buffer 声明
auto& ctx = *RHIExecuteContext::Current();
RHIHandle vbEntity = ctx.CreateEntity();
ctx.Add<ImportedTag>(vbEntity);
ctx.Add<ResourceName>(vbEntity, ObjectName{"TriangleVB"});

PendingBufferInit init;
init.m_descriptor      = vbDesc;
init.m_heapMemoryLevel = HeapMemoryLevel::Device;     // 默认 Device
init.m_hostMemoryAccess = HostMemoryAccess::Write;     // 仅 Host 时有意义
ctx.Add<PendingBufferInit>(vbEntity, init);
// 下一帧 OnFrameBegin 自动物化为 Components::Buffer (+ TODO: BackingBuffer)

// Image 声明同理
ctx.Add<PendingImageInit>(imgEntity, { imgDesc, HeapMemoryLevel::Device, HostMemoryAccess::Write });

// View 声明
ctx.Add<ViewHierarchy>(viewEntity, { resourceEntity });
ctx.Add<RHI::BufferViewDescriptor>(viewEntity, viewDesc);
// → 自动物化为 Components::BufferView (+ TODO: BackingBufferView)

// PerFrame 资源加 tag
ctx.Add<PerFrameTag>(cbufferEntity);
```

**自动物化逻辑（OnFrameBegin）**：

```cpp
void RHIResourceSystem::OnFrameBegin()
{
    auto& ctx = *RHIExecuteContext::Current();
    Device* device = Service<RHIInterface>::Get()->GetDevice();

    CreateBuffers(ctx, *device);
    CreateImages(ctx, *device);
    ProcessBufferMaps(ctx);             // 同步 host buffer 写
    CreateBufferViews(ctx, *device);
    CreateImageViews(ctx, *device);

    m_frameIndex = (m_frameIndex + 1) % device->GetDescriptor().m_frameCountMax;
}

void RHIResourceSystem::CreateBuffers(RHIContext& ctx, Device& device)
{
    // <PendingBufferInit> & !<Buffer, BufferPerFrame> 是新声明的
    auto view = ctx.GetView<PendingBufferInit>(Exclude<Components::Buffer, Components::BufferPerFrame>);
    eastl::vector<RHIHandle> toDestroy;
    view.each([&](RHIHandle e, const PendingBufferInit& init)
    {
        BufferPool* pool = SelectBufferPool(init);
        // ... 单帧 / PerFrame 分支，每个槽位 InitBuffer
        // 失败收集进 toDestroy；成功 Add<Components::Buffer/BufferPerFrame> + Remove<PendingBufferInit>
    });
    for (RHIHandle e : toDestroy) ctx.DestoryEntity(e);
}
```

幂等：物化后 entity 已有 `Components::Buffer` / `Components::BufferPerFrame`，循环靠 `Exclude<...>` 自动跳过。

**Pool 选择规则**：见已完成事项 #14，按 `HeapMemoryLevel × HostMemoryAccess × BindFlags` 三轴路由到六个 pool 之一。

**逃生口**：caller 可自己创建 `Ptr<RHI::Buffer>` 后直接 `ctx.Add<Components::Buffer>(e, ...)` —— `Exclude` 不命中，跳过自动物化。给 swap chain 这种特殊场景留路。

---

### B. AsyncUploadSystem（数据驱动上传）

**职责**：扫 `UploadPendingTag` 标记的 entity，把 CPU 端数据通过专用 copy queue 异步上传到 GPU。

**位置**：`Engine/Code/RunTime/Feature/RHI/System/AsyncUploadSystem.{h,cpp}`，归 RHI 层

**身份**：`ISystem` + `FrameEventBus::Handler`（不需要 Service）

#### 数据契约（住在 RHIContext 上的组件）

```cpp
namespace Spark::RHI
{
    //! Discovery tag — entity has staged upload data not yet flushed to GPU.
    struct UploadPendingTag {};

    //! Component on an imported Buffer entity. m_data is NON-OWNING — caller
    //! must keep the source memory alive until BOTH PendingBufferUpload AND
    //! BufferUploadSubmitted are removed from the entity.
    struct PendingBufferUpload
    {
        const void* m_data              = nullptr;
        size_t      m_dataSize          = 0;
        uint64_t    m_destinationOffset = 0;
    };

    //! Component on an imported Image entity. Same lifetime contract as
    //! PendingBufferUpload — m_data is non-owning.
    struct PendingImageUpload
    {
        const void*      m_data                = nullptr;
        size_t           m_dataSize            = 0;
        ImageSubresource m_subresource {};
        Origin           m_destinationOrigin {};
        Size             m_size {};
        Format           m_sourceFormat        = Format::Unknown;
        uint32_t         m_sourceBytesPerRow   = 0;
        uint32_t         m_sourceBytesPerImage = 0;
    };

    //! Per-frame "rest state" of an imported resource. Declared by the owner
    //! when the resource is registered into the RHIContext. Two systems read it:
    //!  - AsyncUploadSystem: derives the cross-queue release barrier
    //!  - RenderGraph: seeds ResourceStateTracker from m_initial each frame
    struct ImportedResourceState
    {
        ResourceState      m_initial;
        AttachmentStage    m_initialStage = AttachmentStage::Any;
        HardwareQueueClass m_initialQueue = HardwareQueueClass::Graphics;
        ResourceState      m_final;
        AttachmentStage    m_finalStage = AttachmentStage::Any;
        HardwareQueueClass m_finalQueue = HardwareQueueClass::Graphics;
    };

    //! Marks a Buffer entity whose upload has been submitted to the copy queue
    //! and is pending the cross-queue acquire barrier on graphics queue.
    //! Added by AsyncUploadSystem::SubmitBatch with the acquire barrier already
    //! constructed (mirror of the release barrier emitted on copy queue).
    //! Consumed by the RenderGraph executer: when a pass first uses the resource
    //! AND the fence is ready, it emits m_acquireBarrier on graphics queue
    //! (paired with a fence wait) and removes this component.
    struct BufferUploadSubmitted
    {
        uint64_t      m_fenceValue   = 0;
        Fence*        m_uploadFence  = nullptr;
        BufferBarrier m_acquireBarrier {};
    };

    //! Image counterpart to BufferUploadSubmitted; same semantics.
    struct ImageUploadSubmitted
    {
        uint64_t      m_fenceValue   = 0;
        Fence*        m_uploadFence  = nullptr;
        ImageBarrier  m_acquireBarrier {};
    };

    //! Sync-write path for Host buffers. Processed by RHIResourceSystem
    //! (NOT AsyncUploadSystem): Map → memcpy → Unmap. Only valid for Host
    //! heap buffers. Device buffers must use PendingBufferUpload.
    struct PendingBufferMap
    {
        const void* m_data       = nullptr;
        size_t      m_byteOffset = 0;
        size_t      m_byteCount  = 0;
    };
}
```

#### Entity 状态机

```
ctx.Add<PendingBufferUpload> + ctx.Add<UploadPendingTag>
        │
        ▼
[UploadPendingTag]                  ← 已声明，尚未提交。资源**绝对未就绪**。
        │  AsyncUploadSystem::OnFrameBegin (SubmitBatch):
        │   检查 ImportedResourceState → 构造 cross-queue barrier pair
        │   → Add<BufferUploadSubmitted/ImageUploadSubmitted>(acquire barrier + fence)
        │   → 移除 PendingX/UploadPendingTag → enqueue batch
        ▼
[BufferUploadSubmitted /           ← 已提交到 copy queue，等 GPU + 等 acquire。
 ImageUploadSubmitted]               资源**可能就绪也可能未就绪**。
        │  RG executer (per-pass, 未来实现):
        │   if (m_fenceValue <= m_uploadFence->GetCompletedValue())
        │     graphicsQueue.Wait(fence) + emit m_acquireBarrier
        │     + Remove<BufferUploadSubmitted/ImageUploadSubmitted>
        ▼
[no upload-related component]      ← 资源 GPU 端就绪，graphics queue 可见，可安全使用。
```

**关键变化 vs 旧设计**：不再有 CPU 侧 `PollCompletions`。Submitted 组件由 RG executer 在 **pass 级别**消费（emit acquire barrier + 移除），保证 acquire 在 graphics queue 的正确 timeline 位置，且不早于依赖此资源的 pass。

#### API

```cpp
class AsyncUploadSystem final
    : public ISystem
    , public FrameEventBus::Handler
{
public:
    struct Descriptor
    {
        size_t m_stagingSizeInBytes = 16 * 1024 * 1024;  // per packet
    };

    AsyncUploadSystem() = default;
    explicit AsyncUploadSystem(const Descriptor&);

    // FrameEventBus
    void OnFrameBegin() override;   // SubmitBatch: snapshot entities + enqueue

    // CPU-blocking sync flush (init-time paths).
    void FlushUploadPackets();

    // 注意：未暴露 GetUploadFence 公共 getter。RG executer 从 entity 上的
    // BufferUploadSubmitted / ImageUploadSubmitted 组件拿 fence 指针和
    // m_fenceValue，执行 per-pass 的 acquire barrier + fence wait。
protected:
    void InitInternal()     override;
    void ShutdownInternal() override;
};
```

#### 内部结构

```cpp
struct FramePacket
{
    Ptr<Buffer>          m_stagingBuffer;       // Host+Write, CopyRead
    uint8_t*             m_mappedPtr = nullptr; // persistent map
    uint32_t             m_offset    = 0;
    uint64_t             m_fenceValue = 0;
    Ptr<Fence>           m_fence;               // per-packet fence (NOT shared)
    Ptr<CommandRecorder> m_commandRecorder;
};

// Source data + resolved target, packed for the upload thread.
struct BufferUpload
{
    const void* m_data              = nullptr;
    size_t      m_dataSize          = 0;
    Buffer*     m_targetBuffer      = nullptr;  // 裸指针：调用方契约保证活到 batch 处理完
    uint64_t    m_destinationOffset = 0;
};

struct ImageUpload { /* 类似，含 subresource/origin/size/format/row pitch */ };

struct Batch
{
    uint64_t                    m_fenceValue;
    eastl::vector<BufferUpload> m_bufferUploads;
    eastl::vector<ImageUpload>  m_imageUploads;

    // Cross-queue release barriers, constructed on main thread from each
    // target's ImportedResourceState. Emitted on copy queue after copies.
    // Indices align with m_bufferUploads / m_imageUploads respectively.
    eastl::vector<BufferBarrier> m_bufferReleaseBarriers;
    eastl::vector<ImageBarrier>  m_imageReleaseBarriers;
};

class AsyncUploadSystem : ...
{
    Descriptor                  m_descriptor;
    Ptr<CommandQueue>           m_copyQueue;
    Ptr<BufferPool>             m_stagingPool;
    eastl::vector<FramePacket>  m_packets;
    uint32_t                    m_currentPacketIndex = 0;

    // 外部契约 fence — 每 batch Signal 一次。m_pendingValue 由 upload 线程
    // 独占写入；主线程通过 m_batchFenceValue 私有计数器分配 batch fence 值,
    // 不触碰 fence 本身。
    Ptr<Fence>                  m_uploadFence;
    uint64_t                    m_batchFenceValue = 0;

    // Main → upload thread pipe — per-batch transaction (NOT per-request)
    std::mutex                  m_mutex;
    std::condition_variable     m_cv;
    eastl::deque<Batch>         m_pendingBatches;
    std::thread                 m_uploadThread;
    eastl::atomic<bool>         m_running {false};
};
```

#### 关键流程

**OnFrameBegin（主线程）**：

```cpp
void OnFrameBegin()
{
    auto& ctx = *RHIExecuteContext::Current();
    // 没有 PollCompletions: BufferUploadSubmitted / ImageUploadSubmitted
    // 由 RG executer 在 pass 级别消费（emit acquire barrier + 移除）
    SubmitBatch(ctx);
}

void SubmitBatch(RHIContext& ctx)
{
    auto bufferView = ctx.GetView<UploadPendingTag, PendingBufferUpload>();
    auto imageView  = ctx.GetView<UploadPendingTag, PendingImageUpload>();
    Batch batch;

    bufferView.each([&](RHIHandle e, const PendingBufferUpload& pending) {
        auto* owning = ctx.TryGet<Components::Buffer>(e);
        if (!owning || !owning->m_buffer) {
            if (ctx.Has<PendingBufferInit>(e)) return;          // 同帧物化未完成
            LOG_ERROR(...);
            ctx.Remove<UploadPendingTag>(e);
            ctx.Remove<PendingBufferUpload>(e);
            return;
        }
        // 跨队列 handoff 必须声明 post-upload rest state
        auto* imported = ctx.TryGet<ImportedResourceState>(e);
        if (!imported) {
            LOG_ERROR("[AsyncUploadSystem] Entity {} carries PendingBufferUpload "
                      "but no ImportedResourceState.", ...);
            ctx.Remove<UploadPendingTag>(e);
            ctx.Remove<PendingBufferUpload>(e);
            return;
        }
        Buffer* target = owning->m_buffer.get();

        batch.m_bufferUploads.push_back({ pending.m_data, pending.m_dataSize,
                                          target, pending.m_destinationOffset });

        // 构造 cross-queue barrier pair: release (copy queue) + acquire (graphics)
        BufferBarrier barrier;
        barrier.m_buffer    = target;
        barrier.m_srcUsage  = AttachmentUsage::Copy;
        barrier.m_srcAccess = AttachmentAccess::Write;
        barrier.m_dstUsage  = imported->m_initial.m_usage;
        barrier.m_dstAccess = imported->m_initial.m_access;
        barrier.m_srcStage  = AttachmentStage::Copy;
        barrier.m_dstStage  = imported->m_initialStage;
        barrier.m_srcQueue  = HardwareQueueClass::Copy;
        barrier.m_dstQueue  = imported->m_initialQueue;

        batch.m_bufferReleaseBarriers.push_back(barrier);   // batch 侧 release
        // 同一份 barrier 存入 entity 组件作为 acquire（镜像）
        ctx.Add<BufferUploadSubmitted>(e, BufferUploadSubmitted{
            batch.m_fenceValue, m_uploadFence.get(), barrier });

        ctx.Remove<UploadPendingTag>(e);
        ctx.Remove<PendingBufferUpload>(e);
    });
    // imageView.each(...) 类似，产出 ImageUploadSubmitted

    if (batch.m_bufferUploads.empty() && batch.m_imageUploads.empty())
        return;

    batch.m_fenceValue = ++m_batchFenceValue;

    { std::lock_guard lk(m_mutex); m_pendingBatches.push_back(eastl::move(batch)); }
    m_cv.notify_one();
}
```

**上传线程**：

```cpp
void UploadThreadMain()
{
    while (m_running.load()) {
        Batch batch;
        { std::unique_lock lk(m_mutex);
          m_cv.wait(lk, [&]{ return !m_running.load() || !m_pendingBatches.empty(); });
          if (!m_running.load()) break;
          batch = eastl::move(m_pendingBatches.front());
          m_pendingBatches.pop_front();
        }
        ProcessBatch(batch);
    }
}

void ProcessBatch(Batch& batch)
{
    auto* packet = &m_packets[m_currentPacketIndex];
    packet->m_commandRecorder->Reset();
    CommandList* cmdList = packet->m_commandRecorder->GetCommandList();

    auto SubmitFramePacket = [&]() {
        cmdList->Close();
        m_copyQueue->ExecuteCommands({ &cmdList, 1 });
        packet->m_fenceValue = packet->m_fence->Increment();
        m_copyQueue->Signal(*packet->m_fence);

        m_currentPacketIndex = (m_currentPacketIndex + 1) % m_packets.size();
        packet = &m_packets[m_currentPacketIndex];
        if (packet->m_fenceValue > packet->m_fence->GetCompletedValue())
            packet->m_fence->WaitOnCpu();
        packet->m_offset = 0;
        packet->m_commandRecorder->Reset();
        cmdList = packet->m_commandRecorder->GetCommandList();
    };

    // 1. Pre-copy barriers: transition every target → Copy/Write on copy queue
    for (const auto& upload : batch.m_bufferUploads) {
        BufferBarrier pre = ConvertToCopyWrite(*upload.m_targetBuffer);
        pre.m_srcQueue = HardwareQueueClass::Copy;
        pre.m_dstQueue = HardwareQueueClass::Copy;
        cmdList->QueueBarrier(pre);
    }
    for (const auto& upload : batch.m_imageUploads) {
        ImageBarrier pre = ConvertToImageCopyWrite(*upload.m_targetImage);
        pre.m_srcQueue = HardwareQueueClass::Copy;
        pre.m_dstQueue = HardwareQueueClass::Copy;
        cmdList->QueueBarrier(pre);
    }
    cmdList->FlushBarriers();

    // 2. Copies — 大缓冲自动分片，单次拷贝 ≤ packet size
    for (const auto& upload : batch.m_bufferUploads) { /* memcpy + CopyItem */ }
    for (const auto& upload : batch.m_imageUploads) { /* AlignUp + memcpy 逐行 + CopyItem */ }

    // 3. Release barriers: cross-queue handoff Copy/Write → COMMON
    for (const auto& barrier : batch.m_bufferReleaseBarriers)
        cmdList->QueueBarrier(barrier);
    for (const auto& barrier : batch.m_imageReleaseBarriers)
        cmdList->QueueBarrier(barrier);
    cmdList->FlushBarriers();

    // 4. Batch end: close + execute + external fence signal + packet fence stamp
    cmdList->Close();
    m_copyQueue->ExecuteCommands({ &cmdList, 1 });
    m_copyQueue->Signal(*m_uploadFence, batch.m_fenceValue);
    packet->m_fenceValue = packet->m_fence->Increment();
    m_copyQueue->Signal(*packet->m_fence);
}
```

#### 跟 RenderGraph 的集成

**跨队列 acquire 由 RG executer 按 pass 驱动**（待实现，T5 后半）：

- executer 在执行 pass 时，扫描 pass 引用的 resource entity，如果其上还有 `BufferUploadSubmitted` / `ImageUploadSubmitted`：
  1. 检查 `m_fenceValue <= m_uploadFence->GetCompletedValue()`（GPU 端 copy 已完成）
  2. 若未完成则 block/wait
  3. Emit `graphicsQueue.Wait(*m_uploadFence, m_fenceValue)`
  4. Emit `m_acquireBarrier` on graphics queue（COMMON → `m_initial`）
  5. `ctx.Remove<BufferUploadSubmitted/ImageUploadSubmitted>()`

**为什么是 pass 级别而非 frame 级别**：
- Acquire barrier 必须在 graphics queue 的正确 timeline 位置（pass 开始前），不能过早（可能被后续命令覆盖）也不能过晚（resource 在被使用前必须是正确状态）
- 多个 pass 可能先后使用同一 resource，只需第一个使用它的 pass emit acquire
- 这跟 barrier compile 的 pass-scope 语义一致

**主线程 OnFrameBegin 不再 poll**：`SubmitBatch` 只负责提交 batch + 挂 `BufferUploadSubmitted`/`ImageUploadSubmitted` 组件。CPU 侧不主动清除这些组件——executer 是唯一的"就绪裁判"。

#### 消费者侧两种用法（一套机制全覆盖）

**A. 阻塞式（默认）**：外层完全不感知 `BufferUploadSubmitted`/`ImageUploadSubmitted`。RG executer 在 pass 使用资源前自动 wait fence + emit acquire barrier。Init-time 同步路径通过 `FlushUploadPackets` 等待。

**B. Fire-and-forget**（streaming）：消费者自己 poll `ctx.Has<BufferUploadSubmitted>(e)`。未就绪就用 fallback（低 mip 贴图 / 跳过 draw）；就绪后 executer 自动完成 acquire。

---

## OnFrameBegin Handler 排序

```
1. RHIResourceSystem::OnFrameBegin     ← 物化新声明的 Buffer/Image/View
2. AsyncUploadSystem::OnFrameBegin     ← SubmitBatch (snapshot entities + enqueue)
3. RenderGraph::OnFrameBegin           ← Import 资源挂 Backing*, 更新 PerFrame slot
4. ... 其它 frame-begin handler ...
```

需要在 `SparkEngine::SetUp()` 中通过 Init 注册顺序保证（`m_dx12Rhi` 已先 Init），或者让 `FrameEventBus` 提供显式 order key。具体机制实现时查证。

---

## 未完成事项（按推荐顺序）

### [x] T1. RenderSystem 适配 RHIInterface 的 Device

**已完成**：
- `RenderSystem::InitRHIData` 改用 `rhi->EnumeratePhysicalDevices()` + `rhi->InitDevice(*selected, desc)`
- `RHIData` struct 已删 `m_device`，只保留 `m_swapChain`
- 所有 device 引用走 `Service<RHI::RHIInterface>::Get()->GetDevice()`

### [ ] T2. SandBox RHI 示例适配

**依赖**：T1

**改动点**：
- `HelloTriangle.cpp` / `DrawShape.cpp`：删 `m_device` 字段，`CreateDevice()` 改为 `m_rhi->InitDevice(...)`
- `m_device` 用法替换为 `m_rhi->GetDevice()`
- SwapChain **不动**（仍在 sample 私有管理）

### [x] T3. RHIResourceSystem 实现

**已完成**，见已办事项 #14。

### [x] T4. Upload 组件 + AsyncUploadSystem 实现

**已完成**，见已办事项 #15。

### [x] T4b. Backing* bridge（已完成）

**依赖**：T3

**方案演进**：最初考虑把 `Backing*` 搬到 RHI 层或新建 BackingBridgeSystem。最终采用**在 RenderGraphBuilder import 路径懒挂 Backing\***——单帧资源用 `Has<>` guard set-once，PerFrame 用 `AddOrReplace` 每帧更新到当前 slot。无需新 system、无需移动组件命名空间。

**实现位置**：[RenderGraphBuilder.h](Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraphBuilder.h) 的 `ImportImageAttachment` / `ImportBufferAttachment` 及 view 绑定路径：

```cpp
// Single-frame: set-once
if (auto* img = rhiContext.TryGet<Image>(resource)) {
    if (!rhiContext.Has<BackingImage>(resource))
        rhiContext.Add<BackingImage>(resource, BackingImage{ img->m_image.get() });
}
// Per-frame: AddOrReplace every OnFrameBegin (slot rotates with m_frameIndex)
else if (auto* imgPF = rhiContext.TryGet<ImagePerFrame>(resource)) {
    rhiContext.AddOrReplace<BackingImage>(resource,
        BackingImage{ imgPF->m_images[m_frameIndex].get() });
}
// Buffer / BufferView / ImageView 同样模式
```

- 单帧 Backing* 在第一次 import 时 set-once，终生有效
- PerFrame Backing* 在每帧 `Import*Attachment` 时（`RefreshPerFrameBackings` 或 import lambda 内）`AddOrReplace` 指向 `m_xxx[m_frameIndex]`
- 无额外 system、无额外 scan —— 跟着已有的 import 路径走

### [~] T5. 跨队列 barrier + wait（半完成）

**依赖**：T4

**已完成（release 侧）**：AsyncUploadSystem 在 ProcessBatch 中 emit pre-copy barriers（ConvertToCopyWrite, intra-copy-queue）+ 拷贝完毕后 emit release barriers（Copy/Write → COMMON, cross-queue release）。Batch 末尾 `m_copyQueue->Signal(*m_uploadFence, batch.m_fenceValue)`。

**待完成（acquire 侧）**：RG executer 在 pass 执行时，检查资源 entity 上的 `BufferUploadSubmitted` / `ImageUploadSubmitted`：
1. 若 `m_fenceValue > m_uploadFence->GetCompletedValue()` → block/wait
2. Emit `graphicsQueue.Wait(*m_uploadFence, m_fenceValue)`
3. Emit `m_acquireBarrier` on graphics queue（COMMON → `m_initial`）
4. `ctx.Remove<BufferUploadSubmitted/ImageUploadSubmitted>()`

**关键**：per-pass 的 acquire 必须在 graphics queue 的正确 timeline 位置（pass 开始前）。这是唯一需要 fence wait 的地方——所有 upload 的 GPU 完成检查、barrier 插入、组件清除都在 executer 的 pass-begin 路径完成。

~~方案 1 getter / 方案 2 ECS 扫描 已废弃。~~ Acquire 不从 system 拿 fence——从 entity 组件拿（`BufferUploadSubmitted::m_uploadFence`），每个 entity 独立 wait + barrier，最后 remove 组件。

### [ ] T6. SRG 创建/销毁的 builder 接口（原 #12）

**依赖**：T3（因为 SRG 也是 entity，理论上能复用 RHIResourceSystem 的物化路径）

**需要的接口**（建议加在 `RenderGraphBuilder` 上作为静态方法，对称于 `ImportImageAttachment`）：

```cpp
//! 注册一个外部已创建的 SRG（concrete 实例），返回 entity handle
static RHIHandle ImportShaderResource(
    ObjectName name,
    Ptr<RHI::ShaderResourceLayout> layout,
    Ptr<RHI::ShaderResource>       srg);

//! 注册一个 layout-only entity（per-draw / per-material slot 用）
static RHIHandle RegisterShaderResourceLayout(
    ObjectName name,
    Ptr<RHI::ShaderResourceLayout> layout);

//! 销毁 SRG entity（owner 在 Shutdown 调用）
static void DestroyShaderResource(RHIHandle entity);
```

实现要点：
- `ImportShaderResource`：CreateEntity → 加 `ImportedTag` + `ShaderResourceTag` + `ResourceName` + `ShaderResourceLayout` + `ShaderResource (owning)` + `BackingShaderResource (raw)`
- `RegisterShaderResourceLayout`：不加 BackingShaderResource（layout-only 信号）
- `DestroyShaderResource`：从 RHIContext 删除 entity

**未来优化**：等 RHIResourceSystem 稳定后，可以把 SRG 也改成"声明 ShaderResourceLayoutDescriptor 自动物化"，跟 buffer/image 完全统一。第一版用上面这套显式接口先把路打通。

### [ ] T7. TrianglePass 端到端实现（原 #14）

**依赖**：T3 + T4 + T4b + T6

按声明式终态写：

```cpp
void TrianglePassFeature::CreateVertexBuffer()
{
    auto& ctx = *RHIExecuteContext::Current();
    RHIHandle vbEntity = ctx.CreateEntity();
    ctx.Add<ImportedTag>(vbEntity);
    ctx.Add<ResourceName>(vbEntity, ObjectName{"TriangleVB"});

    // 声明 post-upload rest state（AsyncUploadSystem + RG 都用它构造 barriers）
    ImportedResourceState importedState;
    importedState.m_initial      = ResourceState::VertexBuffer;
    importedState.m_initialStage = AttachmentStage::VertexInput;
    importedState.m_initialQueue = HardwareQueueClass::Graphics;
    importedState.m_final        = ResourceState::VertexBuffer;
    importedState.m_finalStage   = AttachmentStage::VertexInput;
    importedState.m_finalQueue   = HardwareQueueClass::Graphics;
    ctx.Add<ImportedResourceState>(vbEntity, importedState);

    PendingBufferInit init;
    init.m_descriptor      = vbDesc;
    init.m_heapMemoryLevel = HeapMemoryLevel::Device;
    ctx.Add<PendingBufferInit>(vbEntity, init);

    // m_triangleBytes 是 feature 的成员，保活到 BufferUploadSubmitted 被清除前不释放
    PendingBufferUpload upload;
    upload.m_data              = m_triangleBytes.data();
    upload.m_dataSize          = m_triangleBytes.size();
    upload.m_destinationOffset = 0;
    ctx.Add<PendingBufferUpload>(vbEntity, upload);
    ctx.Add<UploadPendingTag>(vbEntity);

    m_vbEntity = vbEntity;
}

void TrianglePassFeature::CreateViewSRG()
{
    // 构造 ShaderResourceLayout（MVP constant）
    // 创建 ShaderResource，init with layout
    m_viewSRGEntity = RenderGraphBuilder::ImportShaderResource(
        ObjectName{"ViewSRG"}, eastl::move(layout), eastl::move(srg));
    m_srg = RHIExecuteContext::Current()->Get<BackingShaderResource>(m_viewSRGEntity).m_shaderResource;
}

void TrianglePassFeature::CreateTrianglePass()
{
    auto& passContext = *PassExecuteContext::Current();
    SPARK_RENDER_PASS(passContext, "TrianglePass")
        .Queue(RHI::HardwareQueueClass::Graphics)
        .VertexShader(m_vertShader).FragmentShader(m_fragShader)
        .RenderTargetLayout(/*...*/)
        .ShaderResource(0, m_viewSRGEntity)
        .Build([](auto& b){ /* Import swap chain RTV */ })
        .Execute([this](ExecuteWork& work, RenderGraphExecuter&) {
            // viewport, bind vertex buffer, draw 3
        })
        .Finalize();
}

void TrianglePassFeature::UpdateViewSRG()
{
    m_srg->SetConstantRaw(/*offset*/0, &mvp, sizeof(mvp));
    auto& ctx = *RHIExecuteContext::Current();
    ctx.Add<RHI::RHIUpdateTag>(m_viewSRGEntity);
}
```

Shader 资产创建：写两个 `.hlsl`，编入 CMake，加载为 `ShaderAsset`。

### [ ] T8. Smoke test（原 #15）

**依赖**：T7

构建、运行 `bin/Debug/TrianglePass.exe`，确认：
- UI Pass（custom pipeline）正常显示
- TrianglePass（PSO + SRG + Upload）正常显示三角形
- ViewSRG 数据每帧通过 batch Compile 上传到 GPU
- Vertex buffer 通过 AsyncUploadSystem 上传后渲染正常
- 没有 GPU validation error

---

## 设计原则与决策记录（重要！下次接手必读）

### 资源所有权三档

| 档 | 例 | Owner | RHIContext 怎么看到 |
|---|---|---|---|
| **Engine-life** | View/Scene/Lights SRG, ShadowAtlas, MaterialSRG | Feature System (agent) | Imported entity，system 在 Init 创建、Shutdown 销毁 |
| **Frame-life** | GBuffer, HDR 等 transient texture/buffer | RG Compiler | Transient entity，每帧分配/释放 |
| **SwapChain** | 后台缓冲 + 视图 | RenderGraph 自己 | Imported per-frame entity |

**契约**：所有 Imported 资源（包括 SRG），注册方负责生命周期。RG 永远只持 raw 指针（`BackingX`），不延长 owner 生命周期。owner shutdown 前必须 `Unimport`。

### System 是 agent，不是 container

**最关键的心智转换**。错误想法：

```cpp
// ❌ system 持有 SRG 的 Ptr<>
class ViewSystem {
    Ptr<RHI::ShaderResource> m_viewSRG;
};
```

正确：

```cpp
// ✅ SRG 的 Ptr<> 在 entity 上，system 只是创建/更新/销毁的 agent
class ViewSystem {
    RHIHandle m_viewSRGEntity;
    void InitInternal()    { m_viewSRGEntity = builder.CreateShaderResource(...); }
    void OnFrameBegin()    { /* SetConstant + 标 RHIUpdateTag */ }
    void ShutdownInternal(){ builder.DestroyShaderResource(m_viewSRGEntity); }
};
```

**理由**：数据放 entity 上原生支持批处理、N 个 pass 共享、调试 / 热重载 / 序列化统一路径。

**例外**：device、factory、command queue 这种**单例 + opaque + 不批处理**的对象，留在 system 成员里（走 `Service<>` 模式）。判断标准：复数 + 同形 + 需要批量遍历 → 进 ECS；单例 + opaque → 留 system。

### SRG 三档按更新频率分类

| 档 | 例 | Owner agent | 写时机 |
|---|---|---|---|
| **Per-View / Per-Scene** | ViewSRG, SceneSRG, LightsSRG | ViewSystem / SceneSystem / LightSystem | OnFrameBegin |
| **Per-Pass** | TonemapParamsSRG, BloomParamsSRG, UIPassSRG | 该 pass 所属的 Feature System | OnFrameBegin |
| **Per-Material / Per-Draw** | 材质 SRG、Draw SRG | MaterialSystem / TransformSystem | 资产事件 / Transform 变化事件 |

**关键澄清**："Per-Draw" 是误称，准确说是 **Per-Material**（一个材质 SRG 服务上千个 draw 实例）。真正逐 draw 变化的数据（model matrix）属于 Per-Object。

### Per-Material / Per-Object SRG 的 pass-side 表达

pass-builder 期不知道具体绑哪个 entity（要看 draw 列表），但 PSO 编译需要 layout。解法：

- MaterialSystem 在 Init 创建 **layout-only entity**（只有 `ShaderResourceLayout`，没有 `BackingShaderResource`）
- pass 在 builder 期：`.ShaderResource(slot, materialLayoutEntity)`
- PSO compiler 读 `ShaderResourceLayout` 拼 `PipelineLayoutDescriptor`，正常工作
- Executer 在 pass-begin `TryGet<BackingShaderResource>(materialLayoutEntity)` → nullptr → 跳过自动绑
- execute lambda 内部 per-draw 切：`cl.BindShaderResource(slot, ctx.Get<BackingShaderResource>(draw.materialEntity).m_shaderResource)`

builder 只有一个 `.ShaderResource()` 方法（没有 `.ShaderResourceSlot()`）—— executer 的 dispatch 是单一真相，只看 entity 上有没有 `BackingShaderResource`。

### SRG 是绑定容器，不是 render graph 节点

SRG 里可以塞 imageView/bufferView，但 render graph **看不到**这些塞进去的资源 —— 它只看 `ImagePassAttachment` / `BufferPassAttachment` 组件。

**铁律**：
- SRG 里只放**整帧只读、不参与渲染图依赖**的东西（cbuffer、static texture、sampler、shadow atlas SRV view 等）
- 任何被 PassA 写、PassB 读的资源，必须在两个 pass 都声明 attachment（即便它也通过 SRG 的 SRV 槽暴露给 shader）

延迟渲染例：ShadowAtlas 在 LightsSRG 的 SRV 槽里，但 LightingPass 必须显式 `b.ReadImageAttachment("ShadowAtlas", PixelShaderRead)`，否则 RG 不知道要插 barrier，GPU 在 LightingPass 执行时还停留在 DepthWrite state，race。

### SRG 数据写入三阶段

```
[1] Set + 标 dirty                [2] Batch Compile                [3] 读取
SRG.SetImageView(...)             ctx.GetView<RHIUpdateTag,        cl.BindShaderResource(...)
SRG.SetConstant(...)                 BackingShaderResource>()      cl.Draw(...)
ctx.Add<RHIUpdateTag>(e)            .each(s.m_srg->Compile())
                                  ctx.Clear<RHIUpdateTag>()
```

- **OnFrameBegin**：各 agent system 写数据 + 标 `RHIUpdateTag`
- **OnFrameCompileBegin**：RG compiler 统一 batch flush
- **OnFrameCompileEnd 之后**：所有 SRG immutable

**强制规则**：agent 只 Set + 标 dirty，**不要自己调 Compile**。

### SRG 是统一抽象，push constant 是后端 lowering

model matrix 这种"逐 draw 变化、小数据"的东西**仍然走 SRG**（在 Per-Object DrawSRG 的 constants 段），后端选择把它实现成 push constant 还是 cbuffer 是后端的事。上层只调 `cl.BindShaderResource(slot, srg)`。

### Pass entity 只放引用，不放 SRG 数据

`PassShaderResources` 组件里只有 `RHIHandle`（slot → entity）。**不缓存 layout、不缓存 backing 指针**。

| 信息 | 单一来源 | 谁在用 |
|---|---|---|
| `ShaderResourceLayout` | SRG entity 上 | PSO compiler |
| `BackingShaderResource` | SRG entity 上 | Executer / execute lambda |
| slot 顺序 | pass 的 `PassShaderResources.m_slots` | compiler + executer 都按 slot 索引 |

好处：SRG 热重载时 pass 自动跟上，无需失效任何缓存。slot 索引 == shader 的 register space (DX12) / set index (Vulkan)。

### 字符串名字 vs 类型化引用

短期：用字符串名字（`ResourceName`）做 SRG 跨模块引用。

长期目标（不阻塞当前开发）：

| SRG 类型 | 谁知道它存在 | 长期推荐引用方式 |
|---|---|---|
| 引擎内置（View/Scene） | 所有人都允许引用 | `XxxSystem::Get()->GetXxxSRG()` 类型化访问器 |
| Feature 私有（Tonemap/UI） | 只有自己 Feature | 自己的成员变量 |
| 跨模块 layout（Material/Draw） | shader + material + pass | `EngineSrgLayouts::Xxx` 类型化常量 |

字符串只用于：调试日志、editor inspector、序列化。**运行时查找应该走类型化引用**。

### Upload 系统几个关键决策

- **专用 copy queue**：GPU 端真异步（DMA 引擎并行 graphics queue），必须有
- **CPU 端独立线程**：开始就有，避免后续接 streaming 时改动面大；用 SubmitBatch 一次性 handoff 设计降低锁竞争
- **Per-frame batch handoff**：snapshot 在主线程做（ECS 单线程约定），处理在 upload thread。同步原语压到每帧一次
- **non-owning `m_data`**（最终选定）：`PendingBufferUpload::m_data` 是 `const void*`，调用方保活到 `BufferUploadSubmitted`/`ImageUploadSubmitted` 清除前。理由：上传源大多是 asset 系统里已经持有的 buffer，让 AsyncUploadSystem 做一次额外的 owning vector 拷贝白白浪费内存和带宽；调用方维护生命周期更自然。代价是契约转嫁——必须明确文档化
- **大数据自动分片**：单 request > packet size 自动切多个 packet（FramePacket ring）
- **状态机三态**：`UploadPendingTag` → `BufferUploadSubmitted`/`ImageUploadSubmitted` → cleared（由 RG executer 消费）。无 CPU 侧 PollCompletions —— acquire barrier 必须在 graphics queue 的正确 timeline 位置 emit
- **Per-packet fence + 外部契约 fence 两套**：packet 轮转用各自 fence，避免一个 packet 等到不相关 batch 的进度；对外只暴露 `m_uploadFence`，每 batch 末尾 Signal 一次。`m_uploadFence` 的指针存在每个 `BufferUploadSubmitted`/`ImageUploadSubmitted` 组件上供 executer 使用
- **`m_uploadFence` pending 单写**：主线程通过 `m_batchFenceValue` 私有计数器分配 batch fence 值，永不触碰 fence；upload 线程通过 `CommandQueue::Signal` 独占写 pending。绕开 `Fence::Increment` + `Signal::SetPendingValue` 的双写 race
- **Host-visible buffer 不走这条路**：调用方挂 `PendingBufferMap`（同步 Map/memcpy/Unmap），由 RHIResourceSystem 处理

### Cross-queue barrier 设计决策

- **`ImportedResourceState` 是单一真相源**：调用方在创建资源 entity 时必须声明 post-upload rest state（`m_initial`/`m_initialQueue`）。AsyncUploadSystem 用它构造 release barrier（Copy/Write → COMMON），RG 用它 seed `ResourceStateTracker`。两个系统读同一份数据，barrier 状态天然一致，不存在 mismatch
- **Barrier pair 在 SubmitBatch 构造，分发到两处**：同一份 `BufferBarrier`/`ImageBarrier` 被复制到 batch 的 `m_*ReleaseBarriers`（upload 线程在 copy 后 emit）和 entity 的 `BufferUploadSubmitted::m_acquireBarrier`/`ImageUploadSubmitted::m_acquireBarrier`（executer 在 pass 开始前 emit）。src/dst queue 不同（Copy→Graphics），DX12 backend 看到 `srcQueue != dstQueue` 时 release 侧 emit `target→COMMON`，acquire 侧 emit `COMMON→dstUsage`
- **Pre-copy barrier 跟 release 职责分离**：pre-copy 是 intra-queue transition（any→Copy/Write@Copy），release 是 cross-queue handoff（Copy/Write→COMMON）。两者缺一不可——没有 pre-copy，资源可能不在 Copy 状态；没有 release，graphics queue 看不到 copy 结果
- **Acquire 由 executer 在 pass 级别驱动，不是 frame 级别**：acquire barrier 必须插入 graphics queue 的正确位置（依赖该资源的 pass 开始前），不能过早（会被后续 transition 覆盖）也不能过晚（pass 执行时资源还在 COMMON）

### 不变量小结

- **Init**：子类先做 backend init，**末尾** chain 父类（context push 在 backend ready 之后）
- **Shutdown**：子类**开头** chain 父类（resource/context unwind 在 backend teardown 之前）
- **OnFrameBegin handler 顺序**：RHIResourceSystem（物化新资源）→ AsyncUploadSystem（SubmitBatch，snapshot + enqueue）→ 其它
- **每个 SRG 只有一个 owner agent**（设计强制，未来 Set 阶段可并行）
- **Compile 在同一处**（便于将来开 `parallel_for`）
- **Upload 就绪检查不在 CPU 侧**：`BufferUploadSubmitted`/`ImageUploadSubmitted` 由 RG executer 在 pass 级别消费，CPU 侧不主动 poll
- **所有走 staging upload 的资源必须有 `ImportedResourceState`**：缺失则 SubmitBatch 报错跳过

---

## 不在本轮范围（明确划界）

- **类型化 SRG 引用迁移**："长期目标"，等核心机制稳定后再做
- **Per-Object DrawSRG 自动化创建**：哪个 system 在游戏对象 spawn 时创建 DrawSRG entity —— 属于 scene/transform 系统范畴
- **MaterialSystem 实现**：当前没有 material asset 概念，先 hardcode 一个 layout 用就行
- **并行 Compile**：等单线程版本稳定后再上 `parallel_for`
- **SwapChain 搬到 RHIInterface**：SwapChain 是渲染特定的，留在 Render 层
- **多 RHI backend 同时存在**：当前只 DX12，Vulkan 暂不考虑
- **跨进程 / 跨设备 fence**：暂不考虑

---

## 关键文件参考

- 组件定义：
  - [Engine/Code/RunTime/Feature/Render/Pass/Component/RHIComponents.h](Engine/Code/RunTime/Feature/Render/Pass/Component/RHIComponents.h)
  - [Engine/Code/RunTime/Feature/Render/Pass/Component/PassComponents.h](Engine/Code/RunTime/Feature/Render/Pass/Component/PassComponents.h)
- Pass builder：[Engine/Code/RunTime/Feature/Render/Pass/PassBuilder.h](Engine/Code/RunTime/Feature/Render/Pass/PassBuilder.h)
- RG builder：[Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraphBuilder.h](Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraphBuilder.h)
- RG compiler：[Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraphCompiler.cpp](Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraphCompiler.cpp)
- RG executer：[Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraphExecuter.cpp](Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraphExecuter.cpp)
- RHI ShaderResource：[Engine/Code/RunTime/Feature/RHI/Resource/ShaderResource/](Engine/Code/RunTime/Feature/RHI/Resource/ShaderResource/)
- RHI 限制：[Engine/Code/RunTime/Feature/RHI/RHILimits.h](Engine/Code/RunTime/Feature/RHI/RHILimits.h)（`ShaderResourceCountMax = 8`）
- RHIContext：[Engine/Code/RunTime/Feature/RHI/Context/RHIContext.h](Engine/Code/RunTime/Feature/RHI/Context/RHIContext.h)
- RHIInterface：[Engine/Code/RunTime/Feature/RHI/RHIInterface.h](Engine/Code/RunTime/Feature/RHI/RHIInterface.h)
- DX12 backend：[Engine/Code/RunTime/Feature/RHI/Backend/DX12/RHISystem.h](Engine/Code/RunTime/Feature/RHI/Backend/DX12/RHISystem.h)
- RenderSystem：[Engine/Code/RunTime/Feature/Render/RenderSystem.h](Engine/Code/RunTime/Feature/Render/RenderSystem.h)
- TrianglePass 骨架：[SandBox/Program/RenderGraph/TrianglePassFeature.h](SandBox/Program/RenderGraph/TrianglePassFeature.h)
