# 渲染图改造：Pass 是描述，流是执行

设计理念、已定的结构与待办清单。由 P3 的 Bloom 触发，但改的是渲染图本身。

---

## 缘起

P3 的 Bloom 需要一条 **N 步互相依赖的 compute 链**：第 j 步读第 j-1 级、写第 j 级，每步之间必须有屏障；
N 跟渲染分辨率走（720p~4K 约 5~7），窗口一改就变。

它暴露出渲染图的几个隐含假设不再成立：

- **pass 集合是编译期的**（`SPARK_PASS_TAG` 展开成一个类型），而 N 是运行期的；
- **排序与屏障以 pass 为单位**，链要求在 pass 内部的步与步之间发屏障；
- **一个 pass 一个 space 只有一份描述符**，链的每一步读写不同的资源；
- **渲染图里还没有 compute 的提交路径**——执行器只提交 `DrawItem`，compute pass 全部落进"没有批次"的分支，
  工作挤在一次 Execute hook 里。

同形状的后续用例：P4 的 HZB、现由 `ShadowMaskSystem` 代管 draw 的 `ShadowProjectionPass`、在 Execute 里手写
两条屏障的 `CopyFrameBufferPass`。NRD 的 dispatch 列表 shader 异构，一个 PSO 装不下，走不透明工作。

追下去发现，这些假设背后是同一个问题：**执行期仍在以 pass 为单位工作，而 pass 的边界难以界定。**

---

## 设计理念

### 一、Pass 是一个功能的执行流程，执行几次由数据决定

对"同一个算法要跑多次"的 pass，业界常见两种做法：

- **每帧重建**（UE RDG、Frostbite、Unity RenderGraph）：没有 pass 注册表，每帧用命令式调用生成 pass 列表，
  循环几次就是几个 pass。
- **模板 + 实例**（O3DE/Atom）：pass 模板定义在数据里，按请求实例化成树，父 pass 可在运行期生成子 pass。

本引擎不采用任何一种。**Pass 是一个实体，它的单位是完成一个功能的执行流程；需要执行多少次，由它的
Scope 与 item 决定。** 生成 N 个仅参数不同的子 pass，是把 pass 的内部细节暴露到了外面。

**评价标准：pass 的静态部分与动态部分是否解耦**——静态部分可以声明式创建，动态部分在运行期自适应。

对一条链，静态的是 shader、绑定布局、算法形状、格式、队列；动态的是 N、每级尺寸、每次 dispatch 的
线程组数。问题只在于**把 N 放进哪一层**：

| 做法 | N 在哪 | 评价 |
|---|---|---|
| 声明固定上限、每帧启用前 N 个 | 以"上限"写死在静态部分，且静态部分复制 N 份 | 最差 |
| 运行期生成 pass 实例 | **pass 集合** | 把动态量放进了最静态的一层——编译期 tag、`PassCapabilities`、按类型查 SRG 全都建在"pass 身份是类型"上 |
| **一个 pass + N 个 Scope** | **实体层**，引擎本来放运行期可变量的地方 | pass 声明对 N 不敏感 |

UE 能把 N 放进 pass 层，是因为它没有静态层可违反；Atom 需要专门的父子 pass 机制才做到，那个机制本身就是
这层耦合的代价。Scope 在静态层没有身份——没有类型、没有 PassTag、没有 PSO，只是运行期的实体数据。

**接受的代价**：图在 pass 内部是瞎的——链内插不进别的工作，链内的屏障错误图无法校验。这个代价对运行期 N
是**永久的**：pass 集合是编译期的，运行期的 N 拆不回多个 pass。链的各级互相别名这一项，可能由按流位置计算
生命周期收回（见「独立推进」）。

### 二、Pass 是描述，流是执行

GPU 看到的是一条命令流，pass 从来不在里面。

- **声明期**只有 pass 与 Scope（以及它们之下的 attachment、item）。
- **编译期**，pass 是**图节点**（依赖边、拓扑排序、队列分配、瞬态生命周期）；**lowering** 把每个 pass 降级成
  有序的 Scope，编译产物写在 Scope 与 attachment 上。
- **执行期**只有排好序的 Scope 与提交表，执行器只解释这条流，不再认识 pass。

pass 的边界仍需定义，但这个定义只影响图的粒度与 lowering，不再影响屏障放在哪、绑定怎么建、Work 怎么切。

### 三、流是 item 序列加三类标注，按性质分，不按来源分

| 类别 | 成员 | 性质 | 切出的区间 |
|---|---|---|---|
| **状态** | PSO、绑定、viewport | 区间上的值，幂等，可任意重放 | 状态区间 |
| **事件** | 屏障、跨队列 wait / signal | 两个 item 之间的点，只执行一次 | 事件区间 |
| **括号** | Begin / End render pass（含 loadOp clear），以后的调试标记、GPU 计时 | 成对，不幂等 | render pass |

- 这些区间都不是执行期的结构，只是流被某一类标注切开后的样子。"batch"一词只留作状态区间的称呼——GPU-driven
  时一个状态区间就是一次 ExecuteIndirect。
- **标注挂在位置上（item 之间），不挂在 item 上。** 零长度的区间因此可以表达：零 item 的 render pass 就是
  中间没有 item 的一对括号，照样清屏、照样执行前面的屏障。
- **同一位置的处理顺序：闭括号 → 事件 → 开括号 → 状态 → item。** 图形 pass 的屏障先于 Begin，由此保证。
- **约束只有两条**：事件点不能落在括号内部；Work 切点不能落在括号内部（直到 render pass 支持 suspend / resume）。
  跨队列事件强制在该处切出一次提交。

### 四、Pass 的边界由定义产生，不是约定

- **图形 pass = 一次 `VkRenderingInfo` 声明 = 一对括号 = 恰好一个 Scope。** Vulkan 走 dynamic rendering，附件
  集合与布局在 Begin 时定死，边界自然诞生——在一个渲染通道内部，无从把自己的渲染目标声明回 shader read。
- **compute pass = 一个 PSO**，可有任意多个 Scope。依据是第一条的标准：静态部分（shader）是一份。
- **copy pass 没有 PSO，也没有括号**，可有任意多个 Scope。

推论：Bloom 的降采样与上采样是两个 pass（两个 shader）。

检视过的反例（阴影图集按 viewport 分块、layered rendering 写多层、MSAA resolve、同一目标换管线状态）都不需要
多次 Begin。唯一可能来敲门的是**多视图且各视图附件不同**——那时的答案是按视图实例化 pass，与本条一致。

### 五、Scope：pass 内有序的一段，段内 item 互无依赖，每个资源只有一种使用方式

今天的隐含假设"一个 pass 内对同一资源的访问能用一个状态满足"（写在 `MergeImageBarriers` 对访问取并集那一步）
本身没错，错在承载它的单位：它属于 Scope，不属于 pass。

> 与 Atom 的 Scope 不同：Atom 的 Scope 是 FrameGraph 中与 pass 基本一一对应的 GPU 工作单元；这里的 Scope 比
> pass 更细，是 pass 内的一段。

- **Scope 是声明单位，不是执行期的区间。** 它是两类标注共同的来源：它的 attachment 经屏障编译产生事件，
  它的绑定与 pass 的 PSO、视图一起构成状态。一个 Scope 展开成多个状态区间（每视图、每 PSO 变体各一）；
  多个相邻 Scope 若互无冲突，落在同一个事件区间里。
- **Scope 边界是屏障可能出现的位置**；实际有没有，由相邻 Scope 之间有无冲突决定。pass 粒度是"只有一个 Scope"
  的特例，也就是今天的全部 pass。
- **attachment 归属 Scope，item 归属 Scope。** 两者不直接关联。一个 attachment 用于多个 item，就是这些 item
  在同一个 Scope；它们之间若有依赖，按定义就不该在同一个 Scope。
- **用法不同就是不同的 attachment。** 每个 Scope 都读的共享输入（如 LUT），每个 Scope 各声明一个——它是该 Scope
  的访问事实，也是绑定的来源，不是冗余。
- **Scope 之间有序，Scope 内的 item 无序。** item 不需要顺序键。
- **屏障判据只有一条：相邻两个 Scope 对同一资源，状态不同，或至少一方是写。** 状态不同是转换屏障；状态相同是
  纯执行序 / 内存依赖（DX12 的 UAV 屏障、Vulkan 两次 dynamic rendering 写同一附件之间的依赖）。后者由后端决定
  如何落地，编译器不替它删掉——跟随更严格的后端。

**不成立的条件**：出现一组 item，彼此之间需要屏障，却又必须共享同一次 render pass。目前没有这样的用例。

### 六、item 进入 Scope 只有一种方式：在 Scope 上声明

声明的对象可以是**单个 item**，也可以是**一个集合**：

| | 例子 | 声明 |
|---|---|---|
| 单个 | 全屏三角形、链的每一步、ShadowProjection 的 instanced draw、一次拷贝 | `Draw` / `Dispatch` / `Copy`：创建一个 item |
| 集合 | 网格 | `Select<DrawTags...>()`：创建一个选择，查询带这些 DrawTag 的 item |

- **"场景事实还是算法结构"只回答 item 由谁产出，不再决定 pass 怎样消费它。** 作者只需回答"这个 item 是已有的
  还是我造的"——这是归属问题，不是路由问题。
- **选择就是查询。** 今天 router 往场景 item 上打的 PassTag，只是 `AcceptDrawTags` 这个谓词结果的缓存：派生
  本身是无条件的，`m_accepts` 只检查 DrawTag。"pass X 选择此 item"等价于"此 item 带着 X 要的 DrawTag"。
- **场景 DrawItem 继续共享、继续持久。** DrawItem 与 pass 无关（PSO 是 Scope 上的状态），同一网格给 DepthPre、
  GBuffer、Shadow 的字节完全相同。每帧重新声明的只是选择本身，一个集合一个实体。持久化语义（派生一次、依赖
  死亡时回收）不能丢——每帧重新翻译是 `TODO_DrawItemPersistencePlan.md` 当初要治的瓶颈。
- **算法性 item 不再伪造上游产物。** 为全屏三角形建一个 `GeometrySpec` 实体让 router 派生、再让多个 pass
  Accept，是为了走统一路径而伪造的；`ShadowMaskSystem` 与 `ShadowProjectionPass` 各自算一遍 slice 数，也是缺少
  "pass 产出 item"的机制才不得不这么写。
- 一个 Scope 可以同时有选择与单个 item；Scope 内本来无序，混在一起没有问题。compute 的 Scope 同样可以选择。

**会改主意的条件**：出现无法用标签查询（含排除）表达的选择，比如需要一份跨帧维护的有状态成员表。那时预先
算好的成员关系回来，但只作为某种选择的实现，不改变"声明集合"这个形式。

### 七、屏障计算留在编译期

选择展开出的 item 在一个 Scope 内无序，集合内部没有先后，它们需要的屏障只能落在 Scope 起点。执行期唯一会变的
是逐视图剔除掉哪些 item——剔除不改变屏障，所以屏障不必挪到执行期。

### 八、子资源屏障与此正交

子资源屏障解决的是**粒度**，Scope 解决的是**位置**。前者做得再好，若 N 次 dispatch 在一个 Scope 里，图在它们
之间仍然什么都不插。

- **声明侧已经具备**：`ImagePassAttachment::m_viewDescriptor` 带完整的 mip / array / depth 范围。信息在
  `MergeImageBarriers` 按资源实体合并时被丢掉；它需要的谓词 `ImageViewDescriptor::OverlapsSubResource` /
  `IsSameSubResource` 已写好，零调用者。
- **主体代价在 `ResourceStateTracker`**：一个资源一个状态 → 一组子资源区间各自的状态。
- **Bloom 不需要它。** 链用每级独立纹理即可：层级由 **Scope 静态选定**。mip 链只在 **shader 运行期选层级**时
  才必需（HZB 的 ray march）。

---

## 已定的结构

### Pass 的声明

**静态链基本不变。** `Queue`、shader、管线状态、`Binds`、`RendersView`、`Inactive` 保留，都是 lowering 的输入。
变化只有：

- `.Accepts<>()` 移出静态链，成为 Scope 上的 `Select<>()`。
- `.Execute()` 不再人人都有：执行器直接从流里提交 item，默认的 `SubmitDrawBatch` 不再需要。`.CustomPipeline()`
  与 `.Execute()` 合成一个声明（如 `.Opaque(fn)`），只给不透明工作。
- `.Compile()` 消失：它今天做的"把 attachment 视图接到 shader 输入上"由 attachment 自己声明（见下）；常量
  sampler 这类静态内容移到静态链。

**动态部分只有一个回调**，声明资源、Scope、每个 Scope 的 attachment 与 item：

- **N 只在一处算。** 资源级数与 Scope 个数来自同一个循环。
- **版本自然解析。** 回调仍按 pass 的声明顺序执行；第 j 个 Scope 写第 j 级时 bump 版本，第 j+1 个 Scope 读到的
  就是它。pass 读自己写的版本不产生图边——建边时忽略自环，图只关心 pass 之间。
- **创建与访问分开。** `Create(name, desc)` 只引入资源，访问只在 Scope 上声明。
- **Scope 的个数由签名约束**（同 `RendersView` 只收一个 tag 的做法）：render pass 的回调直接拿到它唯一的
  Scope；compute / copy pass 的回调拿到 pass 声明器，可多次开 Scope。
- **attachment 直接声明它绑定到哪个 shader 输入**，编译器按反射出的布局查这个名字决定去处（见「绑定」）。不绑定到
  shader 的 attachment（渲染目标、深度、拷贝源 / 目标）没有绑定名，角色由 usage 决定。Scope 之间不同的标量
  （如本级尺寸）同样按名字声明：`s.Constant("g_OutputSize", size)`。
- **item 用声明时返回的句柄引用 attachment**（如拷贝 item 引用它的 src / dst）。slot 名只剩给 hook 查找的用途，
  hook 没了它也退场；调用处的 `<SPARK_PASS_TAG(...)>` 模板参数随之去掉。

示意（API 名字未定）：

```cpp
// 图形 pass：回调拿到唯一的 Scope
.RendersView<MainViewTag>()
.Build([](RenderScope& s) {
    s.Create("ShadowMask", desc);
    s.RenderTarget("ShadowMask", clearToOne);
    s.Read("GBufferNormal").Bind("g_GBufferNormal");
    s.Read("SceneDepth", asR32).Bind("g_Depth");
    s.Draw(DrawLinear(3), instances = sliceCount);   // slice 数只在这里算
})

// 网格 pass
.Build([](RenderScope& s) { ...; s.Select<OpaqueTag>(); })

// compute 链
.Build([](PassDecl& p) {
    const uint32_t n = BloomLevels(p.GetRenderSize());
    for (uint32_t j = 1; j < n; ++j) { p.Create(Level(j), ...); }
    for (uint32_t j = 1; j < n; ++j) {
        auto s = p.Scope();
        s.Read(Level(j - 1)).Bind("g_Input");
        s.Write(Level(j)).Bind("g_Output");
        s.Constant("g_OutputSize", LevelSize(j));
        s.Dispatch(GroupsFor(j));
    }
})

// CopyFrameBufferPass：两个 Scope，屏障由编译器推导，不再需要 Execute
.Build([](PassDecl& p) {
    auto s0 = p.Scope();  s0.Write("SwapChain", asRenderTarget);  s0.Clear(black);
    auto s1 = p.Scope();  auto src = s1.Read("SceneColor", asCopySrc);
                          auto dst = s1.Write("SwapChain", asCopyDst);
                          s1.Copy(src, dst);
})
```

### 绑定：space2 每个 pass 一组，Scope 之间的差异是 root constant

一个 `ShaderBindings` 是一张完整的描述符表，GPU 整张绑定；若 Scope 之间的差异也走描述符表，每个 Scope 就得各有
一组，随之而来的是"每帧重建的 Scope 如何找到它跨帧持久的那组"这个身份问题（DX12 的 `ShaderBindings` 内部按
`FrameCountMax` 轮换描述符表，按持久对象设计，不能每帧新建）。引擎已有的两样东西让这个前提不必成立：

- **bindless 已在用**：材质 shader 经 `ResourceDescriptorHeap[index]` 读纹理，`ImageView` / `BufferView` 提供
  `GetBindlessReadIndex()` / `GetBindlessReadWriteIndex()`。
- **`DispatchItem` 已支持 root constant。**

于是：

- **space2 每个 pass 一组，与今天相同**：sampler，以及所有 Scope 都相同的输入（如 LUT）。
- **Scope 之间不同的东西是 bindless 索引加标量**（"读第 j-1 级""写第 j 级""本级尺寸"），作为 **root constant**
  写进 Scope 的状态，执行器应用 Scope 状态时设置。它们直接写在命令列表里、每帧重设，不占描述符，没有在途帧
  问题，也没有身份问题。
- **去处由 shader 决定**：`.Bind(name)` 在反射布局里查到的是 space2 的 SRV / UAV，就把视图写进 pass 那组；查到的
  是 root constant 结构的字段，就写入该视图的 bindless 索引（读用 `ReadIndex`，写用 `ReadWriteIndex`）。
  `.Constant(name, value)` 只能落在 root constant。声明侧不区分。
- **校验**：同一 pass 的不同 Scope 给 space2 同一槽位绑了不同视图即报错——space2 在一个 pass 内只有一份。
- 图形 pass 只有一个 Scope，没有 Scope 间差异，`DrawItem` 里被注释掉的 root constant 不构成阻碍。
- 跨后端：Vulkan 需要描述符索引 + mutable descriptor 对应 `ResourceDescriptorHeap`，材质系统已依赖这一点，
  这里不引入新负担。

**会改主意的条件**：出现 Scope 之间不同、却无法 bindless 访问的输入。那时才需要每个 Scope 一组绑定，身份问题
随之回来。

### Scope 与归属关系的实体表示

```cpp
struct Scope           { Pass m_pass; uint32_t m_index; };   // 身份 + pass 内次序
struct ScopeAttachment { RHIHandle m_scope; };               // attachment 所属的 Scope
struct ScopeItem       { RHIHandle m_scope; };               // 单个 item、选择所属的 Scope
struct ItemSelection   { void (*m_collect)(RHIContext&, RHIHandle view, eastl::vector<RHIHandle>& submitList); };
```

```
PassContext（静态）       RHIContext（每帧）                             RHIContext（持久）
Pass ◄── Scope::m_pass ── Scope 实体
                            ▲                 ▲
            ScopeAttachment │                 │ ScopeItem
                            │       ┌─────────┴────────────┐
     attachment 实体 ───────┘   单个 item 实体          选择实体 ──查询──► 场景 DrawItem
     ImagePassAttachment        DrawItem / Dispatch…  ItemSelection      （带 DrawTag，不带 PassTag）
```

- **attachment 与 item 用两个组件**：没有哪一步同时遍历两者——事件与括号只看 attachment，提交区间只看 item 与选择，
  执行器只回到 attachment。合用一个组件只会让排序多出"有没有资源"一级，并要约定 item 排在段尾。

- **Scope 在 RHIContext，不在 PassContext**：它的子实体都在 RHIContext，引用都是 `RHIHandle`；`Pass` 的实体掩码
  只有 8 位且没有版本号，是为少量静态实体设计的。
- **引用一律子指向父**，Scope 不持有子列表。
- `m_collect` 由 `Select<Tags...>()` 模板实例化，写法同今天 `PassCapabilities` 里的函数指针。
- **每帧清空规则**：`Scope`、`ScopeAttachment`、`ScopeItem` 只出现在每帧实体上，帧末销毁带有它们的实体即可。场景
  item 永远不带它们。

### 编译产物放在哪里

**原则：数据放在引起它的实体上，并与它的变化频率对齐。** 不为编译产物另建记录实体——那样的记录大多是已有
实体上数据的副本。

| 产物 | 放在 | 理由 |
|---|---|---|
| 括号（`RenderPassBeginInfo`） | Scope | 图形 pass 恰好一个 Scope，一一对应；Begin 在提交区间前、End 在其后，由 Scope 隐含 |
| 屏障 | 引起它的 attachment | 屏障由一次访问引起，按资源合并后一条屏障对应一个 attachment；今天的 `CompiledImageBarrier` 已是如此 |
| 跨队列 release | 生产方的 attachment | 由生产方这次访问之后紧跟另一队列的访问引起 |
| 跨队列 wait / signal | Scope | 发生在 Scope 边界，每个队列至多一个 |
| 外部 fence wait | 首次触碰资源的 attachment | 由这次首次访问引起 |
| 颜色输出编号 | 渲染目标 attachment（`ColorAttachmentIndex`） | 声明时决定，存储按资源排序后不能再靠顺序推 |
| 随 pass 变的状态：PSO、space2、pass 共享绑定 | Scope | lowering 把 pass 的信息写到 Scope 上，执行器因此不必认识 pass |
| 随 Scope 变的状态：root constant（bindless 索引 + 标量） | Scope | 本来就属于它 |
| 随视图变的状态：viewport、space1 | View 实体 | 本来就在那里 |

完整状态 = Scope 这部分 + 当前视图这部分，不存快照。Work 从任何位置开始，重新应用这两部分即可。

### lowering：排序，线性扫描两遍

每一步只读三样东西：Scope 的子实体、pass 的静态数据（编译好的 PSO、队列、视图类型、`Binds`、静态 sampler）、
资源上的追踪器。除提交表外没有别的全局结构。依赖图仍建在 pass 上：声明 attachment 时经 Scope 知道所属 pass，
照样记入 `m_attachmentUses`，pass 读自己写的版本产生的自环忽略。

**第 0 步：排序。**

- **Scope 存储**按（pass 拓扑位置，Scope 序号）排序。
- **ScopeAttachment 存储**按（pass 拓扑位置，Scope 序号，资源）排序。
- **ScopeItem 存储**按（pass 拓扑位置，Scope 序号）排序；Scope 内的 item 无序，不需要更细的键。

排序后每个 Scope 的 attachment、item 各自连成一段；同一段里访问同一资源的 attachment 彼此相邻，按资源合并只看
相邻元素，不需要 map。今天逐 pass 打 / 清 `AttachmentCompilingTag` 的循环随之删除。

没有声明任何 attachment 的 pass 不进图，它的 Scope 在建图后即销毁，所以进入排序的 Scope 都有拓扑位置。

**第 1 遍：瞬态资源。** 遍历 attachment 得到每个瞬态资源的首末使用位置，分配并写好 backing。与今天相同，生命周期
的单位可以细到 Scope（见「独立推进」）。必须单独成一遍：下一遍写绑定要用到视图与它们的 bindless 索引。

**第 2 遍：逐 Scope 产出全部编译结果。**

```
for scope in Scope 存储（已排序）:
    pass        = scope.m_pass
    attachments = ScopeAttachment 存储里属于 scope 的一段
    items       = ScopeItem 存储里属于 scope 的一段

    // 事件：按资源遍历 attachments（相邻的已合并）
    for 资源 r:
        t = r 上的追踪器
        首次触碰: t = 初始状态; 若有外部 fence → scope 的 wait
        need = 状态不同 || 任一方写 || 队列不同
        if need:
            if 队列不同:
                release → t.lastAttachment;  signal → t.lastScope;  wait → scope
            acquire → 本 attachment
        t = { 新状态, scope, 本 attachment }

    // 括号（图形 pass）
    scope.BeginInfo = 由 attachments 里的渲染目标 / 深度 attachment 构建

    // 状态（Scope 这部分）
    scope.state = { pass 的 PSO, pass 的共享绑定, pass 的 space2,
                    root constant = 绑定到常量字段的 attachment 的 bindless 索引 + .Constant 的标量 }
    绑定到 space2 的 attachment 视图 + pass 的静态 sampler → 写进 pass 的 space2（同槽不同视图即报错）

    // 提交区间
    begin = submitList.size()
    for view in (pass 的就绪视图, 或 {无}):
        若有 view: submitList += view 句柄             // 后面没有 item 也照写
        submitList += items 里的单个 item
        for 选择 sel in items: sel.m_collect(view, submitList)   // 逐视图剔除挂在这里
    scope.submitRange = { begin, submitList.size() }
```

- **写几乎全是局部的**：只写当前 Scope 与它的子实体。唯一例外是跨队列时回填上一个生产方的 release 与 signal——
  今天把 release 推到上一个 pass 上也是这样，属于执行开始前的回填。
- **跨队列同步由资源追踪推出**：图的边全部来自资源访问，队列间依赖同样由此得出，不再需要
  `CompilePassCrossQueue2` 单独按前驱 / 后继推导。signal 值每个生产方 Scope 每帧至多一个、按队列单调递增；
  wait 按源队列只留最大值。
- **提交切分由组件直接决定**：带 wait 的 Scope 之前切、带 signal 的之后切，`BuildSegments` 消失。

| 一遍 | 读 | 写 |
|---|---|---|
| 0：排序 | Scope、ScopeAttachment、ScopeItem | 三个存储的顺序 |
| 1：瞬态资源 | attachment | 资源的 backing |
| 2：逐 Scope | attachments、items、pass 静态数据、追踪器 | attachment 上的 acquire / release；Scope 上的 wait、signal、BeginInfo、state、submitRange；提交表 |

**顺带的校验**：合并相邻 attachment 时发现同一资源两种用法即报错（今天 `MergeImageBarriers` 的断言挪到这里）；
"图形 pass 只有一个 Scope"由签名保证；"事件不落在括号内"因此自动成立。

**entt 的约束**（3.16，`registry.sort<T>(compare)` 原地重排紧凑数组并同步稀疏数组，按 `begin()`→`end()` 遍历
即为比较函数的升序）：

- 被 owning group 拥有的存储不能排序——`Scope`、`ScopeAttachment`、`ScopeItem` 不进任何 owning group。
- in_place 删除策略的存储有墓碑时不能排序——三者可平凡移动、不声明 `in_place_delete`，走默认的 swap_and_pop。
- 多组件 view 由最小的存储驱动迭代，不一定按排好的顺序走——lowering 只遍历单个存储（`GetStorage<T>().each()`）、
  按实体 `TryGet` 其他组件，或用 `view.use<ScopeAttachment>()` 指定驱动存储。
- 排序后该存储不能再增删，否则顺序被打乱——lowering 期间三者都不增删；给 attachment、item 添加别的组件不受影响。

### 流 = 排好序的 Scope + 提交表

- **Scope 存储即流的顺序**（第 0 步已排好）。执行器按流顺序遍历全部 Scope，每个队列各开一个 CommandList；ScopeAttachment 存储按同一顺序
  排好，执行器同步推进两者，像归并一样一起往前走，Scope 不持有 attachment 列表，也不记下标。执行器不回到 item：
  它们已在 lowering 时写进提交表。
- **Scope 的提交区间是提交表里的一段**：`ScopeSubmitRange { begin, end }`，提交表 `m_submitList` 由执行器持有、
  lowering 经参数写入。lowering 展开 Scope 时，每个就绪视图**先写入该视图的句柄，再写它的 item**：

  ```
  提交表: … | view0 | item item item … | view1 | item item … | …
             └──────── Scope S: ScopeSubmitRange { begin, end } ────────┘
  ```

  视图句柄就是提交表这条流上的一个状态标注，和"标注挂在位置上"、"一个句柄是什么由它的组件决定"是同一条规则。
  viewless 的 Scope（compute、copy）没有视图句柄。**没有 item 的视图也照写句柄**：不撤回，不依赖 CPU 知道 item
  个数——GPU-driven 时本来就不知道；代价只是几次幂等的状态设置。不透明 hook 因此必须接受空的视图段。
- **所有 item 都进提交表**，单个 item 也是——图形 pass 的单个 item（全屏三角形）按视图重放，同样多次出现。
- **选择在 lowering 时按视图展开**，命中的 item 写在该视图句柄之后；逐视图剔除挂在这一步。
- **提交只在同步点切分**：每个队列在相邻两个同步点之间录成一个 CommandList，带 wait 的 Scope 之前、带 signal 的
  Scope 之后切出一次提交。今天 `ExecuteGroup` / `ExecuteWork` 还叠着两件事——多线程并行录制（按负载把 draw 分给
  多个 CommandList）与提前提交（录完一段先交给 GPU）——**这次都不实现**：多线程录制尚未就绪，现状本就是单线程；
  最终目标 GPU-driven 下一帧只剩几百条命令，单线程录制远低于 1ms，帧间流水线也已盖住提前提交想省的空隙。
- **跨队列顺序由 fence 在 GPU 上保证**：fence 带值（Vulkan 对应 timeline semaphore），wait 可以先于 signal 提交，
  CPU 端只需同一队列内有序。多线程时拆成三步：按流顺序切分出录制单元（串行）；并行录制，按流顺序排优先级；
  各队列按自己的单元顺序提交，互不等待。单线程的执行器就是三步合一：按流顺序录制，切出一段就交给它的队列。
- **`WorkStart` 作为接缝保留**：Scope 上可以带一个切点标签，执行器遇到它就换一个 CommandList；按预算打标签的
  逻辑不建。图形 Scope 内部不能切（括号），所以切点只落在 Scope 边界；render pass 支持 suspend / resume 后需要
  Scope 内的切点，届时切点作为一种标注句柄写进提交表。**重新考虑的条件**：profiling 显示录制成为瓶颈，或出现
  大量无法 GPU-driven 的 CPU draw（CPU 排序的大量透明物体、编辑器调试绘制）。
- **不透明工作**：Scope 上挂 execute hook 的引用，代替提交区间；执行后状态缓存失效。
- **静态导入资源保持现有路径，不进 Scope**：它们（顶点缓冲、采样纹理等）用途确定且唯一，attachment 挂在资源实体
  本身、不带 `ScopeAttachment`，由 `CompileStaticResourceBarriers` 按队列一次性处理，状态直接读 RHI 资源、不经追踪器。执行器
  在每个队列的第一个 Scope 之前执行它的预屏障（外部 fence wait 在最前）。
- 今天执行器的 `PassBarriers`、`SubmitBatch`、`PassSubmitTable`、`ExecuteWorkItem`、`QueueSegment` /
  `ExecuteGroup` 全部由此取代，`BuildSegments` / `BuildExecuteGroups` / `BuildExecuteWorks` 随之删除。

**执行循环**：

```
for each Scope（按序）:
    上一个 Scope 收尾: [End] → 它的 attachment 上的 release → signal
    wait → 本 Scope 的 attachment 上的屏障 → [Begin] → 应用 Scope 的状态
    for each submitList[range.begin, range.end):
        视图句柄 → 应用它的 viewport、space1
        其他     → 提交 item
```

"闭括号 → 事件 → 开括号 → 状态 → item"写在这个固定两层的循环里。零 item 的 Scope 提交区间为空，屏障与 Begin / End
照常执行，清屏不受影响。

### 三个例子的降级

```
图形 pass（两个视图）              compute 链（N 个 Scope）          不透明工作（UI）
Scope P.0                          Scope P.s（s = 0..N-1）            Scope P.0
  attachment: 屏障 × k               attachment: 该 Scope 的屏障        attachment: 屏障
  BeginInfo, PSO, space2             PSO, space2, 该 Scope 的 root const  BeginInfo
  提交: | v0 | items | v1 | items    提交: | dispatch item             execute hook
```

### 唯一的非 ECS 结构：提交表

提交表是**一个 Scope 按视图排好序的提交序列**：推导不出来的内容（CPU 逐视图剔除的结果、以后 CPU 排序的透明物体）
只能存下来，而同一 DrawItem 被多个 pass、多个视图各提交一次，每次出现建一个实体意味着每帧上千个实体的增删。

- **执行器按条目的组件分派**：`View` 是状态标注，其余按 DrawItem / DispatchItem / CopyItem 提交——"种类由组件决定"。
- **代价**：每个条目多一次"是不是视图"的判断，一次稀疏集查找，相对 Submit 可忽略。
- **GPU-driven 后变短，形状不变**：间接绘制也只是一种单个 item（引用剔除输出的 attachment），提交表从"每视图上千
  个 DrawItem"变成"每视图若干个间接绘制"；仍由 CPU 处理的 draw 照旧。它的 GPU 端后继是剔除 pass 输出的间接参数
  缓冲，一条普通的图资源依赖。
- **会改主意的条件**：提交表要直接上传给 GPU 当纯 item 列表时，夹在里面的视图句柄得先剔掉——届时改为 lowering
  另记视图边界。

### 单个 item 的生命周期：每帧重建

- **对账问题整个消失**：不存在跨帧活下来的过期 item（链从 7 级缩到 6 级时残留的第 7 个）。
- **与所引用的资源同生同灭**：单个 item 引用的多是瞬态附件，它们本来就每帧重新声明。
- **句柄只在本帧有效**，任何东西不得跨帧持有。
- **挂在 item 上的 GPU 对象随 item 一起销毁即可**：引擎所有 GPU 对象都是延迟释放的。
- 规模：每帧新增的实体只有 Scope、单个 item、选择，数十个量级。

---

## 需要完成的事

### 前置

0. **root constant 端到端**：「绑定」一节假定已具备，实际没有——`DispatchItem::m_rootConstants` 只有字段，DX12
   `Submit` 不读；RHI `CommandList` 无设置接口；反射不产出 root constant 布局（`SetRootConstantsLayout` 零调用者）。
   需要：跨后端的 shader 约定（DX12 root constants / Vulkan push constant，占用哪个 space 实现时再定）、反射识别、
   DX12 root 参数、`CommandList::SetRootConstants`。只卡第 7 条的 root constant 部分与第 17 条。

### 声明

1. **Scope 实体与归属**：`Scope`、`ScopeAttachment`、`ScopeItem`；attachment 挂 `ScopeAttachment`，单个 item、选择挂 `ScopeItem`。
2. **pass 声明改造**：单一动态回调；创建与访问分开；render pass 回调拿唯一 Scope，compute / copy 可开多个；
   attachment 声明绑定目标；删除 `.Compile()`，`.CustomPipeline()` + `.Execute()` 合成 `.Opaque()`。
3. **选择**：`Select<DrawTags...>()` 与 `ItemSelection`；`.Accepts<>()` 移除。
4. **router 只做派生**：不再打 PassTag；`PassCapabilities` 删去 `m_accepts` / `m_markSubmitItem` /
   `m_collectSubmitItems`。

### 编译

5. **屏障以 Scope 为单位编译**：ScopeAttachment 存储排序后线性遍历，追踪器记上一个 attachment（Scope 从它的
   `ScopeAttachment` 读），删除 `AttachmentCompilingTag` 循环；屏障写在引起它的 attachment 上，release 写在生产方 attachment 上。判据统一为
   "状态不同或至少一方是写"——今天 `Merge*Barriers` 丢弃 src == dst 的屏障，相邻两个 pass 以 storage 写同一资源
   时没有任何屏障，这一条随之修正。
6. **跨队列同步由资源追踪推出**：wait / signal 写到 Scope 上，删除 `CompilePassCrossQueue2`。
7. **绑定分流**：`.Bind` 按反射布局落到 pass 的 space2 或 root constant（bindless 索引）；`.Constant` 写 root
   constant；Scope 的 root constant 由执行器在应用 Scope 状态时设置；space2 同槽不同视图的校验。
8. **lowering**：第 0 步排序与第 2 遍；把 PSO、绑定、`RenderPassBeginInfo` 写到 Scope 上；按视图展开提交区间进
   提交表（视图句柄分隔）；`BasicContext` 补 storage 排序的封装，以及单存储遍历 / 指定驱动存储的 view 写法。
9. **提交切分**：只在同步点切，由 Scope 上的 wait / signal 直接决定；`WorkStart` 只留接缝（执行器认它），负载切分
   不实现。

### 执行

10. **执行器改为遍历 Scope**：删除 `PassBarriers` / `SubmitBatch` / `PassSubmitTable` / `ExecuteWorkItem` /
   `QueueSegment` / `ExecuteGroup` 等表，执行循环只认 Scope、attachment 与提交表。
11. **dispatch 提交路径**：`DispatchItem` 作为单个 item 进提交表。
12. **不透明工作**：Scope 上的 execute hook，执行后状态缓存失效。

### 迁移（验证用例）

13. **全屏三角形改由 pass 声明**，删掉 RenderSystem 里那个 `GeometrySpec` + `FullScreenTriangleTag` 实体。
14. **`ShadowProjectionPass` 收回自己的 draw**，slice 数只算一处，`ShadowMaskSystem` 不再代管 DrawItem。
15. **`CopyFrameBufferPass` 改为两个 Scope**：清屏与拷贝之间的屏障由编译器推导，删掉手写的两条屏障与 Execute。
16. **UI 改为不透明工作**。
17. **Bloom 降采样 / 上采样**：第一个多 Scope 的 compute pass（P3 步骤 2、3）。

### 独立推进，不是前提

18. **子资源屏障**：`ImageBarrier` 带子资源范围（DX12 展开成逐子资源的屏障）、合并按范围重叠、追踪器按子资源区间、
    同一 Scope 读写重叠子资源时报错。等到有链改用 mip 链（HZB）时再做。
19. **瞬态生命周期按流位置计算**：取代 pass 位置，链的各级可能因此互相别名。

---

## 落地进展

每一步结束引擎照常出图；旧实现不删、只停止调用，最后统一清理。

| 段 | 内容 | 条目 | 状态 |
|---|---|---|---|
| A | 执行侧换成 Scope；旧声明 API 保留，每个 pass 一个 Scope | 1、5、6、8、9、10、12 | A1–A4b 完成（A4b 帧率问题待查），A4c 待做 |
| B | 新声明 API，迁移全部 pass，删旧 API | 2、3、4、7（space2 部分）、13–16 | |
| C | root constant 与 dispatch 提交路径 | 0、7（其余）、11 | |
| D | Bloom | 17 | |

### A 已完成

| 提交 | 内容 |
|---|---|
| `11f3b31` | `Scope` / `ScopeAttachment` 实体；`ContextStorage::Sort` 及锁定 entt 迭代顺序的测试 |
| `ab7587a` | `SortScopes`；没声明 attachment 的 pass 不进图，其 Scope 建图后销毁 |
| `b1bd158` | 屏障按 Scope 编译，`Pre*` / `Post*Barrier` 挂在 attachment 上；新判据只多出 SceneColor 四条写后写 |
| `dbe1855` | `RenderPassBeginInfo` 挂在 Scope 上；颜色编号改为声明时写入的 `ColorAttachmentIndex` |
| `e83b1da` | `ScopeWait` / `ScopeSignal` / `ExternalWait`；fence 值按流顺序两阶段分配 |
| `6d01d72` | A4a：`ScopeState`、`ScopeSubmitRange` 与提交表，旧路径对照校验 |

过渡适配（A4 后删）：`CollectPassBarriers`、`CollectPassBeginInfo`、`CollectPassSync`、`SplitPassesByQueue`。

**未验证**：现有 pass 全在 Graphics 队列，跨队列路径没有运行时覆盖，第一个 compute pass 接入时补验。

### A4 方案

- **A4a lowering 产出，不消费**：`CompileScopeState` 从 pass 抄 PSO 与共享绑定的指针
  （`ScopeState`）；`CompileScopeSubmitRanges` 把每个 Scope 按就绪视图展开进执行器的提交表（`ScopeSubmitRange`），
  视图句柄在前、item 在后，item 仍用旧的 `m_collectSubmitItems` 收集，过滤规则照搬 `BuildPassSubmitTable`，但空的
  视图段不撤回。`ResolveTargetViewport`（改为收 `RenderPassBeginInfo`）与 `ResolveViewShaderBindings` 移到
  `RenderGraphUtils.h`。验收：`ValidateScopeSubmitRanges`（`s_scopeSubmitValidation`，A4c 删）逐 pass 对比新区间
  与旧 `SubmitBatch`——剔掉视图句柄后 item 逐个相等，非空视图段与 batch 一一对应且 viewport 相同。已在空场景
  （全屏 pass、空视图段、单个阴影视图）运行无断言；**带网格的场景、多个阴影视图尚未覆盖**。
- **A4b 新执行器**：`ExecuteScopes` 按流顺序遍历全部 Scope，同步推进 `ScopeAttachment` 游标，
  每个队列各开一个 CommandList。wait / 外部 wait 前切、signal 后切，认 `WorkStartTag`（只有接缝，无人添加）；
  别名屏障在 pass 第一个 Scope 开头向 pool 取；release 屏障只入队，随同一 CommandList 上的下一次 Flush 一起提交。
  提交区间里带 `View` 的句柄应用 viewport 与 space1（有 PSO 才绑），其余按组件（DrawItem / DispatchItem / CopyItem）
  提交，下标即提交表中的位置。hook 引用由 lowering 写成 Scope 上的 `ScopeExecute`；`RenderPassBuilder` 不再默认装
  `SubmitDrawBatch`，有 hook 的 Scope 按"每个视图段调一次（含空段）、空区间调一次"调用，Skybox / UI / copy 未改。
  队列与别名屏障用的 pass 位置直接从 pass 读。`s_scopeExecution`（A4c 删）可切回旧执行器对照，旧 `Execute` 为此
  在无 hook 时回退到 `SubmitDrawBatch`；帧末的"队列本帧是否有工作"改由 `IsQueueActive` 回答。验收：新旧两条
  路径在空场景各运行 40 秒，D3D12 debug layer（遇错中断）与断言均未触发；带网格的场景画面正确。
  **帧率问题待查**：带网格的场景里新路径帧率明显低于旧路径，原因未定位。空场景（每帧 6 次 Submit）的对照：
  执行段新路径约 1.85ms、旧路径约 1.41ms，这 0.4ms 与 item 数无关，而与 Scope 数有关。可疑点之一：
  编译阶段的 `CollectPassBarriers` 仍在查询 `GetDeviceMemoryBarriers`，新执行器在执行段又查一次。但 Scope 数不随网格增加，所以这一点解释不了带网格场景的差距。
- **A4c 清理**：删除所有已不被调用的函数、组件与适配。

待定（括号内为倾向）：不透明工作后的状态失效，RHI 缺接口（暂不做，UI 目前是最后一个 pass）。

### 落地中确定、原方案没写到的

- **颜色编号必须显式**：旧代码靠两次 entt 逆序遍历抵消得到声明顺序，按资源排序后不再成立。
- **signal 值按生产方的流顺序分配**：扫描时按消费方发现顺序分配会让同一队列的值倒退，所以先记录生产方，再按流
  顺序统一赋值。
- **别名屏障不挂 attachment**：一个资源可能对应多条，挂上去需要容器；数据本在 pool 里，执行时按 pass 位置取。
- **外部 fence wait 挂在首次触碰的 attachment 上**，不放 Scope 上的列表。
- **不引入事件表**：屏障会永久存在，把它们抄进提交表是另建记录，违背"数据放在引起它的实体上"。

### B / C 开工前要定的

- `.Constant` 也按反射落到 space2 cbuffer（TemporalAA 每帧写 space2 标量），与 `.Bind` 对称。
- space2 本帧没被 attachment 绑定的槽由 lowering 统一写 null（Skybox 注释说明了不写的后果）。
- `ComputePassBuilder` 与 render pass 一样自动创建 space2 SRG。
- 用到 bindless 的 pass 断言设备支持（root signature 的直接索引标志受 `m_bindless` 控制）。
- Skybox 的 Execute 只是条件绘制，改为 Build 里条件 `Draw`；不透明工作只剩 UI。

---

## 待优化

按"契合 ECS、数据驱动、设计自然、可扩展"四条评估后，概念层（pass 是描述 / 流是执行、Scope、数据放在引起它的
实体上、单一屏障判据、选择即查询）站得住，问题集中在机械层。以下三处优先回看：

- **排序 + 同步推进的不变量横跨编译与执行。** 第 0 步排好的 ScopeAttachment 存储顺序要一直保持到执行器同步遍历结束，中间
  不能增删；为此列了四条 entt 约束，说明它依赖的是存储布局而不是数据本身。比较函数还要跨 registry 跳三级
  （`ScopeAttachment` → `Scope` → pass → `PassGlobalTimeline`）。执行器回到 attachment，只因为屏障挂在 attachment 上。
  **关键问题：执行器是否必须回到 attachment？** 若它只需 Scope 与提交表，排序就退回 lowering 内部的实现细节，跨阶段
  不变量随之消失。
- **版本解析依赖 pass 的注册顺序。** 按名字读到的是"在它之前声明的 pass 写出的最新版本"，调换两个 pass 的注册
  顺序语义就变。这是今天已有的隐式依赖，Scope 之间的读写也建立在它上面，分量更重了。
- **`.Bind(name)` 的双重去处。** 同一写法由反射决定落到 space2 还是 root constant，两种机制藏在一个名字后面，出错
  要到编译期才暴露。可考虑声明侧显式区分（如 `.Bind` 与 `.BindIndex`），代价是作者要多知道一件事。

另有两处评估为可接受：提交表（容器 + 下标、混着视图句柄）GPU-driven 后变短但形状不变；残留的函数指针
（`ItemSelection::m_collect`、不透明 hook、`m_collectViews`、`m_resolveSharedBindings`）已少。每帧全量重建与
排序排除了"图不变就复用编译结果"的增量优化，现在不需要，若以后要做会成为障碍。

## 未决

- **PSO 变体**：一个视图段内按变体再切，变体切换同样是状态标注，可像视图句柄一样写进提交表。等变体落地再定。
- **多视图下 Scope 与视图的展开顺序**：只影响"有多个 Scope 又按视图重放"的 compute pass（每个视图一条 Bloom
  链），与"多视图各有附件时按视图实例化 pass"一起等真实用例。
- **不透明工作的比例**：若大量工作无法表达为 item，流的大部分成为不透明工作，本模型的收益大幅缩水。拷贝已可
  表达为 Scope + item，目前只剩 UI，以及以后的 NRD。
- **`TODO_DrawItemPersistencePlan.md` 已部分过时**：它主张每 (Drawable, pass) 一个骨架，理由是 PSO 各 pass
  不同、要存进骨架；PSO 已挪出 item，代码也已走到共享一份 DrawItem，本改造还去掉了 item 上的 PassTag。那份文档
  要跟着更新。

---

## 关联文档

- `TODO_PostProcessPlan.md` —— P3，Bloom 是本改造的第一个用户
- `TODO_RenderPipelineRoadmap.md` —— I3（compute 进图）、I4（子资源屏障）
- `TODO_DrawItemPersistencePlan.md` —— 场景 DrawItem 的持久化，本改造保留其持久语义
- `TODO_MultiViewPlan.md` —— 多视图附件，第四条理念的复验点
