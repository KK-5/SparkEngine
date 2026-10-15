# P5 透明物体　实现方案

路线图 P5 的落地计划。总览见 `TODO_RenderPipelineRoadmap.md`。

本文是草案。2026-10-15 与用户讨论后写下；2026-10-16 按后续讨论改写：分组换成两段式（先可见性、再按 pass 分桶），
PSO 改为由"pass + 材质"查找、可在加载时推导，覆盖材质的状态定为可覆盖。P5 原来的六项里 HeightFog 不做（用户定），
Distortion 待定（D9）。现状一节来自 2026-10-15 / 16 读代码，没有运行验证。

P5 做三件事：**按材质的 PSO 变体**（I5，含 Masked 与双面）、**参数由 CPU 生成的间接绘制**、**前向着色的半透明**。
做完之后，场景物体有两种提交方式，都是间接绘制：参数由 GPU 生成（不透明、投影）与参数由 CPU 生成（要 CPU 决定
顺序或集合的，先是半透明）。直接绘制仍只给零星的绘制。

---

## 状态

| 步骤 | 内容 | 依赖 | 状态 |
|---|---|---|---|
| 1 | 两段式剔除 + 每个 pass 的分桶 + item 级 PSO 切换，用 doubleSided 验证 | D8、D10、D11 待定 | 未开始 |
| 2 | pass 的 layout 以声明为准 + shader 宏排列 | — | 未开始 |
| 3 | Masked | 1、2 | 未开始 |
| 4 | 参数由 CPU 生成的间接绘制（通用机制） | 1；`DrawIndirect` 接收顶点流附件（`TODO_BufferRangePlan.md` §五） | 未开始 |
| 5 | 半透明（前向，BeforeDOF） | 1、4 | 未开始 |
| 6 | Distortion | 5 | 待定（D9） |

1 与 2 互不依赖。1 不碰 layout 与 shader 系统，先用纯渲染状态的变体把拆段、分桶、切换验证掉。

---

## 一、现状

**PSO 与 layout**

- 每个 pass 一个 PSO（`RenderGraphCompiler::CompilePipelineStates`），执行器在每个 Scope 开头设一次
  （`RenderGraphExecuter.cpp:326` 的 `RecordScope`），item 之间不换。PSO 在渲染图编译时同步创建。
- pass 的 `PipelineLayoutDescriptor` 来自 pass 自己 shader 的反射（`PassBuilder.h:28` 的 `BuildPipelineLayoutFromShaders`）。
  DepthPre 只绑 View 与 Instance；Masked 的 PS 要读 `g_Materials`（space3）、bindless 贴图与 sampler，反射出的 layout 会不同。
- `ShaderAsset` 没有宏，也没有排列。
- `RHI::PipelineLibrary` 是从 Atom 移植的空壳：序列化被注释掉，DX12 后端注明 `ID3D12PipelineLibrary` 没有在用。
- `TODO_PerDrawPSOVariant.md` 的方案 A / B 都没有实现。

**材质与分类**

- 材质是 MaterialContext 里的实体，带 `Resource::StandardPBR` 与 `Resource::MaterialState`。`MaterialState` 有
  `m_alphaMode`（Opaque / Mask / Blend）、`m_alphaCutoff`、`m_doubleSided`，注释里预留了在 `AlphaMode` 末尾追加
  Additive / Modulate / 预乘。渲染侧没有读者。
- 每个材质有一个 `g_Materials` 槽位（`MaterialSlotRef`），实例记录里的 `m_materialIndex` 就是它；解析不到时回退到默认材质。
- `MaterialData` 68 字节，没有 alpha cutoff；`m_baseColor.a` 是预留的。
- `SyncOverrideMaterials` 用 `CreateMaterial(matCtx, params)` 建覆盖材质，状态取默认值（不透明、单面），被覆盖材质的状态不会带过去。
- `ClassifyDraw`（`Drawable/DrawMask.cpp:17`）恒为不透明加投影。`TransparentTag`（`Drawable/DrawTag.h:18`）已声明、没有读者。
- `RenderSystem` 的更新顺序是 `MaterialBindingSystem` → `MeshGeometrySystem` → `GeometryBindingSystem` → `InstanceBindingSystem`
  （`RenderSystem.cpp:342-347`），都在渲染图构建之前。

**场景绘制**

- DepthPre / GBuffer / Shadow 各一行 `s.DrawIndirect(arguments, count)`。剔除 pass 每个分类一个，每个视图一个 Scope，
  视图 k 的段从 `k × maxDrawsPerView` 条开始，count 每个视图一个（`TODO_GPUDrivenPlan.md` D7 / D13）。剔除与写 draw 记录在同一个 shader 里。
- `DrawIndirect` 把 `MeshGeometryBaseBufferTag` 的底层 buffer 绑成 VB 与 IB；`TODO_BufferRangePlan.md` §五 定了要改成
  接收顶点流附件。
- DepthPre 的输入布局只声明 POSITION；GBuffer 声明完整的标准顶点格式。lowering 取顶点流步长用的是 pass 的输入布局
  （`CompileItemInputAssembly`）。
- GBuffer 的深度测试是 `Equal`、不写深度。Shadow 有 PS。

**直接绘制**

- `RenderScope::Draw` 每次建一个帧内实体，带一份 `RHI::DrawItem` 与一个持 `eastl::vector` 的 `ItemInputAssembly`，
  lowering 逐个绑 buffer，帧末逐个销毁。每个 item 是微秒级的框架开销，定位就是零星的绘制（`TODO_GPUDrivenPlan.md` D9）。

**主机可见的每帧 buffer**

- `PendingBufferInit` 设 `HeapMemoryLevel::Host` 并加 `PerFrameTag`，`RHIResourceSystem` 建 `m_frameCountMax` 份；
  `PendingBufferMap` 把调用方的内存拷进这一帧那一份，映射指针不出 RHI。`g_Lights` 等经 `StagedArrayBuffer` 用它。
- 渲染图导入 `BufferPerFrame` 时每帧指向当前帧那一份（`RenderGraph.cpp:384`）。
- 已存在 `Components::Buffer` / `BufferPerFrame` 的实体不会再被 `PendingBufferInit` 处理，换容量要换实体。

**光照**

- `Lib/Lights.hlsli` 有 `EvaluateLight` 与 `EvaluateRectLight`。Lights pass 的阴影读 `ShadowMask`（屏幕空间，
  只对不透明深度有效）；按灯采 atlas 的代码在 ShadowProjection 里。

---

## 二、决策记录

### D1　两种提交方式，都是间接绘制　✅ 已定

- **参数由 GPU 生成**：不需要 CPU 决定什么的大量物体（不透明、投影），即 I11 已有的路径，加上 D3 的分桶。
- **参数由 CPU 生成**：需要 CPU 决定顺序或集合的物体。半透明是第一个用例，做成通用机制（D5），不写成半透明专用。
- **直接绘制**保持"零星"的定位：全屏三角形、调试绘制、gizmo。不为大量物体优化它。

不为大量物体优化直接绘制的理由：优化到头（持久缓存绘制命令、排序去重、多线程录制）仍是 CPU 代价随"物体 × 视图 × pass"增长，
且用不上只有 GPU 驱动才做得到的剔除（本帧 HZB 遮挡剔除、cluster 剔除、GPU 选 LOD）；做到那一步要把 D9 刚删的持久 `DrawItem`
体系重新建一遍。间接绘制表达不了的只有记录里放不下的逐绘制状态，它们的处理见下表，剩下的差别只是"参数由谁生成"：

| 逐绘制的变化 | 处理 |
|---|---|
| 换 PSO | 按段切开，每段一次调用 |
| 换 VB / IB | 几何统一进池 |
| 逐绘制常量、贴图 | 实例下标查 buffer、bindless |
| 顺序由 CPU 决定 | CPU 生成参数 |
| scissor、stencil ref 等固定功能状态 | 例外，按段切开或改在 shader 里做 |

### D2　item 带 PSO，执行器在 item 之间切换　✅ 已定方向

- 同一个 pass 的所有 PSO 共用一份 layout（`TODO_PerDrawPSOVariant.md` 的约束不变；layout 怎么来见 D4）。
- 一个 item 用哪个 PSO 来自 D11 的查找；执行器在 item 的 PSO 与当前不同时 `SetPipelineState`。Scope 开头仍设 pass 的默认 PSO。
- 不透明一个桶一个 item，半透明一段一个 item：同一套机制，后者是"每段一个物体"时退化成逐物体切换。

**要查的**：layout 相同时换 PSO，DX12 后端是否保留已绑的资源与 root constant（执行器在视图之间写的 `viewIndex`）。
按原生语义两边都保留（DX12 root signature 不变则 root 参数不失效，Vulkan 兼容 layout 下 descriptor set 不失效），
后端实现可能把绑定标脏。

### D3　两段式：可见性每个视图算一次，各 pass 再按自己的桶分　✅ 已定方向

即 `TODO_GPUDrivenPlan.md` D10 里 2026-10-05 对过的方向。取代本文 2026-10-15 版的"与 pass 无关的组 + 每个 pass 映射"
（那一版多了"组"这个概念和"材质 → 组 → 变体"一层映射，深度类 pass 的调用数是组数而不是它自己的 PSO 数）。

**骨架**（GPU 驱动处理 PSO 不同的通用做法）：一次间接调用里不能换 PSO，所以同一个 PSO 的实例必须在参数 buffer 里连续；
CPU 按每个桶的实例数给它划一段，GPU 往段里追加记录并计数，CPU 不回读；每个桶一次间接调用。

**两段**

- **第一段 可见性**：由现在的剔除 pass 改过来，每个分类一个 pass、每个视图一个 Scope，视锥测试照旧；输出从 draw 记录改成
  可见的实例号（每个视图一段）与每个视图的可见数。与 pass 无关。
- **第二段 分桶**：每个绘制 pass 一个 compute pass（写参数与按参数调用不能在同一个 Scope，I11 D9），每个视图一个 Scope；
  读可见列表，按"材质槽位 → 这个 pass 的桶"查到桶号，往桶的段里追加 draw 记录并计数。
- DepthPre 与 GBuffer 读同一份不透明可见列表，各有自己的第二段，"画同一批物体"由构造保证；以后加双阶段 HZB 遮挡剔除只改第一段。
- 写出的参数段照旧由写的一侧用 `IndirectArgumentsOf(view, offset)` 标在访问上（D13 不变）。

**查找与分桶是两件事**

- **查找**："pass + 材质 → PSO"，决定这个材质在这个 pass 里用什么画（D11）。
- **分桶**：把实例按查找的结果排到一起。桶就是"这个 pass 的一个 PSO"，"材质 → 桶"是查找结果按 PSO 去重。
  分桶之所以存在，只是为了把调用次数从材质数压到 PSO 数。

**桶的粒度**（三种都用同一个查找，区别只在实例按什么粒度排在一起）

| 桶等于 | 每个 pass 的表 | 每个视图的调用数 | DepthPre / GBuffer 共用分段 |
|---|---|---|---|
| 材质 | 不需要（材质槽位就是桶号） | 材质数 | 可以，第二段可并回第一段 |
| 着色描述的组（与 pass 无关） | 不需要 | 组数 | 可以 |
| 这个 pass 的 PSO | 需要（材质槽位 → 桶） | 这个 pass 的 PSO 数 | 不行，各自第二段 |

倾向"这个 pass 的 PSO"：每个材质一个桶在 500 个材质、主视图加 16 个阴影视图时是近 9000 次间接调用，多数在某个视图里条数为 0，
框架的逐 item 开销与 GPU 的逐调用开销都受不了。"每个材质一个桶"最简单，可以作为第一步，以后换成按 PSO 合并时 GPU 一侧只多一次查表。没有定。

**两段之间的问题与解法**

- **第二段怎么知道每个视图的可见列表在哪**（两边各自枚举视图再按次序对齐，少一个就整体错位，D13 指出过）：
  - A. 视图布局（参与的视图、次序、段起点）每帧算一次，两段都读这一份（倾向）。
  - B. 第一段标在访问上，第二段去查：第二段是 compute，常量在 Build 时就要定，渲染图要先支持"compute 的常量在解析时按视图填"。
  - C. 第一段顺便写第二段的间接派发参数：段起点仍要两边一致，只解决一半。
- **每个桶预留多少**（段起点由 CPU 录制时填，CPU 必须事先知道容量）：
  - A. CPU 每帧统计每个（列表，材质槽位）的实例数，各 pass 按自己的分桶加起来再做前缀和（倾向；实例编码时本来就逐个碰实例）。
  - B. 每个桶都按"这个视图全部可见"预留：内存是桶数倍。
  - C. GPU 计数再前缀和：做不到，GPU 算出的起点 CPU 拿不到（I11 D7）。
- **一帧之内一致**：分桶结果、表、容量、绘制声明都基于同一帧的材质与实例状态，同帧算同帧用，没有跨帧的 GPU 状态，不需要延迟回收。
  统计要在两段的 Build 之前完成，现在的更新顺序已满足。
- **第二段派发多少线程**：先用 CPU 知道的上界，超过可见数的线程退出；以后可改成第一段写派发参数、第二段 `DispatchIndirect`。

代价：每个（绘制 pass，视图）多一次派发；每个视图多一段可见列表（每个实例 4 字节）；不透明的参数 buffer 从一份变成 DepthPre、GBuffer 各一份。

### D4　pass 的 layout 以声明为准　✅ 已定方向

- pass 的 layout 由 `Binds<...>` 的各个共享组加上 pass 自己的 space2 决定，不再反射默认 shader 得到。layout 因此在 pass 设置时就确定，
  不再是 compiler 的中间产物（D11 在渲染图之外建 PSO 依赖这一点）。
- 每个变体校验自己引用的资源是这份 layout 的子集。
- 顺带去掉 `SpaceZeroKeepAlive`（`TODO_StructureAlignPlan.md`，路线图 P2 遗留第一条）。I11 D6 里 `InstanceBindings` 加 cbuffer
  被 `CommandList.cpp:219` 的断言挡住，根源相同；`InstanceSlotCount` 要不要挪进去另定。
- **同一个 pass 的变体之间顶点流步长必须一致**：lowering 按 pass 的输入布局取步长。深度类 pass 一律声明完整的标准顶点格式，
  VS 不用的通道不读；加一条"变体与 pass 的步长一致"的校验。

### D5　参数由 CPU 生成：用 RHI 现成的主机可见每帧 buffer，直接读　✅ 已定

- 参数 buffer 是一个 `PerFrameTag` 的主机可见 buffer：上层填自己的 CPU 数组，挂 `PendingBufferMap`，拷进这一帧那一份由
  `RHIResourceSystem` 做；渲染图导入它、每帧指向当前帧那一份，pass 声明 `IndirectArguments` 读。
- **上层不管帧数、暂存与延迟释放**（用户 2026-10-15 定）。不在渲染层做暂存环加 Copy pass；`TODO_BufferRangePlan.md` §三 / §四
  里"暂存环形缓冲 + 拷贝 Pass 在渲染层"那一段与此冲突，那份计划另行设计。
- 不先拷进 transient buffer：参数只有几 KB，显存读省下的延迟可以忽略；以后要在 GPU 上再加工（例如半透明的 HZB 遮挡剔除，
  把被挡住的记录的 `instanceCount` 写 0，顺序与分段不变），由那个 compute pass 把主机 buffer 当 SRV 读、写一个 transient buffer。
- `RenderScope::DrawIndirect` 加不带 count 的重载：（参数访问，字节偏移，条数，PSO）。不走 D13 的查找，偏移由 pass 给。
- **`ItemOfView`**：item 可以只属于某个视图，解析时只放进那个视图（I11 §六"各视图下 item 的序列不同"留下的口子）。
  没有它的 item 照旧在每个视图下提交。
- D3 / D8 里每个 pass 的"材质槽位 → 桶"表也走这套主机可见每帧 buffer。

**要查的**

- 主机堆的 buffer 带 `BufferBindFlags::Indirect`，RHI 的校验与 DX12 后端是否都接受。按原生语义，上传堆的 GENERIC_READ 包含
  INDIRECT_ARGUMENT。
- 容量会变：已建好的实体不再被 `PendingBufferInit` 处理，变大要换实体，新的到下一次 `RHIResourceSystem` 更新才建好。
  那一帧继续用旧的还是跳过，取决于建与读在一帧里的先后，没有读到。

### D6　半透明：CPU 剔除与排序，线性扫描，不建 BVH　✅ 已定

- 世界侧给半透明一个能直接 view 的标签，遍历的只是半透明物体。分类仍只有 `ClassifyDraw` 一个来源。
- 一个函数"收集某个视图可见的半透明"：局部空间的包围盒视锥测试（`viewProjectionNoAA × Model`，与剔除 shader 同一套面），
  排序键取包围盒中心在视图里的深度，从后往前排。以后换空间结构只改这个函数的内部（路线图 §五"先按空间区块粗剔"也落在这里）。
- 不建 CPU BVH：排序本来就要碰每个可见物体，BVH 只省被剔掉的那部分测试，动态物体还要每帧维护。数量大的半透明是粒子，
  按发射器走：一个发射器一个 PSO，发射器内在 GPU 上排序，CPU 只排发射器。场景 BVH 等 CPU 侧有多个空间查询需要时作为独立基础设施设计。
- 排好的顺序里相邻物体的 PSO 相同就并成一段，一段一个 item。item 数等于排序后 PSO 切换的次数，与物体数无关。
  "这个 pass 下这种材质用什么 PSO"与 GPU 分桶用的是同一个查找（D11），只写一处。

不选的：GPU 排序（排完 CPU 不知道 PSO 连续段在哪断开，只在所有半透明共用一个 PSO 时成立，与"每种材质一套 shader"冲突）；
顺序无关透明（多层时颜色不准，与 UE 的半透明行为对不上）。

### D7　半透明的着色与位置　✅ 已定方向

- **前向着色**：`EvaluateLight` / `EvaluateRectLight` 复用；阴影把 ShadowProjection 里按灯采 atlas 的那段抽成函数（ShadowMask
  对半透明无效）；IBL 用天光加预滤波 cube 乘 EnvBRDF；没有 AO 与 SSR；输出乘 PreExposure。灯光循环封装成函数，给 LightGrid 留接缝。
- **深度**：读 SceneDepth 做测试，不写。
- **混合状态**：Translucent 为 SrcAlpha / InvSrcAlpha，AlphaComposite 为 One / InvSrcAlpha，Additive 为 One / One，Modulate 为 DestColor / Zero。
- **位置**：Skybox 之后、TAA 之前（BeforeDOF），只画主视图。
- **不投影**；接受阴影。
- **材质格式**：`AlphaMode` 末尾追加 Additive、Modulate、AlphaComposite；新增 `TranslucencyPass`（BeforeDOF / AfterDOF），AfterDOF 先和
  BeforeDOF 走同一条路，P9 有了 DOF 再分开。编辑器材质窗口跟着加选项。
- `AlphaMode::Blend` 及新增的三种归为半透明，不进不透明与投影者列表。

### D8　分桶的数据结构　待定

前提是 D11 的查找能给出"pass + 材质 → PSO"，查找本身怎么做暂不关心。规则归 pass（用户 2026-10-16 倾向）。
数据按归属与变化频率分三类，各放各的地方；逻辑在系统里，每样数据只有一个写入方。不用"一个对象里装回调加几个容器、每帧重建"的形状。

| 数据 | 形态 | 何时变 |
|---|---|---|
| **桶**：（pass，PSO） | 一个实体，组件只有所属 pass、PSO（查找结果）、在这个 pass 内的下标。有材质映射到它时存在，没有就销毁 | 材质加载、删除、状态改变 |
| **映射**：材质槽位 → 这个 pass 的桶下标 | 每个 pass 一张按材质槽位寻址的表：GPU 资源（第二段读）加 CPU 镜像，按槽位写，同 `g_Materials`。不放在材质实体上（同一个材质在 N 个 pass 里有 N 个值，`TODO_DrawItemShapePlan.md` §七） | 同上 |
| **容量与段**：每个桶这一帧的实例数与段起点 | 桶实体上每帧写的组件。实例数来自 `InstanceBindingSystem` 编码时统计的每个（列表，材质槽位）的实例数，挂在 RHIContext 的一个实体上，同 `InstanceSlotCount` | 每帧 |

维护者是一个渲染层的系统，排在 `MaterialBindingSystem` 之后：材质有变化的那一帧，对每个分桶的 pass 查 PSO、找到或新建桶、写表项、
销毁没有材质再用的桶；每帧汇总容量写到桶上。

**待定**

- **桶实体放在哪个上下文**：倾向 PassContext（属于 pass，与 pass 实体在一起）；也可以是 RHIContext。
- **维护方式**：
  - A. 整体重建（倾向先这样）：有材质变化的帧把受影响 pass 的桶与表全部重算，代价是材质数 × pass 数次查表，只在变化的帧发生；
    桶下标可能重排、整张表重新上传，但表、桶、绘制同帧更新，没有跨帧读旧下标的东西。
  - B. 增量：只改变化材质的表项，每个桶记着有几个材质用它，减到 0 就销毁、回收下标；要维护引用计数，还要发现材质被删。
  材质数大到重建显眼时换成 B，数据结构不变。

### D9　Distortion 做不做　待定

材质模型没有折射输入，做它要先扩材质。倾向与 HeightFog 一样推迟。

### D10　绘制 pass 怎么"每个桶画一次"　待定

- A. pass 在 Build 里遍历自己的桶，每个桶声明一次间接绘制，段内偏移、`maxCount`、count 下标与 PSO 由 pass 给。pass 画了什么一目了然。
- B. pass 仍然只写一行 `DrawIndirect`，解析时按桶展开成每个（视图，桶）一个 item：与 D13 按视图展开同一套机制，pass 不关心有几个桶，
  就像它现在不关心有几个视图（倾向）。桶的说明按 D13 的做法挂在写参数的那次访问上，或由解析直接读这个 pass 的桶实体。

两种选法读的都是 D8 的同一份数据，不影响 D8 的结构。

### D11　PSO 的查找与预编译　方向已定，细节待定

**问题**：2026-10-15 版的做法是渲染图编译时由 compiler 内部的 cache 懒建、第一次画到才同步建。第一次出现必然卡顿，没有持久化，
不能异步，也没法预热。

**两层编译**：shader 编译（HLSL → DXIL / SPIR-V，可离线、按排列）与 PSO 编译（字节码加状态、RT 格式、输入布局 → 驱动编成机器码，
卡顿在这一层）。`ID3D12PipelineLibrary` / `VkPipelineCache` 解决的是第二层的跨运行重复编译，不解决"提前知道要哪些 PSO"。

**其他引擎的提前编译**（凭已有了解，没有重新核对）：API / 驱动级缓存（只管第二次及以后）；录制回放（UE 的 bundled PSO cache、
Unity 的 GraphicsStateCollection，覆盖不全、内容一改就过期）；加载时推导（UE 5.1 起的 PSO Precaching：网格与材质一加载，各 pass
算出可能用到的 PSO 交给后台编译，没好时先不画或用默认材质）；让 PSO 变少或拆开（Vulkan graphics pipeline library、shader object、
visibility buffer；DX12 图形管线没有对应的拆分）。

**方向**

- **PSO 不是"第一次画时才知道"，而是"材质和 pass 都已知时就能推出来"**：pass 对一种材质用什么 PSO，是（材质状态，pass 的固定配置）
  的确定函数。所以查找在材质就绪或状态改变时就能算，同时发出预编译请求；平时只是查表。
- 因此 pass 的规则必须是"不画就能求值"的，输入只有材质状态与 pass 自己的配置。
- **PSO 就绪的语义同贴图、几何**：还没编好的材质，查找结果是"不画"或"默认材质的 PSO"，在分桶表里落成这个材质不进桶或进默认的桶。
- **一个独立于渲染图的 PSO 缓存**：键是完整描述的 hash（shader 字节码、状态、RT 格式、输入布局、layout），负责去重、异步创建、
  回答是否就绪，下面接 RHI 的 `PipelineLibrary` 做跨运行持久化（要先把那个空壳实现起来）。
- **撤掉一条旧约束**：`TODO_PerDrawPSOVariant.md` 定过"PSO cache 是 compiler 内部工具，layout 不出 compiler"。D4 之后 layout 在 pass
  设置时就确定，前提不成立。
- **shader 排列也要能提前**：宏排列的编译是资产系统的异步请求，材质就绪时一起发出，不在建 PSO 时同步编。
- 录制回放以后可以加在这套之上（记下用过的 PSO 键，下次启动预热）。

**待定**

- PSO 没好时：这一帧不画（UE 默认），还是先用默认材质的 PSO。
- 缓存放在哪一层：去重与持久化是对 RHI 原生对象的通用管理（键全是 RHI 的描述，Atom 也有 RHI 层的 PSO cache），倾向 RHI；
  "材质就绪时替各个 pass 推导并发请求"是渲染层的事。
- 查找表由谁在什么时候算：倾向渲染层，在材质就绪或状态改变时，它要知道有哪些 pass、各自是什么配置。与 D8 的维护系统可能是同一个。

### D12　覆盖材质的状态也可以被覆盖　✅ 已定（用户 2026-10-16）

- 分桶只读"物体最终解析出的那个材质"的状态，来源是材质资产还是覆盖不影响分桶的设计。
- 要改的都在上层：覆盖组件除了参数还能带状态，`SyncOverrideMaterials` 建覆盖材质时把状态也写进去。覆盖组件的格式做到时再定，不在 P5 主线上。

---

## 三、步骤

### 1　两段式剔除 + 分桶 + item 级 PSO 切换，用 doubleSided 验证

两个小步：

- **1a 只拆段，不分桶**：剔除 pass 改写可见列表，加每个绘制 pass 的第二段，每个 pass 只有一个桶（所有材质用默认 PSO）。
  画面应与现在完全一样，验证拆段本身没有画错。
- **1b 分桶与 PSO 切换**：D8 的桶、表与容量；D10 的绘制方式；D11 的查找（至少能给出 doubleSided 的变体）；执行器的 item 级切换（D2），
  查后端换 PSO 是否保留绑定。doubleSided：深度类 pass 的变体换 `CullMode::None`；GBuffer 一律按 `SV_IsFrontFace` 翻转法线与切线框架
  （单面物体的背面被剔掉，不需要排列）。这一步只换渲染状态，不需要新 shader，layout 不变。

### 2　layout 以声明为准 + shader 宏排列

- D4：layout 来自声明的绑定组；变体的子集校验；去掉 `SpaceZeroKeepAlive`；深度类 pass 声明完整顶点格式并校验步长。
- shader 的身份变成（资产，宏集合），宏集合进 hash 与缓存键，异步按需编译（D11）。每种材质一套 shader 的 include 重定向留到第二个材质
  shader 出现时（路线图 I10）。

### 3　Masked

- `MaterialData` 加 `m_alphaCutoff`，不透明度取 `baseColor.a × 贴图 alpha`；C++ 与 HLSL 两边的 static_assert 一起改。
- `ClassifyDraw` 读材质状态：Masked 为不透明加投影，Blend 等为半透明。
- DepthPre、Shadow 的镂空变体：PS 采样 alpha 并 clip。DepthPre 里不透明的桶排在镂空的桶前面（带 clip 的 PS 会关掉 early-Z）。
- GBuffer 的镂空变体不 clip：深度 `Equal`，DepthPre 裁掉的像素深度对不上。

### 4　参数由 CPU 生成的间接绘制

- D5 的参数 buffer、不带 count 的 `DrawIndirect` 重载、`ItemOfView`。
- 顶点与索引按 `TODO_BufferRangePlan.md` §五 接收顶点流附件（这一项要先于本步完成）。

### 5　半透明

- 材质格式与编辑器（D7）。
- 半透明标签、收集与排序、按 PSO 切段写参数（D6）。
- 前向着色库与 pass（D7）。

### 6　Distortion

待 D9。

---

## 四、验证

- **单元测试**：桶的建立与销毁（材质增删、状态改变）、"材质槽位 → 桶"表、每个桶的容量与段起点、两段共用的视图布局、
  `ItemOfView` 的解析、CPU 排序与切段。
- **画面由用户确认**，每步交一次。我这边只跑短时间的运行，确认能跑、日志里没有新的报错。
- 步骤 1a：画面与拆段之前完全一样。
- 步骤 1b：双面物体背面可见、单面物体不变；同一场景切换 doubleSided 后画面随之变化。
- 步骤 3：镂空叶片的轮廓在深度、GBuffer、阴影里一致。
- 步骤 5：半透明叠加顺序正确；半透明受阴影与 IBL；相机移动时排序随之变化。

---

## 五、已知但不做

- 半透明不写 Velocity，运动的半透明物体在 TAA 下拖影（UE 有让半透明输出速度的选项）。
- 双面半透明先画背面再画正面（UE 的 two-sided separate pass）。
- 半透明投影。
- 半透明的排序优先级（UE 的 `TranslucencySortPriority`）。
- PSO 的录制回放预热（D11 之上的加法）。
- Additive / Modulate 与顺序无关，可以走 GPU 分桶的路；先统一走 CPU 排序。
- 光追里的镂空（`CANDIDATE_NON_OPAQUE_TRIANGLE`），留在 P7。
- HeightFog（用户 2026-10-15 定不做）。

---

## 关联文档

- `TODO_RenderPipelineRoadmap.md` —— 总览；P5、I5、I10
- `TODO_GPUDrivenPlan.md` —— D7（列表与分段）、D9（pass 自己声明绘制）、D10（与多 PSO 的关系，两段式的出处）、D13（按视图取段）、§六（半透明的三条路）
- `TODO_PerDrawPSOVariant.md` —— 同 pass 共享 layout 的约束仍然成立；"PSO cache 不出 compiler"由 D11 撤掉
- `TODO_DrawItemShapePlan.md` —— §五 per-batch PSO、§六 变体与排序的边界、§七 per-(object, pass) 数据不放实体上
- `TODO_BufferRangePlan.md` —— §五 `DrawIndirect` 接收顶点流附件；§三 / §四 的写入方式与 D5 冲突，另行设计
- `TODO_StructureAlignPlan.md` —— `SpaceZeroKeepAlive` 的由来（D4）
