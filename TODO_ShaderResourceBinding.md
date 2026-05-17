# ShaderResource 绑定方案重构

## 总览

两个核心改动：

1. SRG 绑定：Pass 用 `RHIHandle` 声明 PerPass SRG（executer 自动绑），用 `Ptr<Layout>` 声明 PerDraw SRG（lambda 绑）
2. DrawItem 提取：世界 entity 经提取管线产出 DrawItem entity，`DrawItemData` 只携带 per-draw 数据

## 当前设计的问题

### SRG 绑定

```cpp
.ShaderResource(slot, entity)   // entity 携带 layout + 可能的 instance
```

四个症结：

1. **Pass build 必须先有 entity** —— 即使 pass 完全不在乎 instance 是否就绪，也得提前创建 layout-only 占位 entity 来满足 API
2. **API 名字暗示传入 instance** —— `.ShaderResource(...)` 读起来像是 "传一个 SRG 实例"，跟 pass 实际只用 layout 不符
3. **Layout 1:N instance 的关系被 entity 隐式压成 1:1** —— PerMaterial 这种本质 1:N 的场景被迫造出"layout-only 占位 entity"假装 1:1
4. **dispatch 真相源（BackingShaderResource 存在性）隐藏在 entity 组件结构里** —— 用户读 pass 代码看不出哪个 slot 是 engine 绑、哪个 lambda 绑

### DrawItem

现有 `DrawItem` 直接抄自 Atom，把 per-pass（PSO）和 per-draw（SRG、VB/IB）数据混装在同一个 POD 里。PSO 不是 per-draw 对象，engine SRG（View/Scene）也不该每次 draw 提交一遍。

---

## Part 1: SRG 绑定

### 核心设计：按 1:1 vs 1:N 区分 slot 类型

PerPass 频率的 SRG 永远是 1 个 instance（ViewSRG、SceneSRG 全帧唯一），PerDraw 频率的 SRG 有 N 个 instance（Material 每个资产一个，DrawSRG 每个实体一个）。

| 频率 | Instance 数 | Pass 声明 | 绑定时机 |
|---|---|---|---|
| PerPass | 1（View/Scene/Lights 等） | `RHIHandle` (SRG entity) | Executer 自动绑 |
| PerDraw | N（Material/Draw 等） | `Ptr<ShaderResourceLayout>` | Lambda 手动绑 |

PSO 同理——per-pass 声明，executer 自动设。

### PassShaderResources 结构

```cpp
struct PassShaderResources
{
    // PerPass: SRG entity → executer 自动取 Components::ShaderResource 绑
    eastl::fixed_vector<RHIHandle,
                        Limits::Pipeline::ShaderResourceCountMax> m_perPassSlots;
    // PerDraw: layout → lambda 自己挑 instance 绑
    eastl::fixed_vector<Ptr<RHI::ShaderResourceLayout>,
                        Limits::Pipeline::ShaderResourceCountMax> m_perDrawLayouts;
};
```

### PassBuilder API

```cpp
// PerPass: 传 SRG entity (1:1)，executer 自动绑
// PerDraw: 传 Layout Ptr (1:N)，lambda 里绑
// PSO: 也属于 per-pass，声明后 executer 自动设

SPARK_RENDER_PASS(passCtx, "Main")
    .PipelineState(m_forwardPSO)                          // PSO → executer 自动设
    .ShaderResource(0, m_viewSRGEntity)                   // RHIHandle → PerPass 自动绑
    .ShaderResource(1, m_sceneSRGEntity)                  // RHIHandle → PerPass 自动绑
    .ShaderResource(2, m_materialLayout)                  // Ptr<Layout> → PerDraw lambda 绑
    .ShaderResource(3, m_perDrawLayout)                   // Ptr<Layout> → PerDraw lambda 绑
    .Execute([this](ExecuteWork& work, RenderGraphExecuter&)
    {
        auto& cmdList = *work.m_commandList;
        // PSO + ViewSRG + SceneSRG 已由 executer 绑好

        auto view = ctx.GetView<MainPassTag, DrawItemData>();
        view.each([&](const DrawItemData& item)
        {
            for (auto& srg : item.m_materialSrgs)
                cmdList.SetShaderResourceForDraw(*srg);
            if (item.m_perDrawSrg)
                cmdList.SetShaderResourceForDraw(*item.m_perDrawSrg);
            cmdList.SetVertexBuffer(item.m_vb);
            cmdList.SetIndexBuffer(item.m_ib);
            cmdList.DrawIndexed(item.m_args);
        });
    });
```

### PSO compile 如何取 Layout

两种 slot 都需要给 `PipelineLayoutDescriptor` 提供 layout：

- **PerPass slot**：从 entity 取 `Components::ShaderResourceLayout::m_layout`
- **PerDraw slot**：直接用 `Ptr<ShaderResourceLayout>`

`BuildPipelineLayoutDescriptor` 按 PerPass → PerDraw 的顺序拼接，保持 frequency ordering。

### Executer auto-bind

```
Pass-begin:
  SetPipelineState(pso)              ← per-pass
  for (entity : perPassSlots)
      srg = entity.Components::ShaderResource.m_shaderResource
      SetShaderResourceForDraw(*srg) ← auto-bind

Execute lambda:
  用户只写 per-draw SRG + 几何 + Draw
```

### Bind-time slot 解析

`ShaderResource` 持有 `GetLayout()` → `Ptr<ShaderResourceLayout>`。PSO compile 时 `PipelineLayoutDescriptor::Finalize()` 建立 `layout → slot/rootParamIndex` 映射。bind 时 state cache 查表：

```
SetShaderResourceForDraw(srg)
  → srg->GetLayout()
    → 当前 PSO 查 layout → slot index
      → m_srgsByIndex[slot] != srg ? bind : no-op
```

### SRG entity

**每个 SRG 实例都有对应的 entity**，compile 路径统一走 ECS sweep。

```cpp
m_viewLayout = factory.CreateShaderResourceLayout(viewLayoutDesc);
m_viewLayout->Init(*device);
m_viewSRG    = factory.CreateShaderResource(...);
m_viewSRG->Init(*m_viewLayout);

m_viewSRGEntity = ctx.CreateEntity();
ctx.Add<ShaderResourceTag>(m_viewSRGEntity);
ctx.Add<ResourceName>(m_viewSRGEntity, { "ViewSRG" });
ctx.Add<Components::ShaderResourceLayout>(m_viewSRGEntity, { m_viewLayout });
ctx.Add<Components::ShaderResource>(m_viewSRGEntity, { m_viewSRG });
```

Entity 的价值：

- **Dirty tracking**：`RHIUpdateTag` + `Components::ShaderResource` view → `CompileShaderResources` 统一 batch flush，compile 的唯一路径
- **PerPass auto-bind**：executer 从 entity 取 `Components::ShaderResource` 自动绑
- **ECS introspection**：调试器 / inspector 枚举所有 SRG 实例
- **Naming / 生命周期**：`ResourceName` + 统一 destroy

Feature system 同时持有 `Ptr<RHI::ShaderResource>`（自己绑 PerDraw 时用）和 `RHIHandle`（dirty 标记 + PerPass 传给 PassBuilder）。

### Compile 路径：统一 ECS sweep

没有 `srg->Compile()` 式自编译。所有编译统一走：

```
ctx.Add<RHIUpdateTag>(entity)
  → CompileShaderResources 扫 <RHIUpdateTag, Components::ShaderResource>
    → 收集所有 dirty ShaderResource* → ShaderResourceCompiler::Compiler(batch)
      → ctx.Remove<RHIUpdateTag>(entity)
```

1. **数据更新**（分散）：各 feature 在 `OnFrameBegin`/`OnTick` 调 `SetConstant`、`SetImageView`，然后 `Add<RHIUpdateTag>`
2. **编译**（集中）：`CompileShaderResources` 统一 sweep → batch compile → DX12 描述符拷贝
3. **绑定**（Ptr 直绑）：PerPass 由 executer auto-bind，PerDraw 由 lambda 调用 `SetShaderResourceForDraw`

### SRG 组件归宿

| 组件 | 角色 |
|---|---|
| `ShaderResourceTag` | 保留（识别 SRG entity） |
| `Components::ShaderResourceLayout` | 保留（持有 layout Ptr，PSO compile 取 layout 来源） |
| `Components::ShaderResource` | 保留（持 instance Ptr，compile view 数据源，auto-bind 来源） |
| `RHIUpdateTag` | 保留（dirty 标记） |
| `BackingShaderResource` | **删** —— compile view 改为 `<RHIUpdateTag, Components::ShaderResource>` |
| layout-only 占位 entity | **不存在** —— 全部 SRG entity 都持 instance |

---

## Part 2: DrawItem 提取管线

### 数据分层

```
Per-pass（executer auto-bind）:
  ├─ PSO (PipelineState)
  ├─ ViewSRG, SceneSRG, LightsSRG

Per-draw（lambda 绑）:
  ├─ Material SRG(s)     ← 材质相关
  ├─ Per-draw SRG        ← model matrix / instance 数据
  ├─ VB, IB
  └─ DrawArguments
```

### DrawItemData 组件

只包含 per-draw 数据，PSO 和 engine SRG 不进这里：

```cpp
struct DrawItemData
{
    DrawArguments    m_args;

    // --- Geometry ---
    VertexBufferView m_vb;
    IndexBufferView  m_ib;

    // --- Per-draw SRGs ---
    eastl::fixed_vector<Ptr<RHI::ShaderResource>, 2> m_materialSrgs;
    Ptr<RHI::ShaderResource> m_perDrawSrg;
};
```

### DrawItem entity

世界 entity 经提取管线产出 DrawItem entity，挂在 RHIContext 中：

```
世界 Entity ("PlayerMesh")
  ├─ Transform, MeshRef, MaterialRef

提取管线（Build 阶段）
  ↓ 决定该 mesh 在哪些 pass 可见

RHIContext Entity #100 (MainPassDrawItem)
  ├─ MainPassTag        ← pass 归属
  ├─ DrawItemData       ← VB/IB + per-draw SRGs
  └─ SourceEntity       ← 反向引用世界 entity

RHIContext Entity #101 (ShadowPassDrawItem)
  ├─ ShadowPassTag
  ├─ DrawItemData       ← Shadow 无材质 SRG
  └─ SourceEntity
```

变体匹配结果（PSO + SRG Ptr）缓存在 DrawItem entity 的组件上，跨帧复用。世界 entity 变化时通过 `SourceEntity` 反向引用重建。

### Execute 完整示例

```cpp
SPARK_RENDER_PASS(passCtx, "Main")
    .PipelineState(m_forwardPSO)
    .ShaderResource(0, m_viewSRGEntity)          // PerPass — executer 绑
    .ShaderResource(1, m_sceneSRGEntity)         // PerPass — executer 绑
    .ShaderResource(2, m_materialLayout)         // PerDraw layout
    .ShaderResource(3, m_drawLayout)             // PerDraw layout
    .Execute([this](ExecuteWork& work, RenderGraphExecuter&)
    {
        auto& cmdList = *work.m_commandList;
        // PSO + index 0~1 的 SRG 已由 executer 自动绑好

        auto view = ctx.GetView<MainPassTag, DrawItemData>();
        view.each([&](const DrawItemData& item)
        {
            for (auto& srg : item.m_materialSrgs)
                cmdList.SetShaderResourceForDraw(*srg);
            if (item.m_perDrawSrg)
                cmdList.SetShaderResourceForDraw(*item.m_perDrawSrg);
            cmdList.SetVertexBuffer(item.m_vb);
            cmdList.SetIndexBuffer(item.m_ib);
            cmdList.DrawIndexed(item.m_args);
        });
    });
```

### 完整场景流转

**Init / Asset Load:**

```
MaterialAsset 加载:
  m_materialLayout = factory.CreateShaderResourceLayout(pbrLayoutDesc);
  m_materialLayout->Init(*device);

  mat.m_srg = factory.CreateShaderResource(...);
  mat.m_srg->Init(*m_materialLayout);
  mat.m_srg->SetImageView(albedoSlot, albedoTex);   // 一次性写入
  mat.m_srg->SetImageView(normalSlot, normalTex);

  mat.m_srgEntity = ctx.CreateEntity();              // SRG entity
  ctx.Add<ShaderResourceTag>(mat.m_srgEntity);
  ctx.Add<Components::ShaderResource>(mat.m_srgEntity, { mat.m_srg });
  ctx.Add<Components::ShaderResourceLayout>(mat.m_srgEntity, { m_materialLayout });

MeshAsset 加载 + PSO 准备:
  m_psos.m_shadow  = ... 创建 Shadow PSO ...
  m_psos.m_forward = ... 创建 Forward PSO ...
```

**每帧 Build:**

```
Culling → Visibility 决定
  → Main pass 可见: 创建/复用 MainPassTag + DrawItemData entity
      → 填入 VB/IB, Material SRG Ptr, PerDraw SRG
  → Shadow pass 可见: 创建/复用 ShadowPassTag + DrawItemData entity
      → Shadow 无材质 SRG（m_materialSrgs 为空）

PerPass SRG 数据更新:
  m_viewSRG->SetConstant(viewProjOffset, &viewProj, sizeof(viewProj));
  ctx.Add<RHIUpdateTag>(m_viewSRGEntity);
```

**每帧 Compile:**

```
CompileShaderResources:     扫 <RHIUpdateTag, Components::ShaderResource> → batch compile
CompilePipelineStates:      PerPass entity → Components::ShaderResourceLayout + PerDraw Ptr<Layout>
                              → BuildPipelineLayoutDescriptor → 创建 PSO
CompileTransientResources:  分配暂存资源
CompileResourceBarriers:    per-pass barrier
```

**每帧 Execute:**

```
Executer:
  SetPipelineState(pso)
  for (entity : perPassSlots) → SetShaderResourceForDraw(*srg)
Lambda:
  扫 <MainPassTag, DrawItemData> → 绑 material SRG + 几何 → DrawIndexed
```

---

## Part 3: CommandList 状态缓存简化

### 当前问题

`CommitShaderResources` 是围绕 DrawItem/DispatchItem 的打包提交设计的**两阶段**模式：

```
阶段1: 把 item 里的 SRG assign 到 slot（SetShaderResource → m_srgsBySlot）
阶段2: 遍历 root params，从 m_srgsBySlot pull 出来逐 root param 绑定（m_srgsByIndex dedup）
```

新方案中 PSO、PerPass SRG、PerDraw SRG 分属不同频率，不再打包在同一个 item 里提交。需要把接口拆成独立的 public 函数。

### 当前状态缓存

```cpp
struct ShaderResourceBindings {
    const PipelineLayout* m_pipelineLayout;
    array<const ShaderResource*, Max> m_srgsBySlot;    // slot → SRG 分配（两阶段中间态）
    array<const ShaderResource*, Max> m_srgsByIndex;   // root param index → SRG（dedup）
    bool m_hasRootConstants;
};

struct State {
    const PipelineState* m_pipelineState;              // PSO dedup
    array<uint64_t, Max> m_streamBufferHashes;          // VB dedup
    uint64_t m_indexBufferHash;                         // IB dedup
    uint32_t m_stencilRef;
    // topology, viewport, scissor ...
    array<ShaderResourceBindings, 2> m_bindingsByPipe; // draw / compute
};
```

### 简化方案

**删除 `m_srgsBySlot`**：新模型直接绑定，`SetShaderResourceForDraw(srg)` 不再先 assign 到 slot 再 pull，而是一步到位查 `m_srgsByIndex` dedup → 真绑定。

**PSO 状态保留引用、去 dedup**：PSO 每 pass 设一次，同一 CMDBuffer 内不太可能连续设同一个 PSO。但 `m_pipelineState` 引用不能删——SRG 绑定时需要通过它拿到 `pipelineLayout` 做 `layout → slot` 映射。root signature 失效逻辑保留（PSO 变 → pipelineLayout 变 → 清空 `m_srgsByIndex`）。

**`RootConstants` 拆出独立函数**：不再跟 `CommitShaderResources` 耦合，由 lambda 单独调。

### 简化后的接口

```cpp
// RHI::CommandList 新增 public 接口（替代 CommitShaderResources）

// PSO — executer 每 pass 调一次
void SetPipelineState(const PipelineState& pso);

// SRG — executer 绑 PerPass，lambda 绑 PerDraw
void SetShaderResourceForDraw(const ShaderResource& srg);
void SetShaderResourceForDispatch(const ShaderResource& srg);

// Root constants — lambda 按需调
void SetRootConstants(const void* data, uint32_t size);
```

内部实现：每个函数自己做 state cache dedup，不再依赖外部传入的打包 item。

### 简化后的状态

```cpp
struct ShaderResourceBindings {
    const PipelineLayout* m_pipelineLayout = nullptr;
    // 只保留 dedup 数组
    eastl::array<const ShaderResource*, Limits::Pipeline::ShaderResourceCountMax> m_srgsByIndex;
};

struct State {
    const PipelineState* m_pipelineState = nullptr;  // 保留引用，去 dedup 分支
    array<uint64_t, Max> m_streamBufferHashes;        // 保留：VB dedup
    uint64_t m_indexBufferHash;                       // 保留：IB dedup
    uint32_t m_stencilRef;                            // 保留
    // topology, viewport, scissor — 保留
    array<ShaderResourceBindings, 2> m_bindingsByPipe;
};
```

### 对比

| 状态 | 处理 |
|---|---|
| `m_srgsBySlot` | **删** — 两阶段变直接绑定 |
| `m_srgsByIndex` | **保留** — SRG dedup，最有价值的状态 |
| `m_pipelineState` dedup 分支 | **去 dedup**，保留引用供 SRG 绑定查 layout |
| `m_pipelineLayout` + root sig 失效 | **保留** — PSO 变时清空 `m_srgsByIndex` |
| `m_hasRootConstants` | **保留** — 精简为 flag |
| `m_bindlessHeapLastIndex` | **删** — 已注释掉的 TODO |
| VB/IB/stencil/viewport 哈希 | **保留** — 轻量，per-draw 有效 |
| `CommitShaderResources` 模板 | **删** — 拆成独立 public 函数 |

---

## 改动清单

### RHI 层

| 文件 | 改动 |
|---|---|
| `RHI/Command/CommandList.h` | 新增 public: `SetPipelineState`, `SetShaderResourceForDraw`, `SetShaderResourceForDispatch`, `SetRootConstants`；删 `CommitShaderResources` 模板 |
| `RHI/Backend/DX12/Command/CommandList.h` | `m_srgsBySlot` 删；`CommitShaderResources` 删；新增各 public 函数 override |
| `RHI/Backend/DX12/Command/CommandList.cpp` | `SetPipelineState` (独立，去 dedup 分支)；`SetShaderResourceForDraw` (直接 dedup+绑定)；删 `CommitShaderResources` 实现 |
| `RHI/Command/DrawItem.h` | 后续废弃或重构成 `DrawItemData` owning 版本 |

### Render 层

| 文件 | 改动 |
|---|---|
| `Pass/Component/PassComponents.h` | `PassShaderResources` 拆为 `m_perPassSlots`（`fixed_vector<RHIHandle>`）+ `m_perDrawLayouts`（`fixed_vector<Ptr<Layout>>`）；新增 `PipelineState` 字段和 `DrawItemData` 组件 |
| `Pass/PassBuilder.h` | `.ShaderResource(slot, RHIHandle)` (PerPass) + `.ShaderResource(slot, Ptr<Layout>)` (PerDraw) 两个重载；新增 `.PipelineState(PSO)` |
| `RenderGraph/RenderGraphCompiler.cpp` | `BuildPipelineLayoutDescriptor` 处理两类 slot 分别取 layout；`CompileShaderResources` view 改为 `<RHIUpdateTag, Components::ShaderResource>` |
| `RenderGraph/RenderGraphExecuter.cpp` | 新增 pass-begin PSO + PerPass SRG auto-bind；删除旧的 `ExecuteWork::Item::m_shaderResources` |
| `Pass/Component/RHIComponents.h` | 删 `BackingShaderResource`；新增 `DrawItemData` |

| 文件 | 改动 |
|---|---|
| `Pass/Component/PassComponents.h` | `PassShaderResources` 拆为 `m_perPassSlots`（`fixed_vector<RHIHandle>`）+ `m_perDrawLayouts`（`fixed_vector<Ptr<Layout>>`）；新增 `PipelineState` 字段和 `DrawItemData` 组件 |
| `Pass/PassBuilder.h` | `.ShaderResource(slot, RHIHandle)` (PerPass) + `.ShaderResource(slot, Ptr<Layout>)` (PerDraw) 两个重载；新增 `.PipelineState(PSO)` |
| `RenderGraph/RenderGraphCompiler.cpp` | `BuildPipelineLayoutDescriptor` 处理两类 slot 分别取 layout；`CompileShaderResources` view 改为 `<RHIUpdateTag, Components::ShaderResource>` |
| `RenderGraph/RenderGraphExecuter.cpp` | 新增 pass-begin PSO + PerPass SRG auto-bind；删除旧的 `ExecuteWork::Item::m_shaderResources` |
| `Pass/Component/RHIComponents.h` | 删 `BackingShaderResource`；新增 `DrawItemData` |
| `RHI/Command/DrawItem.h` | 后续废弃或重构成 `DrawItemData` owning 版本 |

### SandBox / Sample 迁移

| 文件 | 改动 |
|---|---|
| `SandBox/Program/RHI/HelloTriangle.cpp` | 适配新 binding 模式 |
| `SandBox/Program/RHI/DrawShape.cpp` | 同上 |
| `SandBox/Program/RenderGraph/TrianglePassFeature.cpp` | TrianglePass 用新模式跑通端到端 |

---

## 已决议

### 1. PassBuilder 区分两类 slot

- **传入 `RHIHandle`**（SRG entity）→ PerPass → executer 自动从 entity 取 instance 绑
- **传入 `Ptr<ShaderResourceLayout>`** → PerDraw → lambda 挑选 instance 绑

PerPass 是 1:1 的，entity 引用天然正确无歧义。PerDraw 是 1:N 的，只传 layout 保持灵活性。两者的频率区分由参数类型自然表达，不需要额外的 frequency 枚举或 tag 组件。

### 2. PSO 也属 per-pass，executer 自动设

`.PipelineState(PSO)` 声明 → executer 在 pass-begin 调 `SetPipelineState`。不进 per-draw 循环。

### 3. SRG entity 是必须的，compile 统一走 ECS sweep

每个 SRG 实例都有 entity。编译扫 `<RHIUpdateTag, Components::ShaderResource>` view → 批处理，无自编译路径。Feature system 同时持有 `Ptr`（bind 用）和 `RHIHandle`（dirty 标记 + 传给 PassBuilder）。

### 4. DrawItem 提取为 entity，DrawItemData 只存 per-draw 数据

`DrawItemData` 只含 VB/IB、per-draw SRG、DrawArguments。不包含 PSO 和 engine SRG。变体匹配结果缓存在 DrawItem entity 的组件上，世界 entity 变化时通过 `SourceEntity` 反向引用更新。

### 5. Bind-time slot 解析

`ShaderResource::GetLayout()` → PSO compile 时 baked `layout → slot` 映射 → bind 时 state cache 查表。

### 6. Lambda boilerplate 短期不做

等真感到痛了再考虑，不进引擎核心。

### 7. CommandList 状态缓存简化

- 删除 `m_srgsBySlot`：两阶段 assign-then-pull 变为直接绑定
- 删除 `m_pipelineState` dedup 分支：PSO 每 pass 一次，去 dedup；保留引用供 SRG 绑定时查 layout
- 删除 `m_bindlessHeapLastIndex`：已注释掉的 TODO
- 删除 `CommitShaderResources` 模板：拆为独立的 `SetPipelineState`、`SetShaderResourceForDraw/Dispatch`、`SetRootConstants`
- 保留 `m_srgsByIndex`（SRG dedup）、VB/IB/stencil/viewport 哈希、root sig 失效逻辑

### 8. SRG entity 创建不需要 factory helper

手动写组件添加就几行，想抽 helper 是 feature system 内部的事。

---

### 9. StreamBufferView 重命名

`StreamBufferView` 跟流式上传无关，本质是 vertex buffer 绑定的轻量 POD（`Buffer*` + offset + count + stride）。重命名为 **`VertexInputView`**，和已有的 `VertexInput` struct 配套。

此改动独立于 SRG/DrawItem 重构，可随时单独提交。

---

## 不在本轮范围

- `DispatchItem` 重构（Compute pass 对应物，跟 DrawItem 一并处理）
- Per-frequency root signature ordering 优化
- Bindless 资源 / dynamic indexing
- `ShaderVisibility` 精细化（等 shader reflection 就绪后从 reflection 填充）

## 跟其它 TODO 的关系

- **依赖**：无新依赖
- **影响 / 解锁**：[TODO_DataDrivenRHI.md](TODO_DataDrivenRHI.md) 的 **T7 (TrianglePass 端到端)** —— TrianglePass 按新模式实现
- **解除 / 替代**：[TODO_DataDrivenRHI.md](TODO_DataDrivenRHI.md) 的 **T6 (SRG builder 接口)** —— 不需要单独的 factory 函数，T6 取消
