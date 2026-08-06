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

## 二、Pass 声明：两条正交的线

```cpp
.Accepts<OpaqueTag>()                        // 变参：消费哪几类 Drawable
.Binds<MaterialBindingTag, MainSceneTag>()   // 变参：注入哪些全局唯一的共享 SRG
.RendersView<MainViewTag>()                  // 单个：这个 pass 为哪一类 view 循环
```

`Binds<>` 机制**保留**，只把 view 这一类从参数列表里分离出去。

`RendersView` 是**单参数、不是变参包**——一个 pass 同时为主视角和 shadow view 渲染讲不通（attachment、
PSO、RT layout 全不同）。签名写死成单参数，让编译器替这条约束把关；单数名也让它和旁边两个变参声明
一眼可辨。

### SRG 归属变化

| SRG | 现在 | 之后 |
|---|---|---|
| **view (space1)** | `ResolveSharedBinding<MainViewTag>` → 塞进每个 DrawItem | **移出**，execute 循环里每轮绑一次 |
| per-pass (space2) | `ResolveSharedBinding<PassTag>`（`DrawItemBind.h:46`） | 不动，pass 自己的，与 view 无关 |
| material (space3) / scene (space0) | `.Binds<>` 注入 | 不动，确实全局唯一 |
| per-object (space4) | `DrawItemObjectBinding` | 不动 |

## 三、执行：所有 pass 统一成 per-view 循环

```
for view in GetView<ViewTag, View>:
    BindShaderInputsForDraw(view 的 SRG)
    SetViewport / SetScissor(view 的目标区域)
    for item in GetView<PassTag, DrawItem>:
        对该 view 可见？ -> Submit(item)
```

**主视角 pass = 循环次数 1 的特例；分屏 = 2；shadow = N。shadow 不再是特殊路径。**

DrawItem 保持 **per-(Drawable, pass)**，不下探到 view——下探会让骨架数 ×N（16 个投影光源 × 1000 Drawable =
16000 骨架），且光源增删就要重建，直接废掉 `TODO_DrawItemPersistencePlan.md` 的骨架持久化。
DrawItem 骨架在循环中**全程只读**，变的只是 command list 状态。

> 对照 Atom：Atom 在 DrawItem 层面**也不 per-view 复制**——`RHI::DrawPacket` 拥有 per-(object, DrawListTag)
> 的 DrawItem，`View::DrawList` 存的是 `DrawItemProperties{const DrawItem*, sortKey, depth}`，即指针+排序键。
> 差异在 pass 层：Atom 的 `CascadedShadowmapsPass` / `ProjectedShadowmapsPass` 是 `ParentPass`，运行时
> `CreateChildPassesInternal()` 按需创建 N 个 `ShadowmapPass` child，各自关联一个 `RPI::View`。它能这么做是
> 因为 Pass 是运行时对象树。（此段为理解，未逐行核对源码。）

## 三·五、注：CPU DrawList 考虑过，暂缓

一度打算在 execute 之前加一层 per-view DrawList（每个 (view, pass) 一个 `{const DrawItem*, sortKey}` 数组，
剔除/排序/切片都落在它上面），因为 execute 直接扫 ECS 有三个表达不了的东西：per-view 剔除的子集、
per-view 排序、可切片的索引区间（`ExecuteWork::Item::m_draws` + `SetSubmitRange` 整套本来就预设了一个可索引
的列表，今天却是 `{0, 1}` 占位、`SubmitPassDrawItems` 根本没读它）。

**结论是暂不做。** 提交模型的终点是 GPU-driven：compute 剔除 → 写 indirect args + count → 每个 (view, pass)
一次 `ExecuteIndirect`，CPU 侧的 per-draw 记录归零。上面三条理由会被逐条拿走——剔除进 compute、排序本来就
不做 CPU 侧（靠 Z-buffer / OIT）、一次 ExecuteIndirect 没有可切的区间。剩下的只是「每个 view 重扫一遍密集
存储」，相对 `Submit` 本身可以忽略。所以 execute 保持扫 `GetView<PassTag, DrawItem>()`，只是放进 per-view 循环。

这轮推演留下三条与提交模型无关、无论 CPU 还是 GPU-driven 都成立的结论：

- **提交顺序不变量**：提交顺序任意，任何 DrawItem 不得依赖前一个 DrawItem 建立的状态（今天成立——`Submit`
  每次全量设 VB / stencil ref / 全部 shader bindings）。展开成完整的层级归属见 §三·六。
- **(view, pass) 是真实的配对单位**。今天它是「循环的一轮」，GPU-driven 时它是一对 args / count buffer。
  挂点也已经现成——`DrawItemRoute`（`Drawable/DrawItemRoute.h:14`）就是每个 pass 一张类型擦除函数指针表，
  `.RendersView<ViewTag>()` 将来往里冻的是「生成/绑定 indirect args」，形状和 `.Accepts<>()` / `.Binds<>()` 一致。
- **真要排序，排的是列表不是 DrawItem**：DrawItem 很胖（多个 `fixed_vector`），per-view 拷贝会废掉
  `TODO_DrawItemPersistencePlan.md` 的骨架持久化。ECS 里的存储顺序全程不动。

**什么时候回头做**：只有在 GPU 剔除落地之前场景先大到 CPU 剔除变紧急时。那时的最小形态也不是列表，而是每个
drawable 一个按 view 槽位的位掩码（扫描时测一位）——建 mask 比建 N 个数组便宜，且不产生要丢弃的结构。

## 三·六、DrawItem 完备性的作用域 = (pass, view)

把 viewport 和 view SRG 提到循环外，等于 DrawItem 不再自包含。**曾纠结要不要为每个 view 各生成一份
DrawItem 来恢复完备性**（数量 ×N，换取每个 DrawItem 绝对自足）。结论是**不为**。

### 完备性从来不是绝对的

DrawItem 今天就依赖外部状态：render target（`BeginRenderPass` 设的）、barrier（pass 级）。所以真正有用的
不变量不是「DrawItem 绝对自包含」，而是「**同一作用域内的 DrawItem 之间互不依赖、可任意排序**」——
作用域是 `(pass, view)`：

| 层级 | 内容 | 建立时机 |
|---|---|---|
| **pass 级** | render target、barrier、root signature | `BeginRenderPass` 之前 |
| **view 级** | view SRG (space1)、viewport / scissor | 每轮循环一次 |
| **DrawItem 级** | PSO、几何（VB/IB）、per-object SRG、stencil ref | `Submit` 内 |

循环内：任何 DrawItem 不得依赖前一个 DrawItem 建立的状态；view 级 / pass 级状态在循环内不得被改变。

> **PSO 是 DrawItem 级，不是 pass 级。** `m_pipelineState` 字段已在，`CommandList.cpp:449-452` 的 `Submit`
> 里就生效；`RenderGraphExecuter.cpp:282` 的 `ExecuteBindPSO` 只是 pass 级兜底。`TODO_DrawItemPersistencePlan.md`
> 第四节写明每个骨架的 PSO 是「物体侧提示 + pass 的 RT layout」合成的——材质一旦有 shader 变体，
> 同一 pass 内不同物体的 PSO 就不同。

有了这张表，「完备性要不要保证」不再是个需要纠结的形容词，而是可检验的：新加一个状态时问「它属于哪一级」，
答案唯一。

### 为什么不走 per-view DrawItem

**决定性论据：indirect 路线下 viewport 物理上进不了 DrawItem。** `D3D12_INDIRECT_ARGUMENT_TYPE` 的全部取值
（DRAW / DRAW_INDEXED / DISPATCH / VERTEX_BUFFER_VIEW / INDEX_BUFFER_VIEW / CONSTANT / CONSTANT_BUFFER_VIEW /
SHADER_RESOURCE_VIEW / UNORDERED_ACCESS_VIEW / DISPATCH_RAYS / DISPATCH_MESH）**不含 viewport / scissor**，
Vulkan 同理。走 ExecuteIndirect 时 viewport 必须在调用之前设好——per-view 循环不是设计选择，是 API 约束。

直接提交与 indirect 会长期并存，**让两条路结构一致**远比让其中一条「更完备」重要；否则同一引擎里两套心智模型。

其余代价：

- **DrawItem 很胖。** 按 `RHILimits.h` 实测算内联存储 ≈ 800B~1KB（`fixed_vector<Viewport,8>` 192B +
  `fixed_vector<Scissor,8>` 128B + `fixed_vector<uint8_t,256>` 280B + …），正是
  `TODO_DrawItemPersistencePlan.md` 开头说的「KB 级重值」。1000 Drawable × 16 shadow view ≈ 16MB 且每帧线性遍历。
- **范围不止 viewport。** view SRG 同样在循环外，要完备就得两者一起烘进 DrawItem，即彻底的 per-view DrawItem。
- **骨架会跟 view 生命周期绑定**（光源增删、分屏开关都要重建），与 `TODO_DrawItemPersistencePlan.md`
  第五节「骨架锚在 Drawable」直接对撞。

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

该文档第十一节列的未决项「shadow cascade（一 pass 多 view）：DrawItem 是否要下探到 per-(Drawable, pass, view)」
——**本方案的答案是不下探**，理由见第三节。`MaxPassesPerDrawable` 因此不需要为 shadow 留 view 维度的余量。

同节的「可见性过滤的具体接法」——本方案倾向 view mask（第八节），但不与 join 宿主 Drawable 的方案冲突。

## 八、分步落地

1. **View 实体化 + `MainViewTag` 语义提升。** 只有主视角一类，循环次数恒为 1；5 处
   `.Binds<MainViewTag>()` 换成 `.RendersView<MainViewTag>()`；删 `DrawItemBind.h:68-74` 的 viewport 写入。
   **行为零变化——纯重构，可单独验证主视角没画错。**
2. **加 `ShadowViewTag` + `ShadowViewSystem` + `ShadowPass`**（方向光 + 聚光灯）。循环次数第一次 > 1。
3. **点光源 6 面。**
4. **viewMask + culling 接入。**

第 1 步做完，多视口的地基就已经在了；第 2 步 shadow 只是这个地基上的第一个非平凡用例。

## 九、待定 / 未决

- **per-view 排序。** 拟用 `uint64 m_viewMask`（bit v = 对该类型下第 v 个 view 可见）做可见性过滤，比 Atom
  的 per-view DrawList 省掉每帧建 N 个数组。但 mask 只能表达「画不画」，**表达不了 per-view 深度排序**
  （透明物体、opaque front-to-back）。真需要时再补 DrawList，mask 不挡路。
- **View 的目标区域与 pass attachment 的对应。** 循环里要 `SetViewport(view 的区域)`，但 attachment 是 pass
  声明的（`CreateImageAttachment<PassTag>`）、view 是跨 pass 的。倾向 View 只存归一化 rect，pass 在循环里乘上
  自己 attachment 的实际尺寸（shadow atlas 的 tile 布局正好能这么表达），但未定。
- **`Visible<ViewTag>` 与 viewMask 的关系。** 现有 `View/ViewTags.h` 的 `Visible<V>` 是 per-view-**type** 的标记，
  多实例后语义需要重新定义（是退化成 mask，还是保留为类型级的粗筛）。
- **View 实体的生命周期归属。** 主视角 view 由 `ViewBindingSystem` 建；shadow view 由 `ShadowViewSystem` 建。
  光源消失时 view 实体的回收路径（`DeadTag` 级联）需要和现有 reap 机制对齐。
- **N 个 view SRG 的每帧更新成本。** 每个 view 一个 space1 SRG，N ≤ 十几时可接受，但没有实测。若成为瓶颈，
  退路是回到「一个 `g_Views` StructuredBuffer + 索引」，那时才需要 root constant（见背景节的半实现状态）。
