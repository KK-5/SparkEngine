# InstanceBindingSystem 落地方案

> 配合 [TODO_BindingFrequencyDesign.md](./TODO_BindingFrequencyDesign.md) §5 选定方案(正向 + 全局 structured buffer)落地。
> 本文档**是工作草稿**,未拍板项标 ❓,逐项推进 → 决定后转为"已定"。

---

## 0. 现状勘察(已有 / 已确认)

落地前先核对环境,以下能力**已具备**,不需要重新造:

- ✓ `RHI::ShaderBindings::FindBufferInput` + `SetView(BufferView*)` —— SRV(包括 StructuredBuffer)的绑定 API 现成
- ✓ `BufferViewDescriptor::CreateStructured(elementOffset, elementCount, elementSize)` —— StructuredBuffer view 一行创建
- ✓ `BufferPool::MapBuffer` 支持 host-visible(upload heap)持久映射 —— 首版用 mapped pointer 直接写,无需 staging
- ✓ `ViewBindingSystem` 是 plain helper(非 ISystem),由 `RenderSystem` 拥有 + `OnTick` 驱动 —— InstanceBindingSystem 直接复用此模式
- ✓ `EntityReaper.TickOrder = TICK_LAST`,RenderSystem 早于它(per-frame 重排下 Tick 顺序对正确性已无影响——`Update()` 用 `Exclude<DeadTag>` 过滤死亡实体,无论 reap 在它之前或之后跑都得到正确结果)

缺位 / 待核对:
- ✓ RHI 多顶点流(多 VB 槽 + `PER_INSTANCE_DATA` + step rate)能力**本来就在**,只是当前 demo 期间 `DrawRequest` 这个中间 bridge 只用了单 VB——**bridge 的具体形态后续单独定**,不在本设计的硬约束里
- ⚠ PSO `InputLayout` 描述能声明 `PER_INSTANCE_DATA` + `step rate` + `InputSlot` —— 待核对,可能已具备

---

## 1. 文件改动总览

| 操作 | 路径 |
|---|---|
| 新增 | `Engine/Code/RunTime/Feature/Render/Instance/InstanceData.h` |
| 新增 | `Engine/Code/RunTime/Feature/Render/Instance/InstanceSlot.h`(含 `InstanceSlot` + tags) |
| 新增 | `Engine/Code/RunTime/Feature/Render/Instance/InstanceBindingSystem.h/.cpp` |
| 新增 | `Engine/Asset/Shaders/InstanceData.hlsli` |
| 改写 | `Engine/Asset/Shaders/InstanceBindings.hlsl`(cbuffer → StructuredBuffer) |
| 改写 | `Engine/Asset/Shaders/DepthPre/DepthPre.hlsl`(`g_Model` → `GetInstanceData(vin.InstanceIdx).Model`,VS input 加 `INSTANCE_INDEX` 语义) |
| 改写 | `Engine/Code/RunTime/Feature/Render/RenderSystem.h/.cpp`(挂上 InstanceBindingSystem) |
| 改写 | `Engine/Code/RunTime/Feature/Render/Feature/DepthPre/DepthPreProcessor.h/.cpp`(主刀,见 §6) |
| 删除 | DepthPreProcessor 里 `DrawEntity` / `MatrixBindEntity` struct 及其使用 |
| 清理 | `Engine/Code/Editor/UI/Private/Inspector.h/.cpp` 中对 `DrawEntity` / `MatrixBindEntity` 的引用 |

> 注:**RHI 多顶点流能力已具备**,`DrawRequest` 是中间 bridge,本设计**不规定其字段形态**——InstanceBindingSystem 这边产出"mesh VB + ID buffer 两个顶点流"的语义,bridge 如何承载属 `DrawRequest` 后续单独定型的问题。同样地,PSO `InputLayout` 描述对 `PER_INSTANCE_DATA` 的支持是渲染层既有/迟早完整的能力,不在本设计 scope。

---

## 2. 数据类型(C++ / HLSL 单一定义)

**守则**:HLSL 和 C++ 各只有一个定义源,任何 pass / 系统 #include 同一份。

> ⚠ **已知技术债**:`ShaderBindings` 反射当前**只穿透 cbuffer 字段**(`ShaderInputConstant` 携带 byteOffset / stride),**不穿透 StructuredBuffer element struct 内部字段**——`ShaderInputBuffer` 只反射 binding 元信息(name / type / stride / register / space)。
>
> 正确的最终形态是扩展反射 + 提供 `SetStructuredField(bindings, "g_Instances", elemIdx, "Model", value)` 之类的 InputName 注入 API,InstanceBindingSystem 不持 C++ struct(与 cbuffer 同款)。
>
> 当前先走 **C++ 镜像 struct** 的折中:HLSL `InstanceData` 与 C++ `InstanceData` 静默 sync,`static_assert(sizeof == ...)` 兜底。**留 TODO 待 RHI 反射扩展完成后回来拆除 C++ struct**。

```cpp
// InstanceData.h
namespace Spark::Render
{
    struct InstanceData
    {
        Math::Matrix4 m_model;   // object -> world
    };
    // 64B,本身已 16B 对齐;后续加字段时按需处理 padding
}
```

```cpp
// InstanceSlot.h
namespace Spark::Render
{
    //! Per-frame slot 索引,挂在 world entity 上。**严禁跨帧缓存**——
    //! InstanceBindingSystem 实现可以每帧重排,消费者必须每帧重新 query。
    //! 见 §11 不可破坏契约。
    struct InstanceSlot         { uint32_t m_index = UINT32_MAX; };
    struct InstanceBindingTag   {};   // RHIContext 上的全局 g_Instances ShaderBindings 实体识别
    struct InstanceIDBufferTag  {};   // RHIContext 上的全局 ID buffer 实体识别(per-instance vertex stream,见 §2.5)
}
```

```hlsl
// InstanceData.hlsli  (所有需要 InstanceData 的 shader include 它)
#ifndef SPARK_INSTANCE_DATA_HLSLI
#define SPARK_INSTANCE_DATA_HLSLI
struct InstanceData
{
    float4x4 Model;
};
#endif

// InstanceBindings.hlsl(改写)
#ifndef SPARK_INSTANCE_BINDINGS_HLSL
#define SPARK_INSTANCE_BINDINGS_HLSL
#include "InstanceData.hlsli"

StructuredBuffer<InstanceData> g_Instances : register(t0, space1);

// 抽象层:索引由 VS 顶点输入传入(PER_INSTANCE_DATA 通路,见 §2.5),非全局变量
// 迁未来其它路径(例如同 mesh 合批的 indirection 表)时只改函数体,pass 无感
InstanceData GetInstanceData(uint instanceIdx) { return g_Instances[instanceIdx]; }
#endif
```

✓ **索引输送通道(已定)**:per-instance vertex stream + `StartInstanceLocation`,详见 §2.5。

---

## 2.5 InstanceIndex 输送通道 —— per-instance vertex stream

**目的**:每个 draw 把自己的 `InstanceIndex`(`InstanceSlot.m_index`)送进 shader,以便从全局 `g_Instances` 取自己的数据。本节走**硬件 instanced rendering 原生通路**(DX10 起即为此设计的 PER_INSTANCE_DATA + StartInstanceLocation),不发明新机制、不引入额外 root signature 占用、不引入 per-draw cbuffer / descriptor。

### 总体形态

```
slot 0:每个 draw 的 mesh VB        — PER_VERTEX_DATA      — POSITION / NORMAL / UV / ...
slot 1:全局共享 ID buffer           — PER_INSTANCE_DATA,step rate 1  — uint InstanceIdx
        内容 = [0, 1, 2, ..., kCapacity-1]
        InstanceBindingSystem.Init 时写入,Shutdown 销毁,期间永不改

每个 draw:
    DrawIndexedInstanced(
        IndexCount,
        InstanceCount         = 1,                        ← 未合批阶段固定 1
        StartIndex, BaseVertex,
        StartInstanceLocation = entity 的 InstanceSlot.m_index
    );

GPU IA 拉取(per-instance 拉取公式 buffer[StartInstanceLocation + instanceId/R]):
    slot1[slot.m_index + 0/1] = slot1[slot.m_index] = slot.m_index
    → shader VS 输入 InstanceIdx = slot.m_index
    → g_Instances[InstanceIdx] = 该实体数据
```

### HLSL 端形态

```hlsl
// InstanceBindings.hlsl(见 §2)
StructuredBuffer<InstanceData> g_Instances : register(t0, space1);
InstanceData GetInstanceData(uint instanceIdx) { return g_Instances[instanceIdx]; }

// 每个 pass shader 的 VS input
struct VSInput {
    float3 Position    : POSITION;        // slot 0,PER_VERTEX
    uint   InstanceIdx : INSTANCE_INDEX;  // slot 1,PER_INSTANCE
};

float4 VSMain(VSInput vin) : SV_POSITION {
    InstanceData inst = GetInstanceData(vin.InstanceIdx);
    return mul(float4(vin.Position, 1.0), inst.Model);
}
```

### 关键设计点

| 点 | 说明 |
|---|---|
| **ID buffer 一次创建永久持有** | InstanceBindingSystem.Init 写入 `[0,1,2,...,kCapacity-1]`,纯静态身份表。占 256 KB(kCapacity=65536) |
| **shader 端 InstanceIdx 来自 VS input** | 不是 cbuffer / root constant。每个 pass 自己声明 `INSTANCE_INDEX` 语义 |
| **`StartInstanceLocation` 是 DrawInstanced 原生参数** | 在 `m_drawInstanceArgs` 上直接设,零额外机制 |
| **零额外 root signature 占用** | 用 DrawInstanced 原生参数,不占 root constant DWORD,不增 CBV / SRV |
| **零 per-draw CBV / descriptor / binding 实体** | 相比早先 cbuffer 模拟方案,**没有**任何 per-instance binding 实体的对账问题 |
| **跨 mesh 工作** | 不同 draw 用不同 mesh VB(slot 0)和不同 StartInstanceLocation,但 ID buffer(slot 1)永远是同一个共享的 |

### 当前(InstanceCount = 1)→ 合批(InstanceCount = N)的演化

未来合批同 mesh 的 N 个实体到一 draw 时,`InstanceCount = N`、`StartInstanceLocation = base`,IA 自动给 N 个 instance 拉 `[base, base+1, ..., base+N-1]`。这要求**这 N 个实体在 `g_Instances` 里 slot 必须连续存于 `[base..base+N-1]`**——一个新的设计点,见 §10 ❓。**当前未合批,完全不撞这个问题**,方案对未合批阶段已自洽完整。

### 与 `DrawRequest` bridge 的边界

InstanceBindingSystem 这边的输出是**"两个顶点流的语义"**(slot 0 mesh VB + slot 1 全局 ID buffer)以及 `StartInstanceLocation` 值。**怎么把这两段语义携带到底层 CommandList** 是 `DrawRequest` / `DrawItem` 这一层的事——RHI 多顶点流能力本来就在,bridge 形态后续单独定型,不在本设计 scope。

DepthPre 在改造时按"两个顶点流 + StartInstanceLocation"的语义提交;具体 `DrawRequest` 字段叫什么、是 array 还是 fixed_vector 还是别的什么、上限多少,**由 `DrawRequest` 自己的设计周期决定**。

### 跨后端备注

DX12 / Vulkan / Metal 原生都支持多 vertex buffer slot + PER_INSTANCE_DATA + 等价的 `firstInstance` 参数。**所以本方案天然跨后端,无需任何宏分支**。

---

## 3. InstanceBindingSystem 骨架

```cpp
class InstanceBindingSystem
{
public:
    void Init(RHI::RHIContext& rhiCtx);
    void Update();
    void Shutdown(RHI::RHIContext& rhiCtx);

private:
    static constexpr uint32_t kCapacity = 65536;   // 固定上限,overflow 时 LOG_ERROR;后续 dirty-only / grow 等优化落地后这个上限自然不成问题(见 §10 Y4)

    // RHIContext 上的全局共享资源实体(§4 全局,所有 draw 共用)
    RHI::RHIHandle m_bufferEntity   = RHI::NullHandle;  // Components::Buffer  (upload heap StructuredBuffer<InstanceData>)
    RHI::RHIHandle m_bindingsEntity = RHI::NullHandle;  // Components::ShaderBindings (g_Instances @ space1)
    RHI::RHIHandle m_idBufferEntity = RHI::NullHandle;  // Components::Buffer  (per-instance vertex stream,内容 [0..kCapacity-1],见 §2.5)

    // 持久映射(upload heap,StructuredBuffer<InstanceData>)
    InstanceData* m_mapped = nullptr;
};
```

挂载点:`RenderSystem` 作为成员持有 `InstanceBindingSystem m_instanceBindingSystem`,在 `SetUpDefaultPipeline` 里 `Init`,在 `OnTick` 里 **`ViewBindingSystem.Update()` 之后、`Processors.Process()` 之前** 调 `Update()`。

---

## 4. 每帧 Update 流水

```
顺序:
  ViewBindingSystem.Update()
  → InstanceBindingSystem.Update()         ← 本节
  → ProcessorX.Process()                   ← 此时 g_Instances 已就绪,DrawRequest 可放心引用
  → RenderGraph.ExecutePipeline()
```

### 形态:一段 view 遍历搞定

```cpp
void InstanceBindingSystem::Update()
{
    auto* world = WorldExecuteContext::Current();
    if (!world || !m_mapped) { return; }

    // 先清掉上一帧的 InstanceSlot——slot 不跨帧稳定(§11 契约),上一帧的值这一帧
    // 已经无意义,不清掉会让"已死亡 / 已变非 renderable"的实体留下 stale slot。
    // 契约保证:Update 跑完后,InstanceSlot 存在 ⟺ 本帧 renderable。
    world->clear<InstanceSlot>();

    uint32_t idx = 0;
    world->GetView<Transform::WorldTransformMatrix, Mesh::MeshGPUComponent>(Exclude<DeadTag>)
        .each([&](Entity e, const Transform::WorldTransformMatrix& m, const Mesh::MeshGPUComponent&)
    {
        if (idx >= kCapacity) { LOG_ERROR("[InstanceBindingSystem] kCapacity={} overflow",kCapacity); return; }

        // 写当前帧 transform 到 g_Instances 的当前 slot
        m_mapped[idx].m_model = m.m_worldMatrix;

        // 当前帧 slot 挂回 entity,供下游 pass 在 Process 阶段 query
        // 用 emplace 而不是 emplace_or_replace —— 上面 clear 已经保证不存在
        world->emplace<InstanceSlot>(e, InstanceSlot{idx});
        ++idx;
    });
}
```

**就这么多**——没有 freelist、没有 reap phase、没有 DirtyTag、没有 destroy 反向对账。每帧 clear + view 一遍,scatter 写 buffer,挂 slot。

❓ **谓词是否要更广**:目前用 `WorldTransformMatrix + MeshGPUComponent`,排除了 camera/light 等。但 light 未来也可能需要 instance 数据。先按当前最小集做,见 §10 G1。

### 跨帧数据的范式(motion vector 等)

未来需要跨帧数据(典型例:motion vector 需要"上一帧 transform")时,**通过 entity 上的额外 component 表达**,**不依赖 slot 跨帧稳定**——谁需要谁加 component,InstanceBindingSystem 在当前 entity 上读 component 写当前帧的 InstanceData 对应字段:

```cpp
struct PrevWorldTransformMatrix { Math::Matrix4 m_matrix; };

// 上面的 Update 里多两行:
m_mapped[idx].m_prevModel = world->TryGet<PrevWorldTransformMatrix>(e)
                              ? world->Get<PrevWorldTransformMatrix>(e).m_matrix
                              : m.m_worldMatrix;
world->emplace_or_replace<PrevWorldTransformMatrix>(e, m.m_worldMatrix);
```

**全程不涉及 slot 跨帧稳定性**。

### SRV view 不需要每帧标 dirty

`g_Instances` 的 SRV view 在 Init 后**永远不变**(buffer 句柄不变,desc 不变)。**buffer 内容每帧改不影响 SRV view**,所以 `ShaderBindingsUpdateTag` **只在 Init 后首帧打一次**,之后永不再打——`m_bindingsEntity` 上不需要任何 per-frame 维护。

---

## 6. DepthPreProcessor 改造点

### 拿掉(随旧 Combo 1 / retained DrawRequest 模型一起退役)

```cpp
struct DrawEntity { ... };                          // 删
struct MatrixBindEntity { ... };                    // 删
CreatePassShaderBindings<...>(1);                   // 删(不再 per-instance 建 binding)
SetShaderConstant(modelEntity, "g_Model", ...);     // 删
world->Add<DrawEntity>(...);                        // 删
world->Add<MatrixBindEntity>(...);                  // 删
world->Add<SPARK_PASS_TAG("DepthPrePass")>(...);    // 删(per-frame DrawRequest,不再 retained 在 world entity 上)
Exclude<SPARK_PASS_TAG("DepthPrePass")>             // 删(同上)
```

### 新形态:只看 GPU 层组件

```cpp
void Process(const Math::Vector2Int& renderSize)
{
    auto* world   = WorldExecuteContext::Current();
    auto* rhiCtx  = RHI::RHIExecuteContext::Current();
    auto* passCtx = PassExecuteContext::Current();
    if (!world || !rhiCtx || !passCtx) { return; }

    // 1. 拿全局共享实体(view binding + g_Instances binding + ID buffer)
    RHIHandle viewBindingEntity     = /* MainViewTag */ ...;
    RHIHandle instanceBindingEntity = /* InstanceBindingTag */ ...;
    RHIHandle idBufferEntity        = /* InstanceIDBufferTag */ ...;

    // 2. 遍历"本帧 GPU 可渲染"的实体——谓词只看 GPU 层组件
    //    InstanceSlot 存在已经隐含:本帧 renderable(§11 契约)
    //    DepthPre 不接触 WorldTransformMatrix 等世界层概念
    world->GetView<Mesh::MeshGPUComponent, InstanceSlot>().each(
        [&](Entity, const Mesh::MeshGPUComponent& gpu, const InstanceSlot& slot)
    {
        if (gpu.m_vertexBuffer == RHI::NullHandle) { return; }

        DrawRequest req;
        // ... mesh VB / index buffer info / viewport / scissor(同前) ...

        // ShaderBindings:两个共享 binding(无 per-draw)
        req.m_shaderBindingEntities.push_back(viewBindingEntity);       // space0
        req.m_shaderBindingEntities.push_back(instanceBindingEntity);   // space1 (g_Instances)

        // 顶点流语义:slot 0 mesh VB + slot 1 全局 ID buffer
        //   InstanceBindingSystem 只产出"两段语义",DrawRequest bridge 怎么承载是它自己的事(§2.5)
        // req.AddVertexStream(slot=0, gpu.m_vertexBuffer, PER_VERTEX);
        // req.AddVertexStream(slot=1, idBufferEntity,    PER_INSTANCE, stepRate=1);

        // InstanceCount=1,StartInstanceLocation = entity 的本帧 slot
        req.m_drawInstanceArgs = RHI::DrawInstanceArguments(
            /*InstanceCount*/         1,
            /*StartInstanceLocation*/ slot.m_index);   // ← InstanceIndex 进入 shader 的入口

        // 把 DrawRequest 提交给 RenderGraph(per-frame transient)
        ...
    });
}
```

### 关键认知

- DepthPre **零世界层依赖**:`WorldTransformMatrix` / `Transform` 系统这些概念,DepthPre 完全不知道
- DepthPre **零跨帧状态**:不在 world entity 上挂 `DepthPrePassTag` 之类 retained 标签;每帧从零开始,生成 transient DrawRequest
- `InstanceSlot` 是 DepthPre 和 world 之间**唯一的耦合点**,而且它本身就是 GPU 层概念——干净

---

## 7. Shutdown 路径

```cpp
void InstanceBindingSystem::Shutdown(RHI::RHIContext& rhiCtx)
{
    if (m_mapped)
    {
        // UnmapBuffer via BufferPool
        m_mapped = nullptr;
    }
    if (m_bindingsEntity != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_bindingsEntity); }
    if (m_bufferEntity   != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_bufferEntity); }
    if (m_idBufferEntity != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_idBufferEntity); }
}
```

❓ **退出时 world entity 上的 `InstanceSlot` 组件**:不显式清除问题不大(随 world 整体销毁),但要确认顺序:`RenderSystem.Shutdown` 之后再 world destory 是否安全。

---

## 8. 测试 / 验证策略

落地后必须验证:
1. DepthPre 输出深度图与改造前**逐像素一致**(用 RenderDoc / 截屏对比)
2. 实体动态生灭:create + destroy 一个 mesh entity,**当前帧 slot 自动跟随**(per-frame rebuild 天然支持)
3. capacity overflow(超出 4096)有 ASSERT,不静默
4. Shutdown 不泄漏:`Components::Buffer` / `Components::ShaderBindings` / ID buffer 实体在 reap 后真的消失

---

## 9. 实施分步(原子提交)

```
Step 1: 数据结构 + HLSL
  - 新增 InstanceData.h / InstanceSlot.h / InstanceData.hlsli
  - 改写 InstanceBindings.hlsl(暂时无 SRV,先放着不动也 OK,主要确认 #include 链)
  - 验证:编译过

Step 2: InstanceBindingSystem 骨架 + Init/Shutdown
  - 实现 Init(创建 buffer + binding 实体 + Map)、Shutdown
  - 在 RenderSystem 里挂上,Update 暂留空
  - 验证:启动 / 退出无崩溃,RenderDoc 能看到 buffer

Step 3: Update 一段 view 遍历
  - 实现 §4 那一段 Update():per-frame 全量重排 slot + 写 m_mapped + emplace_or_replace InstanceSlot
  - 验证:用日志确认每帧 slot 被正确赋值、数据写入 mapped buffer

Step 4: DepthPre 改造
  - 改 shader(InstanceData.hlsli + 多顶点流 VS input)
  - DepthPreProcessor 谓词改为 `MeshGPUComponent + InstanceSlot`,删 `WorldTransformMatrix`/PassTag/Exclude 全套(见 §6)
  - 改 DrawRequest 构造(挂 ID buffer 第二顶点流、`StartInstanceLocation = slot.m_index`)
  - 删 MatrixBindEntity / DrawEntity 整套
  - 验证:DepthPre 输出深度与改造前一致

Step 5: 清理
  - 清掉 Inspector.cpp 的旧引用
  - 清掉 DepthPreProcessor 的旧字段
  - 更新 TODO_BindingFrequencyDesign.md §当前实现状态
```

---

## 10. 待拍板(❓ 清单)

按优先级排:

### 🔴 阻塞项(不解决无法开工)

**当前无阻塞项**。

> 已剔除的历史阻塞项(供决策回溯):
> - ~~Root constant 通道~~:这是 RHI 抽象的内部实现选择(per-draw 常量推送有多种后端机制),不属于 InstanceBindingSystem 的设计阻塞。RHI 接口能力如不足,作为 RHI 任务推进,与本系统解耦。
> - ~~EntityReaper Tick 顺序~~:已确认 `EntityReaper.TickOrder = TICK_LAST`,RenderSystem 早于它;且 per-frame 重排下 `Exclude<DeadTag>` 已让 Tick 顺序对正确性无影响。

### 🟡 影响设计,但有降级方案

- ~~**Y1. DirtyTag 策略**~~(已定):**走 per-frame 全量重排**(§4),无 DirtyTag、无 freelist、无 reap phase;契约:`InstanceSlot` 是 per-frame 值(见 §11)。
- ~~**Y2. SRV dirty 标记是否必要**~~(已定):**不需要**。SRV view 在 Init 后永远不变(只换内容不换 view),`ShaderBindingsUpdateTag` 只在 Init 后首帧打一次,之后永不再打。
- ~~**Y3. ObjectId 的语义**~~:首版**不加 ObjectId**——无现成消费者(无 picking / 无 GPU 剔除 / 无 outline 等),按"不预先加字段"原则待真实需求出现时再加。届时正好作为"加字段成本 = O(1)"的架构验证点。
- ~~**Y4. 容量上限**~~(已定):**固定 `kCapacity = 65536`**,overflow 时 `LOG_ERROR` + 丢弃后续实体(不阻塞渲染)。理由:Model-only 时 g_Instances 仅 4 MB + ID buffer 256 KB,极其轻量;典型场景几千实体,留 ~10x 余量;真撞顶时是优化信号(dirty-only / grow / GPU-driven 等方案落地后这个上限自然不再是问题)。

### 🟢 待定,不影响开工

- **G1. 谓词广度**:目前 `WorldTransformMatrix + MeshGPUComponent`,未来 light 等需要扩。
- **G2. n-buffering**:首版单 upload heap,CPU 写 m_mapped 时 GPU 可能仍在读上一帧的 g_Instances,理论存在帧间读到错 transform 的 race(per-frame 全量重排下每个 slot 都可能换实体,概率比稳定 slot 方案高)。需要 n-buffer(每帧一份)或 fence 同步。小场景大概率观察不到,但是个待定的正确性项,规模上来前必须做。
- **G3. 同 mesh 合批时的 slot 连续性策略**(未来,合批阶段才撞上):走 per-instance vertex stream(§2.5)后,`InstanceCount=N + StartInstanceLocation=base` 要求合批的 N 个实体在 `g_Instances` 里**slot 连续 [base..base+N-1]**。两条候选路径:
  - **(a) 合批时重排**:扫描可见同 mesh 实体,把它们的 InstanceData 临时拷一份到一个连续 transient buffer,这一帧用 transient buffer 当 g_Instances 源
  - **(b) Indirection 表**:同 mesh 的实体的"实际 slot 号"放在一张 transient indirection 数组里,shader 里多查一层(UE5 GPUScene `InstanceCullingLoadBalancer` 同款)
  - 当前未合批,**完全不撞**这个问题;留作合批阶段开工前的决策点
- **G4. Init 时需要的 hlsl reflection host**:ViewBindings 走 `ViewBindingsReflect.hlsl` dummy entry 反射。InstanceBindings.hlsl 同样需要一个 reflect host 还是已有?
- **G5. RHI 反射穿透到 StructuredBuffer element 字段**(技术债,见 §2 ⚠):扩展后回来拆除 C++ `InstanceData` 镜像 struct,改走 InputName 注入(与 cbuffer 同款)。  
  落地分两步:① 扩 DXC 反射读 element struct 字段表 → ② 扩 `ShaderInputBuffer` API + 添加 `SetStructuredField` 注入接口。

---

## 11. 设计原则回引(避免飘移)

落地过程中保持以下原则,如果某个具体决定违反它们,先停下来 reconsider:

- 不在 RHI 层引入 render-layer 概念(`Attachment` / `Pass` 等)
- HLSL `InstanceData` struct **唯一定义源**(`InstanceData.hlsli`),严禁复制粘贴(见 [TODO_BindingFrequencyDesign.md](./TODO_BindingFrequencyDesign.md) §10 schema 扩展策略)
- update 由源组件 dirty 触发,不按 visibility 筛选(见 [TODO_BindingFrequencyDesign.md](./TODO_BindingFrequencyDesign.md) §6)
- 不为字段扩展预留 padding "防御性占位"(只为对齐做必要 padding)

### 不可破坏契约:`InstanceSlot` 是 per-frame 值

> 这是本系统对外暴露的**唯一契约**。一切实现层的优化(每帧重排 / dirty-only / 静态分区 / GPU 端 update / ...)都必须保持这个契约不变。**任何下游代码不得假设 `InstanceSlot.m_index` 跨帧稳定**。

具体含义:

- **存在性 ⟺ 本帧 renderable**:`InstanceBindingSystem.Update()` 跑完后,`world->view<InstanceSlot>()` 的成员**恰好等于本帧的可渲染实体集合**,无遗漏、无残留。
  - 保证机制:Update 开头 `world->clear<InstanceSlot>()`,然后按 renderable 谓词重新 emplace。
  - 含义:消费者 `GetView<MeshGPUComponent, InstanceSlot>` 拿到的就是干净的渲染集,**不需要再过滤 `DeadTag` / `WorldTransformMatrix` 等**
- **值不跨帧稳定**:`slot.m_index` 是 per-frame 的,跨帧变化任意。消费者**每帧**在 Process 阶段 `world->Get<InstanceSlot>(e)` 拿值,**绝不**把 slot 索引缓存到组件 / member / 静态表
- **跨帧数据通过别的 component 显式存**:motion vector 的 prev transform、可见性时序等,挂独立 component(范式见 §4),不依赖 slot 稳定
- **实现可变,契约不变**:InstanceBindingSystem 内部以后可能改 dirty-only / 静态分区 / GPU 端 update,但上述契约不动,下游零感知
