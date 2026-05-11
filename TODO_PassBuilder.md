# PassBuilder & PSO Compiler & ShaderResource 改造方案

## 背景

`RenderSystem::BuildPipeline()` 当前以裸 `passContext.Add<Component>` 的方式构造 pass 实体（[RenderSystem.cpp:104-149](Engine/Code/RunTime/Feature/Render/RenderSystem.cpp#L104-L149)），存在三个问题：

1. **组件散装、易遗漏**：少加一个 `PassExecuteQueue` 或 `PassAttachmentMarker` 是 silent failure；`SPARK_PASS_TAG("X")` 与 `ObjectName("X")` 字符串若不一致也无人察觉。
2. **PSO 编译路径未走通**：`InitPipeline()` 是空函数；UI Pass 走的是 ImGui 自带管线，从未触发 `PipelineLibrary::CreateGraphicsPipeline`。
3. **ShaderResource (SRG) 没有进入 ECS 模型**：UE/Atom 的 Pass 是 class，可以直接持有 SRG 成员变量；但本引擎 pass 是 entity，SRG 应该住在哪里、谁负责创建/写入/释放，原本未定义。

整体方案分三步：
- **PassBuilder**：链式 API + 编译期 tag 绑定 + `Finalize()` 集中校验。
- **PSO Compiler**：作为 `RenderGraphCompiler` 的一个 stage，按需编译并把结果缓存到 pass 实体的 `PassCompiledPSO` 组件上。
- **ShaderResource Entity**：把 SRG 从"某个 system 的私有成员"改造为"RHIContext 中和 Image/Buffer 同形的 entity"，让多 pass 共享、批处理、调试视图、热重载都成为统一路径。

---

## 已完成事项

### 1. PSO 相关组件 — `Pass/Component/PassComponents.h`

- `CustomPipelinePassTag` — 标记 pass 使用自定义管线（如 ImGui），跳过引擎 PSO 编译
- `SinkPassTag` — 标记编译器每帧合成的 final-transition sink pass
- `PassPipelineState` — 捆绑三类固定函数 PSO 输入：
  - `RHI::InputStreamLayout m_inputStreamLayout`
  - `RHI::RenderTargetLayout m_renderTargetLayout`
  - `RHI::RenderStates m_renderStates`
- `PassCompiledPSO` — 缓存编译好的 `Ptr<RHI::PipelineState> m_pso`
- `PassPSODirtyTag` — shader hot-reload 时强制重编译

### 2. PassBuilder — `Pass/PassBuilder.h`

- `RenderPassBuilder<PassTag>` — 图形 pass 链式 API
  - `.Queue(HardwareQueueClass)` / `.Inactive()` / `.CustomPipeline()`
  - `.VertexShader()` / `.FragmentShader()` / `.GeometryShader()`
  - `.RenderTargetLayout(const RHI::RenderTargetLayout&)` / `.InputLayout()` / `.RenderStates()`
  - **`.ShaderResource(slot, RHIHandle entity)`** — 见第 6 节
  - `.Build(BuildFunction)` / `.Compile(CompileFunction)` / `.Execute(ExecuteFunction)`
  - `.Finalize()` — 集中校验 + emit 所有 ECS 组件
- `ComputePassBuilder<PassTag>` — 计算 pass 链式 API（仅 CS，无图形状态）
- 宏：`SPARK_RENDER_PASS(ctx, NAME)` / `SPARK_COMPUTE_PASS(ctx, NAME)` — 编译期 tag/name 一致
- Finalize 校验：
  - Queue 必设、Build/Execute 必非空
  - 非 CustomPipeline：VS 或 FS 至少一个 + RTV count > 0
  - CustomPipeline：禁止设 shader/PipelineState 方法

### 3. RenderSystem::BuildPipeline 迁移 — `RenderSystem.cpp`

- UI Pass 从手动 `passContext.Add<...>` 改为 `SPARK_RENDER_PASS(...).CustomPipeline()...Finalize()`

### 4. RenderGraphCompiler::CompilePipelineStates — `RenderGraphCompiler.cpp`

- 用 ECS view + `Exclude<CustomPipelinePassTag>` 遍历 pass
- 缓存命中跳过（`PassCompiledPSO` 存在且无 `PassPSODirtyTag`）
- 从 `PassShaders` 提取 `ShaderStageBytecode`，通过 factory 创建 `ShaderStageFunction`
- 图形 pass：组装 `PipelineStateDescriptorForDraw`
- 计算 pass：组装 `PipelineStateDescriptorForDispatch`
- 调用 `factory->CreatePipelineState()->Init(device, descriptor, pipelineLibrary)`
- 结果写回 `PassCompiledPSO`，清除 `PassPSODirtyTag`

### 5. ShaderResourceLayout 内部优化 — `ShaderResource/ShaderResourceLayout.cpp`

- 移除 `AddConstantsLayout(Ptr<ConstantsLayout>)` 公开接口
- `ConstantsLayout` 改为 lazy 创建：首次 `AddShaderInput(constantDescriptor)` 时内部 `new ConstantsLayout()`
- `Finalize()` 和 hash 计算增加 null 保护
- `DrawShape.cpp` 移除手动创建/传入 `ConstantsLayout`

### 6. ShaderResource Entity 模型落地 — `Pass/Component/RHIComponents.h` + `Pass/Component/PassComponents.h` + `Pass/PassBuilder.h`

**RHIComponents.h** 新增组件（仿 Image/Buffer 模式）：
- `ShaderResourceTag` — discovery tag，"列出所有 SRG"用
- `ShaderResource { Ptr<RHI::ShaderResource> }` — owning，lifetime 在 entity
- `BackingShaderResource { RHI::ShaderResource* }` — raw 镜像，给 compiler/executer 热路径
- `ShaderResourceLayout { Ptr<RHI::ShaderResourceLayout> }` — schema，永远存在
- `RHIUpdateTag` — 通用 dirty 标签（不只给 SRG 用，未来 buffer/image 数据更新都可复用），放在 `ImportedTag`/`TransientTag` 旁边

**PassComponents.h** 新增 + 删除：
- 新增 `PassShaderResources { eastl::fixed_vector<RHIHandle, RHI::Limits::Pipeline::ShaderResourceCountMax> m_slots; }`
- 删除 `PassShaderInputs`（pass 不再持有 shader input layout，由 SRG 的 layout 提供）
- 加 `<RHI/Pipeline/PipelineState.h>` 和 `<RHI/RHILimits.h>` include；删 `<RHI/Resource/ShaderResource/ShaderResourceDescriptor.h>`

**PassBuilder.h** 接口变更：
- 删除 `.ShaderInputs(const PassShaderInputs&)`
- 新增 `.ShaderResource(uint32_t slot, RHIHandle entity)`（合并设计——见第 6.4 小节解释）
- 校验：slot < `ShaderResourceCountMax`、entity 非 NullHandle、slot 未占、entity 必有 `ShaderResourceLayout`
- 校验通过 `RHIExecuteContext::Current()->Has<...>` 查询 RHIContext（注意：builder 自己的 `m_context` 是 `PassContext*`，是另一个 ECS 上下文）
- builder 构造时 pre-fill `m_slots` 为 `ShaderResourceCountMax` 个 `NullHandle`，让索引访问安全
- Finalize 末尾无条件 `Add<PassShaderResources>`（即使 pass 没用 SRG 也加空的，让 consumer 不用 TryGet 分支）

### 7. RHIUpdateTag 消费者 + 接入 RenderGraph 编译流程

**依赖**：第 6 项（SRG entity 已能创建）

**实现内容**：

- `RenderGraph::Init()` 中创建 `PipelineLibrary`（从 `RenderSystem` 移入，避免 `RenderSystem` 持有 pipeline library）
- `RenderGraph::ExecutePipeline()` 在编译阶段按以下顺序调用：
  1. `m_compiler.CompileShaderResources(device, context)` — 批量 flush dirty SRG
  2. `m_compiler.CompilePipelineStates(...)` — 编译所有 pass 的 PSO
  3. `m_compiler.CompileTransientResources(...)` — 分配 transient 资源
  4. 遍历所有 pass，编译 barrier + RenderPassBeginInfo
  
- `RenderGraphCompiler::CompileShaderResources()` 实现（[RenderGraphCompiler.cpp:1324-1340](Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraphCompiler.cpp#L1324-L1340)）：
```cpp
void RenderGraphCompiler::CompileShaderResources(RHI::Device& device, RHIContext& context)
{
    auto& view = context.GetView<RHIUpdateTag, BackingShaderResource>();
    auto* factory = Service<RHI::Factory>::Get();
    auto& shaderResourceCompiler = factory->AcquireShaderResourceCompiler(device);
    eastl::vector<RHI::ShaderResource*> shaderResources;
    shaderResources.reserve(view.size_hint());
    view.each([&](RHIHandle handle, BackingShaderResource& shaderResource)
    {
        shaderResources.push_back(shaderResource.m_shaderResource);
        context.Remove<RHIUpdateTag>(handle);
    });
    shaderResourceCompiler.Compiler(shaderResources);
}
```

**关键设计决策**：
- 所有 SRG 的 Compile 在**同一处**集中执行，便于将来开 `parallel_for`
- `RHIUpdateTag` 在 Compile 后立即 Remove，下帧 agent 重新标
- 放在 `CompilePipelineStates` 之前执行——虽然 PSO 不依赖 SRG 数据，但 layout 验证可能依赖

### 8. PipelineLayoutDescriptor 构建（在 PSO Compiler 里）

**依赖**：第 7 项

**实现内容**（[RenderGraphCompiler.cpp:46-99](Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraphCompiler.cpp#L46-L99)）：

- 新增静态函数 `BuildPipelineLayoutDescriptor()`，从 pass 的 `PassShaderResources.m_slots` + `ShaderStageMask` 推导完整 pipeline layout：

```cpp
Ptr<RHI::PipelineLayoutDescriptor> BuildPipelineLayoutDescriptor(
    const PassShaderResources& slots,
    RHI::ShaderStageMask       stageMask)
{
    // 遍历每个 slot
    // 1. 读取 SRG entity 上的 ShaderResourceLayout
    // 2. 提取 ConstantsLayout → ResourceBindingInfo（取首个 constant descriptor 的 register/space）
    // 3. 遍历 buffer/image/sampler/static sampler 列表 → m_resourcesRegisterMap
    // 4. 对每个 layout 调用 desc->AddShaderResourceLayoutInfo(layout, bindingInfo)
    // 5. desc->Finalize() 返回
}
```

- 在 `CompilePipelineStates()` 中，图形 pass 和计算 pass 的 `PipelineStateDescriptor::m_pipelineLayoutDescriptor` 均由此函数填充
- **stageMask 当前用保守值**（所有活跃 stage 的 OR），等 shader reflection 通了再细化 per-resource 的 visibility

### 9. PassExecuteContext 注入架构（RenderSystem 与外部管线）

**动机**：`RenderSystem` 不应硬引用自己的 `Pipeline`，应该让外部代码能通过堆栈注入替代管线。

**具体改造**（[RenderSystem.cpp:144-163](Engine/Code/RunTime/Feature/Render/RenderSystem.cpp#L144-L163)）：

- `RenderSystem::InitInternal()` 末尾执行 `PassExecuteContext::Push(m_pipeline.GetPassContext())`，将默认的 UIPass 管线压入堆栈
- `RenderSystem::ShutdownInternal()` 开头执行 `PassExecuteContext::Pop()`
- `RenderSystem::OnTick()` 改为从 `*PassExecuteContext::Current()` 读取当前 PassContext，不再持有 Pipeline 引用：

```cpp
void RenderSystem::OnTick(float deltaTime)
{
    auto& passContext = *PassExecuteContext::Current(); 
    const uint32_t frameIndex = m_rhiData.m_swapChain->GetCurrentImageIndex();
    m_renderGraph.ExecutePipeline(passContext, frameIndex);
    m_rhiData.m_swapChain->Present();
}
```

- `RenderGraph::ExecutePipeline()` 签名为 `void ExecutePipeline(PassContext& passContext, uint32_t frameIndex)`，直接接受引用，内部不做 Push/Pop

**外部覆盖用法**（[main.cpp:46-48](SandBox/Program/RenderGraph/main.cpp#L46-L48)）：

```cpp
// Push 自己的管线覆盖默认的 UIPass
Spark::Render::Pipeline triPipeline("Triangle");
Spark::Render::PassExecuteContext::Push(triPipeline.GetPassContext());
// ... Init Feature ...
// ... 游戏循环 ...
Spark::Render::PassExecuteContext::Pop();
```

### 10. RenderGraphExecuter 绑定 PSO + SRG

**依赖**：第 8 项（PipelineLayoutDescriptor）和第 9 项（PassExecuteContext 注入）

**实现内容**：

- `ExecuteWork::Item` 新增执行期缓存字段（[RenderGraphExecuter.h:38-46](Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraphExecuter.h#L38-L46)）：
```cpp
struct Item
{
    Pass      m_pass;
    DrawRange m_draws;
    uint32_t  m_itemIndex = 0;
    uint32_t  m_itemCount = 1;

    const RHI::PipelineState* m_pipelineState = nullptr;
    eastl::fixed_vector<const RHI::ShaderResource*, RHI::Limits::Pipeline::ShaderResourceCountMax> m_shaderResources;
};
```

- `ExecuteWork` 新增 `RHI::CommandList* m_commandList` 字段

- `Execute()` 在录制前从 ECS 解析 PSO 和 SRG（[RenderGraphExecuter.cpp:184-240](Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraphExecuter.cpp#L184-L240)）：
  1. 创建设备 CS：`factory.CreateCommandList(device, queueClass)`
  2. 遍历所有 Item，对每个 Item：
     - 从 pass entity 的 `PassCompiledPSO` 取出 `m_pipelineState`
     - 从 pass entity 的 `PassShaderResources.m_slots` 取 slot → entity，再查 RHIContext 的 `BackingShaderResource`，填入 `m_shaderResources`
  3. 执行 pre-barriers + BeginRenderPass → 调用 execute lambda → EndRenderPass + post-barriers
  4. 录制完毕后 `cmdList->Close()`

- `ExecuteFunction` 签名从 `void(RenderGraphExecuter&)` 改为 `void(ExecuteWork&, RenderGraphExecuter&)`，lambda 可通过 `work.m_commandList` 和 `work.m_items[0].m_pipelineState` 访问执行期上下文

- 受影响文件：
  - [PassComponents.h:136-140](Engine/Code/RunTime/Feature/Render/Pass/Component/PassComponents.h#L136-L140)：`PassFunctions::m_executeFunction` 签名变更
  - [PassBuilder.h:26](Engine/Code/RunTime/Feature/Render/Pass/PassBuilder.h#L26)：`RenderPassBuilder::ExecuteFunction` typedef 更新
  - [PassBuilder.h:222](Engine/Code/RunTime/Feature/Render/Pass/PassBuilder.h#L222)：`ComputePassBuilder::ExecuteFunction` typedef 更新

### 11. TrianglePass 骨架搭建（部分完成）

**依赖**：第 10 项 + PassExecuteContext 注入

**已创建文件**：
- [SandBox/Program/RenderGraph/TrianglePassFeature.h](SandBox/Program/RenderGraph/TrianglePassFeature.h) — Feature 类声明
- [SandBox/Program/RenderGraph/TrianglePassFeature.cpp](SandBox/Program/RenderGraph/TrianglePassFeature.cpp) — TODO stub 实现
- [SandBox/Program/RenderGraph/main.cpp](SandBox/Program/RenderGraph/main.cpp) — 入口，演示 PassExecuteContext 注入
- [SandBox/Program/CMakeLists.txt:48-73](SandBox/Program/CMakeLists.txt#L48-L73) — `BUILD_RG_TRIANGLEPASS` target

**TrianglePassFeature 成员经过设计修剪后的最终形态**：

| 成员 | 用途 | 为什么需要 |
|---|---|---|
| `m_viewSRGEntity` (RHIHandle) | RHIContext 中 ViewSRG entity 的快速句柄 | 外部更新时需要查找 entity |
| `m_srg` (RHI::ShaderResource*) | ViewSRG 的裸指针 | SetConstant 时使用，所有权在 entity |
| `m_bufferPool` (Ptr<RHI::BufferPool>) | 顶点/索引 buffer 的 pool | 管理 buffer 生命周期 |
| `m_vertexBuffer` (Ptr<RHI::Buffer>) | 三角形顶点数据 | 绘制时绑 |
| `m_vertShader` / `m_fragShader` (Ptr<ShaderAsset>) | 着色器资产 | PassBuilder 链式 API 需要 |
| `m_rotationAngle` (float) | MVP 旋转角度 | OnTick 更新用 |

**被删除的成员**（及原因）：
- `m_renderSystem` — RenderSystem 通过 Service 访问，不直接持有引用
- `m_srgLayout` — 归 entity 的 `ShaderResourceLayout` 组件所有，Feature 不需要缓存
- `m_trianglePass` (Pass entity) — render graph 迭代 pass entity 走 ECS view，不需要存储
- `m_indexBuffer` — 画单个三角形只需要 3 个顶点 + vertex buffer，不需要 index buffer

**注意**：`m_srg` 改为**裸指针**而非 `Ptr<>`，所有权在 entity 的 `ShaderResource` 组件。Feature 只是更新 SRG 内容的 agent，不延长 lifetime。

**当前状态**：所有 `CreateViewSRG()`、`CreateVertexBuffer()`、`CreateTrianglePass()`、`UpdateViewSRG()` 仍然是 TODO stub（[TrianglePassFeature.cpp:57-90](SandBox/Program/RenderGraph/TrianglePassFeature.cpp#L57-L90)），等待 UploadManager 和完整 SRG 创建路径落地后再填。

---

## 设计原则与决策记录（重要！下次接手必读）

下面这些是讨论中确定的设计决策，**不写在代码里就会被下一次重构破坏**。下次接手前看一遍。

### 6.1 资源所有权三档

| 档 | 例 | Owner | RHIContext 怎么看到 |
|---|---|---|---|
| **Engine-life** | View/Scene/Lights SRG, ShadowAtlas, MaterialSRG | Feature System (agent) | Imported entity，system 在 Init 创建、Shutdown 销毁 |
| **Frame-life** | GBuffer, HDR 等 transient texture/buffer | RG Compiler | Transient entity，每帧分配/释放 |
| **SwapChain** | 后台缓冲 + 视图 | RenderGraph 自己 | Imported per-frame entity |

**契约**：所有 Imported 资源（包括 SRG），注册方负责生命周期。RG 永远只持 raw 指针（`BackingX`），不延长 owner 生命周期。owner shutdown 前必须 `Unimport`。

### 6.2 System 是 agent，不是 container

这是**最关键的心智转换**。曾经的错误想法：

```cpp
// ❌ 错的：system 持有 SRG 的 Ptr<>
class ViewSystem {
    Ptr<RHI::ShaderResource> m_viewSRG;   // 物理 owner 在 system
};
```

正确做法：

```cpp
// ✅ 对的：SRG 的 Ptr<> 在 entity 上，system 只是创建/更新/销毁的 agent
class ViewSystem {
    RHIHandle m_viewSRGEntity;            // 只是个快速查找句柄
    void InitInternal()    { m_viewSRGEntity = builder.CreateShaderResource(...); }
    void OnFrameBegin()    { /* SetConstant + 标 RHIUpdateTag */ }
    void ShutdownInternal(){ builder.DestroyShaderResource(m_viewSRGEntity); }
};
```

**理由**：
- 数据放 entity 上，自然支持批处理（compile 所有 dirty SRG = 一个 ECS view）
- N 个 pass 共享 SRG = N 个 handle 指向同一 entity，零额外成本
- 调试工具、热重载、序列化都走统一路径
- system 只负责"知道往这个 SRG 里塞什么数据"——这是业务逻辑，必须有人写代码

**例外**：device、factory、command queue 这种**单例 + opaque + 不批处理**的对象，留在 system 成员里（走 `Service<>` 模式）。判断标准：复数 + 同形 + 需要批量遍历 → 进 ECS；单例 + opaque → 留 system。

### 6.3 三档 SRG 按更新频率分类

按"谁写、何时写"分三档，**所有档的组件形状完全一致**，只有 owner agent 和写时机不同：

| 档 | 例 | Owner agent | 写时机 |
|---|---|---|---|
| **Per-View / Per-Scene** | ViewSRG, SceneSRG, LightsSRG | ViewSystem / SceneSystem / LightSystem | OnFrameBegin |
| **Per-Pass** | TonemapParamsSRG, BloomParamsSRG, UIPassSRG | 该 pass 所属的 Feature System | OnFrameBegin |
| **Per-Material / Per-Draw** | 材质 SRG（贴图+常量）、Draw SRG（model matrix） | MaterialSystem / TransformSystem 等 | 资产事件 / Transform 变化事件，不是每帧 |

**关键澄清**："Per-Draw" 是个误称，准确说是 **Per-Material**（一个材质 SRG 服务上千个 draw 实例）。真正逐 draw 变化的数据（model matrix）属于 Per-Object，每个渲染对象一个 DrawSRG entity。

### 6.4 Per-Material / Per-Object SRG 的 pass-side 表达

Per-Material 和 Per-Object SRG 在 pass-builder 期**不知道具体绑哪个 entity**（要看 draw 列表）。但 PSO 编译需要知道 layout。解法：

- MaterialSystem 在 Init 时创建一个 **layout-only entity**（只有 `ShaderResourceLayout`，没有 `BackingShaderResource`），名字叫 "MaterialLayout"
- pass 在 builder 期：`.ShaderResource(slot, materialLayoutEntity)`——传入这个 layout-only entity
- PSO compiler 读 `ShaderResourceLayout` 拼 `PipelineLayoutDescriptor`，正常工作
- Executer 在 pass-begin `TryGet<BackingShaderResource>(materialLayoutEntity)` → nullptr → **跳过自动绑**
- execute lambda 内部 per-draw 切：`cl.BindShaderResource(slot, ctx.Get<BackingShaderResource>(draw.materialEntity).m_shaderResource)`

**为什么 builder 只有一个 `.ShaderResource()` 方法（没有 `.ShaderResourceSlot()`）**：

历史方案曾设计过两个方法（concrete vs layout-only），后来合并成一个。理由：

- executer 的 dispatch 是单一真相——它只看 entity 上有没有 `BackingShaderResource`，根本不在乎 builder 当时调了哪个方法
- 两个方法只是 typo 保护，但运行时 GPU 验证层能兜住相同问题
- 一条规则更好教："给 slot 一个 entity，有 Backing 就自动绑，没有就你自己绑"

### 6.5 SRG 是绑定容器，不是 render graph 节点

**这条规则不写清楚的话，将来一定有人踩坑**。

SRG 里可以塞 imageView/bufferView，但 render graph **看不到**这些塞进去的资源——它只看 `ImagePassAttachment` / `BufferPassAttachment` 组件。

举例（延迟渲染）：ShadowAtlas 是一张 imported texture，由 LightSystem 持有。它出现在两处：
- 在 LightsSRG 里（作为 SRV 槽，给 lighting shader 采样）
- 作为 attachment 在 ShadowPass 写、LightingPass 读

**LightingPass 必须显式 `b.ReadImageAttachment("ShadowAtlas", PixelShaderRead)`**，否则 RG 不知道要插 barrier，GPU 在 LightingPass 执行时还停留在 DepthWrite state，race。

**铁律**：
- SRG 里只放**整帧只读、不参与渲染图依赖**的东西（cbuffer、static texture、sampler、shadow atlas SRV view 等）
- 任何被 PassA 写、PassB 读的资源，必须在两个 pass 都声明 attachment（即便它也通过 SRG 的 SRV 槽暴露给 shader）

### 6.6 SRG 数据写入流程（三阶段）

```
[1] Set + 标 dirty                [2] Batch Compile                [3] 读取
─────────────────────             ────────────────────             ──────────
SRG.SetImageView(...)             ctx.GetView<RHIUpdateTag,        cl.BindShaderResource(...)
SRG.SetConstant(...)                 BackingShaderResource>()      cl.Draw(...)
ctx.Add<RHIUpdateTag>(e)            .each(s.m_srg->Compile())
仅 staging                        ctx.Clear<RHIUpdateTag>()
便宜
```

时间线：
- **OnFrameBegin**：各 agent system 在自己的 hook 里写数据 + 标 `RHIUpdateTag`
- **OnFrameCompileBegin**：RG compiler 统一 batch flush dirty SRG（**所有 SRG compile 在同一处**，便于将来开并行）
- **OnFrameCompileEnd 之后**：所有 SRG immutable，N 个 pass 并发读安全

**强制规则**：agent 只 Set + 标 dirty，**不要自己调 Compile**。Compile 由 compiler 统一做。

**Set 阶段并行性**：每个 SRG 只有一个 owner agent（设计强制），所以不同 agent 之间 Set 天然不冲突，可以后期并行。短期串行就行，agent 数量是个位数。

**Compile 阶段并行性**：每个 SRG 的 Compile 独立。但前提是底层 thread-safe（DX12 descriptor heap allocator、constants ring buffer 都需要 atomic offset 分配）。短期单线程实现，后期切 `parallel_for` 是透明优化。

### 6.7 SRG 是统一抽象，push constant 是后端 lowering

`RHI::ShaderResourceLayout` 已经支持 `AddShaderInput(ShaderInputConstantDescriptor)` —— 这就是 SRG 的"constants 段"。后端选择把它实现成什么完全是后端的事：

- 小数据（model matrix 64 字节）→ DX12 root constants / VK push constants
- 大数据 → inline cbuffer / 普通 cbuffer

上层逻辑**只调一个 API**：`cl.BindShaderResource(slot, srg)`，从不操心 push constant vs descriptor table 的区别。

**含义**：model matrix 这种"逐 draw 变化、小数据"的东西**仍然走 SRG**（在 Per-Object DrawSRG 的 constants 段），不要单独为 push constant 开一条命令流 API。

### 6.8 Pass entity 只放引用，不放 SRG 数据

`PassShaderResources` 组件里只有 `RHIHandle`（slot → entity）。**不缓存 layout、不缓存 backing 指针**。理由：

| 信息 | 单一来源 | 谁在用 |
|---|---|---|
| `ShaderResourceLayout` | SRG entity 上 | PSO compiler |
| `BackingShaderResource` | SRG entity 上 | Executer / execute lambda |
| slot 顺序 | pass 的 `PassShaderResources.m_slots` | compiler + executer 都按 slot 索引 |

好处：
- SRG 热重载时 pass 自动跟上，无需失效任何缓存
- N pass 共享 SRG = N 个 handle 指向同一 entity，零额外成本
- 一致性校验只看 SRG entity 一处

slot 索引 == shader 的 register space (DX12) / set index (Vulkan)。

### 6.9 字符串名字 vs 类型化引用

短期：用字符串名字（`ResourceName`）做 SRG 跨模块引用——简单、灵活、调试友好。

长期目标（不阻塞当前开发）：分三档处理耦合：

| SRG 类型 | 谁知道它存在 | 长期推荐引用方式 |
|---|---|---|
| 引擎内置（View/Scene） | 所有人都允许引用 | `XxxSystem::Get()->GetXxxSRG()` 类型化访问器 |
| Feature 私有（Tonemap/UI） | 只有自己 Feature | 自己的成员变量 `m_xxxSrg` |
| 跨模块 layout（Material/Draw） | shader + material + pass | `EngineSrgLayouts::Xxx` 类型化常量 |

字符串只用于：调试日志、editor inspector、序列化。**运行时查找应该走类型化引用**。这层等核心机制稳定后再逐步迁移。

### 6.10 关于一个延迟渲染管线长什么样（参考实现）

```
Owners (engine-life systems)
─────────────────────────────────────────────────────
ViewSystem        ──owns──►  ViewSRG          (matrices, time, jitter)
LightSystem       ──owns──►  LightsSRG        (light list + shadow atlas SRV)
                  ──owns──►  ShadowAtlas      (Ptr<RHI::Image>, persistent)
MaterialSystem    ──owns──►  per-material SRGs (1 per material instance)
                              + "MaterialLayout" layout-only entity
TransformSystem   ──owns──►  per-object DrawSRGs (model matrix)
                              + "DrawLayout" layout-only entity
TonemapFeature    ──owns──►  TonemapParamsSRG + TonemapPass entity
UIFeature         ──owns──►  UIPassSRG        + UIPass entity
DeferredFeature   ──owns──►  Shadow/GBuffer/Lighting pass entities (无私有资源)

Render-graph-life
─────────────────────────────────────────────────────
Transient (RG Compiler 分配)：GBufferA/B/C, Depth, HDR
Imported  (RG 借用 raw 指针)：所有 SRG + ShadowAtlas + SwapChain
```

Pass 声明示例：

```cpp
// LightingPass: 揭示 SRG 边界的关键 pass
SPARK_RENDER_PASS(ctx, "LightingPass")
  .ShaderResource(EngineSrgSlot::View,  viewSRGEntity)   // concrete
  .ShaderResource(EngineSrgSlot::Scene, lightsSRGEntity) // concrete
  .Build([](auto& b){
      b.ReadImageAttachment("GBufferA",   PixelShaderRead);
      b.ReadImageAttachment("GBufferB",   PixelShaderRead);
      b.ReadImageAttachment("GBufferC",   PixelShaderRead);
      b.ReadImageAttachment("Depth",      DepthRead);
      b.ReadImageAttachment("ShadowAtlas",PixelShaderRead); // ← 必须再声明一次！
      b.CreateImage("HDR", descHDR);
      b.WriteImageAttachment("HDR", ColorWrite);
  })
  .Execute([](auto& cl){ cl.DrawFullscreen(); });

// GBufferPass: 演示 per-material + per-draw slot 的用法
SPARK_RENDER_PASS(ctx, "GBufferPass")
  .ShaderResource(EngineSrgSlot::View,     viewSRGEntity)        // concrete
  .ShaderResource(EngineSrgSlot::Material, materialLayoutEntity) // layout-only
  .ShaderResource(EngineSrgSlot::Draw,     drawLayoutEntity)     // layout-only
  .Build([](auto& b){ /* ... 4 个 RT + Depth */ })
  .Execute([&](auto& cl){
      RHIHandle currentMat = NullHandle;
      for (auto& draw : visibleDraws) {
          if (draw.materialEntity != currentMat) {
              cl.BindShaderResource(EngineSrgSlot::Material,
                  ctx.Get<BackingShaderResource>(draw.materialEntity).m_shaderResource);
              currentMat = draw.materialEntity;
          }
          cl.BindShaderResource(EngineSrgSlot::Draw,
              ctx.Get<BackingShaderResource>(draw.drawSrgEntity).m_shaderResource);
          cl.SetIndexBuffer(draw.ib);
          cl.SetVertexBuffer(draw.vb);
          cl.DrawIndexed(draw.indexCount);
      }
  });
```

---

## 未完成事项

下面按推荐顺序排列，每项标注依赖关系。

### [ ] 12. SRG 创建/销毁的 builder 接口

**依赖**：无（可以现在做）

需要的接口（建议加在 `RenderGraphBuilder` 上，对称于 `ImportImageAttachment`）：

```cpp
// 注册一个外部已创建的 SRG（concrete 实例），返回 entity handle
RHIHandle ImportShaderResource(
    ObjectName name,
    Ptr<RHI::ShaderResourceLayout> layout,
    Ptr<RHI::ShaderResource>       srg);

// 注册一个 layout-only entity（用于 per-draw / per-material slot）
RHIHandle RegisterShaderResourceLayout(
    ObjectName name,
    Ptr<RHI::ShaderResourceLayout> layout);

// 销毁 SRG entity（owner 在 Shutdown 调用）
void DestroyShaderResource(RHIHandle entity);
```

实现要点：
- `ImportShaderResource`：CreateEntity → 加 `ImportedTag` + `ShaderResourceTag` + `ResourceName` + `ShaderResourceLayout` + `ShaderResource (owning)` + `BackingShaderResource (raw)`
- `RegisterShaderResourceLayout`：CreateEntity → 加 `ImportedTag` + `ShaderResourceTag` + `ResourceName` + `ShaderResourceLayout`（**不加 BackingShaderResource**，这是 layout-only 的标志）
- `DestroyShaderResource`：从 RHIContext 删除 entity（Ptr<> 自动 release）

### [ ] 13. UploadManager 模块

**依赖**：无（独立模块，但 TrianglePass 需要它上传顶点数据）

**背景**：引擎当前没有统一的资源上传机制。Vertex buffer 等 RHI 资源的初始数据需要从 CPU 拷贝到 GPU，这个路径应该统一管理，而不是每个 Feature 各自写一套 staging buffer + command list。

**设计方案**（已讨论一致）：

**API 形态**：
```cpp
class UploadManager {
public:
    // Feature 在 Init 或 OnFrameBegin 调用，只注册请求，CPU 端操作
    void QueueUpload(RHI::Buffer* dst, const void* data, size_t size);
    void QueueUpload(RHI::Image* dst, const void* data, size_t size, /* subresource layout */);

    // RenderGraph 在 ExecutePipeline 的 Build 阶段前调用，批量执行所有 upload
    void Flush(RHI::Device& device, RHI::CommandQueue& queue);

    // 帧末：推进 ring buffer 指针，释放当前帧的 staging 内存
    void EndFrame();
};
```

**核心机制**：
- **Ring-buffer staging memory**：分配 N 个 frame 的 staging buffer（N = frameCountMax，通常 3），每帧轮换一个。新一帧直接复用 N 帧前的那份——此时 GPU 肯定已经消费完毕（N 帧的 serialized 提交保证）
- **单 CommandList 批量执行**：`Flush()` 时把所有 pending upload 打包到一个 command list，执行 copy → barrier → close → submit，降低提交开销
- **GPU 串行执行保证**：同一队列上的 copy barrier → draw 天然有序，不需要 CPU 端 wait
- **大数据不阻塞**：UploadManager 不做分片。大数据上传触发 CPU 端 memcpy + GPU copy，会阻塞当前帧，但这不是 UploadManager 的问题——是调用方的策略问题（大纹理应在加载线程异步做）

**所有权**：UploadManager 由 RenderGraph 创建和管理。Feature 通过 `Service<UploadManager>` 访问。

### [ ] 14. TrianglePass 端到端实现

**依赖**：第 12 项（SRG 注册接口）+ 第 13 项（UploadManager）

需要完成的具体工作（按顺序）：

1. **`CreateViewSRG()`**：构建 ShaderResourceLayout（MVP constant + 可选纹理/采样器），创建 ShaderResource，通过 builder API 在 RHIContext 创建 Imported SRG entity
2. **`CreateVertexBuffer()`**：分配 GPU buffer（StructuredBuffer 或 staging + copy），通过 UploadManager 上传三角形顶点数据
3. **`CreateTrianglePass()`**：用 `SPARK_RENDER_PASS` 链式 API，设置 VS/FS、RenderTargetLayout、绑定 ViewSRG、Build 中 import swap chain RTV、Execute 中设 viewport + 绑顶点 buffer + Draw(3)
4. **`UpdateViewSRG()`**：计算 MVP 矩阵，调 `m_srg->SetConstantRaw(...)` 写常量，标 `RHIUpdateTag`
5. **Shader 资产**：创建简单的三角形 VS/PS `.hlsl`，编入 CMake，加载为 `ShaderAsset`

### [ ] 15. Smoke test

**依赖**：第 14 项

构建、运行，确认：
- UI Pass（custom pipeline 路径，无 PSO/SRG）正常显示
- TrianglePass（PSO + SRG 路径）正常显示三角形
- ViewSRG 数据每帧通过 batch Compile 正常上传到 GPU
- 没有 GPU validation error

---

## 遇到的问题与解决记录

记录在前两个 session 中遇到的具体问题和解决方法，避免未来重复踩坑。

### Q1: `auto&` 绑定 `CreateSystem()` 返回值

**现象**：
```cpp
auto& renderSystem = CreateSystem<Spark::Render::RenderSystem>();
// error: cannot bind non-const lvalue reference to an rvalue
```
**原因**：`CreateSystem<T>()` 返回 `SystemUniquePtr`（即 `eastl::unique_ptr<ISystem>`），临时对象不能绑定到 `auto&`。

**解决**：使用 `auto`（不加 `&`），或 `auto renderSystem = CreateSystem<...>()`.

### Q2: `#include <Render/RenderSystem.h>` — include 路径不对

**现象**：编译报错找不到 `RenderSystem.h`。

**原因**：SparkRender 的 include 根是 `Engine/Code/RunTime/Feature/Render/`，不是 `Engine/Code/RunTime/Feature/`。CMake 的 `target_include_directories` 暴露的是前者。

**解决**：改成 `#include <RenderSystem.h>`，直接使用根相对路径。

### Q3: `#include <Resource/Asset.h>` 缺失导致 `AssetData` 未声明

**现象**：`ShaderAsset` 的 `GetShaderData()` 返回的类型涉及 `AssetData`，编译器报未声明类型。

**解决**：在 `TrianglePassFeature.h` 中添加 `#include <Resource/Asset.h>`。

### Q4: 命名空间解析错误 — `RHIHandle`、`Pass`、`NullPass` 未找到

**现象**：`TrianglePassFeature` 在 `Spark::SandBox` 命名空间下，成员使用 `RHIHandle`、`Pass`、`NullHandle` 等类型时编译报错。

**原因**：这些类型定义在 `Spark::Render` 命名空间下，`Spark::SandBox` 中无 using。

**解决**：使用完全限定名：
- `Spark::Render::RHIHandle`
- `Spark::Render::NullHandle`
- `Spark::Render::Pass`

### Q5: Design mistake — Feature 持有 RenderSystem 引用

**讨论**：初始设计中 `TrianglePassFeature` 持有 `RenderSystem*`，以便访问 Device/CommandQueue。

**结论**：错误。RenderSystem 通过 `Service<>` 模式访问即可。Feature 不应该也不需要持有 RenderSystem 引用。已从成员中移除。

### Q6: Design mistake — Feature 持有 Pass entity

**讨论**：初始设计存储了 `Pass m_trianglePass`。

**结论**：不需要。Render graph 通过 ECS view 迭代所有 pass entity，Feature 不需要存储 pass 句柄。Create 完即可丢弃。

### Q7: `m_srg` 的所有权 — Ptr<> vs 裸指针

**讨论**：`m_srg`（ShaderResource 数据）用 `Ptr<>` 还是裸指针？

**结论**：用裸指针 `RHI::ShaderResource*`。所有权在 RHIContext entity 的 `ShaderResource` 组件上。Feature 只是更新 SRG 内容的外部 agent，不负责生命周期。

### Q8: UploadManager 设计 — GPU 数据有效性保证

**用户问题**："如果上传数据太多阻塞了主线程怎么办？"

**分析**：
- Ring-buffer：N 帧后 GPU 肯定消费完（串行提交保证），无需 wait
- 大数据阻塞是 streaming 问题，不是 UploadManager 的职责
- 如果上传 1GB，CPU memcpy + GPU copy 确实会阻塞，但 UploadManager 不负责分片——调用方应在加载线程异步做

**用户问题**："不阻塞就继续怎么保证这个资源使用功能时是有效的？"

**分析**：同一 CommandQueue 上的操作是串行的——copy → barrier → draw 在 GPU 上按提交顺序执行，draw 执行时 upload 肯定已经完成。

---

## 不在本轮范围（明确划界）

以下事项规模较大，明确不在当前任务里，避免越界：

- **类型化 SRG 引用迁移**：第 6.9 节的"长期目标"，等核心机制稳定后再做
- **Per-Object DrawSRG 自动化创建**：哪个 system 在游戏对象 spawn 时创建 DrawSRG entity、何时销毁——属于 scene/transform 系统范畴，不是渲染层
- **MaterialSystem 实现**：当前没有 material asset 概念，先 hardcode 一个 layout 用就行
- **并行 Compile**：等单线程版本稳定后再上 `parallel_for`
- **PassShaderInputs 的清理**：已删，无后续动作
- **`m_registry` 在 ResourcePool 的清理**：与本轮无关

---

## 关键文件参考

- 组件定义：[Engine/Code/RunTime/Feature/Render/Pass/Component/RHIComponents.h](Engine/Code/RunTime/Feature/Render/Pass/Component/RHIComponents.h)、[Engine/Code/RunTime/Feature/Render/Pass/Component/PassComponents.h](Engine/Code/RunTime/Feature/Render/Pass/Component/PassComponents.h)
- Pass builder：[Engine/Code/RunTime/Feature/Render/Pass/PassBuilder.h](Engine/Code/RunTime/Feature/Render/Pass/PassBuilder.h)
- RG builder（imported 资源接口的对照）：[Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraphBuilder.h](Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraphBuilder.h)
- RG compiler：[Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraphCompiler.cpp](Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraphCompiler.cpp)
- RG executer：[Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraphExecuter.cpp](Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraphExecuter.cpp)
- RHI ShaderResource：[Engine/Code/RunTime/Feature/RHI/Resource/ShaderResource/ShaderResource.h](Engine/Code/RunTime/Feature/RHI/Resource/ShaderResource/ShaderResource.h)、[Engine/Code/RunTime/Feature/RHI/Resource/ShaderResource/ShaderResourceLayout.h](Engine/Code/RunTime/Feature/RHI/Resource/ShaderResource/ShaderResourceLayout.h)
- RHI 限制：[Engine/Code/RunTime/Feature/RHI/RHILimits.h](Engine/Code/RunTime/Feature/RHI/RHILimits.h)（`ShaderResourceCountMax = 8`）
- ECS 上下文：[Engine/Code/RunTime/Feature/Render/Pass/RHIContext.h](Engine/Code/RunTime/Feature/Render/Pass/RHIContext.h)（`RHIExecuteContext::Current()` 取当前 RHIContext）
