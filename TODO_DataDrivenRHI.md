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
ctx.Add<RHI::BufferDescriptor>(vbEntity, vbDesc);
// 完。下一帧 OnFrameBegin 自动物化为 Buffer + BackingBuffer。

// Image 声明同理
ctx.Add<RHI::ImageDescriptor>(imgEntity, imgDesc);

// View 声明
ctx.Add<ViewHierarchy>(viewEntity, { resourceEntity });
ctx.Add<RHI::BufferViewDescriptor>(viewEntity, viewDesc);
// → 自动物化为 BufferView + BackingBufferView
```

**自动物化逻辑（OnFrameBegin）**：

```cpp
void RHIResourceSystem::OnFrameBegin()
{
    auto& ctx = *RHIExecuteContext::Current();
    Device* device = Service<RHIInterface>::Get()->GetDevice();

    MaterializeBuffers(ctx, *device);
    MaterializeImages(ctx, *device);
    MaterializeBufferViews(ctx, *device);
    MaterializeImageViews(ctx, *device);
}

void RHIResourceSystem::MaterializeBuffers(RHIContext& ctx, Device& device)
{
    // <ImportedTag, BufferDescriptor> & !<Buffer> 就是新声明的
    auto view = ctx.GetView<ImportedTag, RHI::BufferDescriptor>(Exclude<Buffer>);
    view.each([&](RHIHandle e, const RHI::BufferDescriptor& desc)
    {
        BufferPool& pool = SelectPoolFor(desc);
        Ptr<RHI::Buffer> buf = pool.AllocateBuffer(desc);
        ctx.Add<Buffer>(e, Buffer{ buf });
        ctx.Add<BackingBuffer>(e, BackingBuffer{ buf.get() });
    });
}
```

幂等：物化后 entity 已经有 `Buffer` 组件，下次 view 不再命中。

**Pool 选择策略（第一版）**：
- `m_deviceBufferPool`：device-local，bind-flags-all，shared-queue-all
- `m_deviceImagePool`：普通 device-local 纹理
- 未来按需细分（host-visible / attachment-only / 等）

**逃生口**：caller 可自己创建 `Ptr<RHI::Buffer>` 后直接 `ctx.Add<Buffer>(e, ...)` —— view 不命中，跳过自动物化。给 swap chain 这种特殊场景留路。

---

### B. AsyncUploadSystem（数据驱动上传）

**职责**：扫 `UploadPendingTag` 标记的 entity，把 CPU 端数据通过专用 copy queue 异步上传到 GPU。

**位置**：`Engine/Code/RunTime/Feature/RHI/Upload/AsyncUploadSystem.{h,cpp}`，归 RHI 层

**身份**：`ISystem` + `Service<AsyncUploadSystem>::Handler` + `FrameEventBus::Handler`

#### 数据契约（住在 RHIContext 上的组件）

```cpp
namespace Spark::RHI
{
    //! Discovery tag — entity has staged upload data not yet flushed to GPU.
    struct UploadPendingTag {};

    //! Component on an imported Buffer entity. Owning move-in of source bytes.
    struct PendingBufferUpload
    {
        eastl::vector<uint8_t> m_data;
        uint64_t               m_destinationOffset = 0;
    };

    //! Component on an imported Image entity.
    struct PendingImageUpload
    {
        eastl::vector<uint8_t> m_data;
        ImageSubresource       m_subresource {};
        Origin                 m_destinationOrigin {};
        Size                   m_size {};
        Format                 m_sourceFormat = Format::Unknown;
        uint32_t               m_sourceBytesPerRow   = 0;
        uint32_t               m_sourceBytesPerImage = 0;
    };

    //! "已提交 GPU、未完成" 状态。由 AsyncUploadSystem 在 SubmitBatch 时挂上,
    //! 每帧顶端 poll fence 之后移除。消费者用 ctx.Has<UploadInFlight>(e) 判
    //! 断资源是否就绪。
    struct UploadInFlight
    {
        uint64_t m_pendingValue = 0;
    };
}
```

#### Entity 状态机

```
ctx.Add<PendingBufferUpload> + ctx.Add<UploadPendingTag>
        │
        ▼
[UploadPendingTag]                ← 已声明，尚未提交。资源**绝对未就绪**。
        │  AsyncUploadSystem::OnFrameBegin (submit phase):
        │   snapshot + move data + 提交 copy queue + Add<UploadInFlight> + 移除 PendingUpload/UploadPendingTag
        ▼
[UploadInFlight { fenceValue }]   ← 已提交，等 GPU。资源**可能就绪也可能未就绪**。
        │  AsyncUploadSystem::OnFrameBegin (poll phase, 次帧):
        │   if (uploadFence.GetCompletedValue() >= m_pendingValue) Remove<UploadInFlight>
        ▼
[no upload-related component]     ← 资源 GPU 端就绪，可安全使用。
```

#### API

```cpp
class AsyncUploadSystem
    : public ISystem
    , public Service<AsyncUploadSystem>::Handler
    , public FrameEventBus::Handler
{
public:
    struct Descriptor
    {
        size_t   m_stagingSizeInBytes = 16 * 1024 * 1024;       // per packet
        uint32_t m_frameCount         = Limits::Device::FrameCountMax;
    };

    // FrameEventBus
    void OnFrameBegin() override;     // poll completions, submit new batch

    // Cross-queue GPU wait point — RG 在 graphics submit 前调
    Fence&   GetUploadFence();
    uint64_t GetMaxInFlightValue() const;

    // CPU 阻塞，Init-time 同步路径
    void     FlushAndWait();

protected:
    void InitInternal()     override;     // create copy queue, packets, fence, spawn thread
    void ShutdownInternal() override;     // stop thread, drain, release
};
```

#### 内部结构

```cpp
struct FramePacket
{
    Ptr<Buffer>      m_stagingBuffer;        // UPLOAD heap
    Ptr<CommandList> m_commandList;
    uint8_t*         m_mappedPtr = nullptr;  // persistent map
    uint32_t         m_offset    = 0;
    uint64_t         m_fenceValue = 0;
};

struct Batch
{
    uint64_t                                            m_fenceValue;
    eastl::vector<PendingBufferUpload>                  m_buffers;
    eastl::vector<eastl::pair<Buffer*, /*dstOffset*/uint64_t>>  m_bufferTargets;
    eastl::vector<PendingImageUpload>                   m_images;
    eastl::vector<Image*>                               m_imageTargets;
    eastl::vector<RHIHandle>                            m_entities;     // 用于挂 UploadInFlight
};

class AsyncUploadSystem : ...
{
    Descriptor                  m_descriptor;
    Ptr<CommandQueue>           m_copyQueue;
    Ptr<BufferPool>             m_stagingPool;
    eastl::vector<FramePacket>  m_packets;
    uint32_t                    m_currentPacketIndex = 0;

    Ptr<Fence>                  m_uploadFence;
    eastl::atomic<uint64_t>     m_maxInFlightValue {0};

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

    // Phase 1: poll completions (清掉已完成的 UploadInFlight)
    PollCompletions(ctx);

    // Phase 2: snapshot + submit new batch
    SubmitBatchInternal(ctx);
}

void PollCompletions(RHIContext& ctx)
{
    const uint64_t completed = m_uploadFence->GetCompletedValue();
    auto view = ctx.GetView<UploadInFlight>();
    view.each([&](RHIHandle e, const UploadInFlight& f) {
        if (f.m_pendingValue <= completed)
            ctx.Remove<UploadInFlight>(e);
    });
}

uint64_t SubmitBatchInternal(RHIContext& ctx)
{
    auto bufferView = ctx.GetView<UploadPendingTag, PendingBufferUpload, BackingBuffer>();
    auto imageView  = ctx.GetView<UploadPendingTag, PendingImageUpload,  BackingImage>();
    if (bufferView.size() == 0 && imageView.size() == 0)
        return 0;

    const uint64_t batchValue = m_uploadFence->Increment();
    Batch batch{ batchValue };

    bufferView.each([&](RHIHandle e, PendingBufferUpload& up, BackingBuffer& dst) {
        batch.m_entities.push_back(e);
        batch.m_buffers.push_back(eastl::move(up));
        batch.m_bufferTargets.emplace_back(dst.m_buffer, up.m_destinationOffset);
        ctx.Add<UploadInFlight>(e, { batchValue });
    });
    // ... 类似处理 image
    ctx.Clear<UploadPendingTag>();
    ctx.Remove<PendingBufferUpload>(/*all in batch*/);

    { std::lock_guard lk(m_mutex); m_pendingBatches.push_back(eastl::move(batch)); }
    m_cv.notify_one();
    m_maxInFlightValue.store(batchValue);
    return batchValue;
}
```

**上传线程**：

```cpp
void UploadThreadMain()
{
    while (m_running) {
        Batch batch;
        {
            std::unique_lock lk(m_mutex);
            m_cv.wait(lk, [&]{ return !m_running || !m_pendingBatches.empty(); });
            if (!m_running) break;
            batch = eastl::move(m_pendingBatches.front());
            m_pendingBatches.pop_front();
        }
        ProcessBatch(batch);
    }
}

void ProcessBatch(Batch& batch)
{
    for (size_t i = 0; i < batch.m_buffers.size(); ++i) {
        auto& src = batch.m_buffers[i];
        auto& [dst, dstOff] = batch.m_bufferTargets[i];

        // 分片处理：单 request > packet size 时切多个 packet
        size_t copied = 0;
        while (copied < src.m_data.size()) {
            size_t chunk = min(src.m_data.size() - copied, m_descriptor.m_stagingSizeInBytes);
            FramePacket& pkt = BeginPacket(chunk);
            memcpy(pkt.m_mappedPtr + pkt.m_offset, src.m_data.data() + copied, chunk);
            // record CopyBufferDescriptor: staging → dst[dstOff + copied]
            pkt.m_offset += chunk;
            copied += chunk;
            if (pkt.m_offset >= m_descriptor.m_stagingSizeInBytes)
                EndPacket(pkt, /*intermediate value*/);
        }
    }
    // images 同理（CopyBufferToImageDescriptor）

    // 最后一个 packet 信号到 batch.m_fenceValue
    EndPacket(currentPacket, batch.m_fenceValue);
}

FramePacket& BeginPacket(size_t bytesNeeded)
{
    auto& pkt = m_packets[m_currentPacketIndex];
    if (pkt.m_offset + bytesNeeded > m_descriptor.m_stagingSizeInBytes) {
        EndPacket(pkt, pkt.m_fenceValue);
        m_currentPacketIndex = (m_currentPacketIndex + 1) % m_packets.size();
        auto& next = m_packets[m_currentPacketIndex];
        m_uploadFence->WaitOnCpuValue(next.m_fenceValue);  // 等 GPU 消费完
        next.m_offset = 0;
        next.m_commandList->Reset();
        return next;
    }
    return pkt;
}

void EndPacket(FramePacket& pkt, uint64_t fenceValue)
{
    pkt.m_commandList->Close();
    m_copyQueue->ExecuteCommands({ pkt.m_commandList });
    m_copyQueue->Signal(*m_uploadFence, fenceValue);
    pkt.m_fenceValue = fenceValue;
}
```

#### 跟 RenderGraph 的集成

RG 在 `ExecutePipeline` 顶端加 4 行：

```cpp
if (auto* upload = Service<AsyncUploadSystem>::Get()) {
    uint64_t v = upload->GetMaxInFlightValue();
    if (v > upload->GetUploadFence().GetCompletedValue())
        m_commandQueueContext.GetCommandQueue(HardwareQueueClass::Graphics)
            .Wait(upload->GetUploadFence(), v);
}
```

GPU 端 graphics queue 等到 copy queue 把所有已提交的 upload 都信号完，再开始 draw。

#### 消费者侧两种用法（一套机制全覆盖）

**A. 阻塞式（默认）**：外层完全不感知 UploadInFlight。RG 顶端的跨队列 wait 保证 graphics 提交前所有 in-flight upload 已完成。Init-time 也可用 `FlushAndWait()` 同步等。

**B. Fire-and-forget**（streaming）：消费者自己 poll `ctx.Has<UploadInFlight>(e)`。未就绪就用 fallback（低 mip 贴图 / 跳过 draw / ...）。就绪后由 streaming 系统切换 SRG view 指针。

---

## OnFrameBegin Handler 排序

```
1. RHIResourceSystem::OnFrameBegin     ← 物化新声明的 Buffer/Image/View
2. AsyncUploadSystem::OnFrameBegin     ← Poll completions + submit new batch
3. ... 其它 frame-begin handler ...
```

需要在 `SparkEngine::SetUp()` 中通过 Init 注册顺序保证（`m_dx12Rhi` 已先 Init），或者让 `FrameEventBus` 提供显式 order key。具体机制实现时查证。

---

## 未完成事项（按推荐顺序）

### [ ] T1. RenderSystem 适配 RHIInterface 的 Device

**依赖**：无（RHIInterface 已就绪）

**改动点**：
- `RenderSystem::InitRHIData`：物理设备筛选 + DeviceDescriptor 仍保留在这里，但 `factory->CreateDevice() + Init()` 改成 `rhi->InitDevice(*selected, desc)`
- `RenderSystem` 删 `m_rhiData.m_device`，所有引用替换为 `Service<RHIInterface>::Get()->GetDevice()`
- 全 Debug build 验证 + 跑 SparkEditor 烟雾测试

### [ ] T2. SandBox RHI 示例适配

**依赖**：T1

**改动点**：
- `HelloTriangle.cpp` / `DrawShape.cpp`：删 `m_device` 字段，`CreateDevice()` 改为 `m_rhi->InitDevice(...)`
- `m_device` 用法替换为 `m_rhi->GetDevice()`
- SwapChain **不动**（仍在 sample 私有管理）

### [ ] T3. RHIResourceSystem 实现

**依赖**：T1

**子任务**：
1. 新建 `Engine/Code/RunTime/Feature/RHI/Resource/RHIResourceSystem.{h,cpp}`
2. CMakeLists.txt 注册新 cpp
3. Init 创建 canonical pools（`m_deviceBufferPool` / `m_deviceImagePool`）
4. OnFrameBegin 实现四类物化：Buffer / Image / BufferView / ImageView
5. `SparkEngine::SetUp` 中创建 + Init，必须在 `m_dx12Rhi->Init()` 之后、`m_renderSystem->Init()` 之前
6. 测试：写个临时 demo entity，确认能自动物化

### [ ] T4. Upload 组件 + AsyncUploadSystem 实现

**依赖**：T3（物化先就绪）

**子任务**：
1. 在 `Engine/Code/RunTime/Feature/RHI/Context/` 或 `Feature/Render/Pass/Component/RHIComponents.h` 加四个组件：
   - `UploadPendingTag` / `PendingBufferUpload` / `PendingImageUpload` / `UploadInFlight`
   - **位置选择**：upload 组件归属感更接近 RHI 层，建议放在 `Feature/RHI/Upload/UploadComponents.h`
2. 新建 `Engine/Code/RunTime/Feature/RHI/Upload/AsyncUploadSystem.{h,cpp}`
3. CMakeLists.txt 注册
4. `InitInternal`：建 copy queue（从 `CommandQueueContext` 拿，或自己直接 `factory->CreateCommandQueue(HardwareQueueClass::Copy)`，需查现有 API）、建 staging pool（UPLOAD heap）、分配 N 个 FramePacket、map staging buffer 持久化、起 upload thread
5. `OnFrameBegin`：poll + submit batch
6. Upload thread：drain queue → process batch → packet ring + 分片
7. `ShutdownInternal`：`m_running=false; m_cv.notify_all(); m_uploadThread.join(); m_copyQueue.WaitForIdle(); 释放资源`
8. `FlushAndWait()` 实现：`m_uploadFence->WaitOnCpu()` 到 `m_maxInFlightValue`，然后立刻调一次 poll phase
9. `SparkEngine::SetUp` 注册，handler order 必须在 RHIResourceSystem 之后

### [ ] T5. RenderGraph 接入 cross-queue wait

**依赖**：T4

`RenderGraph::ExecutePipeline` 顶端加 4 行：

```cpp
if (auto* upload = Service<AsyncUploadSystem>::Get()) {
    uint64_t v = upload->GetMaxInFlightValue();
    if (v > upload->GetUploadFence().GetCompletedValue())
        m_commandQueueContext.GetCommandQueue(HardwareQueueClass::Graphics)
            .Wait(upload->GetUploadFence(), v);
}
```

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

**依赖**：T3 + T4 + T6

按 v4 声明式终态写：

```cpp
void TrianglePassFeature::CreateVertexBuffer()
{
    auto& ctx = *RHIExecuteContext::Current();
    RHIHandle vbEntity = ctx.CreateEntity();
    ctx.Add<ImportedTag>(vbEntity);
    ctx.Add<ResourceName>(vbEntity, ObjectName{"TriangleVB"});
    ctx.Add<RHI::BufferDescriptor>(vbEntity, vbDesc);
    ctx.Add<PendingBufferUpload>(vbEntity, { eastl::move(triangleBytes), 0 });
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
- **owning data**（`eastl::vector<uint8_t> m_data`）：UploadManager 持有用户数据所有权，调用方不用管生命周期。代价是一次额外 memcpy
- **大数据自动分片**：单 request > packet size 自动切多个 packet（FramePacket ring）
- **状态机三态**：UploadPendingTag → UploadInFlight → cleared。消费者可选阻塞（默认）或 fire-and-forget
- **Host-visible buffer 不走这条路**：调用方自己 Map 即可，不需要 staging

### 不变量小结

- **Init**：子类先做 backend init，**末尾** chain 父类（context push 在 backend ready 之后）
- **Shutdown**：子类**开头** chain 父类（resource/context unwind 在 backend teardown 之前）
- **OnFrameBegin handler 顺序**：RHIResourceSystem → AsyncUploadSystem → 其它
- **每个 SRG 只有一个 owner agent**（设计强制，未来 Set 阶段可并行）
- **Compile 在同一处**（便于将来开 `parallel_for`）

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
