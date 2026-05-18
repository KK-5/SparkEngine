# T7: TrianglePass 端到端实现

## Context

`TrianglePassFeature` 是 `SandBox/Program/RenderGraph/` 下的完整 RenderGraph 通路骨架。用新的 Shader Resource Binding 架构 + 数据驱动资源管线跑通端到端渲染。

## 改动范围

| 文件 | 动作 |
|---|---|
| `SandBox/Asset/Shader/TriangleMVP.hlsl` | **新建** |
| `SandBox/Program/RenderGraph/TrianglePassFeature.h` | 加 swap chain view 参数 + 新成员 |
| `SandBox/Program/RenderGraph/TrianglePassFeature.cpp` | 填完 5 个 TODO |
| `SandBox/Program/RenderGraph/main.cpp` | 创建 RHIResourceSystem + AsyncUploadSystem，传 swap chain view |

---

## Step 1: 新建 TriangleMVP.hlsl

`SandBox/Asset/Shader/TriangleMVP.hlsl`

```hlsl
cbuffer ViewConstants : register(b0, space0) { float4x4 g_MVP; };
// VSMain: mul(g_MVP, float4(position, 1.0))
// PSMain: return float4(color, 1.0)
// Input: POSITION(float3) + COLOR(float3)
```

## Step 2: main.cpp 创建数据驱动管线

- 创建 `RHIResourceSystem`（ISystem + FrameEventBus::Handler）
- 创建 `AsyncUploadSystem`（ISystem + FrameEventBus::Handler）
- 两者在 `renderSystem->Init()` 之后 `triFeature.Init()` 之前 Init，确保 `FrameEventBus::Broadcast(OnFrameBegin)` 时它们已注册
- 通过 `renderSystem->GetRenderGraph().GetSwapchainView()` 取 swap chain view handle，传给 `triFeature.Init(swapchainView)`

**注意**：需要给 `RenderSystem` 加一个公开的 `GetRenderGraph()` getter。

## Step 3: 实现 CreateVertexBuffer（走 RHIResourceSystem + AsyncUploadSystem）

数据驱动流程：

```cpp
auto& ctx = *RHIExecuteContext::Current();
RHIHandle vbEntity = ctx.CreateEntity();

// 1. 声明 post-upload rest state
ctx.Add<ImportedTag>(vbEntity);
ctx.Add<ResourceName>(vbEntity, ObjectName{"TriangleVB"});
ImportedResourceState imported;
imported.m_initial = ResourceState::VertexBuffer;
imported.m_initialStage = AttachmentStage::VertexInput;
imported.m_initialQueue = HardwareQueueClass::Graphics;
imported.m_final = ResourceState::VertexBuffer;
imported.m_finalStage = AttachmentStage::VertexInput;
imported.m_finalQueue = HardwareQueueClass::Graphics;
ctx.Add<ImportedResourceState>(vbEntity, imported);

// 2. 声明 buffer descriptor（由 RHIResourceSystem 物化）
PendingBufferInit init;
init.m_descriptor.m_bindFlags = BufferBindFlags::InputAssembly | BufferBindFlags::CopyWrite;
init.m_descriptor.m_byteCount = sizeof(g_triangleVertices);
init.m_heapMemoryLevel = HeapMemoryLevel::Device;
ctx.Add<PendingBufferInit>(vbEntity, init);

// 3. 声明 upload 数据（由 AsyncUploadSystem 提交）
PendingBufferUpload upload;
upload.m_data = g_triangleVertices;
upload.m_dataSize = sizeof(g_triangleVertices);
upload.m_destinationOffset = 0;
ctx.Add<PendingBufferUpload>(vbEntity, upload);
ctx.Add<UploadPendingTag>(vbEntity);

m_vbEntity = vbEntity;
```

关键：`m_vbEntity` 只是一个 handle，实际的 `Ptr<Buffer>` 在物化后挂到 entity 的 `Components::Buffer` 上。Execute 时从 entity 取 backing。

## Step 4: 实现 CreateViewSRG

同之前的方案，手动创建 layout + SRG + entity：

```cpp
layout->AddShaderInput(ShaderInputConstantDescriptor("g_MVP", 0, 64, 0, 0));
layout->SetBindingSlot(0);
layout->Finalize();
srg->Init(device, layout);
// entity 挂 ImportedTag + ShaderResourceTag + ResourceName +
//   Components::ShaderResourceLayout + Components::ShaderResource
```

## Step 5: 实现 CreateTrianglePass

`Init()` 参数接收 `RHIHandle swapchainView`，存为 `m_swapchainView`。

- 加载 `TriangleMVP.hlsl` 为 `ShaderAsset`
- `InputStreamLayoutBuilder` 构建 input layout
- `SPARK_RENDER_PASS(passContext, "TrianglePass")`
  - `.Queue(Graphics)`
  - `.VertexShader(m_vertShader).FragmentShader(m_fragShader)`
  - `.InputLayout(...).RenderTargetLayout(...).RenderStates(no depth)`
  - `.ShaderResource(0, m_viewSRGEntity)` → executer auto-bind
  - `.Build([this](auto& b) { ImportImageAttachment("SwapChain", {m_swapchainView, Write, RT, Clear}); })`
  - `.Execute([this](auto& work, auto&) { ... })`
  - `.Finalize()`

### Execute lambda

```cpp
// 从 VB entity 取物化后的 buffer
auto* backingBuf = RHIExecuteContext::Current()->TryGet<BackingBuffer>(m_vbEntity);
// 如果没有（第一帧可能还在上传），跳过
if (!backingBuf || !backingBuf->m_buffer) return;

cmdList->SetViewport(viewport);
cmdList->SetScissor(scissor);

DrawItem drawItem;
drawItem.m_drawArguments.m_type = DrawType::Linear;
drawItem.m_drawArguments.m_linear.m_vertexCount = 3;
drawItem.m_vertexBufferView.AddVertexInputView(
    VertexInputView(*backingBuf->m_buffer, 0, sizeof(verts), sizeof(TriangleVertex)));
cmdList->Submit(drawItem);
```

## Step 6: 实现 UpdateViewSRG

- 每帧递增旋转角、计算 MVP
- `m_srg->SetConstantRaw(mvpIdx, &mvp, sizeof(mvp))`
- `ctx.Add<ShaderResourceUpdateTag>(m_viewSRGEntity)`

## Step 7: Init 调用顺序

```
main.cpp:
  renderSystem->Init()         // 创建 swap chain entities
  rhiResourceSystem->Init()    // 注册 FrameEventBus handler
  asyncUploadSystem->Init()    // 注册 FrameEventBus handler
  triFeature.Init(swapchainView)
    FindSwapChainView  →  删除
    CreateViewSRG()
    CreateVertexBuffer()       // 声明 entity，不立即物化
    CreateTrianglePass()
    TickBus::BusConnect()
```

---

## 关键约束

- Scissor 构造：`Scissor(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY)`
- Viewport 构造：`Viewport(float minX, float maxX, float minY, float maxY)`
- `Components::ShaderResource::m_shaderResource`、`Components::ShaderResourceLayout::m_layout`
- 不创建 BackingShaderResource（已删除）
- DrawItem 无 m_pipelineState（PSO 由 executer auto-bind）
- VB entity 物化发生在第一个 OnFrameBegin（RHIResourceSystem），上传发生在同一个 OnFrameBegin（AsyncUploadSystem）。可能需要等待第一帧或调用 FlushUploadPackets

## 验证

1. 编译 `TrianglePass` 无错误
2. 运行后窗口出现旋转彩色三角形
3. 无 DX12 validation layer error
