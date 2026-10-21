# P5 透明物体　实现方案

路线图 P5 的落地计划。总览见 `TODO_RenderPipelineRoadmap.md`。

本文是草案。2026-10-15 与用户讨论后写下；2026-10-16 按后续讨论改写：分组换成两段式（先可见性、再按 pass 分桶），
PSO 改为由"pass + 材质"查找、可在加载时推导，覆盖材质的状态定为可覆盖；同日再定分桶的数据组织（D8：管线键 + 键级的分配关系）
与绘制方式（D10 选 B）；2026-10-17 补桶的先后（变体带优先级）、D10 里 count 偏移的写法、CPU 路径的查找落点，同日把"键 → 变体"
从 pass 的回调改成一套独立的解释机制（D13），并定下代码里的命名（桶为 `DrawBin`，第二段为 `InstanceBinning`）。P5 原来的六项里 HeightFog 不做（用户定），Distortion 待定（D9）。
2026-10-18 取消步骤 2：宏排列不做、Masked 推后，layout 那一半作为独立的修正做完（D4）；同日 D5 改为 CPU 给出有序实例列表、GPU 写记录。
2026-10-19 定桶不事先固定（D14），作为步骤 4 的前置 4-0；同日从"CPU 直接提交、每个实例一次"重新推了一遍提交的形态，
定下 D14 的框架：剔除按（视图，Tag）输出可见列表，排序与分组是一套独立的机制，CPU 路径由 CPU 写记录（推翻 D5 原来的做法），
GPU 路径是同一结构在"CPU 不知道可见性"下的形式，只换 binTable 与组的来源。2026-10-20 写下数据组织的草案（D15）：键号、
组件里不放 vector、每帧的绘制准备放进独立的 `DrawContext`，同日按它做完 4-0（分 a ~ d 四小步），GPU 路径已换到新的数据组织上，
CPU 路径（步骤 4）的部分仍是草案。2026-10-21 做完 4-0 之后的整理：组只有 `DrawGroup` 一个类型，系统改名 `DrawGroupSystem`、目录 `DrawList/`，，读组改为二分取一段。现状一节来自 2026-10-15 / 16 读代码，没有运行验证。

P5 做三件事：**按材质的 PSO 变体**（I5，含 Masked 与双面）、**顺序由 CPU 决定的间接绘制**、**前向着色的半透明**。
做完之后，场景物体只有一种提交方式：间接绘制，每组一次调用。差别只在谁知道要画哪些实例：CPU 知道（半透明），CPU 排序、分组、
写记录，条数确切；只有 GPU 知道（不透明、投影，GPU 剔除），CPU 按上限定组，GPU 写记录并计数。直接绘制仍只给零星的绘制。

---

## 状态

| 步骤 | 内容 | 依赖 | 状态 |
|---|---|---|---|
| 1-0 | 变体的解释（D13）：管线键、变体、解释函数、"pass 默认描述 + 变体 → PSO 描述" | — | ✅ 完成 |
| 1 | 两段式剔除 + 每个 pass 的分桶 + item 级 PSO 切换，用 doubleSided 验证（1a-1、1a-2、1b 三个小步） | 1-0（PSO 先同步建，D11 不挡） | ✅ 完成 |
| ~~2~~ | ~~pass 的 layout 以声明为准 + shader 宏排列~~ | — | 取消（用户 2026-10-18），见 D4 |
| 3 | Masked | 1、宏排列 | 推后（用户 2026-10-18） |
| 4-0 | GPU 路径的 binTable 与组改由 PSO 查找 + 排序分组从直方图得出（D14、D15），画面不变 | 1 | ✅ 完成（a ~ d）及之后的整理（七项） |
| 4 | CPU 路径：（视图，Tag）剔除、排序分组、CPU 写记录、每组一次间接调用（D14、D15），用简单着色的半透明验证 | 4-0 及其整理 | 进行中（4a ~ 4c 完成，余 4d ~ 4f） |
| 5 | 半透明的着色（前向，BeforeDOF） | 4 | 未开始 |
| 6 | Distortion | 5 | 待定（D9） |

1 不碰 layout 与 shader 系统，先用纯渲染状态的变体把拆段、分桶、切换验证掉。宏排列现在不做：只改渲染状态的变体用不上它，
它唯一的用户是 Masked，而半透明更重要（用户 2026-10-18），所以 Masked 一起推后，下一步是 4。

---

## 一、现状

**PSO 与 layout**

- 每个 pass 一个 PSO（`RenderGraphCompiler::CompilePipelineStates`），执行器在每个 Scope 开头设一次
  （`RenderGraphExecuter.cpp:326` 的 `RecordScope`），item 之间不换。PSO 在渲染图编译时同步创建。
- pass 的 `PipelineLayoutDescriptor` 来自 pass 自己 shader 的反射（`PassBuilder.h:28` 的 `BuildPipelineLayoutFromShaders`），
  在 `PassBuilder::Finalize` 时就建好，以 `PassPipelineLayout` 挂在 pass 上；`PassShaders`、`PassPipelineState` 也在 pass 上，
  拼一个 PSO 描述需要的东西在渲染图编译之前都已齐全。DepthPre 只绑 View 与 Instance；Masked 的 PS 要读 `g_Materials`（space3）、bindless 贴图与 sampler，反射出的 layout 会不同。
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
- `DrawIndirect` 把 `MeshGeometryBaseBufferTag` 的底层 buffer 绑成 VB 与 IB，加上实例 ID 流（`PassScopes.cpp` 的
  `RenderScope::DrawIndirect`）；记录里的几何偏移来自 `MeshGeometry` 的 `m_firstIndex` / `m_vertexOffset`，即网格在这块原生 buffer
  里的起点。`TODO_BufferRangePlan.md` §五 提过要改成接收顶点流附件，那份计划用户还在犹豫。
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

### D1　都是间接绘制，实例由 GPU 或 CPU 给出　✅ 已定（2026-10-19 按 D14 改写）

- **实例由 GPU 给出**：不需要 CPU 决定什么的大量物体（不透明、投影），即 I11 已有的路径，加上 D3 的分桶。
- **实例由 CPU 给出**：需要 CPU 决定顺序或集合的物体，CPU 剔除、排序、分组并写出记录（D5、D14）。半透明是第一个用例，
  做成通用机制，不写成半透明专用。
- **直接绘制**保持"零星"的定位：全屏三角形、调试绘制、gizmo。不为大量物体优化它。

不为大量物体优化直接绘制的理由：优化到头（持久缓存绘制命令、排序去重、多线程录制）仍是 CPU 代价随"物体 × 视图 × pass"增长，
且用不上只有 GPU 驱动才做得到的剔除（本帧 HZB 遮挡剔除、cluster 剔除）；做到那一步要把 D9 刚删的持久 `DrawItem`
体系重新建一遍。间接绘制表达不了的只有记录里放不下的逐绘制状态，它们的处理见下表，剩下的差别只是"实例由谁给出"：

| 逐绘制的变化 | 处理 |
|---|---|
| 换 PSO | 按段切开，每段一次调用 |
| 换 VB / IB | 几何统一进池 |
| 逐绘制常量、贴图 | 实例下标查 buffer、bindless |
| 顺序由 CPU 决定 | CPU 排序后写出记录 |
| scissor、stencil ref 等固定功能状态 | 例外，按段切开或改在 shader 里做 |

### D2　item 带 PSO，执行器在 item 之间切换　✅ 已定方向

- 同一个 pass 的所有 PSO 共用一份 layout（`TODO_PerDrawPSOVariant.md` 的约束不变；layout 怎么来见 D4）。
- 一个 item 用哪个 PSO 来自它的组（D14），组的 PSO 由 D13 的变体加 pass 的默认描述得到；执行器在 item 的 PSO 与当前不同时
  `SetPipelineState`。Scope 开头仍设 pass 的默认 PSO。
- 一组一个 item：不透明每个 PSO 一组，半透明按顺序切出的每段一组，同一套机制；后者在"每段一个物体"时退化成逐物体切换。

**换 PSO 后的绑定**（1b 查过）：按原生语义 layout 相同时两边都保留（DX12 root signature 不变则 root 参数不失效，Vulkan 兼容
layout 下 descriptor set 不失效）；但 DX12 后端每个 PSO 自建一个 `PipelineLayout`，换 PSO 时重设 root signature、清掉已绑定的 SRG。
执行器在每次切换后重新写出 pass 的绑定、Scope 的 root constant 与当前视图的 `viewIndex`；后端以后按描述合并相同的 layout，
这些重绑定就成了空操作。

### D3　两段式：可见性每个视图算一次，各 pass 再按自己的桶分　✅ 已定方向

即 `TODO_GPUDrivenPlan.md` D10 里 2026-10-05 对过的方向。取代本文 2026-10-15 版的"与 pass 无关的组 + 每个 pass 映射"
（那一版多了"组"这个概念和"材质 → 组 → 变体"一层映射，深度类 pass 的调用数是组数而不是它自己的 PSO 数）。

本节是 GPU 路径，即 D14 的一般形态在"CPU 不知道可见性"下的形式：其中按上限预留、count buffer、按视图偏移、binTable 都由这个限制而来，
本身是对的；要改的只是 binTable 与桶的来源（D14、4-0）。

**骨架**（GPU 驱动处理 PSO 不同的通用做法）：一次间接调用里不能换 PSO，所以同一个 PSO 的实例必须在参数 buffer 里连续；
CPU 按每个桶的实例数给它划一段，GPU 往段里追加记录并计数，CPU 不回读；每个桶一次间接调用。

**两段**

- **第一段 可见性**：由现在的剔除 pass 改过来，每个分类一个 pass、每个视图一个 Scope，视锥测试照旧；输出从 draw 记录改成
  可见的实例号（每个视图一段）与每个视图的可见数。与 pass 无关。
- **第二段 分桶**：每个绘制 pass 一个 compute pass（`InstanceBinning`，名字带上绘制 pass 的名字，如 `DepthPreInstanceBinning`；
  写参数与按参数调用不能在同一个 Scope，I11 D9），每个视图一个 Scope；读可见列表，以实例的管线键查这个 pass 的表得到桶号与段起点（D8），
  往桶的段里追加 draw 记录并计数。
- DepthPre 与 GBuffer 读同一份不透明可见列表，各有自己的第二段，"画同一批物体"由构造保证；以后加双阶段 HZB 遮挡剔除只改第一段。
- 写出的参数段照旧由写的一侧用 `IndirectArgumentsOf(view, offset)` 标在访问上（I11 D13 不变）。

**查找与分桶是两件事**

- **查找**：D13 的解释"（管线键，pass）→ 变体"，变体加 pass 的默认 PSO 就是这个物体在这个 pass 里用的 PSO（D11）。
- **分桶**：把实例按查找的结果排到一起。桶就是"这个 pass 的一个 PSO"，"键 → 桶"是查找结果按变体去重。
  分桶之所以存在，只是为了把调用次数从材质数压到 PSO 数。

**桶的粒度**（三种都用同一个查找，区别只在实例按什么粒度排在一起）

| 桶等于 | 每个 pass 的表 | 每个视图的调用数 | DepthPre / GBuffer 共用分段 |
|---|---|---|---|
| 材质 | 不需要（材质槽位就是桶号） | 材质数 | 可以，第二段可并回第一段 |
| 着色描述的组（与 pass 无关） | 不需要 | 组数 | 可以 |
| 这个 pass 的 PSO | 需要（管线键 → 桶，D8） | 这个 pass 的 PSO 数 | 不行，各自第二段 |

定为"这个 pass 的 PSO"（2026-10-16）：每个材质一个桶在 500 个材质、主视图加 16 个阴影视图时是近 9000 次间接调用，多数在某个视图里
条数为 0，框架的逐 item 开销与 GPU 的逐调用开销都受不了。按 PSO 合并后，现在的 StandardPBR 下每个 pass 只有几个桶；深度类 pass
在以后有了每种材质一套 shader 时也只有少数几个（不透明、不镂空、不改顶点的材质都用 pass 的默认 PSO）。

**两段之间的问题与解法**

- **第二段怎么知道每个视图的可见列表在哪**（两边各自枚举视图再按次序对齐，少一个就整体错位，I11 D13 指出过）：
  - A. 视图布局（参与的视图、次序、段起点）每帧算一次，两段都读这一份（倾向）。
  - B. 第一段标在访问上，第二段去查：第二段是 compute，常量在 Build 时就要定，渲染图要先支持"compute 的常量在解析时按视图填"。
  - C. 第一段顺便写第二段的间接派发参数：段起点仍要两边一致，只解决一半。
- **每个桶预留多少**（段起点由 CPU 录制时填，CPU 必须事先知道容量）：
  - A. CPU 每帧统计每个（分类位，管线键）的实例数，各 pass 按自己的分桶加起来再做前缀和（✅ 定，做法见 D8；实例编码时本来就逐个碰实例）。
  - B. 每个桶都按"这个视图全部可见"预留：内存是桶数倍。
  - C. GPU 计数再前缀和：做不到，GPU 算出的起点 CPU 拿不到（I11 D7）。
- **一帧之内一致**：分桶结果、表、容量、绘制声明都基于同一帧的材质与实例状态，同帧算同帧用，没有跨帧的 GPU 状态，不需要延迟回收。
  统计要在两段的 Build 之前完成，现在的更新顺序已满足。
- **第二段派发多少线程**：先用 CPU 知道的上界，超过可见数的线程退出；以后可改成第一段写派发参数、第二段 `DispatchIndirect`。

代价：每个（绘制 pass，视图）多一次派发；每个视图多一段可见列表（每个实例 4 字节）；不透明的参数 buffer 从一份变成 DepthPre、GBuffer 各一份。

### D4　pass 的 layout 以声明为准　退回（2026-10-18）

在 P5 里它的理由是带宏的排列会多引用资源（镂空的 DepthOnly 要读材质），反射默认 shader 得到的 layout 装不下。宏排列不做，
变体只改渲染状态、shader 永远是 pass 默认的那套，这个理由就没有了。

其中与 P5 无关的那一半已经作为独立的修正做完（`3e2faf93`、`a45295d2` 与随后删掉 `SpaceZeroKeepAlive` 的一次提交）：
共享组在 C++ 里声明自己的 layout（`Binding/SharedBindings.h`，space 号统一在 `Binding/BindingSpaces.h`），五个
`*BindingsReflect.hlsl` 反射宿主删除；pass 的 layout 里它绑的组的 space 整份取自组的声明，shader 读的共享资源与声明逐项校验
（`BuildPassPipelineLayout`）。`CommandList.cpp:219` 的断言因此不再挡共享组加 cbuffer，`InstanceSlotCount` 要不要挪进
`InstanceBindings` 另定。

**另一条，不属于 P5，2026-10-18 已修**：间接绘制的 VB 固定按 `StandardVertex::Stride`（48）绑定（`PassScopes.cpp` 的
`RenderScope::DrawIndirect`），而 DepthPre / Shadow 的 PSO 输入布局原先只声明 POSITION，步长 12。DX12 的步长在 VB view 上，
所以能画；Vulkan 把步长烘进 pipeline，会按 12 读。现在两者和 GBuffer 一样手动声明完整的 `StandardVertex`（输入布局描述绑定的几何，
不是 VS 读哪几个通道）；以后有了第二种顶点布局，再统一建顶点工厂来给出输入布局（用户定）。
（本节原先写"lowering 按 pass 的输入布局取步长"，那只对直接绘制的 `CompileItemInputAssembly` 成立。）

### D5　顺序由 CPU 决定：CPU 写记录　✅ 已定（用户 2026-10-15；2026-10-18 改为 GPU 写；2026-10-19 改回 CPU 写）

谁写记录由谁知道要画哪些实例决定。GPU 剔除时只有 GPU 知道，记录只能由 GPU 写，CPU 交出的 binTable 与上限就是它知道的全部；
CPU 路径上 CPU 什么都知道，就由 CPU 直接写出最终结果，不经过槽位列表与 GPU 的转写。

- **CPU 写记录**：排好序、分好组之后（D14），每个实例一条 `{indexCount, instanceCount = 1, firstIndex, vertexOffset, firstInstance = 槽位}`，
  几何取自 `g_Geometries` 的 CPU 镜像；按顺序写进 RHI 现成的主机可见每帧 buffer（`PendingBufferInit` 加 `PerFrameTag`，怎么封装见 D15"还要设计的"），各视图、各组首尾相接，
  条数确切、没有空洞。`firstInstance` 取槽位，与 GPU 那条路同一个约定。容量固定为实例容量，不扩容，超出的丢弃并报错。
- 两个 API 都允许间接参数在主机可见内存（DX12 upload heap，Vulkan 带 `INDIRECT_BUFFER` 用途的 host-visible buffer）；半透明条数不多，
  跨总线读参数的代价可以忽略。
- **绘制一侧**：每组一次间接调用，参数偏移是组的起点，条数是组的确切数，不需要 count buffer（用户 2026-10-19 同意）。组怎么交给解析见 D14。
- **上层不管帧数、暂存与延迟释放**（用户 2026-10-15 定）。不在渲染层做暂存环加 Copy pass；`TODO_BufferRangePlan.md` §三 / §四
  里"暂存环形缓冲 + 拷贝 Pass 在渲染层"那一段与此冲突，那份计划另行设计。
- GPU 路径的 binTable 也走主机可见每帧 buffer：长度是这一帧用到的最大键号加一（D15），每帧整张写。

**不选的**

- CPU 给出有序的槽位列表（`TranslucencyInstances`），由 `TranslucencyInstanceBinning` 在 GPU 上写成记录（2026-10-18 版）：理由是记录只在一处写。
  但这是把 GPU 路径"CPU 信息不足、只能交给 GPU"的做法套到了 CPU 什么都知道的一般情况上，多一个 compute pass 和一块槽位 buffer，
  只为了让记录格式少一个写的地方（用户 2026-10-19 否）。以后要在 GPU 上再加工 CPU 给出的记录（例如半透明的 HZB 遮挡剔除，把被挡住的
  记录的 `instanceCount` 写 0），是在 CPU 写好的记录上加一个 pass，不需要先回到这种做法。
- 在 RHI 里加"每帧同队列拷进显存"：要解决的若是 GPU 读主机内存，先受益的该是 `g_Instances` 这类大表，是所有每帧表一起的决定，不为这里单做。

### D6　半透明：CPU 剔除与排序，线性扫描，不建 BVH　✅ 已定

- 分类仍只有 `ClassifyDraw` 一个来源；剔除只遍历编码给出的 `TranslucentTag` 候选，结果是（视图，`TranslucentTag`）的可见实例（D14、D15）。
- 视锥测试：局部空间的包围盒（`viewProjectionNoAA × Model`，与剔除 shader 同一套面）。以后换空间结构只改剔除的内部
  （路线图 §五"先按空间区块粗剔"也落在这里）。
- 不建 CPU BVH：排序本来就要碰每个可见物体，BVH 只省被剔掉的那部分测试，动态物体还要每帧维护。数量大的半透明是粒子，
  按发射器走：一个发射器一个 PSO，发射器内在 GPU 上排序，CPU 只排发射器。场景 BVH 等 CPU 侧有多个空间查询需要时作为独立基础设施设计。
- 排序与分组走 D14 的通用机制：`TranslucentTag` 声明从后往前，排序键是（包围盒中心在视图里的深度，键），深度相同的按键排以少切几次；
  相邻 PSO 相同的并成一组，一组一个 item，item 数等于排序后 PSO 切换的次数，与物体数无关。PSO 与 GPU 路径同样由（pass，键）现算变体、
  拼出描述后查 PSO 缓存（D15），只写一处。
- 半透明 pass 只声明 `.Draw<TranslucentTag>()`，不注册 D3 的第二段：它的记录由 CPU 写（D5），没有 count buffer。

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

### D8　分桶的数据组织：管线键 + 键级的分配关系　✅ 已定（用户 2026-10-16，2026-10-17 改规则与命名）

> 2026-10-19：按 D14 重做。留下的：管线键；编码时按（分类位，键）计数，作为 GPU 路径里 Tag 的直方图，不再发布到键实体；
> `DrawBinPipeline` 所代表的"不重复建 PSO"，改由按描述哈希的 PSO 缓存承担（D15）。换掉的：键实体、分配实体、`KeyBinned` / `PassBinned`、布局与投影两轮遍历、
> 挂在长期实体上的 `DrawBinRange`：桶就是排序分组每帧的输出，binTable 是"每个键落进哪一组"。`viewStride` 与按视图偏移在 GPU 路径里保留（D14）。
> 2026-10-20：下文"键空间必须小而稠密"与"再改成只给出现过的组合编号"是错的：键与组合编号的个数都随内容增长、不固定，按键值寻址的表
> 本身不成立。GPU 一侧改为按键号寻址，CPU 一侧不按键缓存（D15）。
> 下面是 4-0 之前代码里的样子，留作记录。

CPU 分桶要交出三样：**键 → 桶的映射**（第二段把一个可见实例写进哪个桶）、**每个桶的容量与段起点**（第二段写记录的位置，
也是 CPU 录制间接调用的参数偏移与 `maxCount`）、**每个桶的 PSO**（item 之间切换）。桶在代码里叫 `DrawBin`（同 Nanite 的 raster bin、
Bevy 的 bin：一个管线、一次间接调用画的一组实例；不用 bucket，UE 的 state bucket 是 CPU 上合并相同命令，另一回事）。

**管线键**（`PipelineKey`，参照 Bevy 的 `MeshPipelineKey`）

- 决定一个物体在某个 pass 里用哪个 PSO 的，是它的特性（D13）：材质的 shader、材质的特性（alphaMode、doubleSided，以后的 WPO、
  像素深度偏移、着色模型），以及物体自己的少数因素（例如镜像物体要翻转剔除方向，UE 的 `bReverseCulling`）。打包成一个整数：
  管线键 = shader 编号 × 特性组合数 + 特性位，特性位 = alphaMode | doubleSided | 物体标志 | 材质特性位。以后加顶点工厂编号，是又一个乘数维度。
- 编码时与 `ClassifyDraw` 同处、逐实例每帧算出，写进 `InstanceData`（`m_pad` 改为 `m_pipelineKey`），只有这一个来源。物体换材质、
  切换标志，只是下一帧写出另一个键：不维护逐物体或逐材质的映射。
- 位的排布与解码函数（`GetAlphaMode`、`IsDoubleSided`、`GetShaderIndex` 等）只在 `PipelineKey.h` 一处，别处不做位运算。
  第一步：alphaMode 占 3 位（D7 追加后共 6 个值，一次留够），doubleSided 1 位，物体标志与材质特性位 0 位，特性组合数 16；shader 编号恒为 0。
  键空间就是 16。
- shader 编号以后由 shader 一侧给出连续编号，经"材质 → 它引用的 shader"取得，与 `MaterialSlotRef` 同一条路。
- 键空间必须小而稠密：GPU 表按它寻址。shader 多到上千、表到几万项时，再改成只给出现过的组合编号。

**变体**：（键，pass）→ `PipelineVariant` 由 D13 的解释系统给出；pass 上只声明 `.Draw<Tag>()`。同一个 pass 里变体相同就是同一个桶。

**桶的先后**：`PipelineVariant` 里带优先级，由解释系统按通用规则给出（变体带上会关掉 early-Z 的宏时排在后面，例如镂空：两次绘制共享
深度内容，先画的不透明用完整的 early-Z 把深度填好，镂空的像素在 PS 之前就被剔掉；GBuffer 的模板不响应镂空，先后无所谓）。
布局时每个 pass 内先按优先级、再按变体排，`index` 与 `first` 按排好的顺序给，不依赖实体的存储顺序。优先级参与"变体相同"的比较，
它由同样的输入算出，PSO 改动相同的键优先级也相同，不会因此多拆桶。

**实体**

| 实体 | 上下文 | 组件 | 何时建 |
|---|---|---|---|
| 管线键 | RHIContext（同 `InstanceSlotCount`） | `PipelineKey{value}`；每帧写的 `KeyInstanceCount`（每个分类位一个数） | 编码第一次见到这个键 |
| 桶 | PassContext | `DrawBinOf{pass}`、`PipelineVariant`、`DrawBinPipeline`（1b 起）；每帧写的 `DrawBinRange{index, first, capacity}` | 第一条分配指向它 |
| 分配 | PassContext | `DrawBinAssignment{pass, key, bin}` | 新键或新 pass 出现 |
| pass（已有） | PassContext | `PassDrawList{分类位}`（`.Draw<Tag>()` 加）、`PassBinTable`（GPU 表）；每帧写的 `PassBinLayout{binCount, viewStride}` | pass 声明 |

分配关系建在键这一级，数量是键数 × 分桶的 pass 数，很小，也很少变。发现新增用的标签是键上的 `KeyBinned` 与 pass 上的 `PassBinned`。

**系统**（一帧内的先后；1 是现有的 `InstanceBindingSystem`，2 ~ 4 是 `DrawBinSystem`，排在它之后、`ExecutePipeline` 之前）

1. **编码**：算出键写进记录；按（分类位，键）计数，用的是写进记录的 `m_drawMask` 与键，几何未就绪（`m_drawMask` 为 0）与容量溢出
   未编码的实例不计入，与第一段能挑出的实例是同一批。键空间是编译期常量，计数用系统内的固定数组，每帧清零。结束时把计数写到各个键实体的
   `KeyInstanceCount` 上，没见过的键新建实体。
2. **分配**（低频，用 Exclude 发现新增，同 GlobalBuffer 发现新材质）：`GetView<PipelineKey>(Exclude<KeyBinned>)` 对每个有 `PassDrawList`
   的 pass 求变体（D13），在这个 pass 的桶里找变体相同的、没有就新建（1b 起同时建 PSO），建一条分配；`GetView<PassDrawList>(Exclude<PassBinned>)`
   对已有的全部键做同样的事。pass 的加入与移除整体重算。
3. **布局**（每帧）：遍历分配，`capacity += key 的计数[pass 的分类位]`；每个 pass 内按（优先级，变体）排序，依次给 `index` 与 `first`
   （前缀和），累计出 `PassBinLayout`。`viewStride` 按 1024 条一档、`binCount` 按 4 个一档取整，免得第二段的 transient buffer 跟着换。
4. **投影**（每帧）：遍历分配，写 `table_pass[key.value] = {index, first}`；没有分配的键写"不画"。表是关系的投影，同 `g_Materials`；
   GPU 一侧是 RHI 的主机可见每帧 buffer（D5），CPU 镜像因为键空间是常量而用固定数组，与 buffer 句柄一起放在 `PassBinTable` 里。

渲染图里第二段读这张表，解析 `DrawIndirect` 读这个 pass 的桶（D10）。

**计数为什么每帧做、为什么按键数**：段起点与 `maxCount` 是 CPU 录制时填的，GPU 算出的拿不回来（D3），所以每个桶要有 CPU 知道的上界；
编码本来每帧经过每个实例，顺带数最简单。按键数而不按桶数，是因为编码不知道 pass，同一个键在不同 pass 里可能落进不同的桶。

**非 ECS 的部分，各有理由**

- 编码循环里的局部累加数组：逐实例按值找键实体太贵，先在系统内累加，结束时每个键写一次组件。
- 键值 → 实体的查找：发布计数时按值找键实体。键空间现在是 16，线性扫描；键变多时在系统内加索引。
- GPU 表：GPU 要按键直接寻址，只能是数组。

**以后的加法**：PSO 异步编译时桶上加就绪标签；投影时一个键的分配有任一个桶没就绪，就在所有 pass 的表里写"不画"
（否则 DepthPre 画了、GBuffer 没画，`Equal` 下是空洞）；shader 编号、材质特性位、顶点工厂编号进键；物体标志加位。

**不选的**

- 每帧对每个材质、每个 pass 求变体：材质可达数万。
- 分配建在材质级（材质，桶）：物体一侧影响 PSO 的因素进不来；GPU 表按材质槽位寻址，大小与空洞同 `g_Materials`。
- 分配建在物体级（物体，桶）：数量是物体数 × pass 数，加载帧批量建；GPU 表按物体槽位寻址，只在变化时局部更新，要一种还没有的
  "持久 buffer、局部更新"写法。
- 把 MaterialState 打包成键、shader 身份以后再加：shader 身份要去重成实体、经引用取得，加进来时获取方式整个变掉，不是加法。

### D9　Distortion 做不做　待定

材质模型没有折射输入，做它要先扩材质。倾向与 HeightFog 一样推迟。

### D10　绘制 pass 怎么"每个桶画一次"　✅ 选 B（2026-10-16）

pass 仍然只写一行 `DrawIndirect`，解析时读这个 pass 的 `DrawBin`（D8），展开成每个（视图，桶）一个 item：与 I11 D13 按视图展开
同一套机制，pass 不关心有几个桶，就像它现在不关心有几个视图。2026-10-19 起桶是 D14 排序分组的输出，展开的方式不变。

- 参数偏移 = 写参数的访问用 `IndirectArgumentsOf` 标出的视图起点 + `first` × 记录大小；视图之间相隔 `viewStride` 条。
- count 偏移 = 写 count 的访问标出的视图起点 + `index` × 4。视图起点由第二段标出，即视图次序 × `binCount` × 4；解析不算视图次序（I11 D13）。
- `maxCount` = `capacity`。PSO = 桶的 `DrawBinPipeline`（1a 没有，用 pass 的默认 PSO）。`capacity` 为 0 的桶不展开。
- 展开的先后按 `index`，即布局排好的桶序（D8 的优先级）。

不选 A（pass 在 Build 里逐桶声明）：每个分桶的 pass 都要重复同一段遍历，pass 里多出与它的功能无关的东西。

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

- **PSO 不是"第一次画时才知道"，而是"键和 pass 都已知时就能推出来"**：一个管线键在某个 pass 里用什么 PSO，是（键，pass 的默认配置，
  pass 模板声明的排列范围）的确定函数（D13）。新键出现时分配系统就求出变体、建桶，同时发出预编译请求；平时只是查表。
- D13 的解释只用这些静态输入，不画就能求值；预编请求就是"pass 模板 × 顶点工厂 × 材质 shader × 宏集合 × 状态覆盖"，即变体本身。
- **PSO 就绪的语义同贴图、几何**：还没编好的键，在所有 pass 的表里落成"不画"或进默认的桶（D8"以后的加法"）。
- **一个独立于渲染图的 PSO 缓存**：键是完整描述的 hash（shader 字节码、状态、RT 格式、输入布局、layout），负责去重、异步创建、
  回答是否就绪，下面接 RHI 的 `PipelineLibrary` 做跨运行持久化（要先把那个空壳实现起来）。
- **撤掉一条旧约束**：`TODO_PerDrawPSOVariant.md` 定过"PSO cache 是 compiler 内部工具，layout 不出 compiler"。layout 在
  `PassBuilder::Finalize` 时就已建好挂在 pass 上（现状一节），前提不成立。
- **shader 排列也要能提前**：宏排列的编译是资产系统的异步请求，材质就绪时一起发出，不在建 PSO 时同步编。
- 录制回放以后可以加在这套之上（记下用过的 PSO 键，下次启动预热）。

**待定**

- PSO 没好时：这一帧不画（UE 默认），还是先用默认材质的 PSO。
- 缓存放在哪一层：去重与持久化是对 RHI 原生对象的通用管理（键全是 RHI 的描述，Atom 也有 RHI 层的 PSO cache），倾向 RHI；
  "材质就绪时替各个 pass 推导并发请求"是渲染层的事。
- ~~查找表由谁在什么时候算~~：D8 的分配系统调用 D13 的解释，在新键或新 pass 出现时。

### D12　覆盖材质的状态也可以被覆盖　✅ 已定（用户 2026-10-16）

- 分桶只读"物体最终解析出的那个材质"的状态，来源是材质资产还是覆盖不影响分桶的设计。
- 要改的都在上层：覆盖组件除了参数还能带状态，`SyncOverrideMaterials` 建覆盖材质时把状态也写进去。覆盖组件的格式做到时再定，不在 P5 主线上。

### D13　PSO 变体的解释：一套独立的机制　✅ 已定（用户 2026-10-17）

**为什么单独做**：桶的分配（D8）、PSO 预编（D11）、半透明的 CPU 路径（D6）问的是同一个问题："这个物体在这个 pass 里用什么 PSO"。
它是一套独立的机制（`Render/PipelineState/`，无状态的函数），分桶只是它的使用者之一。

**概念**

| 概念 | 是什么 | 归谁 |
|---|---|---|
| 特性 | 被画的东西身上会让某些 pass 的 PSO 不同的事实：双面、镂空、混合模式、材质 shader，以后的镜像翻转、顶点动画、顶点工厂 | 引擎定义，封闭的集合 |
| 管线键 | 一个实例所有特性的打包（D8） | 物体一侧 |
| 变体 | 对（键，pass）解释后的结果，`PipelineVariant` | 解释系统产出 |

特性的集合是封闭的：加一种特性要同时改材质格式、键的位、shader 约定与解释代码，本来就是引擎层面的改动。pass（包括以后的用户 pass）
不发明特性。

**变体**：固定大小的值类型，按值比较，不含容器。字段随步骤增加：

- 优先级（1a）；
- 状态覆盖：剔除模式（1b）、混合状态（步骤 5），没被覆盖的用 pass 默认的；
- shader 的选择：特性宏集合（位集，每位一种特性的宏）、材质 shader 编号（不用时为"无"）、顶点工厂编号（以后）。pass 模板属于 pass，不在变体里。

**静态信息各回各的地方**：pass 上只声明 `.Draw<Tag>()`（画哪种列表、参与分桶），没有逐特性的声明。原因是逐个看，这些都不是 pass 的选择：

- **对所有 pass 都一样的，是解释系统里的通用规则**：双面 → 不剔除；镜像翻转 → 剔除方向反过来（排在双面之后，已是 None 的不变）；
  混合模式 → 按 alphaMode 查固定的表得到混合状态（不透明与投影者列表里没有半透明的键，分类已经过滤）；顶点工厂 → VS 与输入布局；
  优先级 → 变体带上会关掉 early-Z 的宏时排在后面。
- **pass 的 shader 模板的性质，由模板自己声明**：模板支持哪些特性宏（DepthOnly 处理 `MATERIALBLENDING_MASKED`，GBuffer 不处理），
  以及是否忽略材质 shader（默认使用，GBuffer 就是；DepthOnly 声明忽略，只在镂空、顶点动画这类特性需要时才用）。这就是宏排列的范围，
  shader 宏排列本来就要知道一个 shader 支持哪些宏；UE 同样让 shader 自己声明（`ShouldCompilePermutation`）。怎么声明到做宏排列时再定。
- **宏属于特性，不属于 pass**：所有 pass 模板遵守同一套宏约定（名字沿用 UE），所以没有"这个 pass 用哪个排列"这样的参数。
- **真正属于某个 pass 的例外**（例如以后阴影想对双面物体用另一种剔除）：到时给 pass 加一个可选的覆盖组件，是例外不是常规写法。现在没有。

**解释**（输入：键、pass 的默认配置、pass 模板声明的排列范围；固定步骤）

1. 从默认变体出发：优先级 0，没有覆盖，没有宏，不用材质 shader。
2. 应用通用规则，按引擎定义的顺序；几个特性改同一个字段时谁先谁后只在这里决定。
3. 键里的特性凡是模板支持的，加上对应的宏；用到材质代码的，标记需要材质代码。
4. 模板不忽略材质 shader，或第 3 步有特性需要材质代码，填上键里的 shader 编号，否则为"无"。
5. 规整：没用到的字段清成默认值，保证效果相同的变体按值也相同，不多拆桶。

**使用者**

- PSO 查找（D15）：每帧现算变体、拼描述，按描述的哈希查 PSO 缓存，没有才建。
- PSO 的建立：同一个系统里一个通用函数，把"pass 的默认描述 + 变体"变成 PSO 描述（1b 只改剔除；有了宏排列之后选 shader 排列；步骤 5 加混合状态）。
  D11 的预编请求以后从这里发出。
- D14 的排序分组：两条路径都经同一个缓存查 PSO，变体的优先级进不透明的排序键。

**不选的**

- pass 上存规则回调（`PassBucketRule{分类位, 回调}`，函数指针或 `eastl::function`）：组件里存的是逻辑，确定性只能靠自觉。
  `PassCapabilities` 里的函数指针是另一回事：它们是类型擦除，背后是同一份模板代码，只是把 `ViewTag` / `BindingTags` 带到运行期，
  可以换成运行期的类型 id 或值；规则是每个 pass 各不相同的策略，换不掉。
- 每个 pass 一张按特性组合预先填好的变体表（用"匹配条件 → 覆盖"的声明填）：一套小语言，读起来不直白。
- 每种特性一个响应组件挂在 pass 上（`DoubleSidedResponse`、`AlphaMaskResponse` 等），系统统一解释：声明太多，而其中多数是通用规则或模板的性质，
  并不是 pass 的选择。

### D14　桶不事先固定：剔除、排序分组、提交　✅ 框架已定（用户 2026-10-19），细节待定

**问题**：D8 把桶建成长期存在的（pass，变体），容量每帧更新，视图则靠"同一份布局在参数 buffer 里重复视图个数次"（`viewStride`）隐式进来，
起点由分桶 pass 用 `IndirectArgumentsOf` 标出、解析时去查。它的前提是：键的集合决定了每个 pass 在每个视图下有哪些桶，而且跨帧稳定。
半透明打破了这个前提：同一个 PSO 在一个视图的顺序里可以出现好几段，段数随视图、随帧变化。"键 → 组"的表同理，也固定不了。
只要还想着事先把桶固定下来，往下就只能一直打补丁。

**已定**

- 桶不跨帧保留，也不由键的数量推出。
- 一次间接调用的 PSO、参数从哪里读、最多几条，是 CPU 录制时填的参数（`ExecuteIndirect` / `vkCmdDrawIndexedIndirectCount` 都不能在调用内
  换 PSO，偏移也不由 GPU 决定）。所以每个（pass，视图）这一帧有哪几次调用，由 CPU 在提交前决定；CPU 已有的信息足够决定：不透明是
  这一帧出现的 PSO 与计数给出的上限，半透明是确切的段。
- 给 GPU 的分组依据（不透明的实例写进哪一次调用）由这一帧准备好的提交派生，是提交的结果，不是提交的前提。
- PSO 缓存（D11）以后做。

**为什么从头推**：D8 是先做了 GPU 路径，再从它的表示（一块 buffer、按视图偏移、按上限预留、binTable）反推一般情况，半透明进来时
处处对不上。所以先假设"CPU 剔除、CPU 直接提交、每个实例一次"推出最自然的形态，再看 GPU 路径缺了哪条信息、因此多出哪些东西。

**一般形态**（每帧）

1. **剔除**：与 pass 无关，按（视图，Tag）做。要剔除哪些组合由所有 pass 的（视图类别，Tag）声明合起来得出，没人画的组合不剔除。
   结果是每个（视图，Tag）一组可见实例，没有顺序（同 Bevy 视图上的 `VisibleEntities`；数据形式见 D15）。
   多个 pass 读同一个（视图，Tag），"画同一批物体"由构造保证（DepthPre 与 GBuffer 的 `Equal`）。
2. **排序分组**：一套独立的机制，不在 pass 上（pass 只用 `.Draw<Tag>()` 声明画哪种 Tag，同 D13）。在渲染图构建之前，对每个声明了
   `.Draw<Tag>()` 的 pass、在它渲染的每个视图下算一次：读（视图，Tag）的可见实例，每项由（pass，键）查出 PSO（D15），按排序键排，
   相邻 PSO 相同的并成一组。分组不是单独的一步，是排序的结果；判据是"一次调用中途不能换的状态相同"，现在只有 PSO。
   同一份列表不同 pass 读出的顺序不同，因为 PSO 与优先级属于 pass，所以结果的身份是（视图，pass）。
3. **写记录**：CPU 按顺序写进主机可见每帧 buffer（D5）。
4. **提交**：每组一次间接调用，条数确切，不需要 count buffer。

**排序键的形状由 Tag 声明**：顺序有没有要求是被画的东西的性质（半透明不管哪个 pass 画都要从后往前），不是 pass 的选择。
写在 Tag 类型上（如 `static constexpr DrawOrder Order = DrawOrder::BackToFront;`，不写为 `Any`），经 `.Draw<Tag>()` 带到运行期，
同 `ViewTag`、`BindingTags` 经 `PassCapabilities`。

| Tag 的顺序 | 排序键 | 组数 |
|---|---|---|
| `Any`（不透明、投影） | （优先级，变体），可选再按深度从前往后（利于 DepthPre / Shadow 的 early-Z） | 等于 PSO 数 |
| `BackToFront`（半透明） | （深度，键） | 由场景决定，同一个 PSO 可以多组 |

`Any` 的排序键不能只用管线键：同一个（pass，键）的 PSO 必然相同，但不同的键在一个 pass 里可能是同一个 PSO（GBuffer 不区分镂空；
以后 DepthPre 忽略材质 shader），只按键排会把同一个 PSO 拆成几组；优先级也属于 pass。`BackToFront` 深度相同时按键排只为少切几次 PSO，
键与 pass 无关，所以这一次排序可以在（视图，Tag）这一级做，各 pass 只按顺序合并。

**长期状态只有一样**：按描述哈希的 PSO 缓存（D15）。键 → 变体 → 描述每帧现算。排序分组的结果每帧算出、用完即弃。

**GPU 路径是同一结构在"CPU 不知道可见性"下的形式**

| 一般形态 | GPU 路径 | 原因 |
|---|---|---|
| 可见列表在 CPU 上，条数确切 | 在 GPU 上（`InstanceCullingPass`），一块 buffer 按视图偏移，按上限预留 | CPU 拿不到 GPU 的结果 |
| 排序分组的输入是可见实例 | 输入是 Tag 的（键，上限条数）直方图，即编码时的计数；顺序随意，所以直方图在信息上等价于可见列表 | 同上 |
| 组数、条数由排序给出 | 所有可能出现的组都准备好，条数是上限，GPU 上可能是空组 | 同上 |
| CPU 写记录 | GPU 写（`InstanceBinning`），按 binTable 查组、计数 | 只有 GPU 知道哪些实例可见 |
| 每个视图各自的组 | 每个视图的组相同，同一份布局按 `viewStride` 重复 | 每个视图的直方图相同，排序分组对每个视图给出相同的组 |

所以排序分组在两条路上是同一段代码，只是输入的项不同：CPU 路径每项是一个可见实例（条数 1），GPU 路径每项是直方图里出现过的一个键
（条数是上限）。GPU 路径从它得到每组的容量（并进这组的各项条数之和）与 binTable（每个键那一项落进了哪一组）。分桶 pass、shader、
`IndirectArgumentsOf` 与 D10 的展开都是这个限制下的表示，不改；4-0 只换 binTable 与组的来源。

以后若用上 GPU 生成命令（`VK_EXT_device_generated_commands` 的间接执行集对应 PSO 缓存，同 pass 共用 layout 已由 D2 保证）或 D3D12 work graphs，
变的只是第 2 ~ 4 步在 GPU 上怎么做；剔除的输出、PSO 查找与 Tag 的顺序声明不变。前提是"CPU 知道组和偏移"只在排序分组与提交里，不漏到剔除与视图。

**待定**

- ~~排序分组每帧结果的数据形式与存放~~、~~可见列表的形式~~、~~一个 Tag 走哪条路径由谁决定~~：草案见 D15。
- 不透明是否在 PSO 之后再按深度从前往后。
- 两个重叠的 Tag（投影者与不透明）在同一类视图下都要时，同一个实例对同一个视图会测两次；现在没有这种组合，出现时改为每个（实例，视图）
  测一次、写进各 Tag 的列表，只是剔除内部的事。

**讨论中否掉的**

- 每个视图的段挂在半透明 pass 上（一张按视图的段表），解析时按视图筛：只是给半透明单独开口子。
- 绘制列表放在视图上、分配关系加视图维度、拆成"PSO 一层 + 每视图的桶"两层：仍是在事先固定的桶上加维度。
- 每个物体一个实例实体，带按 Tag 排的变体数组：变体按 pass 定而不是按 Tag（DepthPre 与 GBuffer 同收 `OpaqueTag`、PSO 不同），落回（物体，pass）
  级的数据；变体只取决于（键，pass），逐物体算是重复。
- `DrawBinSystem` 每帧按计数为每个（pass，视图）算出桶：仍是事先按键把桶定下来，只是每帧重算。
- 半透明在 GPU 上排序：排序本身可以做，但排完 PSO 在哪里切换只有 GPU 知道，CPU 录制时不知道几段、各多长、各用什么 PSO；按最坏情况录制
  调用数是物体数 × PSO 数，读回结果要晚一到两帧、相机一动分段就对不上，GPU 生成命令两边不普遍可用。见 D6。
- 每段一个每帧实体（`{pass, view, 次序, first, maxCount, count 下标, PSO}`）：数据放不下就拆出新实体，没有来自段本身的理由；段就是一个 item。
- 写方把每个视图的各段标在访问上（把 `IndirectArgumentsOfView` 从一个起点扩成一串段），解析只读标记：随即冒出"一个访问多段怎么标"
  "PSO 由谁标"这类问题，说明方向不对。
- 排序分组放在 pass 上：它用到的输入（可见列表、Tag 的顺序、PSO 查找）没有一样是某个 pass 自己的逻辑，算法对所有 pass 相同。

### D15　D14 的数据组织　GPU 路径已做（4-0），CPU 路径为草案（用户 2026-10-20）

D14 的框架落到数据结构上。GPU 路径的部分随 4-0 做完；CPU 路径随步骤 4 逐步做（4a 列表实例、4b 剔除，其余为方案）。

**Tag 的声明**（`DrawTag.h`）

- 每个 Tag 声明顺序要求与走哪条路径：`static constexpr DrawOrder Order`（`Any` / `BackToFront`）、`static constexpr DrawCulling Culling`
  （`Gpu` / `Cpu`）。不透明、投影者为 `Any` + `Gpu`，半透明为 `BackToFront` + `Cpu`。一个 pass 只画一个 Tag，所以一个 pass 整个走一条路径，
  解析时不会两种混在一起。
- 运行期以 draw-mask 位号代表 Tag：`TranslucentTag` 占第 2 位，`DrawMaskBitCount` 为 3。`.Draw<Tag>()` 把位号、`Order`、`Culling`
  抄进 `PassDrawList`，不另设类型 id。

**PSO 查找：每帧重算，只按描述的哈希缓存 PSO**（用户 2026-10-20）

- 键 → 变体是 D13 的纯函数，每次现算；变体 → `BuildDrawPipelineDescriptor` 也每帧现拼。唯一缓存的是建好的 PSO：
  `DrawPipelineCache`，描述的 `GetHash()` → PSO，只保证同一份描述不重复建，PSO 同步建（D11 以后改异步）。即 D11 的
  "独立于渲染图、键是完整描述的哈希"的最简形式；RHI 现有的 `PipelineStateCache`（自 Atom 移植）现在不能用。
- 缓存由渲染图编译器持有，是临时方案：正式的缓存就绪后，看是否移进 RenderGraph（RHI 对象都由它建立与管理），需要时再传进去。
  pass 的默认 PSO（`CompilePipelineStates`）与各组的 PSO 都从它取，所以变体为空的组与默认 PSO 是同一个。
- 组上不存 PSO：解析展开 item 时，由（pass，组的变体）拼描述、查缓存。拼描述的次数等于展开出的 item 数（组数 × 视图数）；
  分组只比变体，变体相同必是同一个 PSO。
- 表里同时存一份描述，哈希相同时再比一次描述，免得冲突时用错 PSO。不淘汰。两个 pass 的描述完全相同时自然共用一个 PSO。
- stage function 每个 pass 只建一次（`PassStageFunctions`，render 与 compute 都在 `Finalize` 时建），描述按指针比较它，描述里只放引用；
  每帧拼描述只是复制几个指针与渲染状态、算一次哈希（layout 的哈希已缓存）。以后变体选 shader 排列时，stage function 按（shader，宏集合）缓存。

**键号 `PipelineKeyIndex`：GPU 按它寻址**

- 键的个数随内容增长，不固定，按键值寻址的表不成立（D8 注）；而 GPU 的分桶 shader 手里只有实例记录，要一个能直接寻址的号。
- 命名同材质：`InstanceData::m_pipelineKeyIndex` 对应 `m_materialIndex`，取号的 `PipelineKeyIndexOf(键)` 对应 `MaterialIndexOf`；
  以后被持有的那个叫 `PipelineKeySlotRef`，对应 `MaterialSlotRef`（Slot 是被持有的东西，Index 是写进记录的号）。
- 以后的做法同 `MaterialSlotRef`：一个 `SlotPool`，用到这个键的实例在世界实体上各持一份 `SlotRef`（带引用计数），最后一个不用了号归还。
  编码系统内部一张"键值 → `SlotWeakRef`"的 hash map 做按值查找（ECS 之外，唯一理由是逐实例按值查），弱引用活着就复制，失效或没有就分配。
- 一个实例的键号只在它自己的键变了时才变，那本来就是一次要上传的改动，所以与以后 `g_Instances` 的增量上传兼容。
  每帧按排序位置编号的下标做不到这一点：新出现一个键，后面的位置全部挪动，所有实例记录都要重写。
- 键号的三个性质：使用期间不变、尽量紧凑（`SlotPool` 先给最小的空闲号）、寻址长度跟用到的最大号走。都与"总共最多多少"无关：
  容量增长与 `g_Instances` 以后改成数量可变是同一个问题（槽位池能长大、buffer 换成更大的一块），到时一起解决。
- **只有编码知道键与键号的对应**：对外只有 `PipelineKeyIndexOf` 一个入口，以后挪进编码系统内部。别处要的键值、键号与长度都从编码发布的
  `DrawListKey` 元素里拿（见下），不反查，也就不必去找编码系统。CPU 一侧的 PSO 查找按键值现算，用不到键号。
- **第一版简化**（用户 2026-10-20，4-0b 已做）：键号就是键值，不建 `SlotPool`、`SlotRef` 与 hash map。别处不假设键号等于键值；
  shader 只拿键号寻址 binTable，不从中解码特性，键号超出表长时跳过（越界读得 0，会被当成第 0 组）。编码的计数数组与 binTable 的
  buffer 都按键空间定长；换成槽位池时两者一起改成随键号增长。

**binTable 与直方图**

- 直方图是编码发布的 `DrawListKey{Tag 位, 键号, 键值, 实例数}` 元素（`DrawContext` 里）：绘制列表（一个 Tag 的实例）里出现的每个键一个，
  实例数是这一帧编码时带这个 Tag 位与这个键的可绘制实例数；一个实例带两个 Tag 位就在两个列表里各计一次。编码先在系统内的数组里累加
  （逐实例按值找元素太贵），结束时发布。名字对应 `PassDrawList`：GPU 路径下 CPU 对一个列表的描述是一组 `DrawListKey`，CPU 路径下是逐个的
  `VisibleInstance`。以后编码改成增量的，累加也改成增量维护（拿到、放下键号时加减一）。
- binTable 每个 pass 一份：同一个键在不同 pass 里落进不同的组（DepthPre 分开不透明与镂空，GBuffer 不分），组号与 first 属于 pass。
  `table[键号] = IndirectLayoutEntry{countIndex, firstRecord}`：countIndex 是这一组在 pass 的组里排第几、即读 count buffer 里这个视图那段的第几个；
  firstRecord 是这组记录在视图参数段里的起点。空着的号写 `NotDrawn`。长度取这一帧 `DrawListKey` 里最大的键号加一（GPU 分桶看得到的实例都计过数，
  `m_drawMask` 为 0 的不进剔除）。
- 分桶 pass 要的都在 pass 上的 `PassIndirectLayout` 里：表的 buffer（主机可见每帧，pass 第一次画列表时建，每帧整张写）、这一帧的表长、
  每个视图几个 count（`m_countsPerView`）、几条记录（`m_recordsPerView`）；后两个按档取整，免得分桶 pass 的 transient buffer 跟着换。
  表的 CPU 镜像是 `DrawGroupSystem` 私有的数组。表照 entry 写：分组时每个 entry 记下它落进第几组（`m_groupIndex`），
  `table[entry 的键号] = {组号, 组的 first}`，组上不带 entry 区间（entry 是系统里逐 pass 重填的临时数组）。

**组件里不放 vector：每帧的绘制准备放进独立的 `DrawContext`**

- 组件放在 ECS 的存储里，会被移动、交换、替换，里面 vector 何时重新分配不归写组件的人管；"原地改写"只是写法上的注意，不是机制保证。
- 一个组件的 storage 本身就是一块 arena：建实体是分配位置，加组件是写入，清空后 dense 数组保留容量、实体 id 回收，还能排序；
  稳定之后没有新的堆分配，由 storage 自己保证。元素与所有者多对一，所有者写在元素的字段里，消费者沿排好序的 storage 按所有者切开。
- 独立的上下文 `DrawContext = BasicContext<DrawEntity>`：这些数据语义上不属于 RHIContext 也不属于 PassContext，只在渲染系统内部流转。
  `RenderSystem` 持有，每帧开头整体 `clear()`，"里面的东西只活一帧"是这个上下文本身的语义；`ExecutePipeline` 前压入 `DrawExecuteContext`
  （同 `PassExecuteContext`），分桶 pass 的 Build 与解析经它取得。对其他上下文只存句柄，不持有。

| 元素 | 组件 | 谁写 | 排序 |
|---|---|---|---|
| 列表里的键 | `DrawListKey{Tag 位, 键号, 键值, 实例数}` | 编码，只给 `Gpu` 的 Tag | — |
| 列表实例 | `DrawListInstance{Tag 位, 槽位, 键, indexCount, firstIndex, vertexOffset, 世界包围盒}` | 编码，只给 `Cpu` 的 Tag | — |
| 可见实例 | `VisibleInstance{视图, Tag 位, 槽位, 键, 三个几何字段, 深度}` | 剔除（`DrawListCullingSystem`） | （视图，Tag 位，深度降序，键） |
| 组 | `DrawGroup{pass, 视图, 次序, 变体, firstDraw, maxDraws}` | `DrawGroupSystem` | （pass，视图，次序） |

- 头文件按数据流切开，各自写明谁写、谁读（`Render/DrawList/` 下）：`DrawList.h` 是列表的元素（`DrawListKey`、`DrawListInstance`、
  `VisibleInstance`），编码与剔除写、`DrawGroupSystem` 读；`DrawGroup.h` 是 entry、组、`GroupDrawEntries` 与读组的 `DrawGroupsOf`，`DrawGroupSystem` 写、渲染图读；
  pass 上的 `PassDrawList`（`.Draw<Tag>()` 加）与 `PassIndirectLayout`（`DrawGroupSystem` 写、分桶 pass 读）各在 `Pass/Component/` 下一个头文件。
- 读组：`SortDrawGroups` 按（pass，视图，次序）排 storage，`DrawGroupsOf` 以同一个顺序二分，返回（pass，视图）在 storage 里连续的一段，
  不复制、不全量过滤（CPU 路径每个视图的组数随场景，全量过滤是平方）。

- 列表实例（用户 2026-10-21 定名，"候选"是口语）由编码给出，与 `DrawListKey` 是一对：GPU 剔除的列表 CPU 只知道键与实例数，
  CPU 剔除的列表知道每个实例。计数器只数 GPU 剔除的位（`CpuCulledDrawMask` 由各 Tag 声明的 `Culling` 折叠得出）。编码本来逐个经过每个实例、解析材质、算键、调 `ClassifyDraw`，键只算一处；几何取自世界实体上的 `MeshGeometry`，
  世界包围盒由 `m_localBounds` 与模型矩阵算出，不回读 `g_Instances` 的暂存镜像（绑定缓冲只写不读）。这就是步骤 4 原来待定的"从哪里收集"。
- 可见实例直接带上排序与写记录要的字段，不存指回列表实例的下标。剔除对每个（视图，位）只做一次，用视图的 `ViewFrustum`、
  不测近平面（`Frustum::IntersectsAABBIgnoringNear`，与 `InstanceCulling.hlsl` 一致）；深度取盒子中心在视空间的 z。
  `VisibleInstancesOf` 以排序的顺序二分出（视图，位）的一段，同 `DrawGroupsOf`（两者都是 `DrawElementRange`）。
- **CPU 路径的绘制参数属于列表，不属于 pass**（用户 2026-10-21）：一条记录 `{indexCount, 1, firstIndex, vertexOffset, 槽位}`
  全部取自可见实例，先后是列表从远到近的顺序，都与哪个 pass 画它无关；与 pass 有关的只有组在哪里断开（变体由 pass 的渲染状态求出）。
  所以剔除系统排好序后，按 storage 的顺序为每个可见实例写一条记录，所有 CPU 剔除的列表共用一块参数 buffer（资源 Tag
  `CpuCulledDrawArgumentsTag`，主机可见每帧，`Indirect`，按档取整、不够时换一块）；一个可见实例的记录就在它在 storage 里的位置
  （`FirstDrawOffset`）。pass 用 `Import<CpuCulledDrawArgumentsTag>` 显式导入，身上不挂资源：pass 与资源是两个轴。
  `DrawGroupSystem` 只在这串记录上切组，不碰 RHI。GPU 路径的参数是每个 pass 一份，那是限制逼出来的：分组与写记录在同一次 dispatch 里，
  写之前就要知道这个 pass 的组。
- **CPU 剔除的 Tag 必须声明 `BackToFront`**（用户 2026-10-21）：记录的顺序由列表决定，pass 只切不排；顺序无关的物体应走 GPU 剔除，
  不留回退。`.Draw<Tag>()` 里 `static_assert`，`DrawGroupSystem` 里再 ASSERT。
- 组总在一个视图下。GPU 路径的组来自整个 Tag 的直方图，每个视图都相同，为 pass 渲染的每个视图各写一份；视图取自 pass 的
  `m_collectViews`，与渲染图展开 item 时是同一个调用。
- first 从这个视图的参数起点算起。起点是组地址里由 `DrawGroupSystem` 以外的写入方决定的那一部分：GPU 路径里分桶 pass 按视图预留
  固定大小，把视图那段的起点标在写它的访问上（`IndirectArgumentsOf`）；CPU 路径的整个地址都由 CPU 决定，没有别人定起点，即为 0。
  CPU 的参数 buffer 语义上就不分段（每组只需要它那段记录的位置），不是"能省掉分段"；给它补标 0 等于假装有另一个写入方（用户 2026-10-21）。
  解析经 `TryGetIndirectArgumentsStart` 取起点（4d）。

**系统私有的连续数组**：编码累加计数的数组、binTable 的 CPU 镜像、CPU 参数 buffer 的镜像、键值 → 键号的 hash map（换成槽位池后）、描述哈希 → PSO 的表。它们要稠密寻址、整块上传或按值查找，不是一组元素，
只在系统内部读写；上传时 `PendingBufferMap` 指向系统成员的内存，系统在它就有效。pass 上的 `PassIndirectLayout` 只留表的 buffer 句柄与长度。

**每帧的流程**（都在 `RenderSystem::Update` 里、渲染图构建之前）

1. `DrawContext::clear()`。
2. 编码（`InstanceBindingSystem`）：键号写进记录；`Gpu` 的 Tag 累加计数、结束时发布 `DrawListKey`，`Cpu` 的 Tag 写 `DrawListInstance`。
3. 剔除（`DrawListCullingSystem`）：对每个有 pass 在其下画 `Cpu` Tag 的（视图，位），视锥测试列表实例，写可见实例；排序；
   按排好的顺序写 CPU 参数 buffer 的镜像。
4. 排序分组（`DrawGroupSystem`，`Update` 按 pass 的路径分派，最后 `SortDrawGroups`）：
   - `Gpu` 的 pass（`UpdateGpuCulledPass`）：这个 Tag 的每个 `DrawListKey` 一项（由键值求变体、实例数），按（优先级，变体）排、同变体合并；
     写 `PassIndirectLayout`（`WriteIndirectLayout`，只有这条路径有：binTable 按 entry 的键号写，每个视图的 count 与记录数取整），
     再为 pass 渲染的每个视图各写一份组。
   - `Cpu` 的 pass（`UpdateCpuCulledPass`）：对 pass 渲染的每个视图，取（视图，位）的可见实例，按顺序求变体，相邻同变体合并（不排序），
     组的 first 加上这一段的 `FirstDrawOffset`，写组。
5. 渲染图：分桶 pass 的 Build 照旧，读 `PassIndirectLayout`；`Cpu` 的 pass 的 Build 用 `Import<CpuCulledDrawArgumentsTag>` 导入参数 buffer，`DrawIndirect(argumentsName)`
   用一个不带 count 的重载；`AppendItemForView` 经 `DrawGroupsOf` 取（pass，这个视图）的组，每组一个 item，偏移 = first × 记录大小，
   maxCount = count，PSO 由（pass，变体）的描述查缓存。RHI 不用改：`m_countBuffer` 为空即不带 count，DX12 后端已支持。

**不选的**

- `PassKeyPipelines`（按键索引的 pass → 管线表）与 `PipelineKeyCount` 大小的固定数组：键空间不固定；键 → 变体本来就能现算。
- 管线实体（PassContext 里每个（pass，变体）一个，带 `PipelineOf` / `VariantPipeline`，按变体建索引）：它只为不重复建 PSO，
  描述的哈希就够，不需要一套长期存在的实体与索引。
- 键号 = "出现过的组合编号"：编号个数同样随内容增长、不固定（D8 注）。
- binTable 存这一帧出现过的键值并排序、shader 里二分查找：可行，但有了键号就不需要查找。
- 每帧按"键在这一帧有序键列表里的位置"编号写进 `InstanceData`：表稠密、shader 不用查找，但与增量上传冲突（见上）。
- 把键号容量定为实例容量：依赖了一个以后不成立的固定上限（`g_Instances` 也会改成数量可变），需要的只是上面三个性质。
- 三个全局入口 `KeySlotOf` / `KeyOfSlot` / `KeySlotBound`（槽位 → 键值反查、长度）：换成槽位池后后两个要查编码系统里的登记表，
  排序分组的系统调它们就是一个系统去找另一个系统；改由 `DrawListKey` 同时带键值与键号、长度取最大键号。
- 组件里放 vector、要求"取出来原地改写"：靠写法保证，组件被移动、交换、替换时就失效。
- 专门的 `FrameArena<T>`（不可拷贝、不可移动、只有保留容量的 `Reset`）由生产系统持有、组件里放区间：渲染图读组要拿到生产系统，
  经 `Service` 等于一个系统直接调另一个系统；ECS 的 storage 已经是一块 arena。
- 可见实例与组直接放进 RHIContext / PassContext：语义上归不进任何一个，也要逐类型清空。
- CPU 路径每个 pass 一块参数 buffer、挂在 pass 上（`PassDrawArguments`）：参数与 pass 无关；把 pass 要用的资源写到 pass 上，pass 与资源混成一个轴。
- pass 的 Build 按 `PassTag` 找 pass、或经 `GetPass()` 读自己身上的组件：pass 因此知道了列表机制。`InstanceBinning` 现在按 `PassTag`
  找它服务的绘制 pass，是为方便的写法，以后要改（用户 2026-10-21）。
- 渲染图在 `DrawIndirect` 里替 pass 导入它的参数：资源必须由 pass 显式引入，不能在后台偷偷带上。
- 给 CPU 的参数 buffer 每个视图补标起点 0：见上，语义不对。
- CPU 剔除 + `Any`：顺序无关的物体走 GPU 剔除，不为它让 `GroupDrawEntries` 改成只切不排。

**还要设计的**

- 名字已定：`DrawContext`、`DrawEntity`、组 `DrawGroup`、系统 `DrawGroupSystem`、剔除 `DrawListCullingSystem`、目录 `DrawList/`
  （用户 2026-10-21：桶的说法已抹掉，Bin 又与 Binding 易混；GPU 上的 `InstanceBinning` 不改）。
- 键号换成槽位池时：`PipelineKeySlotRef` 放在世界实体的哪个组件上，实例换键时的替换时机（第一版不涉及）。
- `DrawContext` 的清空与 `DrawExecuteContext` 的压入放在 `RenderSystem::Update` 的哪里，编码之前的系统会不会也要往里写。
- GPU 路径里 pass 介入了资源的生成（binTable 与每个 pass 的参数），pass 与资源两个轴怎么拆开，用户还在想（2026-10-21）。

---

## 三、步骤

### 1-0　变体的解释

在 `Render/PipelineState/` 下，纯函数，不依赖 ECS，单元测试覆盖：

- `PipelineKey.h`：键的位布局、`MakePipelineKey` 与解码函数，键空间 16。
- `PipelineVariant`（优先级、剔除覆盖、特性宏、材质 shader 编号）与 `FeatureMacro`。
- `InterpretPipelineVariant(键, pass 的渲染状态, ShaderTemplateFeatures)`：D13 的五步。双面 → `CullMode::None` 已经写好；
  `ShaderTemplateFeatures`（支持的宏、是否忽略材质 shader）在有宏排列之前没有来源，调用方传默认值。
- `BuildDrawPipelineDescriptor`：pass 的 shader、`PassPipelineState`、`PassPipelineLayout` 加变体 → `PipelineStateDescriptorForDraw`。
  `CompilePipelineStates` 用它建 pass 的默认 PSO（空变体）；现在只套用剔除覆盖，带宏或材质 shader 的变体要等宏排列。

### 1　两段式剔除 + 分桶 + item 级 PSO 切换，用 doubleSided 验证

三个小步：

- **1a-1 CPU 一侧，不碰渲染**：
  - `InstanceData::m_pad` 改为 `m_pipelineKey`，HLSL 镜像跟着改。
  - `InstanceBindingSystem`：`ResolveMaterialIndex` 改成先解析出材质句柄，槽位与 `MaterialState` 都从它读，键由它算出；按（分类位，键）计数，
    发布到键实体。
  - `RenderPassBuilder` 加 `.Draw<Tag>()`，`Finalize` 时加 `PassDrawList`；DepthPre、GBuffer 写 `.Draw<OpaqueTag>()`，Shadow 写
    `.Draw<ShadowCasterTag>()`。
  - `DrawBinSystem` 的分配、布局、投影，由 `RenderSystem` 持有，排在 `m_instanceBindingSystem.Update` 之后。变体由 1-0 的
    `InterpretPipelineVariant` 求出（模板性质传默认值），双面的键已经会落进自己的桶。
  - 全部可以用单元测试验证。
- **1a-2 GPU 拆成两段，桶都用 pass 的默认 PSO**：
  - 第一段：`InstanceCulling.hlsl` / `InstanceCullingPass` 改写可见列表（`OpaqueVisibleInstances` / `OpaqueVisibleCount`，投影者同样两个名字），
    清零、视锥测试、"一个视图都没有时也写一次"、取整照旧；可见列表不打 `IndirectArgumentsOf` 标记。
  - 从 `BuildDrawList` 抽出一个函数，按视图类型给出"参与的视图（句柄、`g_Views` 下标）、`viewCapacity`、`maxPerView`"，两段都调它，
    D3 的视图布局先不另做机制。
  - 第二段：`InstanceBinning.hlsl` 与每个绘制 pass 的 `<Pass>InstanceBinning`（同剔除 pass，在 `InstanceBinningPass::SetUp` 里逐个显式写出，不自动生成）：按 `PassTag` 找到绘制 pass，
    读它的 `PassBinLayout`、导入它的 `PassBinTable`，建参数 buffer（`viewCapacity × viewStride` 条）与 count buffer（`viewCapacity × binCount` 个）；
    一个清零 Scope，再每个视图一个 Scope，标出视图起点（参数 k × `viewStride` × 20 字节，count k × `binCount` × 4 字节），线程数 `maxPerView`。
    写 draw 记录的几行从剔除 shader 挪过来。`.Binds<InstanceBindingTag, GeometryBindingTag>()`。
  - 绘制 pass 的 `DrawIndirect` 换成自己那对 buffer 的名字；`ResolveItemForView` 按 D10 展开，要能放入多个 item 并拿到 PassContext 与 pass。
  - 删掉 `InstanceCullingPass` 的四个 `*DrawArguments` / `*DrawCount` 名字。
  - 画面应与现在完全一样，验证拆段本身没有画错。
- **1b PSO 切换**："双面 → `CullMode::None`"这条通用规则 1-0 已经写好，pass 不用写任何东西；GBuffer 一律按 `SV_IsFrontFace`
  翻转法线与切线框架（单面物体的背面被剔掉，不需要排列）；执行器的 item 级切换（D2），查后端换 PSO 是否保留绑定。变体 PSO 按
  （pass，变体）用 `BuildDrawPipelineDescriptor` 同步建，以 `DrawBinPipeline` 挂在桶上。这一步只换渲染状态，
  不需要新 shader，layout 不变。

### ~~2　layout 以声明为准 + shader 宏排列~~　取消（用户 2026-10-18）

layout 那一半见 D4。宏排列等到有了用户再做，即推后的 Masked；原来列的内容留作那时的起点：

- shader 的身份变成（资产，宏集合），宏集合进 hash 与缓存键，异步按需编译（D11）。每种材质一套 shader 的 include 重定向留到第二个材质
  shader 出现时（路线图 I10）。
- shader 模板声明自己的排列范围：支持哪些特性宏、是否忽略材质 shader（D13）。解释系统的第 3、4 步读它。
- 待定：排列多引用的资源要装得进 pass 的 layout，而 pass 自己的 space2 取自默认 shader 的反射。一种做法是材质代码只多引用
  共享组与 bindless（GBuffer 的 `g_MatSampler` 从 space2 挪进材质组），另有"取所有排列反射的并集"、"space2 也在 C++ 声明"。

### 3　Masked　推后（用户 2026-10-18）

依赖宏排列（DepthOnly 要一个 clip 的排列），而半透明更重要。

- `MaterialData` 加 `m_alphaCutoff`，不透明度取 `baseColor.a × 贴图 alpha`；C++ 与 HLSL 两边的 static_assert 一起改。
- `ClassifyDraw` 读材质状态：Masked 为不透明加投影，Blend 等为半透明。
- DepthOnly 模板（DepthPre、Shadow 共用）声明支持 `MATERIALBLENDING_MASKED`：PS 采样 alpha 并 clip，需要材质代码；解释系统按通用规则
  给它靠后的优先级，排在不透明的桶之后（D8"桶的先后"）。
- GBuffer 模板不声明镂空的宏，镂空在 GBuffer 里不改变 PSO：深度 `Equal`，DepthPre 裁掉的像素深度对不上，不需要 clip。

### 4-0　GPU 路径换 binTable 与组的来源（D14、D15）　✅ 完成（2026-10-20）

步骤 4 的前置（用户 2026-10-19 定）。只改不透明与投影这条路，画面应与现在完全一样，之后半透明直接接在同一套排序分组上。
这一步是后面的基础：Tag 的声明、PSO 查找、键号、`DrawContext` 与排序分组都在这里建好。分四小步，各自可构建、可测、画面不变：
4-0a PSO 缓存（✅ `4c50e6a`）、4-0b 键号入口（✅ `9e05b9b`）、4-0c 排序分组函数 `GroupDrawEntries` 与 Tag 声明（✅ `88b36aa`）、
4-0d 切换（✅ `2ca6b53`：`DrawContext`、`DrawListKeyCounter` 发布 `DrawListKey`、`DrawBin` 成为 `DrawContext` 的元素并带变体、
解析与分桶 pass 改读、删旧）。

做完时的说明：PSO 缓存与组的 PSO 只由运行覆盖，没有单元测试。

**之后的整理**（用户 2026-10-20 提出：4-0 逻辑对，但组件散、`DrawBinSystem` 的流程绕，定义串不起来）。根子是 D14 的一般形态只有
GPU 路径一个生产者，一般的类型上带了 GPU 路径的含义，GPU 路径自己的东西又散在几处。在步骤 4 之前做完，CPU 路径再接在整理好的结构上。

- ✅ 组不带 PSO（`2e0ee4c`）：缓存移到渲染图编译器，展开 item 时由（pass，变体）查得；pass 的默认 PSO 也经它。
- ✅ 组总在一个视图下（`bd9970f`）：GPU 路径为 pass 渲染的每个视图各写一份组，不再以"视图为空"代表所有视图。
- ✅ GPU 路径的东西收成一个组件（`38b78db`）：`PassIndirectLayout` 取代 `PassBinLayout` 与 `PassBinTable`，表的 buffer 按键空间定长、不扩容。
- ✅ 组只有一个类型（`fc057b4`）：`DrawGroup` 与 `DrawBin` 合并为 `DrawGroup`，即 `DrawContext` 的元素；分组时每个 entry 记下它落进第几组，
  GPU 路径照 entry 写表，组上不带 entry 区间。
- ✅ `UpdatePass` 按职责拆开（`64f6aa9`）：拆出只有 GPU 路径才有的 `WriteIndirectLayout`，其余留在 `UpdateGpuCulledPass` 一条主线上；
  `Update` 处先看到 pass 走哪条路径。造 entry、写组太短，不单独成函数（用户 2026-10-21）。
- ✅ 头文件按数据流切开（`cc7bf7d`），见 D15；同时改名：目录 `DrawBin/` → `DrawList/`，`DrawBinSystem` → `DrawGroupSystem`。
- ✅ 读组返回排好序的一段（`d590a9b`）：`SortDrawGroups` 与 `DrawGroupsOf`，二分，不做全量过滤。
- 留到步骤 4：`DrawGroup::m_index` 现在兼作组序与 count 下标，两者在 GPU 路径里永远相等；CPU 路径出现条数确切、没有 count 的组时拆成两个字段。

**4-0 本身做的**（名字是 4-0d 时的，整理后的见 D15）：

- Tag 声明 `Order` / `Culling`，`PassDrawList` 带上它们（D15）。
- PSO 查找：键 → 变体 → 描述每帧现算，渲染图编译器里一张描述哈希 → PSO 的表只防重复建，pass 的默认 PSO 也从它取。
  D13 的解释照旧；`BuildDrawPipelineDescriptor` 引用 pass 上建好的 stage function。
- 键号：第一版键号即键值（D15"第一版简化"），只有编码经 `PipelineKeyIndexOf` 取号；`InstanceData::m_pipelineKeyIndex`
  （HLSL 镜像跟着改名），binTable 按它寻址，shader 超出表长时跳过（4-0b）；4-0d 起直方图是 `DrawListKey` 元素，binTable 长度取最大键号加一。
- `DrawContext` 与 `DrawExecuteContext`；组是其中的 `DrawBin` 元素，pass 渲染的每个视图一份。
- 排序分组：D14 的通用机制，这一步只有 GPU 路径一个使用者，输入是 Tag 的直方图；输出组与 binTable。
- 删掉键实体、`KeyInstanceCount`、`PublishKeyInstanceCounts`、分配实体、`KeyBinned` / `PassBinned`、`DrawBinOf` / `DrawBinRange`、
  `Layout`、`WriteBinTables`、`BuildPipelines`，以及 `PassBinTable` 里的定长数组。
- 分桶 pass、`InstanceBinning.hlsl`、`IndirectArgumentsOf`、D10 的展开不动；`CollectDrawnBins` 改为 `CollectDrawBins`，读 `DrawContext` 里的组。

### 4　CPU 路径的间接绘制，用简单着色的半透明验证

这条路的第一个使用者是半透明，所以把步骤 5 的前一部分并进来，用最终会留下的代码验证（用户 2026-10-18 定），不写临时的验证路径：

- D14 / D15 的一般形态：编码给出 `TranslucentTag` 的列表实例；`DrawListCullingSystem` 做（视图，`TranslucentTag`）剔除，写可见实例并排序，
  再写 CPU 参数 buffer；`TranslucentTag` 声明 `BackToFront` + `Cpu`；`DrawGroupSystem` 在记录上切组；每组一次间接调用，不用 count buffer：
  `DrawIndirect` 加不带 count 的重载，`AppendItemForView` 取（pass，视图）的组。与现有 `DrawIndirect` 共用几何绑定（一段代码）。
  这条路对几何的依赖与不透明的 GPU 路径完全相同，几何池以后怎么改都在同一处一起改，不以 `TODO_BufferRangePlan.md` 为前提。
- D6：半透明的分类与标签、视锥测试。标签 `TransparentTag` 改名为 `TranslucentTag`（用户 2026-10-19 同意），占 draw-mask 第 2 位。
  从哪里收集已定为编码给出列表实例（D15），不回读 `g_Instances`、`g_Geometries` 的暂存镜像。
- 分六小步，前五步画面不变（没有东西分进半透明）：
  - ✅ 4a（`dc005c5`）：`TranslucentTag` 与第 2 位、`CpuCulledDrawMask`、计数器只数 GPU 剔除的位、编码写 `DrawListInstance`。
  - ✅ 4b（`4ad8aaf`）：`DrawListCullingSystem`、`VisibleInstance` 与 `VisibleInstancesOf`、`DrawElementRange`、
    `Frustum::IntersectsAABBIgnoringNear`（并改正反向 Z 下平面的标注）。
  - ✅ 4c（`9d18647`）：剔除系统排好序后写 CPU 参数 buffer（`CpuCulledDrawArgumentsTag`，主机可见每帧、`Indirect`，按 1024 一档、不够时换一块），
    `FirstDrawOffset` 给出一段可见实例在其中的起点；`DrawGroupSystem` 的 `UpdateCpuCulledPass` 在变体变化处切组、不碰 RHI，
    两条路径共用 `CollectPassViews`；CPU 剔除必须 `BackToFront`（`.Draw<Tag>()` 的 `static_assert` 与运行时 ASSERT）；
    主机上传池允许 `Indirect`（DX12 上传堆的 `GENERIC_READ` 含间接参数，Vulkan 主机可见 buffer 可带 `INDIRECT_BUFFER`）。
  - 4d　渲染图：不带 count 的间接绘制
    - `RenderScope::DrawIndirect(argumentsName)`：不带 count 的重载。几何绑定（几何池的原生 buffer 作两条流、ID 流、`Uses<>`）
      与带 count 的重载抽成一段共用代码。
    - `RenderGraphBuilder::AddScopeDrawIndirect` 的 count 可为 `NullHandle`（断言只对给出的访问做）；`ItemIndirectArguments` 的 count
      为空即不带 count；`CompileItemIndirectArguments` 只在有 count 时取它的 backing。RHI 不用改：`m_countBuffer` 为空即不带 count。
    - `TryGetIndirectArgumentsStart(context, buffer, view, outByteOffset)`，放在 `TryGetIndirectArgumentsOfView` 旁：在视图下画这块
      buffer 从哪里读起，即写入方为这个视图定下的起点（`IndirectArgumentsOf`），没有写入方定就是 0；buffer 按视图分段却缺这个视图时
      返回 false（原来 `AppendItemForView` 里的断言挪进来）。注释按 D15"起点"一条的语义写，不写成"有段取段、没段取 0"的分支。
    - `AppendItemForView` 重排：参数的起点经它取，有 count 时 count 的起点也经它取；pass 画列表就按组展开（偏移 = 起点 + first × 记录大小，
      有 count 时 count 偏移 = count 起点 + 次序 × 4，maxCount = `m_maxDraws`，PSO 由（pass，变体）查缓存）；不画列表的照旧出一个 item。
    - 单元测试：起点的三种情况（分段取视图那段、不分段为 0、分段缺视图为 false）；不带 count 的间接绘制展开出的 item；
      不透明的画面不变。
  - 4e　`TranslucencyPass`
    - `Feature/Translucency/TranslucencyPass` 与 `Shaders/Translucency/Translucency.hlsl`：VS 同 GBuffer（顶点工厂、材质的顶点偏移），
      PS 不打光，输出 `float4(baseColor × preExposure, opacity)`。
    - 渲染状态：混合 SrcAlpha / InvSrcAlpha，深度只测不写（`GreaterEqual`，反向 Z；写掩码 Zero，`DepthRead(SceneDepth)` 取只读 DSV），
      剔除 Back（双面的由变体改为 None）。SceneColor 先 Load 再 Store，混合在 Skybox 写过的结果之上。
    - 位置：Skybox 之后、TemporalAA 之前（BeforeDOF）；`.RendersView<MainViewTag>()`、`.Draw<TranslucentTag>()`、
      `.Binds<ViewBindingTag, MaterialBindingTag, InstanceBindingTag>()`；材质采样器同 GBuffer。不写 Velocity。
    - Build 显式导入参数：`p.Import<CpuCulledDrawArgumentsTag>(InstanceLists::CpuCulled::DrawArgumentsName())`，成功才
      `s.DrawIndirect(同名)`。attachment 名放进 `InstanceLists`（新的 `CpuCulled` 一组）。pass 不读任何列表机制的组件，不加 `GetPass()`。
    - 材质模板：`inputs.Opacity = mat.BaseColor.a × 贴图 alpha`（现在固定为 1）；GBuffer 不读它，不受影响。
    - 运行时确认：上传堆上的参数 buffer 作为间接参数读时，渲染图不发不合法的 barrier（上传堆资源只能处于 `GENERIC_READ`）；
      没有半透明物体时画面不变、日志无新报错。
  - 4f　分类
    - `ClassifyDraw` 改为由材质状态分类：`Blend` → `TranslucentTag`（不投影），其余照旧为不透明 + 投影。签名改为接收解析后的
      `MaterialState`；编码（`InstanceBindingSystem`）与 `MeshGeometryComposer` 两个调用方都先解析材质（`ResolveMaterial` 挪到两边都能用的地方）。
    - `DrawMaskTest` 按材质状态测分类。
    - 画面由用户用自己准备的场景确认：半透明物体可见、从远到近混合、不进阴影、不透明物体不变。
- 半透明 pass 只做最简单的着色：不打光，输出带不透明度的基色，pass 默认的混合状态为 SrcAlpha / InvSrcAlpha，深度只测不写。
  不透明度取 `baseColor.a × 贴图 alpha`：glTF 的 `baseColorFactor` alpha 与 `alphaMode` 已导入并写进 `MaterialData`，只是材质模板里固定写成 1。
- `ClassifyDraw` 最后改（Blend → 半透明，不投影），中间任何一步都不会出现 Blend 物体从画面上消失的状态。
- 验证用的场景由用户准备。

### 5　半透明的着色

- 材质格式与编辑器（D7 追加的 alphaMode 与 `TranslucencyPass`）。
- 混合状态进变体（D13），按 alphaMode 查表。
- 前向着色库：灯光循环、按灯采阴影 atlas、IBL（D7）。

### 6　Distortion

待 D9。

---

## 四、验证

- **单元测试**：管线键的编码与解码、解释系统的各条规则与规整、PSO 缓存（同一份描述不重复建、不同键同变体同 PSO、哈希相同而描述不同时不混用）、
  键号（换成槽位池时：同键共用、最后一个持有者放下即归还、换键换号）、`DrawListKey` 的发布（按 Tag 位与键计数、两个 Tag 位各计一次）、排序分组（两种排序键、相邻同 PSO 合并、
  不同键同 PSO 并进一组、按优先级的组序、容量与起点、binTable）、`DrawContext` 的元素排序与按所有者切开、D10 展开出的 item、
  两段共用的视图枚举、（视图，Tag）剔除（含不测近平面）、CPU 写出的记录与在记录上切组。
- 步骤 4-0：画面与改之前完全一样。
- **画面由用户确认**，每步交一次。我这边只跑短时间的运行，确认能跑、日志里没有新的报错。
- 步骤 1a-1：只有单元测试与短时间运行，画面不该有任何变化。
- 步骤 1a-2：画面与拆段之前完全一样。
- 步骤 1b：双面物体背面可见、单面物体不变；同一场景切换 doubleSided 后画面随之变化。
- 步骤 3：镂空叶片的轮廓在深度、GBuffer、阴影里一致。
- 步骤 4：半透明以简单着色画出；叠加顺序正确，相机移动时排序随之变化；不同材质的半透明交错时各段用对 PSO；不透明画面不变。
- 步骤 5：半透明受灯光、阴影与 IBL；各种混合模式的结果正确。

---

## 五、已知但不做

- 半透明不写 Velocity，运动的半透明物体在 TAA 下拖影（UE 有让半透明输出速度的选项）。
- 双面半透明先画背面再画正面（UE 的 two-sided separate pass）。
- 半透明投影。
- 半透明的排序优先级（UE 的 `TranslucencySortPriority`）。
- PSO 的录制回放预热（D11 之上的加法）。
- 参数 buffer 按全量预留留下的空洞：每个视图、每个桶都按列表的全部实例预留，可见的远少于全部。只对剔除后的结果排段要由 GPU 决定段起点，
  而 `ExecuteIndirect` / `vkCmdDrawIndexedIndirectCount` 的参数偏移是录制参数、间接命令不能换 PSO。做到它要按（桶，网格）实例化
  （可见实例号由 GPU 压缩，写进固定位置记录的 `StartInstanceLocation`）、cluster 几何加顶点拉取，或 GPU 生成命令
  （`VK_EXT_device_generated_commands`、D3D12 work graphs，不是两边都普遍可用）。等几何改造后再看。
- Additive / Modulate 与顺序无关，可以走 GPU 分桶的路；先统一走 CPU 排序。
- 光追里的镂空（`CANDIDATE_NON_OPAQUE_TRIANGLE`），留在 P7。
- HeightFog（用户 2026-10-15 定不做）。

---

## 关联文档

- `TODO_RenderPipelineRoadmap.md` —— 总览；P5、I5、I10
- `TODO_GPUDrivenPlan.md` —— D7（列表与分段）、D9（pass 自己声明绘制）、D10（与多 PSO 的关系，两段式的出处）、D13（按视图取段）、§六（半透明的三条路）
- `TODO_PerDrawPSOVariant.md` —— 同 pass 共享 layout 的约束仍然成立；"PSO cache 不出 compiler"由 D11 撤掉
- `TODO_DrawItemShapePlan.md` —— §五 per-batch PSO、§六 变体与排序的边界、§七 per-(object, pass) 数据不放实体上
- `TODO_BufferRangePlan.md` —— §五 `DrawIndirect` 接收顶点流附件只是接口形式，不是本文任何一步的前提；§三 / §四 的写入方式与 D5
  冲突，另行设计
- `TODO_StructureAlignPlan.md` —— `SpaceZeroKeepAlive` 的由来（D4）
