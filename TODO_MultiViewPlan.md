# 多 View 体系设计（View 实体化 + Pass per-view 循环）

## 背景与问题

当前 View 是「一个数据结构 + 一个单例 SRG」：

- `View`（`Feature/Render/View/View.h`）只有 `m_worldToView` / `m_viewToClip` / `m_exposure`。
- `ViewBindingSystem::Init` 建**一个** space1 SRG 实体，打上 `MainViewTag`；`Update` 每帧从主相机刷新它。
- pass 通过 `.Binds<MainViewTag>()` 拿到它，`ResolveSharedBinding` 把它塞进该 pass 每个 DrawItem 的
  `m_shaderBindings`。

`ViewBindingSystem.cpp:101` 自己写着「multi-view will tag a binding **per view type**」——问题正在这句上：
**per view type 不够，得 per view instance**。shadow 的 N 个视角是同一个 type；分屏的两个视角也是。

`MainViewTag` 现在同时承担了两件事：**「这是哪类 view」**（类型）和**「这是那个唯一的 SRG 实体」**（单例定位）。
`ResolveSharedBinding` 的注释 "a global singleton per tag" 就是后者的直白表述。要多 View，必须把这两件事拆开。

### 触发用例与硬约束

直接触发是 ShadowPass（方向光 1 view / 聚光灯 1 view / 点光源 6 view，运行时数量）。但**不为 shadow 单独设计**——
多视口（编辑器视口、反射探针、cubemap 捕获）都通向同一处，View 是地基，越晚动越贵。

已确认的机制约束（都实测/读码确认过，不是推测）：

- `Submit(DrawItem)`（`Backend/DX12/Command/CommandList.cpp:472-478`）会遍历 `drawItem.m_shaderBindings`
  逐个 `BindShaderInputsForDraw`。**同 space 后绑覆盖前绑**——所以若 N 个 view SRG 都打同一 tag 走
  `.Binds<>()`，每个 draw 会拿到全部 N 个、最终只有最后一个生效（画出来全是同一个视角）。
- `BindPassDrawItems`（`Drawable/DrawItemBind.h:68-74`）**每帧无条件**把每个 DrawItem 的 viewport/scissor
  写成全屏 `renderSize`；`CommandList.cpp:461-466` 见 `m_viewportsCount != 0` 就覆盖 command list 上的设置。
- `CommandList` 有 `SetViewports` / `SetScissors`（`RHI/Command/CommandList.h:51-54`），execute 中途可改。
- root constant：layout 侧已实现（`Backend/DX12/Pipeline/PipelineLayout.cpp:44-57` 会建 root parameter），
  但 CommandList 侧的设值路径被移除（`Backend/DX12/Command/CommandList.h:23` "SetRootConstants removed"）。
  `DrawItem::m_rootConstants` 是给它留的位。**本方案不依赖它**。

### 改造面

`MainViewTag` 全仓库 12 处引用：5 处 `.Binds<MainViewTag>()`（DepthPre / GBuffer / Lighting / Skybox / Tonemap）、
2 处 `Visible<MainViewTag>`（恒真占位）、1 处 `ViewBindingSystem` 打 tag，其余是注释。**比预期小。**

---

## 一、核心决策

**View 实体化：每个 view 是 RHIContext 里的一个实体，带 `View` 组件 + 一个 view 类型 tag，并引用自己的 space1 SRG 实体。**

`MainViewTag` **语义提升**为类型标记（「所有主视角 view」），不新增 role tag：今天集合大小是 1，行为不变；
分屏时是 2。5 处 pass 声明的 tag 名一个字都不用改。

| 维度 | 性质 | 载体 |
|---|---|---|
| view 的**类型** | 编译期可知 | tag：`MainViewTag` / `ShadowViewTag` / 将来 `ReflectionViewTag` |
| view 的**身份** | 运行时，数量可变 | 实体：`View` 组件 + `ViewShaderBindings` + 类型 tag |

`GetView<MainViewTag, View>()` 返回 1 个（单相机）或 2 个（分屏）；`GetView<ShadowViewTag, View>()` 返回 N 个。
**数量差异不再需要不同机制表达。**

### 组件划分：View 实体与 SRG 实体分开

| 实体 | 组件 |
|---|---|
| **View 实体** | 类型 tag（`MainViewTag` / `ShadowViewTag`）、`View`、`ViewShaderBindings { RHIHandle }` |
| **SRG 实体** | `Components::ShaderBindings`（+ 瞬时 `ShaderBindingsUpdateTag`），**不打任何 view tag** |

SRG 不与 View 同实体：ShaderBindings 实体是「一份不带语义的 shader 输入，靠 tag 被 pass 选中」，pass 不需要
理解它的内容；View 数据是语义。分开保持这一层的干净。附带一个结构性好处：SRG 实体不打 view tag，
`ResolveSharedBinding<MainViewTag>` 就找不到它，第二节「view SRG 移出 `.Binds<>` 路径」由结构保证而非靠约定。

引用方向是 View → SRG（循环是 view 优先的，反向要扫）。编码那一步是
`for each (View, ViewShaderBindings) → WriteViewConstants(view, handle)` —— `WriteViewConstants`
（`View/View.h:66`）本来就收 `(const View&, RHIHandle)` 两个参数，一行不用改。产生 view 的系统只写
`View` 组件，不碰 SRG。

**生命周期**：view 实体销毁时，顺着 `ViewShaderBindings` 一并销毁它的 SRG 实体。SRG 池化（回收进空闲池
而不是销毁）是后期优化，先不做。

## 二、Pass 声明

```cpp
.Accepts<OpaqueTag>()                        // 变参：消费哪几类 Drawable
.Binds<MaterialBindingTag, MainSceneTag>()   // 变参：这个 pass 要哪些共享 SRG
.RendersView<MainViewTag>()                  // 单个：为哪一类 view 循环
.SortBy<DrawSortMode::BackToFront>()         // 可选：组内排序策略，不写 = None
```

`RendersView` 是**单参数、不是变参包**——一个 pass 同时为主视角和 shadow view 渲染讲不通（attachment、
PSO、RT layout 全不同）。签名写死成单参数，让编译器替这条约束把关；单数名也让它和旁边两个变参声明
一眼可辨。

### SRG 归属变化

`Binds<>` 的**语义**变了：从「把这些 SRG 塞进该 pass 每个 DrawItem 的 `m_shaderBindings`」变成
「这个 pass 的 DrawList 要绑哪些 SRG」。声明写法不变，注入的落点从 per-draw 变成 per-list。

| SRG | 现在 | 之后 |
|---|---|---|
| **view (space1)** | `ResolveSharedBinding<MainViewTag>` → 塞进每个 DrawItem | → **DrawList 字段**，每组绑一次 |
| **per-pass (space2)** | `ResolveSharedBinding<PassTag>`（`DrawItemBind.h:46`） | → **DrawList**，每组绑一次 |
| **material (space3) / scene (space0)** | `.Binds<>` → 塞进每个 DrawItem | → **DrawList**，每组绑一次 |
| per-object (space4) | `DrawItemObjectBinding` | **不动**，真正 per-draw |

现状是每个 DrawItem 的 `m_shaderBindings` 里装 4 个指针、其中 **3 个对全 pass 相同**，且
`BindPassDrawItems` 每帧 `clear()` + 4 次 `push_back` 重建一遍。全部移到 DrawList 之后：

- `fixed_vector<const ShaderBindings*, 8>` → 单指针（per-object 那一个）
- 每帧的 clear + push_back **消失**，骨架这一项变成真只读
- `Submit` 内从 4 次 `BindShaderInputsForDraw` 降到 1 次 —— M 个 draw 省 **3M 次**绑定调用

## 三、执行：所有 pass 统一成 per-view 循环

概念上每个 pass 都是「为它声明的每个 view 各画一遍」：

```
for view in GetView<ViewTag, View>:
    绑 view 的 SRG、设 view 的 viewport
    提交对该 view 可见的 DrawItem
```

**主视角 pass = 循环次数 1 的特例；分屏 = 2；shadow = N。shadow 不再是特殊路径。**

这个循环的**实际载体是 DrawList**（§三·五）——「一轮循环」就是「一个 DrawList」，view 级状态是它的字段
而不是循环变量。DrawItem 在其中**全程只读**，变的只是 command list 状态。

DrawItem 不下探到 view：下探会让骨架数 ×N（16 个投影光源 × 1000 Drawable = 16000 份），且光源增删就要重建，
直接废掉 `TODO_DrawItemPersistencePlan.md` 的骨架持久化。

> 对照 Atom：**view 维度上两边没有分歧**，都是共享 DrawItem + 列表存引用——`View::DrawList` 存的是
> `DrawItemProperties{const DrawItem*, sortKey, depth}`。真正的分歧只在 pass 类型这一维（§三·五末）。
> pass 层的差异是 Atom 的 `CascadedShadowmapsPass` / `ProjectedShadowmapsPass` 运行时
> `CreateChildPassesInternal()` 创建 N 个 child pass、各自关联一个 `RPI::View`，因为它的 Pass 是运行时对象树。
> （Atom 相关内容为理解，未逐行核对源码。）

## 三·五、DrawList 机制

### 结论反转：从「暂缓」到「必须做」

一度的结论是暂不做 CPU DrawList，理由是「execute 直接扫 ECS 表达不了的三件事（per-view 剔除子集、per-view
排序、可切片区间）都会被 GPU-driven 逐条拿走」。

**这个前提破了。** 多出第四条 GPU-driven **拿不走**的：

> `ExecuteIndirect` 一次调用内**所有 draw 必须共享同一个 PSO**。`D3D12_INDIRECT_ARGUMENT_TYPE` 不含
> 「换 PSO」这一项（Vulkan 要 `VK_NV_device_generated_commands` 那类扩展），所以**按 PSO 分组是硬约束**，
> 不是 CPU 实现的产物。

分组这件事无论如何都得有地方做，那就是 DrawList。原先「(view, pass) 是配对单位」的说法也要修正为
**(view, pass, PSO组)**。

### DrawItem 退化成 per-object

`TODO_DrawItemPersistencePlan.md` 第一节给 per-pass DrawItem 的理由**只有一条**：

> PSO per-pass 不同 → DrawItem 必须 per-pass 一个

DrawList 按 PSO 分组之后，PSO 上升成组级状态，**这条唯一的理由消失**。逐字段检查后 DrawItem 里不再有任何
pass 相关的东西：

| 字段 | 归属 |
|---|---|
| VB / IB view、draw arguments、per-object SRG、startInstance | **物体** → 留在 DrawItem |
| PSO | → DrawListEntry |
| viewport / scissor、view SRG、共享 SRG | → DrawList |
| stencil ref | pass 级 → DrawList |

于是 **DrawItem 与 Drawable 变成 1:1**，应当是同一实体上的两个组件（render 层 `Drawable` + RHI 层 `DrawItem`），
不再是独立实体。连带简化：

- DrawItem 数量从 `Drawable × pass` 降到 `Drawable`
- **`DrawItem` 上不再打 `PassTag`**，`DrawItemRoute::m_marks` 和 `MarkPassTag<PassTag>` 整个删除
- `DerivedDrawItems` 反向引用不再需要（1:1，同实体）
- `DrawItemRoute::m_accepts` 语义从「派生一个 DrawItem」变成「把这个 Drawable 收进我的 DrawList」
- `BindPassDrawItems` 里剩下的 `startInstance` 更新不需要按 pass 分，改成一趟全局
  `GetView<DrawItem, DrawItemInstanceSlot>()` 密集遍历

「DrawItem 完备性」这场纠结也随之消失：DrawItem 里只剩物体数据、没有任何「怎么画」的状态，无所谓完备与否。

### 数据结构

```cpp
struct DrawListKey
{
    RHIHandle                 m_view;   // view 实体；NullHandle = 该 pass 不按 view 分
    const RHI::PipelineState* m_pso;    // 分组模式下组内一致；排序模式下为空
};

struct DrawListEntry
{
    RHIHandle                 m_item;     // DrawItem 实体，不存指针（见下）
    const RHI::PipelineState* m_pso;      // (物体 × pass) 合成的结果
    uint32_t                  m_sortKey;  // 每帧刷新
};

struct DrawList
{
    DrawListKey                    m_key;
    RHI::Viewport                  m_viewport;   // view 的归一化 rect × attachment extent
    RHI::Scissor                   m_scissor;
    const RHI::ShaderBindings*     m_viewSrg;
    eastl::vector<DrawListEntry>   m_entries;    // 成员：构建期维护，跟骨架同生命周期
    eastl::vector<uint32_t>        m_order;      // 每帧：排序+剔除结果，size = 本帧可见数
};
```

挂载：`PassDrawLists { eastl::vector<DrawList> }` 作为组件挂在 **pass 实体**上。不做成独立实体——DrawList 是
pass 的执行计划不是资源，且 `BuildExecuteWorks` 要一次拿到某 pass 的全部 list 做负载估算。

**PSO 落在 `DrawListEntry` 而非 DrawItem 或 DrawList**：PSO 是 `(物体, pass)` 的函数，而 entry 正好是
`(物体, pass, view)` 的实例——DrawItem 太窄（不含 pass），DrawList 太宽（不含物体）。这个位置同时让排序模式
（组内 PSO 不一致、逐个切换）能复用同一套结构。

**存 `RHIHandle` 不存 `const DrawItem*`**：entt 组件存储是密集数组，移除任一 DrawItem 时末尾元素会 swap 进
空位，**所有指向后方元素的裸指针当场失效且无信号**。而 DrawList 跨帧持久、骨架增删是常态。`RHIHandle` 带
version 位可 `Valid()` 检测。先例是 `TODO_DrawItemPersistencePlan.md` 第五节的
`DerivedDrawItems{fixed_vector<RHIHandle, N>}`，理由相同。

**`m_entries` / `m_order` 分开**是关键：排序排的是 `m_order`，`m_entries` 全程不动，所以增删维护不会被排序
打乱。而 `m_order` + 它的 size 正是 GPU-driven 的 **compacted index buffer + count buffer**，切换时语义不变。

### 三个更新频率必须分开

写错了就会退化成每帧重建：

| 更新什么 | 触发 | 频率 | 动作 |
|---|---|---|---|
| **DrawList 集合** | view 增删（光源 / 相机） | 稀疏 | 建 / 删整个 list |
| **`m_entries`** | 骨架增删（Drawable 增删） | 稀疏 | push / swap-erase |
| **`m_order`** | 排序键、可见性 | **每帧** | 只重排索引 |
| `m_viewport` / `m_viewSrg` | view 数据刷新 | 每帧 | 一个 list 一次，不是每 entry |

稳态（无增删）下前两行**零成本**。可见性走旁路 mask / count，**不从 list 里删成员**——这条对齐 GPU 侧
「组成员固定、count 变」的形态，是最容易写错的一项。

### 分组：find-or-create，时机在 PSO 就绪之后

`m_pipelineState` 要等 RenderGraph compile 阶段才合成（`RenderGraphCompiler.cpp:1097` 现在还是 pass 级
`PassCompiledPSO`），所以分组**不能在 router 建骨架时顺手做**，但也不能每帧重做。用与 router 完全同构的
Exclude 门控增量处理：

```
router 现在：  GetView<DrawableTag, Drawable>(Exclude<DeadTag, DrawItemsDerivedTag>)
分组步骤：     GetView<RHI::DrawItem>(Exclude<DrawListedTag>)
               → PSO 已就绪的插进对应 DrawList，打 DrawListedTag
```

稳态下这个 view 是空集。于是 DrawItem 上的 tag 从**编译期归属标记**（每 pass 一个 `PassTag`）减到
**一个运行时状态标记**（`DrawListedTag`）。

**销毁时的定位**：DrawItem 销毁要从所在 list 移除。不新增归属组件，直接遍历所有 pass 的 lists 做
swap-erase——销毁是稀疏事件，6 个 pass × 几十个 list 的线性扫完全可接受。归属完全由「它出现在哪个 DrawList 里」
表达。

**现阶段 key 会退化**：PSO 仍是 pass 级，同一 pass 内 `m_pso` 恒定，实际只按 view 分组。这是正确的中间态——
PSO 下放到物体侧提示 + 按 pass 合成之后（`TODO_DrawItemPersistencePlan.md` 第四节），同一字段自然产生多组，
key 的结构和执行循环一个字不用改。

### 排序：分组与排序正交，策略由 pass 声明

两者是**正交的两个能力**，此前混为一谈过：

| | 分组 | 排序 |
|---|---|---|
| 键来自 | **状态共享需求**：PSO、view | **正确性 / 性能**：深度、材质 |
| 何时确定 | 构建期 | **每帧**（深度是 view-dependent） |
| GPU-driven 下 | 每组一对 args / count buffer | compute sort，或用 OIT 避开 |

**排序在 GPU-driven 下也存在**，只是执行者变成 compute——所以接口必须支持它，不能按「indirect 不需要排序」
去设计。

```cpp
.Accepts<TransparentTag>()
.RendersView<MainViewTag>()
.SortBy<DrawSortMode::BackToFront>()   // 不写 = None
```

**一个真冲突**：透明物体的深度排序与 PSO 分组互斥——一分组，跨组的深度顺序就断了。所以分组策略是 per-pass
的选择，DrawList 有两种模式（同一套结构）：

| 模式 | key 含 pso | entry 的 pso | 用于 |
|---|---|---|---|
| **分组** | 是（组内一致） | 冗余但一致 | opaque / shadow，可走 indirect |
| **排序** | 否 | 逐个不同，提交时切换 | transparent，CPU 提交 |

排序模式走不了 indirect，但那是透明渲染的固有性质，OIT 之前绕不开。

**sortKey 的来源是这里唯一的麻烦**：DrawItem 上没有空间信息，得走骨架的宿主引用拿 Drawable 的包围盒，再和
`m_key.m_view` 的相机位置算距离——每帧随机访问宿主组件。所以 **opaque 默认 `None`**（PSO 分组已消掉状态切换，
front-to-back 的 early-Z 收益要 profiling 说话），只有 transparent 付这个钱，而它数量本来就少。

### 执行：提交步骤上升到 executer

**这些状态设置命令不出现在任何 pass 的 execute 里**，而是 `RenderGraphExecuter` 的步骤，与
`ExecutePreBarriers` / `ExecuteBeginRenderPass` 同级：

```cpp
// RenderGraphExecuter::Execute 内，与既有步骤并列
ExecuteBindPSO / ExecutePreBarriers / ExecuteBeginRenderPass
for each DrawList on this pass:          // 读 PassDrawLists 组件
    ExecuteDrawListState(cmd, list);     // viewport / scissor / view SRG / 共享 SRG / PSO
    ExecuteDrawListSubmit(cmd, list);    // 遍历 m_order 提交
ExecuteEndRenderPass / ExecutePostBarriers
```

```cpp
void ExecuteDrawListState(CommandList* cmd, const DrawList& list)
{
    cmd->SetViewport(list.m_viewport);
    cmd->SetScissor(list.m_scissor);
    cmd->BindShaderInputsForDraw(*list.m_viewSrg);
    for (const auto* srg : list.m_sharedSrgs) { cmd->BindShaderInputsForDraw(*srg); }
    if (分组模式) { cmd->SetPipelineState(*list.m_key.m_pso); }
}

void ExecuteDrawListSubmit(CommandList* cmd, const DrawList& list, RHIContext& ctx)
{
    for (uint32_t i : list.m_order)
    {
        const DrawListEntry& e = list.m_entries[i];
        if (排序模式) { cmd->SetPipelineState(*e.m_pso); }
        cmd->Submit(ctx.Get<RHI::DrawItem>(e.m_item));
    }
}
```

`ExecutePassViewportState`（`RenderGraphExecuter.cpp:263`，读 pass 级 `PassViewportState`）**被
`ExecuteDrawListState` 取代**——viewport 从 pass 级升到 DrawList 级，读的组件换了，架构位置不变。

外层按 `m_key` 排过序的话，相邻 list 只有 pso 不同时可跳过前几行（增量状态设置）。GPU-driven 时
`ExecuteDrawListSubmit` 整个换成一次 `ExecuteIndirect`，`ExecuteDrawListState` 原样保留——**切换点收敛在
executer 的一个私有步骤上**。

### 声明式 pass 没有 `m_executeFunction`

不是「有个默认实现」，是那个字段为空。`RenderPassBuilder::Finalize`（`Pass/RenderPass.h:256-258`）里
这段要**删掉**：

```cpp
funcs.m_executeFunction = m_executeFunction
    ? eastl::move(m_executeFunction)
    : ExecuteFunction(SubmitPassDrawItems<PassTag>);   // ← 删
```

`RenderGraphExecuter::Execute` 里那句 `if (funcs.m_executeFunction)` 本来就是 null-check，天然支持空。
`SubmitPassDrawItems<PassTag>` 随之删除。

**为什么这不只是「省几行抄写」**：默认实现仍然把状态设置留在 execute 这一层，`.Binds<>()` 声明的 SRG 由框架
自动绑、`.RendersView<>()` 声明的 viewport 却要在 execute 里手写命令——**同为声明，执行方式不对称**。读代码的
人无从判断哪些命令框架已经发了、哪些要自己发，分界线只能靠记忆。

上升到 executer 之后，对称性恢复，**每一条都是「声明 → 组件 → executer 读组件执行」**：

| 声明 | 加什么组件 | 谁执行 |
|---|---|---|
| `.Accepts<>()` | `DrawItemRoute` | DrawItemRouter |
| `.Binds<>()` | 共享 SRG 列表 | executer（`ExecuteDrawListState`） |
| `.RendersView<>()` | view 类型 | executer（同上） |
| `.SortBy<>()` | 排序策略 | 每帧排序步骤 |
| attachment 声明 | `RenderPassBeginInfo` | executer（`ExecuteBeginRenderPass`） |
| barrier 编译结果 | `PassBarriers` | executer（`ExecutePreBarriers`） |

于是「哪些是框架做的、哪些要自己写」有了不需要记忆的答案：

| pass 类型 | execute | 状态谁设 |
|---|---|---|
| **声明式**（绝大多数） | **不存在** | 框架全部 |
| **`.CustomPipeline()`**（如 UIPass） | 自己写 | 自己全部 |

**没有中间地带**——与 §四「viewport 归属二选一」是同一原则的两个面。这也和 Pass 的本质一致：pass 是一个实体，
往上加组件塑形，`RenderPassBuilder` 只是简化加组件的流程，不构成另一层机制。

### 状态归属：四层

| 层级 | 内容 | 设置频率 |
|---|---|---|
| **pass** | render target、barrier、root signature | `BeginRenderPass` 前一次 |
| **DrawList** | viewport / scissor、view SRG、共享 SRG（material / scene / instance）、stencil ref | 每组一次 |
| **DrawListEntry** | PSO、sortKey | 分组模式整组一次；排序模式逐个 |
| **DrawItem** | VB/IB view、draw arguments、per-object SRG、startInstance | 每 draw |

新加一个状态时问「它属于哪一层」，答案唯一。**共享 SRG 的绑定次数从 M 次（每 draw 重绑）降到组数级别**是
这个划分的直接收益。

### 与 `ExecuteWork` 的结合

`ExecuteWork::Item::m_draws` + `SetSubmitRange` 整套本来就预设了一个**可索引、可切片的 draw 列表**，
`m_itemIndex` / `m_itemCount` 是「该 pass 被拆成第几段 / 共几段」。今天 `BuildExecuteWorks` 只能写死
`{0, 1}`，因为没有那个列表可切。DrawList 正是缺的那一半：

| | DrawList 提供 | ExecuteWork 提供 |
|---|---|---|
| 分组 / 排序 | key、`m_order` | — |
| 切片 | 可索引的 entries | 按负载决定切点 |
| 并行 | — | 每段一个 CommandList |

结合后 `Item` 的自然形态是 `(DrawList, entryRange)` 而非 `(pass, drawRange)`。**更直接的收益是负载度量**：
`BuildExecuteGroups` / `BuildExecuteWorks` 那两个 TODO 都写着「按负载」，而负载今天无从得知；有了 DrawList
就是各 list 的 `m_order.size()` 之和。

**但 pass 内部拆分现在做不了**：`RenderPassBeginInfo` 没有 suspend/resume 字段，一个 pass 的 draw 不能跨
CommandList（Begin/End 必须在同一个里配对）。所以目前只有 **pass 之间合并**可做（Tonemap 这种只画一个全屏
三角形的 pass 不必独占一个 CommandList）。见 §九。

### 与 Atom 的分歧定位

| 维度 | Atom | 本方案 |
|---|---|---|
| **pass 类型**（DrawListTag） | 每个一份 DrawItem | **共享一份** |
| **view** | 共享（DrawList 存指针） | 共享（DrawList 存 handle） |
| **PSO 落点** | DrawItem 里 | DrawListEntry 里 |

**view 维度上两边从来没有分歧**，真正的分歧只有 pass 类型这一维，成因是**提交模型的终点不同**：

- Atom 的 `View::DrawList` 是按 pass 类型**分类**，组内 PSO 可各不相同、提交时逐个切换。这在 CPU 逐 draw
  提交下完全合理，换来**排序完全自由**。PSO 既然 per-draw 携带，就必须 per-(object, DrawListTag) 存一份。
- 我们受 `ExecuteIndirect` 约束，**分组是硬性的**；分组一硬性，PSO 就上升成组级，DrawItem 里那份即冗余。

次要因素：Atom 的 `DrawPacket` 是**打包**的——一个物体的所有 DrawListTag 的 DrawItem、SRG 指针数组、sortKey
连续布局在一次分配的变长内存里，`DrawPacketBuilder::End()` 时才定大小、之后不可变。所以 per-pass 在 Atom 里
的增量成本≈一次分配里多几个 slot，而 ECS 下同样的选择要付 N 个实体的开销。

但**打包不解决遍历局部性**：它连续的方向是「一个物体的多个 tag」，而提交遍历的方向是「一个 tag 的多个物体」，
两者垂直——Atom 提交时同样是跨 DrawPacket 的随机解引用。ECS 下我们的解引用至少都落在同一个密集数组内。
真正解决它的是 GPU-driven（args buffer 连续、GPU 顺序读），不是 CPU 侧的内存布局技巧。

ECS 只是次要便利（handle 是天然的共享引用、1:1 时可合成同一实体的两个组件），**不是** B 成立的原因。

per-view DrawItem 唯一的表面优势「单一提交路径 / 多线程录制更简单」不成立：indirect 那边照样要分层，
而按 `(pass, view)` 分段同样可并行，段内 DrawItem 照样互不依赖。

## 四、viewport 归属：从 per-draw 回到 view

现在 viewport 有两个来源：`DrawItemBind.h:68-74` 每帧写进每个 DrawItem 的副本，和 `PassViewportState`
（pass 注册时 `.ViewportScissor(...)` 传入）。**两个来源都去掉**，viewport 只来自 view。

- `PassViewportState` 组件和 `.ViewportScissor(...)` builder 方法真删。
- `DrawItemBind.h:68-74` 那段每帧写入真删。
- **`RHI::DrawItem` 的 `m_viewports` / `m_viewportsCount` / `m_scissors` / `m_scissorsCount` 四个字段
  连同 DX12 `Submit` 里的对应分支，也删**（理由见下）。

### per-draw viewport 字段一并删掉

查证过：除了 `DrawItemBind.h:68-74`（本来就要删）和 DX12 `Submit` 的读取，全仓库**没有其他消费者**——
SandBox 的两个 RHI sample 也不用。

两条理由，第二条更重要：

- **占 1/3 空间且恒为 0。** `fixed_vector<Viewport,8>` 192B + `fixed_vector<Scissor,8>` 128B = 330B+，
  而 DrawItem 总量才 ~800B-1KB。删掉直接砍掉三分之一，对每帧线性遍历的缓存行为是白拿的。
- **留着等于留一个已知会咬人的陷阱。** `Submit` 里 per-draw viewport **优先级高于** command list 状态
  （`CommandList.cpp:461-466`），只要有人往 `m_viewports` 写值，循环里按 view rect 设的那份就被静默覆盖。
  字段还在，将来想给某个 pass 加特殊 viewport 的人会发现「这儿正好有个现成字段」，然后绕过 view 体系——
  症状是那个 pass 的画面不跟 view 走，且只在多 view 时才暴露。删掉后这条路编译期就不通，只能去改 view。

**加回来是加法**（字段 + `Submit` 一个分支，两处），真出现「同一 pass 内不同物体画到不同区域」的需求
（图集烘焙一类）时再加，那时需求形状也清楚了，未必是现在这个 `fixed_vector<_, 8>` 的形状。容量 8 本来是给
viewport array（配合 `SV_ViewportArrayIndex` 一次 draw 输出多区域）留的，那是个要连 PSO、shader 语义一起做的
完整特性，缩成 1 个也用不了——所以没有「保留但缩容量」的中间态。

**分两步做**，字段删除不必和渲染层改动同一个 commit：

1. 删 `DrawItemBind.h:68-74` 的写入 + `PassViewportState`，让 `m_viewportsCount` 恒为 0，跑通 view 体系
   —— 渲染层行为改动，可验证。
2. 确认主视角和 shadow 都对之后，再删 `RHI::DrawItem` 的字段和 `Submit` 的分支 —— 纯 RHI 瘦身，零行为变化。

不构成跨后端债（`CLAUDE.md` 的 "Never defer cross-backend correctness"）：DX12 和 Vulkan 都是「这个能力存在
但引擎不用」，不是「先只管 DX12」。

pass 那份实际上是两件事叠在一起，分开之后各自都有更好的归属：

- **默认全屏** —— 该由 pass 自己 attachment 的 extent 推导，不是 authored 数据。现在 5 个 pass 传的都是
  硬编码 `RHI::Viewport(0.f, 1920.f, 0.f, 1080.f)`（`Feature/GBuffer/GBufferPass.cpp:81` 等），注册时快照、
  resize 不更新；今天没暴露是因为 DrawItem 那份副本每帧用实时 `renderSize` 盖掉了它。
  **所以删副本必须和这一条同时做**，否则窗口一不是 1920×1080 就错。
- **区域指定** —— 归 View，循环里设一次。

### 归属是二选一，没有中间地带

| | 谁设 viewport |
|---|---|
| 声明 `.RendersView<>()` | 引擎，循环里按 view rect 设 |
| 不声明 | pass 自己，在 `Execute` 里 |

先例已经有了：`.CustomPipeline()` 就是「PSO 我自己管」的声明，UIPass 用了它就得自己 `SetPipelineState`。
viewport 是同一件事的另一面，不是新规则。

**删掉的是声明式捷径，不是能力**——`work.m_commandList->SetViewport(...)` 在 `Execute` 里一直可调，
且比那条捷径更强（捷径是注册时快照、resize 不更新，本来就是坏的）。

`PassViewportState` 该删的深层原因是它是个**半吊子中间态**：不是「引擎替你管」（不调
`.ViewportScissor(...)` 就没有，`RenderGraphExecuter.cpp:265` 的 `TryGet` 静默跳过），也不是「你自己管」
（在 execute 之前由 executer 设置，pass 作者在自己的 `Execute` 里看不到它发生过）。正是这种中间态
制造「我以为引擎管了」的错觉。

推论：**pass 没有对应 view 时循环 0 次、什么都不画，是正确行为**，不需要兜底。需要 viewport ⟺ 要光栅化
⟺ 必须知道从哪个视角画 ⟺ 必须有 view；反过来不声明 view 的 pass（copy / compute / 自绘 UI）本来就不碰
viewport。「既不声明 view、又要光栅化」的 pass 构造不出来，所以不存在「继承上一个 pass 残留 viewport
然后静默画错」的场景。

### View 存归一化 rect，不存像素 Viewport

`{0,0,1,1}` 为默认，直接放 `View` 结构里（不单独开组件：写者是同一个——shadow 的 tile 分配和投影矩阵本来
就一起算，主视角恒为常量，单独组件只多一个「缺省即全屏」分支）。

归一化而非像素，决定性理由是**同一个 view 会被不同分辨率的 pass 使用**：主视角将来有半分辨率的 SSAO /
bloom，像素值一到半分辨率就错，归一化对任何 target 尺寸都对。换算是现成的——`Viewport::GetScaled`
（`RHI/Viewport/Viewport.h:22`）正好是这个操作，循环里即 `fullTargetViewport.GetScaled(view 的 rect)`。

scissor 不单独存，从同一个 rect 推。scissor ≠ viewport 是特效级需求，届时属于 pass / draw 级覆盖，
不是 view 概念。深度范围（`m_minZ` / `m_maxZ`）保持默认 0..1、暂不进 View；`GetScaled` 也支持归一化 Z，
将来是加字段而非改结构。

这一节是本方案「不是凑合」的佐证：若走 pass 内特判的凑合路线，ShadowPass 必须特判成
`m_viewportsCount == 0`（否则 atlas tile 被全屏 viewport 覆盖，阴影全废）；补上 view 抽象后
**特判不存在了**，反而是删代码。

## 五、边界：pass 内循环 vs 多 Pipeline

| 场景 | 机制 |
|---|---|
| shadow atlas、分屏 —— 同一 RT 不同区域 | **pass 内循环** |
| 编辑器两个独立视口 —— 独立 RT + 独立 pass 链 | **多 `Pipeline`** |

Atom 同样分层：一个 `RenderPipeline` 内可有多个 View，完全独立的渲染目标是多个 `RenderPipeline`。
`Feature/Render/Pass/Pipeline.h` 现在只是 PassContext 的壳，多视口将来落在这一层。

**结论：pass tag 的编译期唯一性可以保住**（`Pass/RenderPass.h:200-202` 的 assert 不用动），
`GetView<PassTag>` O(1) 定位、`CreateImageAttachment<PassTag>` 的编译期 attachment 归属全部保留。
一度考虑的「pass 运行时实例化」需求在 View 实体化后消失大半。

## 六、ShadowPass 在这个体系下的落点

- 一个 `ShadowPass`（一个编译期 tag）、一张 shadow atlas、`.RendersView<ShadowViewTag>()`。
- `.Accepts<ShadowCasterTag>()`——`Drawable/DrawTag.h:21` 的 `ShadowCasterTag` 已存在（"Not wired yet"），
  `TODO_DrawItemPersistencePlan.md` 第八节写明加 shadow 是「加一个分类 tag + 加一条映射 + 加一个 pass」。
- `BeginRenderPass` 对整张 atlas 开一次、`loadAction = Clear` 清一次，循环只改 viewport/scissor + view SRG。
  N 个 pass 实例反而要 N 次 BeginRenderPass。
- 每个 shadow view 一个 space1 SRG，走**统一的 view 路径**，不需要之前设想的
  「index SRG + `g_ShadowViews` StructuredBuffer」技巧（那是 view 概念缺位时的绕道）。
- **shadow 的 VS 必须 `#include "ViewBindings.hlsl"`**。space1 SRG 的布局是从 `ViewBindingsReflect.hlsl`
  反射出来的（`View/ViewBindingSystem.cpp:35`），而绑定时用的是**当前 PSO 的** layout 去查 space；两边的
  space1 组对不上，`Backend/DX12/Command/CommandList.cpp:205-209` 的
  `cbv.m_rootIndices.size() == cd.m_gpuConstantAddresses.size()` 断言会当场炸。
- 新光源当帧创建的 view SRG，循环里要 gate 一下（未就绪就跳过该 view，代价是少一帧阴影，
  而不是绑到未编译的 SRG）。`ShadowViewSystem::Update` 放在 `RenderSystem.cpp:247` 的
  `m_viewBindingSystem.Update` 旁边，同帧的 `CompileShaderInputs` 就能扫到。

### atlas 的实现要点

**attachment**：和 DepthPrePass 同构，只是尺寸是自己的常量，**不能用 `builder.GetRenderSize()`**
（那是 swap chain 尺寸）。`loadAction = Clear` 在 `BeginRenderPass` 发生、在循环之外，所以整张清一次；
depth→SRV 的 barrier 也是整张一次，LightingPass 按 `AttachmentId("ShadowAtlas")` 读。

**tile → 归一化 rect**：起步用固定的 2 的幂网格就够，归一化 rect × atlas 尺寸严格落在整数上。

```cpp
constexpr uint32_t kGrid = 4;                     // 4×4 = 16 tile
const uint32_t gx = slot % kGrid, gy = slot / kGrid;
const float s = 1.0f / kGrid;
view.m_rect = { gx * s, gy * s, (gx + 1) * s, (gy + 1) * s };
```

将来要「方向光占 2×2 个 tile、远处点光源降到 1/4 tile」时才需要真正的矩形装箱器，rect 的表达不变。

**scissor 必须设，不能只设 viewport。** viewport 只做「NDC → 像素」的映射，**它不裁剪**；裁剪由 scissor
和 guard band 做。顶点落在 NDC 之外（如 x = 1.4）时经 viewport 映射会落进相邻 tile，把别人的深度写坏。
症状是「某些角度下相邻光源的阴影互相污染」，很难查。第四节让 viewport 和 scissor 从同一个 rect 推，
正好堵住这个坑。

**tile 变换预乘进矩阵**，不要让 shader 知道 tile 布局：

```text
worldToShadowUV = TileRemap · ClipToUV · worldToClip
```

`ClipToUV` 是 `u = x*0.5+0.5, v = -y*0.5+0.5`（DX 的 v 要翻），`TileRemap` 是 `u' = u*sx + ox`，
两个都是缩放+平移，合成一个矩阵（GLM 列主序，常数项落在 w 列，除 w 之后正好是仿射的平移）：

```cpp
Math::Matrix4X4 m = Math::Matrix4X4Const::IDENTITY;
m[0][0] =  0.5f * sx;   m[3][0] = 0.5f * sx + ox;
m[1][1] = -0.5f * sy;   m[3][1] = 0.5f * sy + oy;
shadowMatrix = m * view.GetWorldToClip();
```

打进 `g_ShadowViews` 的是这个 `shadowMatrix`，不是裸的 worldToClip——tile 布局或 atlas 尺寸变了，
shader 一个字不用改。

**PCF 会跨 tile 采样**：边缘的 PCF taps 会采到隔壁 tile，表现为阴影边缘一圈错误硬边。两个办法一起上——
渲染时 viewport/scissor 比 tile 内缩几像素留 border，采样时把 UV clamp 在内缩矩形里。另外 bias 和 PCF 的
texel step 要按**该 tile 的实际分辨率**算而不是 atlas 分辨率，tile 大小不一时这是常见错误来源。

比较采样器 RHI 侧已具备：`SamplerState` 的 `ReductionType::Comparison` + `m_comparisonFunc`
（`RHI/Resource/Sampler/SamplerState.h:84-85`）。

### shadow view 的产生

`ShadowViewSystem` 每帧从光源重算 shadow view 集合（增删光源只动 view 实体，不动任何 DrawItem 骨架）。
放 World 侧（Light 模块），与 `LightSystem` 把 transform 解成 `LightRenderData` 是同一类工作；render 侧只 marshal。

| 光源 | view 数 | 投影 |
|---|---|---|
| 方向光 | 1（不做级联） | 正交，覆盖场景包围盒 |
| 聚光灯 | 1 | 透视，fov = 2×outerCone |
| 点光源 | 6 | 透视 fov=90°，6 个 atlas tile |

**建议先只做方向光 + 聚光灯**（都是单 view），把 atlas、per-view 循环、lighting 采样跑通；点光源的 cube 面
选择 + tile UV 换算是独立的一块复杂度，混进来会分不清 bug 来源。

### Lighting 侧采样

LightingPass 需要 shadow 矩阵做采样——这是**查询 view 信息**，不是渲染 view，仍需一个
`g_ShadowViews` StructuredBuffer 打包进 space0（和 `g_Lights` 一起，`SceneBindingSystem` marshal）。

每个条目携带（由上面的 atlas 要点决定）：

| 字段 | 用途 |
|---|---|
| `m_worldToShadowUV`（4×4） | 已预乘 tile 变换，`mul` 一次直接出 atlas UV |
| tile 的 UV min / max（4 float） | 采样时 clamp，防 PCF 跨 tile |
| tile 的 texel size | bias / PCF step 按该 tile 分辨率算，不是 atlas 分辨率 |

`LightData`（`SceneBind/LightData.h`）有 `m_pad0` / `m_pad1` 两个 float 空位，`static_assert(sizeof == 64)`
锁着布局——拿 `m_pad0` 当 `m_shadowIndex`（-1 = 不投影）**不用动 64B 布局**。

bias / atlas tile 分辨率 / normal offset 这类参数放 `LightComponent`（authored data），
不写死在渲染层——将来做序列化时白拿。

## 七、与 `TODO_DrawItemPersistencePlan.md` 的接缝

本方案对那份文档的影响比「补充」大，有两处是**修订**：

| 那份文档 | 本方案 |
|---|---|
| 第一节「PSO per-pass 不同 → DrawItem 必须 per-pass 一个」 | **修订**：PSO 移到 `DrawListEntry`，DrawItem 变 per-object（§三·五） |
| 第十一节未决「shadow 是否下探到 per-(Drawable, pass, view)」 | **回答：不下探**（§三），`MaxPassesPerDrawable` 不需要为 shadow 留 view 余量 |
| 第十一节未决「可见性过滤的具体接法」 | 倾向 view mask 走旁路，不改 list 成员（§三·五） |
| 第十一节未决「排序放哪、是否跨帧缓存」 | 排 `m_order`、成员不动；策略 per-pass 声明（§三·五） |
| 第四节「合成的 PSO 填进骨架 `m_pipelineState`」 | **落点改为** `DrawListEntry::m_pso`，流程不变 |
| 第五节 `DerivedDrawItems` 反向引用 | DrawItem 与 Drawable 1:1 后**不再需要** |
| 第十节「`AssembleDrawItems<PassTag, ClassTag, BindingTags...>`」 | 分组步骤取代它的一半；`m_marks` 删除 |

第五节「骨架锚在 Drawable」这条核心**不变，而且更强**了——1:1 之后骨架就是 Drawable 实体上的一个组件。

## 八、分步落地

1. **View 实体化 + `MainViewTag` 语义提升。** 只有主视角一类，循环次数恒为 1；5 处
   `.Binds<MainViewTag>()` 换成 `.RendersView<MainViewTag>()`；删 `DrawItemBind.h:68-74` 的 viewport 写入
   （必须与「默认全屏由 attachment extent 推导」同时做，见 §四）。
   **行为零变化——纯重构，可单独验证主视角没画错。**
2. **DrawList 机制**（§三·五）：数据结构、分组步骤、**提交步骤上升到 executer**
   （`ExecuteDrawListState` / `ExecuteDrawListSubmit` 取代 `ExecutePassViewportState`；删
   `Finalize` 里的默认 `SubmitPassDrawItems<PassTag>`）。此时 PSO 仍是 pass 级、key 退化成只按 view 分，
   但结构完整。同步做 DrawItem 的 per-object 化与 `PassTag` / `m_marks` 删除。
3. **加 `ShadowViewTag` + `ShadowViewSystem` + `ShadowPass`**（方向光 + 聚光灯）。view 维度第一次 > 1。
4. **点光源 6 面。**
5. **`BuildExecuteWorks` 用 entry 数做 pass 间合并** —— 吃上 DrawList 的负载度量，无需动 RHI。
6. **PSO 下放**（物体侧提示 + 按 pass 合成）→ PSO 分组第一次非平凡 → 才谈得上 indirect。
7. **viewMask + culling 接入。**

第 1 步做完多视口的地基就在了；第 2 步是提交模型的地基；第 3 步 shadow 是两者之上的第一个非平凡用例。

**依赖顺序**：PSO 下放 → 材质 shader 变体 → PSO 分组有意义 → indirect。第 6 步的前置是材质变体
（`TODO_MaterialSystemPlan.md`），它决定 PSO cache key 的形状，所以不能提前设计。

## 九、待定 / 未决

- **`startInstance` 的晚绑定缺口。** 它是 DrawItem 里唯一每帧变的字段。
  `TODO_DrawItemPersistencePlan.md` 第三节要求「不写进骨架、提交前现取」，但 `Submit(const DrawItem&)` 是
  按 const 引用读 `m_drawInstanceArgs.m_instanceOffset` 的。要真做到，得让 RHI 在提交时能接收它（多一个参数，
  或 `DrawInstanceArguments` 单独传）；否则只能每帧写回。**这是「骨架只读」能否守住的最后一个缺口。**
- **render pass 的 suspend / resume。** `RenderPassBeginInfo` 缺这个字段，导致一个 pass 的 draw 不能跨
  CommandList，`BuildExecuteWorks` 的 pass 内拆分做不了。两个后端都有原生机制
  （`D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS` / `VK_RENDERING_SUSPENDING_BIT`），且 TBDR 上还关系到 tile memory
  能否跨 CommandList 保持——按「abstraction follows the stricter backend」这个字段本就该有。独立于本方案。
- **`RHI::DrawItem` 的语义与命名。** 删掉 viewport / scissor / rootConstants 后它从「一次 draw call 的全部
  参数」变成「物体部分的参数」，注释和字段需要重新审视一遍。
- **`Visible<ViewTag>` 与 viewMask 的关系。** `View/ViewTags.h` 的 `Visible<V>` 是 per-view-**type** 的标记，
  多实例后语义要重新定义（退化成 mask，还是保留为类型级粗筛）。
- **View 实体的生命周期归属。** 主视角 view 由 `ViewBindingSystem` 建、shadow view 由 `ShadowViewSystem` 建；
  光源消失时 view 实体与其 DrawList 的回收路径（`DeadTag` 级联）需与现有 reap 机制对齐。
- **N 个 view SRG 的每帧更新成本。** 每个 view 一个 space1 SRG，N ≤ 十几时可接受但没实测。若成瓶颈，退路是
  「一个 `g_Views` StructuredBuffer + 索引」，那时才需要 root constant（见背景节的半实现状态）。
- **DrawList entries 的 arena 化。** 现在每个 DrawList 各持一个 `vector`。DrawList 数量上到几百（更多 view ×
  更多 shader 变体）时，改成 pass 级一片连续 arena + `{begin, count}` 切片更优，且正是 GPU buffer 的形状。
  成员访问一律走 `eastl::span` 就能让这个切换对调用方零改动。**现在不做**——几十个 list 的规模下不值得付
  维护偏移的复杂度。
- **CPU 侧遍历局部性。** `ctx.Get<DrawItem>(handle)` 是稀疏集查找 + 密集数组随机索引，M 次随机访问。粗算
  1000 次 miss ≈ 0.03ms，量级上被 `Submit` 内的驱动开销盖过，且 GPU-driven 落地后这条路径整个消失。
  **结论是不预先优化**（曾考虑的「帧内指针缓存」被否：省掉的那次稀疏查找访问的是 ~4KB 数组、大概率命中 L1，
  收益接近噪声）。真要动，选项是让 `m_entries` 按组件存储顺序排（只对 `sortMode == None` 成立）或抽紧凑提交记录。
