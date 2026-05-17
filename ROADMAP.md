# SparkEngine Roadmap

Bevy 架构对比分析后的待办事项，按投入/收获比排序。

## 1. 从 SystemTraits 自动推导调度图

**现状：** `SystemTraits` + `ContextReference` 已实现编译期 component 访问声明（`ReadComponent<T>` / `WriteComponent<T>`），但 `ISystem::Request()` 仅做信息展示，`Engine::SetUp()` 仍手动硬编码 system 顺序。

**目标：** 收集所有 system 的 `ComponentAcquires` → 构建读写冲突图 → 拓扑排序 → 自动并行化无冲突 system。

**关键文件：**
- [SystemTraits.h](Engine/Code/RunTime/Core/ECS/SystemTraits.h)
- [ContextReference.h](Engine/Code/RunTime/Core/ECS/ContextReference.h)
- [ISystem.h](Engine/Code/RunTime/Core/ECS/ISystem.h)
- [Engine.cpp](Engine/Code/RunTime/Engine.cpp)

## 2. Plugin 机制

**现状：** 所有 system 在 `Engine::SetUp()` 中硬编码创建和初始化，模块边界靠手动顺序维护。

**目标：** 设计轻量 Plugin 基类，每个模块通过 plugin 自注册其 system、resource、event。App 通过 `AddPlugin<T>()` 组合。

**动机：** 便于学习实验——换 RHI backend、换一套渲染 pass、加/减一个功能模块都应该是一行调用。

## 3. 函数式 System 支持

**现状：** 每个 system 必须继承 `ISystem`，实现 `Init()` / `Shutdown()` / `InitInternal()` / `ShutdownInternal()`。

**目标：** 提供轻量 wrapper，让普通函数/lambda 也能注册为 system，降低写一个 system 的样板代码量。

## 4. Component Change Detection 从 Bus 切换到标记

**现状：** `ComponentEventBus` 全局广播 component 生命周期事件（Create/WillUpdate/Updated/Remove），所有 listener 都能收到。

**目标：** 借鉴 Bevy 的 `Changed<T>` / `Added<T>` 设计，在 component 层面加 dirty flag，system 仅查询自己关心的变更。

**优势：** 更精准、更高效，避免不必要的广播开销。

## 5. World Entity → DrawItem 提取管线 (Main World ↔ Render World 桥接)

**现状：** RHIContext 已有 `PassTag` + attachment 注册模式，但 World entity（Mesh / Material / Transform）和渲染 pass 之间没有桥接层。DrawItem（[DrawItem.h](Engine/Code/RunTime/Feature/RHI/Command/DrawItem.h)）已有数据结构，但未接入 ECS。

**目标：** World 中的 entity 通过 Extract 步骤转化为 RHIContext 中的 DrawItem entity，每个 DrawItem 挂对应 pass 的 PassTag。Pass 执行时通过 tag 筛选、排序、提交。

**核心流程：**

```
WorldContext (Entity)              Extract               RHIContext (RHIHandle)
┌─────────────────────┐          ═══════════►           ┌──────────────────────┐
│ Mesh                 │                                │ DrawItem              │
│ Material             │   per entity, per pass,        │   m_geometry (VB/IB)  │
│ Transform            │   per view 生成                │   m_pipelineStateKey  │
│ BoundingBox          │   1~N 个 DrawItem              │   m_stencilRef        │
│ DrawItemRefs (反向)  │                                │   m_sortKey           │
└─────────────────────┘                                │   m_viewMask          │
                                                       │ PassTag              │
                                                       │ ViewTag (可选)        │
                                                       └──────────────────────┘
```

**待细化事项：**

- **DrawItem 组件布局** — 在 RHIContext 中定义 `DrawItemComponent`（包裹 `RHI::DrawItem` 的几何/PSO 引用 + sort key + view mask），挂 `PassTag` 做路由。
- **Extract 调度** — Extract 作为 RenderSystem 中的一个步骤，在世界更新后、RenderGraph Build 前执行。初始可以每帧全量重建，后续配合 Roadmap #4（Change Detection）做增量。
- **Entity → DrawItem 反向索引** — World entity 上挂 `DrawItemRefs`（`eastl::vector<RHIHandle>`），entity 变更时沿索引更新对应的 DrawItem。DrawItem entity 生命周期和 World entity 绑定，避免每帧创建/销毁。
- **多 View 支持** — PassTag 不够时，DrawItem 加 `ViewTag` 或 `m_viewMask`（bitmask），查询时做交集筛选。
- **Pass 内排序** — execute function 中拿到 items 后按 pass 需求排序：opaque 按 PSO key 排序减少状态切换，transparent 按深度 back-to-front。排序键放在 DrawItem 组件中，由 extract 阶段或 compile 阶段填入。
- **静态合并** — 后续优化项。多个 static mesh entity 可以合并为一个 DrawItem（合并 VB/IB），在 extract 阶段识别 static tag 并执行。

**关键风险：**
- DrawItem entity 数量 = 可见物体 × pass × view，需确保 entity 持久化而非每帧重建
- 静态合并策略复杂度高（合并粒度、拆分条件），建议先用普通 DrawItem 跑通全管线再做
- 多 View 场景下 `(PassTag, ViewTag)` 的查询效率需关注
