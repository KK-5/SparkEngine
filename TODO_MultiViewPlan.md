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

**View 实体化：每个 view 是 RHIContext 里的一个实体，带 `View` 组件 + 自己的 space1 SRG + 一个 view 类型 tag。**

`MainViewTag` **语义提升**为类型标记（「所有主视角 view」），不新增 role tag：今天集合大小是 1，行为不变；
分屏时是 2。5 处 pass 声明的 tag 名一个字都不用改。

| 维度 | 性质 | 载体 |
|---|---|---|
| view 的**类型** | 编译期可知 | tag：`MainViewTag` / `ShadowViewTag` / 将来 `ReflectionViewTag` |
| view 的**身份** | 运行时，数量可变 | 实体：`View` 组件 + `Components::ShaderBindings` + 类型 tag |

`GetView<MainViewTag, View>()` 返回 1 个（单相机）或 2 个（分屏）；`GetView<ShadowViewTag, View>()` 返回 N 个。
**数量差异不再需要不同机制表达。**

## 二、Pass 声明：两条正交的线

```cpp
.Binds<MaterialBindingTag, MainSceneTag>()   // 全局唯一的共享 SRG，注入进 DrawItem
.RendersViews<MainViewTag>()                 // 这个 pass 为哪一类 view 循环
```

`Binds<>` 机制**保留**，只把 view 这一类从参数列表里分离出去。

### SRG 归属变化

| SRG | 现在 | 之后 |
|---|---|---|
| **view (space1)** | `ResolveSharedBinding<MainViewTag>` → 塞进每个 DrawItem | **移出**，execute 循环里每轮绑一次 |
| per-pass (space2) | `ResolveSharedBinding<PassTag>`（`DrawItemBind.h:46`） | 不动，pass 自己的，与 view 无关 |
| material (space3) / scene (space0) | `.Binds<>` 注入 | 不动，确实全局唯一 |
| per-object (space4) | `DrawItemObjectBinding` | 不动 |

**附带收益**：view SRG 的绑定次数从 M 次（每个 draw 一次，`Submit` 内重绑）降到 N 次（每个 view 一次）。
主视角 N=1，M 通常几百上千。

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

## 四、viewport 归属：从 per-draw 回到 view

`DrawItemBind.h:68-74` 那段每帧写 viewport 的代码**整个删掉**。viewport 由 view 提供、循环里设一次。

这一条是本方案「不是凑合」的佐证：若走 pass 内特判的凑合路线，ShadowPass 必须特判成
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

- 一个 `ShadowPass`（一个编译期 tag）、一张 shadow atlas、`.RendersViews<ShadowViewTag>()`。
- `.Accepts<ShadowCasterTag>()`——`Drawable/DrawTag.h:21` 的 `ShadowCasterTag` 已存在（"Not wired yet"），
  `TODO_DrawItemPersistencePlan.md` 第八节写明加 shadow 是「加一个分类 tag + 加一条映射 + 加一个 pass」。
- `BeginRenderPass` 对整张 atlas 开一次、`loadAction = Clear` 清一次，循环只改 viewport/scissor + view SRG。
  N 个 pass 实例反而要 N 次 BeginRenderPass。
- 每个 shadow view 一个 space1 SRG，走**统一的 view 路径**，不需要之前设想的
  「index SRG + `g_ShadowViews` StructuredBuffer」技巧（那是 view 概念缺位时的绕道）。

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
   `.Binds<MainViewTag>()` 换成 `.RendersViews<MainViewTag>()`；删 `DrawItemBind.h:68-74` 的 viewport 写入。
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
