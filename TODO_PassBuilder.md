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
- **未完成**：`PipelineLayoutDescriptor` 暂未构建（需要 SRG entity 设计落地后才能从 pass 的 `PassShaderResources` 推导）

### 5. ShaderResourceLayout 内部优化 — `ShaderResource/ShaderResourceLayout.cpp`

- 移除 `AddConstantsLayout(Ptr<ConstantsLayout>)` 公开接口
- `ConstantsLayout` 改为 lazy 创建：首次 `AddShaderInput(constantDescriptor)` 时内部 `new ConstantsLayout()`
- `Finalize()` 和 hash 计算增加 null 保护
- `DrawShape.cpp` 移除手动创建/传入 `ConstantsLayout`

### 6. ShaderResource Entity 模型落地 — `Pass/Component/RHIComponents.h` + `Pass/Component/PassComponents.h` + `Pass/PassBuilder.h`

完成项：

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

### 6.5 SRG 是绑定容器，不是 render graph 节点 ⚠️

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

### [ ] 7. SRG 创建/销毁的 builder 接口

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

### [ ] 8. 接入 RenderGraph 编译流程 + RHIUpdateTag 消费者

**依赖**：第 7 项（SRG entity 已经能创建）

- `RenderGraph` 新增 `SetPipelineLibrary(RHI::PipelineLibrary*)` 接口
- `RenderSystem::InitInternal()` 传入 `m_rhiData.m_pipelineLibrary`
- `ExecutePipeline()` 在 topo sort 后、barrier 编译前依次调用：
  1. `m_compiler.FlushDirtyShaderResources()` — 见下
  2. `m_compiler.CompilePipelineStates()` — 已实现

新增 `RenderGraphCompiler::FlushDirtyShaderResources()`：

```cpp
void RenderGraphCompiler::FlushDirtyShaderResources()
{
    auto& ctx = *RHIExecuteContext::Current();
    ctx.GetView<RHIUpdateTag, BackingShaderResource>().each(
        [](RHIHandle e, BackingShaderResource& srg) {
            srg.m_shaderResource->Compile();
        });
    ctx.Clear<RHIUpdateTag>();
}
```

**注意 ordering**：必须在 `CompilePipelineStates` 之前——理论上 PSO 不依赖 SRG 数据，但 layout 验证可能依赖。保险起见放前面。

### [ ] 9. PipelineLayoutDescriptor 构建（在 PSO Compiler 里）

**依赖**：第 7 项

`RenderGraphCompiler::CompilePipelineStates` 当前没建 `PipelineLayoutDescriptor`。需要从 pass 的 `PassShaderResources.m_slots` 推：

```cpp
RHI::PipelineLayoutDescriptor BuildPipelineLayout(RHIHandle passEntity)
{
    auto& slots = ctx.Get<PassShaderResources>(passEntity).m_slots;
    RHI::PipelineLayoutDescriptor desc;
    for (uint32_t slot = 0; slot < slots.size(); ++slot) {
        if (slots[slot] == NullHandle) continue;
        // Layout 一定有，无论 concrete 还是 layout-only
        auto& layoutRef = ctx.Get<ShaderResourceLayout>(slots[slot]);
        desc.AddSrgLayout(slot, layoutRef.m_layout);
    }
    return desc;
}
```

注意：`PipelineLayoutDescriptor::AddSrgLayout` 接口名字是猜的，要看现有 RHI 接口实际叫什么。

### [ ] 10. RenderGraphExecuter 绑定 PSO + SRG

**依赖**：第 9 项

非 raw pass 在 execute 前：

```cpp
void RenderGraphExecuter::BindPassResources(RHIHandle passEntity, RHI::CommandList* cl)
{
    auto& ctx = *RHIExecuteContext::Current();

    // 1. 绑 PSO
    auto& pso = ctx.Get<PassCompiledPSO>(passEntity).m_pso;
    cl->SetPipelineState(pso.Get());

    // 2. 自动绑 concrete SRG（layout-only slot 跳过）
    auto& slots = ctx.Get<PassShaderResources>(passEntity).m_slots;
    for (uint32_t slot = 0; slot < slots.size(); ++slot) {
        if (slots[slot] == NullHandle) continue;
        if (auto* b = ctx.TryGet<BackingShaderResource>(slots[slot]))
            cl->BindShaderResource(slot, b->m_shaderResource);
        // else: layout-only slot, execute lambda 负责绑
    }
}
```

具体的 RHI 接口名（`SetPipelineState` / `BindShaderResource`）要看现有 RHI 实际定义。

### [ ] 11. TrianglePass 端到端用例

**依赖**：第 7-10 项

把 `SandBox/Program/DrawShape` 的三角形移植到 render graph：

- 创建一个 `TrianglePassFeature : ISystem`，在 Init 里：
  - 创建 ViewSRG entity（即使简单也走完整流程）
  - 创建 TrianglePass entity，链式 builder：`.VertexShader().FragmentShader().ShaderResource(0, viewSRGEntity).RenderTargetLayout(...).Build(...).Execute(...)`
- 在 OnFrameBegin 里更新 ViewSRG（即使 view 不变，走 Set + RHIUpdateTag 流程）

这是 PSO 路径 + SRG 路径的端到端验证用例，比 UI Pass（custom pipeline）更能暴露问题。

### [ ] 12. Smoke test

**依赖**：第 11 项

构建、运行，确认：
- UI Pass（custom pipeline 路径，无 PSO/SRG）正常显示
- TrianglePass（PSO + SRG 路径）正常显示三角形
- ViewSRG 数据每帧通过 batch Compile 正常上传到 GPU
- 没有 GPU validation error

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
- RHI ShaderResource：[Engine/Code/RunTime/Feature/RHI/Resource/ShaderResource/ShaderResource.h](Engine/Code/RunTime/Feature/RHI/Resource/ShaderResource/ShaderResource.h)、[Engine/Code/RunTime/Feature/RHI/Resource/ShaderResource/ShaderResourceLayout.h](Engine/Code/RunTime/Feature/RHI/Resource/ShaderResource/ShaderResourceLayout.h)
- RHI 限制：[Engine/Code/RunTime/Feature/RHI/RHILimits.h](Engine/Code/RunTime/Feature/RHI/RHILimits.h)（`ShaderResourceCountMax = 8`）
- ECS 上下文：[Engine/Code/RunTime/Feature/Render/Pass/RHIContext.h](Engine/Code/RunTime/Feature/Render/Pass/RHIContext.h)（`RHIExecuteContext::Current()` 取当前 RHIContext）
