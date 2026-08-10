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

### 生命周期

**每个 view 实体恰好对应一个 world 侧来源**——相机，或光源的一个面。对应关系记在 view 实体上：

```cpp
struct ViewSource { Entity m_source; uint8_t m_face = 0; };   // m_face 给点光源 6 面
```

这是本方案里第一处**跨 context 引用**（RHIContext 的组件里存 world 的 `Entity`）。反方向的先例已经有——
`MeshDrawableComposer` 会往 world 实体上打 `WorldComposedTag`。

**建 / 删走每帧 mark-and-sweep**，与 `MeshDrawableComposer` 同构：产生 view 的系统遍历自己的来源
（相机 / 投影光源），有则 find-or-create，没有对应来源的 view 打 `DeadTag`。N 是十几，线性匹配足够。

销毁级联三步，都已经有落点：

1. view 实体 → `DeadTag`
2. 顺着 `ViewShaderBindings` 一并销毁它的 SRG 实体（SRG 池化是后期优化，先不做）
3. 各 pass 的 DrawList **不需要新机制**——「DrawList 集合对账」本来就是「按当前 view 集合建缺的、删多的」，
   view 没了它的 list 自然成为「多的」

顺带消掉一个今天就存在的隐患：`ViewBindingSystem::Update`（`ViewBindingSystem.cpp:102-110`）现在是遍历所有
相机往**同一个** `m_viewEntity` 里写，多相机时后写覆盖先写。改成按来源 find-or-create 之后不复存在。

## 二、Pass 声明

```cpp
.Accepts<OpaqueTag>()                        // 变参：消费哪几类 Drawable
.Binds<MaterialBindingTag, MainSceneTag>()   // 变参：这个 pass 要哪些共享 SRG
.RendersView<MainViewTag>()                  // 单个：为哪一类 view 循环
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

## 三、执行：一轮循环 = 一个 DrawList

一个 pass 的执行是「遍历它的 DrawList，每个 list 先绑自己的状态、再提交成员」：

```
for list in 该 pass 的 DrawLists:
    绑 list 的 view SRG 与共享 SRG、设 list 的 viewport / scissor
    提交 list 的成员
```

list 的数量由 `.RendersView<Tag>()` 声明的 view 集合决定——**一个 view 一个 list**：主视角 1 个、分屏 2 个、
shadow N 个。**数量差异不再需要不同机制表达，shadow 不是特殊路径。**

关键是 **view 级状态是 list 的字段，不是循环变量**（结构见 §三·五）。DrawItem 在整个过程中**全程只读**，
变的只有 command list 状态。

DrawItem 不下探到 view：下探会让骨架数 ×N（16 个投影光源 × 1000 Drawable = 16000 份），且光源增删就要重建，
直接废掉 `TODO_DrawItemPersistencePlan.md` 的骨架持久化。

### 落点：executer 的步骤

**这些状态设置命令不出现在任何 pass 的 execute 里**，而是 `RenderGraphExecuter` 的步骤，与
`ExecutePreBarriers` / `ExecuteBeginRenderPass` 同级：

```cpp
// RenderGraphExecuter::Execute 内，与既有步骤并列
ExecuteBindPSO(cmd, pass);          // 建立 pipeline layout
ExecuteBindShared(cmd, pass);       // 共享 SRG（material / scene / per-pass），一次
ExecutePreBarriers / ExecuteBeginRenderPass

for (const DrawList& list : PassDrawLists)
{
    ExecuteDrawListState(cmd, list);          // viewport / scissor / view SRG
    for (const DrawBatch& batch : list.m_batches)
    {
        cmd->SetPipelineState(*batch.m_pso);
        for (uint32_t i = batch.m_begin; i < batch.m_end; ++i)
        {
            cmd->Submit(ctx.Get<RHI::DrawItem>(list.m_entries[i]));
        }
    }
}

ExecuteEndRenderPass / ExecutePostBarriers
```

**`ExecuteBindPSO` 必须留在最前**：`BindShaderInputsForDraw` 要用当前 PSO 的 layout 去查 space，没有绑过 PSO
会直接断言（`Backend/DX12/Command/CommandList.cpp:176-180`）。而同一 pass 内所有 PSO 共享一个
`PipelineLayoutDescriptor`（`TODO_PerDrawPSOVariant.md:5` 的架构契约），所以 batch 循环里换 PSO 不会改 layout，
也就不会清掉已绑定的 SRG。

`ExecutePassViewportState`（`RenderGraphExecuter.cpp:263`，读 pass 级 `PassViewportState`）**被
`ExecuteDrawListState` 取代**——viewport 从 pass 级升到 DrawList 级，读的组件换了，架构位置不变。

GPU-driven 时最内层的成员循环整个换成「每个 batch 一次 `ExecuteIndirect`」，外面两层原样保留——**切换点收敛
在 executer 的一个私有步骤上**。

## 三·五、DrawList 机制

DrawList 承担两件事：**按 view 分（一个 view 一个 list）**，以及 **list 内按 PSO 分段**。后者是硬约束——
`ExecuteIndirect` 一次调用内所有 draw 必须共享同一个 PSO（`D3D12_INDIRECT_ARGUMENT_TYPE` 不含「换 PSO」这
一项，Vulkan 要 `VK_NV_device_generated_commands` 那类扩展），所以按 PSO 分组不是 CPU 提交路径的产物，
GPU-driven 之后依然存在。

### DrawItem 是 per-object 的

DrawItem 里只有物体自身的数据，不含任何 pass 或 view 相关的状态：

| 字段 | 归属 |
|---|---|
| VB / IB view、draw arguments、per-object SRG、startInstance | **物体** → 留在 DrawItem |
| PSO | → DrawBatch |
| viewport / scissor、view SRG | → DrawList |
| 共享 SRG | → pass |

所以 **DrawItem 与 Drawable 是 1:1**，是同一实体上的两个组件（render 层 `Drawable` + RHI 层 `DrawItem`），
不是独立实体。连带：

- DrawItem 数量从 `Drawable × pass` 降到 `Drawable`
- `DerivedDrawItems` 反向引用不再需要（1:1，同实体）
- `DrawItemRoute::m_accepts` 决定这个 Drawable 被哪些 pass 消费；router 给实体打上对应的 `PassTag`
  （成员关系的权威记录，见下），并把它插进该 pass 的各 DrawList
- `BindPassDrawItems` 里剩下的 `startInstance` 更新不需要按 pass 分，改成一趟全局
  `GetView<DrawItem, DrawItemInstanceSlot>()` 密集遍历

### 数据结构

```cpp
struct DrawListKey
{
    RHIHandle m_view;    // 目前唯一的区分维度
};

struct DrawBatch                          // 一段共享同一个 PSO 的连续成员
{
    uint32_t                  m_begin, m_end;   // m_entries 上的区间
    uint16_t                  m_variantId;      // 身份，早于 PSO 对象可知
    const RHI::PipelineState* m_pso;            // 解析结果，compile 阶段填
};

struct DrawList
{
    DrawListKey                   m_key;
    RHI::Viewport                 m_viewport;   // view 的归一化 rect × 本 pass attachment extent
    RHI::Scissor                  m_scissor;
    const RHI::ShaderBindings*    m_viewSrg;

    eastl::vector<RHI::RHIHandle> m_entries;    // DrawItem 实体，按 m_variantId 有序
    eastl::vector<DrawBatch>      m_batches;    // m_entries 上相同 variantId 的连续段
};
```

挂载：`PassDrawLists { eastl::vector<DrawList> }` 作为组件挂在 **pass 实体**上。

| 字段 | 用途 |
|---|---|
| `DrawListKey` | 区分同一 pass 下的 DrawList，今天只有 `m_view` 一维 |
| `m_viewport` / `m_scissor` | view 的归一化 rect × 本 pass attachment 的 extent，进 list 时设一次 |
| `m_viewSrg` | 该 view 的 space1 SRG |
| `m_entries` | 成员，存 DrawItem 实体的 `RHIHandle`（不存裸指针：entt 密集存储在移除时会 swap，指针会静默失效），按 `m_variantId` 有序 |
| `m_batches` | `m_entries` 上相同 `m_variantId` 的连续段，每段进入时设一次 PSO |
| `DrawBatch::m_variantId` | `intern(PSO key)` 得到的稠密 id，是这一段的身份 |
| `DrawBatch::m_pso` | 由 `m_variantId` 解析出的 PSO 对象，compile 阶段填 |

**共享 SRG（material / scene / per-pass）不在 DrawList 上**：它们对同一 pass 的所有 list 相同，
放 pass 实体上、进 list 循环之前绑一次。

**状态不跨段累积**：list 的状态是它自己的字段、batch 的 PSO 是它自己的字段，任意切分点之后都能从零重建
全部状态——这是把一个 pass 拆到多个 CommandList 并行录制的前提。

### DrawList 的创建与更新

**成员关系的权威记录是 DrawItem 实体上的 `PassTag`**（router 的 `m_marks` 打，1:N —— 一个物体被几个 pass
消费就带几个 tag）。`DrawList` 是它的**派生缓存**，随时可以扔掉重建。

三条路径：

**1. 创建**（新增 view）——全量查询建全：

```
GetView<PassTag, RHI::DrawItem>()  →  按 m_variantId 排序  →  建 m_batches
```

O(M log M)，只在 view 增删时发生。这同时是启动路径（pass 的第一个 list 也走这条，不存在特例）和安全网
（任何时候怀疑 list 不对，扔掉重建即可，不必保证增量路径永不出错）。

**2. 成员增删**（Drawable 增删、`m_variantId` 变化）——增量维护该 pass 的每个 list：

不能用重建代替——加载场景是逐个物体进来的，每次重建就是 O(M² log M)。分段数组的插入是 **O(batch 数)**
而非 O(M)：往第 k 段插入时，从最后一段往前，每段把自己的第一个元素搬到自己末尾腾出的空位，边界各右移一位，
最后在第 k 段末尾写入。

```
[A A | B B | C C]  插入一个 A
 C: 4→6            [A A | B B | _ C C]
 B: 2→4            [A A | _ B B | C C]
 写入              [A A A | B B | C C]
```

删除对称：先与本段最后一个交换（段内顺序无所谓），再把后面每段的最后一个搬到段首。

**销毁时的定位**由 tag 直接给出——DrawItem 带着哪些 `PassTag`，就去那几个 pass 的 lists 里移除，不需要额外的
归属组件，也不需要扫全部 pass。

**3. 每帧刷新**——compile 之后，只碰 view 状态和 PSO 指针，**不触及成员**。

### 四类更新的频率与落点

| 更新什么 | 触发 | 频率 | 代价 |
|---|---|---|---|
| DrawList 集合 | view 增删（光源 / 相机） | 稀疏 | 建 list 时 O(M log M) |
| `m_entries` / `m_batches` | Drawable 增删、`m_variantId` 变化 | 稀疏 | O(batch 数) |
| `m_viewport` / `m_scissor` / `m_viewSrg` | compile 之后 | 每帧 | O(list 数) |
| `m_batches[i].m_pso` | compile 之后 | 每帧 | O(batch 数) |

**稳态（无增删）下前两行零成本，后两行都不是成员数量级。** 将来接剔除时，可见性走旁路、**不从成员里删**——
这条对齐 GPU 侧「组成员固定、count 变」的形态，是最容易写错的一项。

帧内落点：

```
RenderSystem::OnTick
 1. m_viewBindingSystem.Update()          view 实体（将来 + ShadowViewSystem）
 2. scene / material / instance binding
 3. m_meshDrawableComposer.Update()       Drawable
 4. ★ DrawList 集合对账                    按 .RendersView<Tag>() 建缺的、删多的；新建的走全量查询
 5. m_drawItemRouter.Process()            reap + derive + ★成员进出 list（含分段维护）
 6. 每帧 DrawItem 更新（startInstance）
 7. m_renderGraph.ExecutePipeline()
      build    → attachment 建好，extent 可知
      compile  → ★刷 m_batches[i].m_pso、★算 m_viewport / m_scissor / m_viewSrg
      execute  → 只读
```

第 4 步必须在 router **之前**：当帧新建的 list 由查询建全（此时不含当帧新 Drawable），随后 router 把当帧新
Drawable 插进该 pass 的所有 list，包括刚建的那个——两条路径各管一段，不会重复插入。

`m_viewport` 和 `m_pso` 落在 compile 之后，是因为前者要 attachment 的 extent（build 才建出来）、后者要 PSO
对象（compile 才创建）。

### 状态归属：四层

| 层级 | 内容 | 设置频率 |
|---|---|---|
| **pass** | render target、barrier、共享 SRG（material / scene / per-pass） | `BeginRenderPass` 前一次 |
| **DrawList** | viewport / scissor、view SRG | 每 list 一次 |
| **DrawBatch** | PSO | 每 batch 一次 |
| **DrawItem** | VB / IB view、draw arguments、per-object SRG、startInstance | 每 draw |

新加一个状态时问「它属于哪一层」，答案唯一。

### 与 `ExecuteWork` 的结合

`ExecuteWork` 是**一个 CommandList 的录制内容**，`ExecuteGroup` 是**一组并行录制的 Work**（GPU 按数组顺序
执行）。`Item::m_draws` + `SetSubmitRange` 整套预设了一个可索引、可切片的 draw 列表，`m_itemIndex` /
`m_itemCount` 是「该 pass 被拆成第几段 / 共几段」——今天 `BuildExecuteWorks` 只能写死 `{0, 1}`，因为没有那个
列表可切。

结合后 `Item` 的形态是 **`(pass, listIndex, entryRange)`**：区间是相对某个 DrawList 的 `m_entries` 而言的，
只有 pass 定位不到。

**切分点优先对齐 batch 边界**；切在 batch 中间也成立，只是后半段要重设一次 PSO。任意切分点都能从零重建全部
状态（见「数据结构」末），这是并行录制的前提。

**负载度量**：`BuildExecuteGroups` / `BuildExecuteWorks` 那两个 TODO 都写着「按负载」，而负载今天无从得知；
有了 DrawList 就是各 list 的 `m_entries.size()` 之和。

**但 pass 内部拆分现在做不了**：`RenderPassBeginInfo` 没有 suspend / resume 字段，一个 pass 的 draw 不能跨
CommandList（Begin / End 必须在同一个里配对）。所以目前只有 **pass 之间合并**可做（Tonemap 这种只画一个全屏
三角形的 pass 不必独占一个 CommandList）。见 §六。

## 四、viewport 归属

viewport 只来自 view，现有的两个来源都删掉：

- `PassViewportState` 组件 + `.ViewportScissor(...)` builder 方法
- `DrawItemBind.h:68-74` 每帧往每个 DrawItem 写入的那段
- `RHI::DrawItem` 的 `m_viewports` / `m_viewportsCount` / `m_scissors` / `m_scissorsCount` 四个字段，
  连同 DX12 `Submit` 里读它们的分支——查证过全仓库只有上面那段写入和这处读取两个消费者，SandBox 的两个
  RHI sample 也不用

**默认全屏由 pass 自己 attachment 的 extent 推导**，不是 authored 数据。现在 5 个 pass 传的都是硬编码
`RHI::Viewport(0.f, 1920.f, 0.f, 1080.f)`（`Feature/GBuffer/GBufferPass.cpp:81` 等），注册时快照、resize 不
更新；今天没暴露是因为 DrawItem 那份副本每帧用实时 `renderSize` 盖掉了它。**所以删副本必须和这一条同时做**，
否则窗口一不是 1920×1080 就错。

**分两步做**，字段删除不必和渲染层改动同一个 commit：

1. 删 `DrawItemBind.h:68-74` 的写入 + `PassViewportState`，让 `m_viewportsCount` 恒为 0，跑通 view 体系
   —— 渲染层行为改动，可验证。
2. 确认主视角和 shadow 都对之后，再删 `RHI::DrawItem` 的字段和 `Submit` 的分支 —— 纯 RHI 瘦身，零行为变化。

### View 存归一化 rect，不存像素 Viewport

`{0,0,1,1}` 为默认，直接放 `View` 结构里。归一化而非像素，是因为**同一个 view 会被不同分辨率的 pass 使用**
（主视角将来有半分辨率的 SSAO / bloom），像素值一到半分辨率就错。换算是现成的——`Viewport::GetScaled`
（`RHI/Viewport/Viewport.h:22`）正好是这个操作，即 `fullTargetViewport.GetScaled(view 的 rect)`。

scissor 不单独存，从同一个 rect 推。深度范围（`m_minZ` / `m_maxZ`）保持默认 0..1、暂不进 View。

### 不声明 view 的 pass 自己管

| | 谁设 viewport |
|---|---|
| 声明 `.RendersView<>()` | 引擎，`ExecuteDrawListState` 里按 view rect 设 |
| `.CustomPipeline()` | pass 自己，在 `Execute` 里 |

没有中间地带。先例是 `.CustomPipeline()` 本来就是「PSO 我自己管」的声明，UIPass 用了它就得自己
`SetPipelineState`；viewport 是同一件事的另一面。

## 五、ShadowPass 在这个体系下的落点

> §一~四 与 §八 均已落地。多 view 执行路径由 `SandBox/Program/RenderGraph/MultiView.cpp` 实测——
> 一个 pass、四个 view、每 view 自己的 viewport/scissor 与 space1 SRG，示例代码里没有任何遍历 view 的
> 录制循环。本节按落地后的实际形态重写过。

### 已具备，接 shadow 不需要新机制

| 项 | 依据 |
|---|---|
| 多 view 执行 | `BuildPassDrawTable` 每 view 一个 `DrawList`，`ExecuteDrawListState` 设 viewport/scissor + 绑 view SRG。N=16 与 N=4 是同一条码 |
| depth-only pass | `ResolveTargetViewport`（`RenderGraphExecuter.cpp:33`）无 color attachment 时已回落到 depth attachment 取 extent |
| depth 当 SRV 采样 | `LightingPass.cpp:157-167` 已在做：`m_overrideFormat = R32_FLOAT` + `m_overrideBindFlags = ShaderRead`。atlas 读法一字不差 |
| pass 排序 + barrier | `RenderGraphBuilder::BuildGraph` 是真 topo sort。LightingPass 声明 `ReadImageAttachment("ShadowAtlas")` 即可，无需手工排序 |
| atlas 清一次 + 每 tile scissor | BeginRenderPass 在 pass 级、viewport/scissor 在 DrawList 级，是结构给的，不需要额外约定 |
| view 编码不管来源 | `ViewBindingSystem` 已是纯编码器（扫 `<View, ViewShaderBindings>` 全写），`ShadowViewSystem` 只负责生产 |
| 未就绪 SRG 的 gate | `ResolveViewShaderBindings`（`RenderGraphExecuter.cpp:65`）已区分「没声明 space1」与「声明了但没编译」，后者跳过该 view |
| 比较采样器 | `SamplerState` 的 `ReductionType::Comparison` + `m_comparisonFunc`（`RHI/Resource/Sampler/SamplerState.h:84-85`），DX12 转换已通（`Conversions.cpp:1095`） |
| 深度 bias | `RasterState::m_depthBias` / `m_depthBiasClamp` / `m_depthBiasSlopeScale`（`RHI/Pipeline/RenderStates.h:90-92`） |
| `LightData` 扩展位 | `m_pad0` / `m_pad1` 两个 float 空位。`m_pad0` 改成 `int32_t m_shadowIndex = -1`（-1 = 不投影），同宽，64B 布局不动 |

### 落地形态

- 一个 `ShadowPass`（一个编译期 tag）、一张 shadow atlas、`.RendersView<ShadowViewTag>()`。
- `.Accepts<ShadowCasterTag>()`——tag 已声明（`Drawable/DrawTag.h:21`），但 `MeshDrawableComposer.cpp:165`
  目前只打 `OpaqueTag`，需要补上分类。
- 每个 shadow view 一个 space1 SRG，走**统一的 view 路径**：`CreateViewEntity<ShadowViewTag>(rhiCtx)` 一行。
  渲染路径不依赖 `g_ShadowViews`——那个 buffer 只服务 LightingPass 的采样（「已定的决策」第 4 条）。
- **shadow 的 VS 必须 `#include <ViewBindings.hlsl>`**。space1 SRG 的布局是从 `ViewBindingsReflect.hlsl`
  反射出来的（`View/ViewFactory.cpp:43`），而绑定时用的是**当前 PSO 的** layout 去查 space；两边的
  space1 组对不上，`Backend/DX12/Command/CommandList.cpp:205-209` 的
  `cbv.m_rootIndices.size() == cd.m_gpuConstantAddresses.size()` 断言会当场炸。
- **`ShadowViewSystem` 放 `SparkRender/View/`**，与 `CameraViewSystem` 同构：读 world 的
  `Light::LightRenderData`，写 RHIContext 的 view 实体。`CreateViewEntity` 在 SparkRender，放 Light
  模块会形成反向依赖。`Update` 放在 `RenderSystem::OnTick` 的 `m_cameraViewSystem.Update`
  旁边（两者都是生产者，都在 `m_viewBindingSystem.Update` 之前），同帧的 `CompileShaderInputs` 就能扫到。

### 已定的决策

**1. tile 槽位由 `ShadowViewSlot` 定，不由迭代顺序定。**

`CollectViews`（`Pass/PassCapabilities.h:147`）走 entt view，顺序随实体增删变（`swap_and_pop` 会把末尾元素
填进空洞）。渲染本身不在乎——tile 位置在 view 的 `m_rect` 里，谁先谁后都对。但 `LightData::m_shadowIndex`
指向的 buffer 下标必须与光源稳定对应，否则光源增删的那一帧会把阴影贴到别的光源上，只错一帧、随机复现。

槽位是 view 实体上的组件 `ShadowViewSlot { uint32_t }`，`ShadowViewSystem` 分配。它同时是 tile 索引，
与 `m_rect` 同源。

反向查找另设载体：`ShadowViewSystem` 在 **world 光源实体**上写 `LightShadowIndex { int32_t }`（-1 = 不投影）。
`SceneBindingSystem::Update`（`SceneBindingSystem.cpp:350`）是按 `GetView<Light::LightRenderData>()` 的 entt
顺序密集打包 `g_Lights` 的，slot 由迭代顺序决定，所以它必须能从光源实体直接读到 shadow 下标——在它现有那趟
lambda 里 `TryGet` 一次即可，不走跨 context 的 view 实体。生命周期同 `MainViewRef`：mark-and-sweep 时一并
`Remove`。

**2. 剔除控制「这一帧提不提交」，不控制「建不建 view 实体」。**

下文「哪些光源产生 shadow view」的判据本质都是「这一帧这个 tile 不重画」。若实现成不建 view 实体，
`Internal::CreateViewShaderBindings`（`View/ViewFactory.cpp:78`）就会每帧 create/destroy 一个 `ShaderBindings`
+ constant buffer。

**view 实体跟随光源的生命周期**（mark-and-sweep 的来源是光源，不是剔除结果），剔除另设 gate：一个通用 tag
`ViewInactiveTag`，`CollectViews` 改成 `Exclude<DeadTag, ViewInactiveTag>`。不引入 shadow 专属概念——编辑器
视口隐藏、反射探针降频是同一件事，语义对所有 view 类型都成立。

**3. bias 走 view 常量 + 采样端，slope-scale 只有一个全局值。**

`RasterState` 在 PSO 里，而 PSO 是 pass 级的（`PassCompiledPSO`，`BuildDrawBatches` 无条件填同一个），所以
一个 ShadowPass 的所有 view 共用一份 slope-scale bias——方向光的正交和聚光灯的透视想要的值差得远。

constant bias 与 normal offset 进 view 的 space1 常量、在采样端应用；slope-scale 作为 ShadowPass 的单一
PSO 状态。**不为 per-view 光栅状态去做 PSO 变体**：DrawBatch 层理论上能承载，但今天一个 list 恒为一个 batch，
拆开的代价远大于收益。

**4. per-view SRG 与 `g_ShadowViews` 并存，不合并。**

渲染路径继续用 per-view space1 SRG（一次只画一个 view，绑 CBV 最直接，不依赖 root constant）；LightingPass
一次 draw 要读 N 个 shadow view，`g_ShadowViews` StructuredBuffer 必然存在。

两者不是两份真相：**都从同一个 `View` 组件读**（`rhiCtx.Get<View>(v)`），是同一份数据的两次编码，不是两条
各自算矩阵的 marshal 路径。合并 CBV 那条要补 `SetRootConstants`（`Backend/DX12/Command/CommandList.h:23`），
属于独立收益，不与 shadow 捆绑。

**5. atlas 是持久（imported）资源，不是 transient。**

见下文「atlas 的实现要点」。

### 已知上界：单个 pass 不可拆

ShadowPass 的 submit 数 = casters × views，1000 caster × 8 view = 8000 次提交，全在一对 Begin/EndRenderPass
里。而 `BuildExecuteGroups` 明确「never break on an empty group」——单个超预算的 pass 必须整体进一个 group、
一个 Work、一个 CommandList，`kMaxSubmitsPerCommandList = 512` 对它**完全失效**。

根因是 §七 第一条：`RenderPassBeginInfo`（`RHI/Command/RenderPassBeginInfo.h:52-71`）只有 attachment /
VRS / layerCount / viewMask，没有 suspend / resume，一个 render pass 的 draw 不能跨 CommandList。

且 v1 没有 per-view 剔除，每个 caster 进每个 tile，没有任何降低的杠杆（§八「剔除接入点」的前置条件——覆盖
全部 Drawable 的稠密稳定对象索引——尚不存在，`InstanceSlotRef::m_id` 只覆盖索引化供给的那部分）。

主视角不会这么惨（GBuffer 是 1 view）。v1 两三盏灯可以背着走，但它是这套东西的天花板。

### 缺的基础设施（与多 view 无关，但挡住正确的方向光阴影）

- **场景包围盒。** 方向光的正交投影要覆盖场景，而 `LightRenderData` 只有方向/位置/强度/range，仓库里也没有
  全局 AABB。v1 用「相机周围固定尺寸的正交盒」顶上，**明确记为占位**，否则大场景一上来就穿帮。
- **authored shadow 参数。** 没有 `m_castShadow` / bias / normal offset / tile 分辨率。要加**两处**：
  `LightComponent`（`SPARK_COMPONENT_TRAITS(..., editable = true)`，加字段走反射/编辑器路径）和
  `LightRenderData`，由 `LightSystem` 搬运——`ShadowViewSystem` 只读后者，与 `SceneBindingSystem` 一致。

### atlas 的实现要点

**attachment 是持久（imported）资源，不是 transient。** 尺寸是自己的常量，**不能用
`builder.GetRenderSize()`**（那是 swap chain 尺寸）。路径与 swap chain 同形：图像建一次，每帧 import 成
attachment，而不是 `CreateImageAttachment` 建 transient；`ResourceStateTracker` 对 imported 资源按 `m_initial`
起手，跨帧已支持。

代价只是那块显存常驻不参与 transient 池 aliasing——atlas 从 ShadowPass 写到 LightingPass 读跨了大半帧，
本来也没多少 alias 空间可省。收益是 tile 缓存（见「后续层级」）不需要改资源形态。

`loadAction = Clear`，在 `BeginRenderPass` 发生、在 DrawList 循环之外，所以**整张每帧清一次**；depth→SRV 的
barrier 也是整张一次，LightingPass 按 `AttachmentId("ShadowAtlas")` 读。

**tile → 归一化 rect**：起步用固定的 2 的幂网格就够，归一化 rect × atlas 尺寸严格落在整数上。
`ViewRect` 的字段序是 `(m_minX, m_maxX, m_minY, m_maxY)`（对齐 `Viewport::GetScaled` 的参数序），不是
`(minX, minY, maxX, maxY)`：

```cpp
constexpr uint32_t kGrid = 4;                     // 4×4 = 16 tile
const uint32_t gx = slot % kGrid, gy = slot / kGrid;
const float s = 1.0f / kGrid;
view.m_rect = { gx * s, (gx + 1) * s, gy * s, (gy + 1) * s };
```

tile 数与 `ViewHandleList` 的 `fixed_vector<RHIHandle, 16>`（`Pass/PassCapabilities.h:24`）一起定——一个点
光源占 6 个 view，三盏点光就溢出到堆。

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

**PCF 会跨 tile 采样**：边缘的 PCF taps 会采到隔壁 tile，表现为阴影边缘一圈错误硬边。渲染时比 tile 内缩
几像素留 border，采样时把 UV clamp 在内缩矩形里。

**`View::m_rect` 存的就是内缩后的 rect**，viewport / scissor / `TileRemap` / 采样 clamp 四者全从它推，全程
只有一个 rect。若只内缩 viewport 而 `TileRemap` 仍用满 tile rect，采样 UV 会整体偏移一个 border 宽度。

bias 和 PCF 的 texel step 要按**该 tile 的实际分辨率**算而不是 atlas 分辨率，tile 大小不一时这是常见错误来源。

### shadow view 的产生

`ShadowViewSystem` 每帧从光源重算 shadow view 集合（增删光源只动 view 实体，不动任何 DrawItem 骨架）。
归属见上文「落地形态」：`SparkRender/View/`，与 `CameraViewSystem` 同构，读 world 的 `LightRenderData`、
写 RHIContext 的 view 实体。**重算的来源是光源，不是剔除结果**（「已定的决策」第 2 条）。

| 光源 | view 数 | 投影 |
|---|---|---|
| 方向光 | 1（不做级联） | 正交，覆盖场景包围盒 |
| 聚光灯 | 1 | 透视，fov = 2×outerCone |
| 点光源 | 6 | 透视 fov=90°，6 个 atlas tile |

**建议先只做方向光 + 聚光灯**（都是单 view），把 atlas、per-view 循环、lighting 采样跑通；点光源的 cube 面
选择 + tile UV 换算是独立的一块复杂度，混进来会分不清 bug 来源。

#### 哪些光源产生 shadow view

判据不是「光源在视野内」——相机背后的点光源照样照亮视野里的物体。按下面的顺序筛，成本主要被后三条压住：

1. **作者标注**：大部分光源（补光、装饰光、小范围点光）根本不投影。最大的一刀，且免费。
2. **影响体积 × 视锥相交**：光源的球 / 锥与相机视锥相交才需要。只能保守——**shadow map 必须在主 pass
   之前渲染**，所以拿不到「这一帧哪些光真的影响了可见像素」（那要等 depth prepass 后的分簇光照剔除）。
   要更准就得用上一帧的结果做时序反馈。这个时序矛盾是 shadow 剔除区别于普通剔除的地方。
3. **屏幕占比 → 分辨率阶梯 + 直接丢弃**：把影响球投影到屏幕，按覆盖像素数决定 tile 大小，低于阈值不投影。
   这是定量控制成本的主力。
4. **预算封顶**：atlas 尺寸固定 → tile 数固定。按优先级（屏幕占比、距离、作者设的重要度）排序装满为止，
   剩下的退化成不投影。**保证最坏情况有上界**，与场景里有多少灯无关。
5. **点光源按面剔除**：6 个面里只画与视锥相交的，常见是 2~3 个。

这几条全部落在 `ShadowViewSystem` 的「建哪些 view 实体」这一步，不涉及任何机制改动。第 5 条尤其顺——一个面
就是一个 view，面级剔除就是少建几个实体。

#### 后续层级：tile 缓存

**不进 v1。** 静态光源 + 静态几何的 tile 画一次不再重画、动态的降频重画，是「大量投影光源」可行的真正原因，
但它是性能层级，不是功能完整性的一部分：方向光的 tile 内容每帧都变（级联跟着相机滑动），最贵的那盏灯吃不到；
per-view 剔除对所有光源都生效、收益更通用，是它的前置而非替代。

三个前置条件，互相独立：

1. **`ImageClearRequest` 加 rect**（`RHI/Command/ClearRequest.h:33-38` 今天只有 clear value / flags /
   image view）。缓存意味着 `loadAction = Load`，失效的 tile 要能单独清。两个后端原生都支持（DX12
   `ClearDepthStencilView` 的 `NumRects/pRects`、Vulkan `vkCmdClearAttachments` 的 `VkClearRect`），但语义
   得先定：Vulkan 的 `vkCmdClearAttachments` 必须在 render pass **内**、清当前绑定的 attachment，而
   `vkCmdClearDepthStencilImage` 在 render pass **外**、收 subresource range 不收 rect；DX12 两种位置都行。
   按「abstraction follows the stricter backend」，这是先定设计再写码的那一类。
2. **失效判定。** tile 要在 caster 移动/增删、光源移动/改参、tile 被重分配、tile 分辨率变化时全部失效。需要
   「光源体积内这一帧有没有东西动过」——那是变更检测，尚未落地。缺可靠失效的缓存产出的是残留错误阴影，
   比慢难查得多。
3. **tile→光源的跨帧稳定分配。** 已由「已定的决策」第 1 条的 `ShadowViewSlot` 提供。

资源形态不在这个清单里：atlas 从一开始就是持久资源。

### Lighting 侧采样

LightingPass 需要 shadow 矩阵做采样——这是**查询 view 信息**，不是渲染 view，需要一个
`g_ShadowViews` StructuredBuffer 打包进 space0（和 `g_Lights` 一起，`SceneBindingSystem` marshal）。
它与 per-view space1 SRG 并存，两者都从同一个 `View` 组件编码（「已定的决策」第 4 条）。

atlas 本身按 `AttachmentId("ShadowAtlas")` 走 `ReadImageAttachment` + Compile 里
`FindPassAttachmentImageView` → `SetPassShaderImage` 进 space2，与 LightingPass 现在读 SceneDepth
一字不差；比较采样器走 `SetPassShaderSampler`（`Pass/PassAccess.h:176`）。

每个条目携带（由上面的 atlas 要点决定）：

| 字段 | 用途 |
|---|---|
| `m_worldToShadowUV`（4×4） | 已预乘 tile 变换，`mul` 一次直接出 atlas UV |
| tile 的 UV min / max（4 float） | 采样时 clamp，防 PCF 跨 tile |
| tile 的 texel size | bias / PCF step 按该 tile 分辨率算，不是 atlas 分辨率 |
| constant bias / normal offset | 「已定的决策」第 3 条：per-view 的 bias 在采样端应用 |

`LightData`（`SceneBind/LightData.h`）有 `m_pad0` / `m_pad1` 两个 float 空位，`static_assert(sizeof == 64)`
锁着布局——`m_pad0` 改成 `int32_t m_shadowIndex = -1`，同宽，**不动 64B 布局**，HLSL 侧同步改成 `int`。
值由 `SceneBindingSystem` 从光源实体的 `LightShadowIndex` 读（「已定的决策」第 1 条）。

### 实现顺序

行为改变只在第 5 步。第 3 步是第一次真正提交 draw，但屏幕上看不见，验证靠抓帧。

**1. 数据与标记，无消费者。**（已完成）

- `LightComponent` 加 `m_castShadow` / `m_shadowBias` / `m_shadowNormalOffset`，进反射；`LightRenderData` 加
  同名镜像；`LightSystem` 搬运。
- `MeshDrawableComposer` 在 `OpaqueTag` 旁边补打 `ShadowCasterTag`。
- 新增 `ShadowViewTag`（`View/ViewTags.h`）、`ShadowViewSlot { uint32_t }` 与 `ShadowViewRefs`
  （`View/ViewComponents.h`）。

tile 分辨率字段推到第 6 步——v1 是固定网格，它是 atlas 尺寸 / kGrid 的推导值，真正的消费者是那一步的分辨率
阶梯，届时才知道它该表达像素数还是等级。`ViewInactiveTag` 同理推到第 6 步，它唯一的消费者是 `CollectViews`
的 `Exclude`。

`LightShadowIndex` 并入 `ShadowViewRefs::m_index`：两者同一个 system 写、同时销毁，拆成两个组件只多一次
ECS 查找和一处可能不同步的地方。

**2. `ShadowViewSystem` 产生 view 实体，仍无 pass 消费。**（已完成）

`SparkRender/View/`，与 `CameraViewSystem` 同构。每帧对 `m_castShadow` 的方向光 / 聚光灯 find-or-create view
实体，分配 `ShadowViewSlot`，由槽位算 tile `m_rect`（内缩后的），算 `m_worldToView` / `m_viewToClip`，
写 `ShadowViewRefs`；无对应光源的 view 打 `DeadTag`、级联销毁 SRG 并释放槽位。`Update` 挂在
`RenderSystem::OnTick` 的 `m_cameraViewSystem.Update` 之后，`Shutdown` 同 `CameraViewSystem`。

atlas 布局单独成头 `View/ShadowAtlasLayout.h`（2048 / 4×4 / 1 texel border / `ShadowTileRect`），放 `View/`
而非 `Feature/Shadow/`，因为第 3、4 步都要读它，依赖方向与 DepthPrePass include `View/ViewTags.h` 一致。

**`m_index` 直接等于第一个 view 的 `ShadowViewSlot`**：`g_ShadowViews` 按 tile 槽位寻址，容量 = tile 数，
不另设紧凑索引。一个索引空间，`m_index` 天然稳定，代价是 buffer 里有未分配的空洞。

方向光的正交盒 v1 用「相机位置周围固定边长 + 沿光方向回退」占位，相机位置从 `MainViewTag` view 的
`Inverse(m_worldToView)[3]` 取——view 实体已经回答了「主相机是哪个」，不重复这个判断。正解是拟合视锥
（用外接球而非 AABB，尺寸对相机旋转不变，才能配合 texel snapping 消抖动）+ 场景 AABB 定沿光方向的 near。

atlas 满时优雅降级：分配失败就不建 `ShadowViewRefs`，该光源无阴影但不破坏别人的 tile，下一帧重试。

验证是免费的：`ViewBindingSystem` 扫的是 `<View, ViewShaderBindings>`，与 view 类型 tag 无关，所以 shadow view
一建出来就被编码，space1 SRG 非空即证明 view 实体、SRG、生命周期三条都对。

**3. ShadowPass 画进 atlas，先不采样。**

- atlas 是持久 Image 实体：`PendingImageInit`（`DepthStencil | ShaderRead`，D32_FLOAT，自己的常量尺寸）+
  `ResourceName`。**不要用 `CreateStaticImage`**——它打 `StaticImportTag`，那是给「从不当 attachment」的
  采样资源用的。
- **Build 里必须 gate 资源就绪**：延迟创建要等一帧，而 `ImportImageAttachment` 断言 `BackingImage` 存在
  （`RenderGraphBuilder.h:425-431`）。没就绪就整个 pass 不声明 attachment。
- shader 直接复用 `Shaders/DepthPre/DepthPre.hlsl`——它已经是 `ViewBindings`(space1) +
  `InstanceBindings`(space4) + `POSITION` / `INSTANCE_INDEX`，`mul(g_ViewProjection, worldPos)` 对光源视角
  同样成立。差异全在 PSO：slope-scale bias、cull mode。
- pass 声明 `.Accepts<ShadowCasterTag>().Binds<>().RendersView<ShadowViewTag>()`，`loadAction = Clear` /
  `storeAction = Store`。

验证靠抓帧看 atlas。失败模式：所有 tile 内容相同 = 跨 DrawList 没重绑 space1；tile 互相溢出 = scissor 没生效
或 rect 缩放错；atlas 全空 = `ShadowCasterTag` 没打到 Drawable 上，或 Build 的就绪 gate 一直没放行。

**4. `g_ShadowViews` + `m_shadowIndex`，shader 尚未使用。**

`LightData::m_pad0` → `int32_t m_shadowIndex`，HLSL 镜像同步。`SceneBindingSystem` 新增 `g_ShadowViews`
StructuredBuffer（与 `g_Lights` 同构的 per-frame + `PendingBufferInit` 路径），从 view 实体的 `View` 组件
编码 `m_worldToShadowUV` / tile UV min-max / texel size；`m_shadowIndex` 从光源实体的 `LightShadowIndex` 读。

验证：抓帧看 buffer 内容，矩阵与 tile 参数与第 2 步算出的一致。

**5. LightingPass 采样，阴影出现。** 行为改变集中在这一步。

`ReadImageAttachment("ShadowAtlas")`（`m_overrideFormat = R32_FLOAT` + `m_overrideBindFlags = ShaderRead`，
与现在读 SceneDepth 一字不差）→ Compile 里 `FindPassAttachmentImageView` + `SetPassShaderImage` 进 space2，
比较采样器走 `SetPassShaderSampler`；shader 里 `SampleCmp` + tile UV clamp + bias。

**6. 投影判据与 gate。**

作者标注（第 1 步已有 `m_castShadow`）、影响体积 × 视锥相交、屏幕占比、预算封顶，全落在 `ShadowViewSystem`
的「建哪些 view 实体」和 gate 上。`CollectViews` 加 `Exclude<ViewInactiveTag>`。

**v1 里 `ViewInactiveTag` 只能表达「这盏灯这一帧完全不投影」，且必须与 `LightShadowIndex = -1` 同帧同源
设置。** 它不能表达「tile 内容没变所以跳过」——atlas 每帧整张 Clear，跳过的 tile 是清空值而不是上一帧内容，
shader 只有在 `m_shadowIndex == -1` 完全不采样时才是对的。那条路属于「后续层级：tile 缓存」。

**7. 点光源 6 面。** 独立的一块复杂度（cube 面选择 + 面级剔除 + tile UV 换算），前六步跑通后再上，
混进来会分不清 bug 来源。

## 六、实现步骤

行为改变集中在第 4 步，其余各步都能单独完成并验证。

**0. 清掉 `Visible<V>`。** 删 `View/ViewTags.h:11-19` 的模板及 `DrawItemRouter.cpp:287`、
`MeshDrawableComposer.cpp:168` 两处标记——没有任何地方读它。

**1. View 实体化。**

- `View` 加归一化 rect 字段（默认 `{0,0,1,1}`）；新增 `ViewShaderBindings`、`ViewSource`
- `ViewBindingSystem` 的 `m_viewEntity` 单例改成按相机 find-or-create：建 view 实体 + 独立 SRG 实体，
  写 `View` 组件；无对应来源的 view 打 `DeadTag` 并级联销毁 SRG 实体
- **`MainViewTag` 暂时同时打在 view 实体和 SRG 实体上**——`ResolveSharedBinding` 用的是
  `GetView<Tag, ShaderBindings>` 的 join，view 实体没有 `ShaderBindings` 组件不会被选中，旧路径照常工作，
  而 `GetView<MainViewTag, View>()` 已经能拿到 view 实体

本步仍只支持单相机（多相机会有多个 SRG 实体带同一 tag，`.Binds<>` 全注入、最后一个生效），第 4 步解除。
验证：画面不变。

**2. 共享 SRG 上移到 pass 级。** `.Binds<>()` 的产物从「写进每个 DrawItem」改成「解析进 pass 实体上的
`PassSharedBindings`」；executer 加 `ExecuteBindShared(cmd, pass)`，位置在 `ExecuteBindPSO` 之后；
`BindPassDrawItems` 里 `m_shaderBindings` 只留 per-object 那一个。view SRG 此时仍走 `.Binds<MainViewTag>()`，
只是绑定位置从每 draw 变成每 pass，单 view 下等价。验证：画面不变，`m_shaderBindings.size() == 1`。

**3. 建 DrawList，但不切执行。** `DrawList` / `DrawBatch` / `PassDrawLists` 数据结构；`.RendersView<Tag>()`
声明；帧内「集合对账」步骤；router 里成员进出 list（PSO 仍是 pass 级，恒为一个 batch）。执行路径不动。
验证：断言每个 list 的 `m_entries.size()` 等于 `GetView<PassTag, DrawItem>()` 的数量。

**4. 执行切到 DrawList + viewport 归 view。** 行为会变，必须整体做完：

- executer 换成 `for list → ExecuteDrawListState → for batch → 提交成员`
- 删 `ExecutePassViewportState`、`PassViewportState`、`.ViewportScissor(...)`
- 删 `DrawItemBind.h:68-74` 的 viewport 写入
- 加「从 pass attachment 求 extent」的辅助（`RenderPassBeginInfo` 没有 render area，需从第一个 attachment 的
  `ImageView → Image` 描述符取），`viewport = extent × view 的归一化 rect`
- 删 `Finalize` 里的默认 `SubmitPassDrawItems<PassTag>` 及各 pass 的 `.Execute(...)` lambda
  （GBuffer / DepthPre 的与默认实现一字不差；Skybox 那个就绪 gate 要另找落点）
- 5 处 `.Binds<MainViewTag>()` 换成 `.RendersView<MainViewTag>()`；SRG 实体不再打 `MainViewTag`

**验证重点是 resize**：把窗口调成非 1920×1080——这是删掉 per-draw viewport 副本后唯一会暴露的地方。

**5. RHI 瘦身。** 删 `RHI::DrawItem` 的 `m_viewports` / `m_viewportsCount` / `m_scissors` /
`m_scissorsCount` 与 DX12 `Submit` 里的对应分支，更新结构体注释。纯删除，零行为。

**6. 多 view 实测。**（已完成）`SandBox/Program/RenderGraph/MultiView.cpp`：一个 pass、四个 view、2×2 铺满，
每 view 自己的 viewport/scissor 与 space1 SRG，同一批成员画四遍。四个机位取 0/50/100/150 度而非 90 的倍数
——立方体绕 Y 有 4 次对称，90 度间隔会让四格轮廓完全相同，只剩贴图能区分，失败模式看不出来。

**前五步都只是单 view 下的等价重构，这一步才真正验证多 view。** 已验证的失败模式：四格相同 = 跨 DrawList
没重绑 space1；互相溢出 = scissor 没生效或 rect 缩放错；只有一格 = `CollectViews` 只收到一个。

---

**DrawItem per-object 化**（合并到 Drawable 实体、删 `DerivedDrawItems`、`startInstance` 改全局遍历）排在这
六步之后：它与多 view 没有依赖关系，却要动 router、composer、instance binding 三处，混进来会让第 4 步的问题
不好定位。

## 七、待定 / 未决

- **render pass 的 suspend / resume。** `RenderPassBeginInfo` 缺这个字段，导致一个 pass 的 draw 不能跨
  CommandList，`BuildExecuteWorks` 的 pass 内拆分做不了。两个后端都有原生机制
  （`D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS` / `VK_RENDERING_SUSPENDING_BIT`），且 TBDR 上还关系到 tile memory
  能否跨 CommandList 保持——按「abstraction follows the stricter backend」这个字段本就该有。独立于本方案。
- **`g_Views` 全量缓冲区能否取代 per-view SRG。** 两者并存已定（§五「已定的决策」第 4 条），这里剩下的是
  能否最终合并掉 CBV 这条路。障碍是 root constant（得告诉 shader「当前是第几个 view」）：layout 侧已实现
  （`Backend/DX12/Pipeline/PipelineLayout.cpp:44-57`），缺的是 CommandList 侧的设值路径
  （`Backend/DX12/Command/CommandList.h:23` "SetRootConstants removed"），补回来是一个虚函数加一次
  `SetGraphicsRoot32BitConstants`。真正的触发点是 GPU 剔除按索引读所有 view，不是 shadow。
- **剔除结果的形态。** 结果按 DrawList 存（per view 天然成立），`Visible<V>`（`View/ViewTags.h:11-19`）及其
  两处标记（`DrawItemRouter.cpp:287`、`MeshDrawableComposer.cpp:168`）删除——tag 是编译期的，表达不了
  per-view-instance，今天也没有任何地方读它。list 内用索引数组还是位掩码，等接剔除时定。

## 八、修正：DrawList 去持久化

第 3、4 步落地后的修正。**本节取代 §三·五 全部内容、§三「落点」里的执行片段、以及 §六 第 3 步。**

### 结论

DrawList 不持久化。每帧按 pass 查一次 DrawItem 得到一个帧内 vector，分组、切分、提交，用完丢弃。
`DrawList` / `DrawBatch` / `DrawListKey` / `PassDrawLists` 这些类型不再存在。

依据：DrawList 跨帧留存的只有**成员关系**一项——`m_viewport` / `m_scissor` / `m_viewBindings` /
`DrawBatch::m_pso` 全部被 `CompileDrawListState` 每帧整体重写。为维护这一项付出的是分段插入/删除、
集合对账、死条目回收，以及「对账必须早于 router」的时序约束。`GetView<PassTag, DrawItem>` 查询本身廉价，
重建不比维护贵。

### 唯一不可去掉的东西

**一段按序号寻址、从切分决策活到录制结束的 draw 序列。** `ExecuteWork::Item` 用 `DrawRange` 切区间、
`SetSubmitRange` / `ValidateSubmitIndex` 按序号校验，这是 pass 内并行录制的前提。它只要求帧内存活，
不要求是 ECS 组件。

### 形态

per pass（executer 持有的帧内 scratch）：

- `eastl::vector<RHI::RHIHandle> draws` —— 一次 `m_collectDrawItems` 查出
- run 边界（同 `variantId` 的连续段），单 variant 时只有一段

per replay（view）：只有一个 view 句柄。**没有剔除时所有 replay 共享同一份 draws，不复制。**

`ExecuteWork::Item` = `(pass, replayIndex, runIndex, DrawRange)`。

### 落点

查询、分组、切分都在 `BuildExecuteTable` 里完成——它本来就在录制前按 pass 走一遍建 segment / group / work。

时序上必须在 `DrawItemRouter` **之后**（原「早于 router」的约束反转，新顺序是自然的那个），
`RenderSystem::OnTick` 里的 `BuildPassDrawLists()` 调用消失。

`CollectPassDrawItems` 加 `Exclude<DeadTag>`：已标死但尚未销毁的 DrawItem（`RHIHandleClearSystem` 在
`TICK_LAST - 1` 才销毁）不再多画一帧。

### 编译态改为执行时解析

`CompileDrawListState` 删除，三项就地取：

| 项 | 来源 |
|---|---|
| PSO | `PassCompiledPSO`，`ExecuteBindPSO` 已在读 |
| view bindings | view 实体 → `ViewShaderBindings` → `Components::ShaderBindings` |
| viewport / scissor | `RenderPassBeginInfo` 第一个 attachment 的 ImageView → Image 描述符 extent，按 view rect 缩放 |

`PassViewportState` 一并删除：它只有一个消费者，且对有 view 的 pass 每帧设完立刻被 list 覆盖。viewport 改为
每 pass 一个局部值，无 replay 时直接用，有 replay 时缩放。

两条约束：

- **录制期间只读 ECS，不写。** viewport 必须是局部值，不能沿用现在的 `AddOrReplace<PassViewportState>`。
- **pass 级 `ExecuteBindPSO` 必须保留**，且在 `ExecuteBindShared` 之前——`BindShaderInputsForDraw` 要用当前
  PSO 的 layout 查 space。run 切换处的 `SetPipelineState` 在单 variant 下是重复绑定，有变体后才是真切换。

### 删除清单

`Drawable/DrawList.h` / `.cpp` 整个文件（`DrawList`、`DrawBatch`、`DrawListKey`、`PassDrawLists`、
`DrawListInsert`、`DrawListRemoveAt`、`BuildPassDrawLists`）、`DeadDrawEntry` + `ReapDeadDrawEntries` +
`ForEachWork`、`CompileDrawListState`、`PassViewportState`、`DrawItemRouter` 里的插入循环、
`RenderSystem` 里的 `BuildPassDrawLists()`。

第 4 步的执行侧保留：item 切分、录制游标、`SubmitDrawBatch`（`Finalize` 无条件装的默认 execute hook）、
`ResolveDrawItems`（去掉死条目分支）。

### 顺带解决的两件事

- **`.RendersView<>()` 与容器的耦合消失。** 它只回答「重放几次、每次是哪个 view」，不再召唤容器。
  不需要 view 的 pass（TonemapPass）可以不声明。
- **一个 pass 下 N 条内容相同的 list 消失**（§三·五 的 per-view 拷贝形态作废）。

### 剔除接入点（取代 §七 最后一条）

剔除结果不进 draws 数组：

- draws 数组旁并排一条 `objectIds`（每个 entry 的稠密对象索引），随 entry 一起移动
- view 实体上挂一张按**对象索引**寻址的位图，剔除写
- 提交时 `if (!visibility.Test(objectIds[i])) continue;`

位图按对象索引而非数组位置寻址，所以数组重建、重排都不使它失效；剔除也不必顺着 `DerivedDrawItems`
扇出到 DrawItem 粒度。前置条件是一套覆盖全部 Drawable 的稠密稳定对象索引——`InstanceSlotRef::m_id`
是雏形，目前只覆盖索引化供给的对象。

### 若将来重新引入持久化

真正要的是**确定性顺序**，不是持久化本身：GPU-driven 的 indirect argument buffer 想增量更新时，
只要每帧重建按稳定键排序，脏区间就精确可算。而 per-object 的重数据本来就在常驻的 `g_Instances` 里，
DrawList 对 GPU 侧的贡献只是一串对象索引。
