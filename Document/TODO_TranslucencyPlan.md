# P5 透明物体　实现方案

路线图 P5 的落地计划。总览见 `TODO_RenderPipelineRoadmap.md`。

本文是草案。2026-10-15 与用户讨论后写下；2026-10-16 按后续讨论改写：分组换成两段式（先可见性、再按 pass 分桶），
PSO 改为由"pass + 材质"查找、可在加载时推导，覆盖材质的状态定为可覆盖；同日再定分桶的数据组织（D8：管线键 + 键级的分配关系）
与绘制方式（D10 选 B）。P5 原来的六项里 HeightFog 不做（用户定），Distortion 待定（D9）。现状一节来自 2026-10-15 / 16 读代码，
没有运行验证。

P5 做三件事：**按材质的 PSO 变体**（I5，含 Masked 与双面）、**参数由 CPU 生成的间接绘制**、**前向着色的半透明**。
做完之后，场景物体有两种提交方式，都是间接绘制：参数由 GPU 生成（不透明、投影）与参数由 CPU 生成（要 CPU 决定
顺序或集合的，先是半透明）。直接绘制仍只给零星的绘制。

---

## 状态

| 步骤 | 内容 | 依赖 | 状态 |
|---|---|---|---|
| 1 | 两段式剔除 + 每个 pass 的分桶 + item 级 PSO 切换，用 doubleSided 验证 | —（PSO 先同步建，D11 不挡） | 未开始 |
| 2 | pass 的 layout 以声明为准 + shader 宏排列 | — | 未开始 |
| 3 | Masked | 1、2 | 未开始 |
| 4 | 参数由 CPU 生成的间接绘制（通用机制） | 1 | 未开始 |
| 5 | 半透明（前向，BeforeDOF） | 1、4 | 未开始 |
| 6 | Distortion | 5 | 待定（D9） |

1 与 2 互不依赖。1 不碰 layout 与 shader 系统，先用纯渲染状态的变体把拆段、分桶、切换验证掉。

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
- 一个 item 用哪个 PSO 来自它的桶（D8、D10）；执行器在 item 的 PSO 与当前不同时 `SetPipelineState`。Scope 开头仍设 pass 的默认 PSO。
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
  读可见列表，以实例的管线键查这个 pass 的表得到桶号与段起点（D8），往桶的段里追加 draw 记录并计数。
- DepthPre 与 GBuffer 读同一份不透明可见列表，各有自己的第二段，"画同一批物体"由构造保证；以后加双阶段 HZB 遮挡剔除只改第一段。
- 写出的参数段照旧由写的一侧用 `IndirectArgumentsOf(view, offset)` 标在访问上（D13 不变）。

**查找与分桶是两件事**

- **查找**：pass 的规则"管线键 → 变体"，变体加 pass 的默认 PSO 就是这个物体在这个 pass 里用的 PSO（D8、D11）。
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

- **第二段怎么知道每个视图的可见列表在哪**（两边各自枚举视图再按次序对齐，少一个就整体错位，D13 指出过）：
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

### D4　pass 的 layout 以声明为准　✅ 已定方向

- pass 的 layout 由 `Binds<...>` 的各个共享组加上 pass 自己的 space2 决定，不再反射默认 shader 得到。改的是 layout 的来源，
  不是时机：现在的 layout 已经在 `PassBuilder::Finalize` 时建好（`PassPipelineLayout`），渲染图之外建 PSO 不依赖 D4。
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
- D8 里每个 pass 的"键 → 桶"表也走这套主机可见每帧 buffer：它只有键空间那么大，每帧整张写。

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
  "这个 pass 下这个物体用什么 PSO"与 GPU 分桶用的是同一个查找：给物体算出管线键，交给这个 pass 的规则（D8），只写一处。

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

### D8　分桶的数据组织：管线键 + 键级的分配关系　✅ 已定（用户 2026-10-16）

CPU 分桶要交出三样：**键 → 桶的映射**（第二段把一个可见实例写进哪个桶）、**每个桶的容量与段起点**（第二段写记录的位置，
也是 CPU 录制间接调用的参数偏移与 `maxCount`）、**每个桶的 PSO**（item 之间切换）。

**管线键**

- 决定一个物体在某个 pass 里用哪个 PSO 的，是材质的 shader、材质的渲染状态，以及物体自己的少数因素（例如镜像物体要翻转剔除方向，
  UE 的 `bReverseCulling`；按帧切换的顶点开关）。三者打包成一个整数：管线键 = shader 编号 × 状态组合数 + 状态位，
  状态位 = alphaMode | doubleSided | 物体标志。
- 编码时与 `ClassifyDraw` 同处、逐实例每帧算出，写进 `InstanceData`（占现在的 `m_pad`），只有这一个来源。物体换材质、切换标志，
  只是下一帧写出另一个键：不维护逐物体或逐材质的映射。
- shader 编号现在恒为 0，物体标志现在一个也没有，两个维度从第一步就在键里。shader 编号以后由 shader 一侧给出连续编号，
  经"材质 → 它引用的 shader"取得，与 `MaterialSlotRef` 同一条路。
- 键空间必须小而稠密：GPU 表按它寻址。shader 多到上千、表到几万项时，再改成只给出现过的组合编号。

**规则**：pass 的分组策略是"键 → 变体"，变体是相对 pass 默认 PSO 的改动（第一步只有剔除模式）；同一个 pass 里变体相同就是同一个桶。
规则以 `PassBucketRule{分类位, 回调}` 挂在 pass 上，回调是 pass 唯一的回调；规则怎么交出去是占位，以后再定。

**实体**

| 实体 | 上下文 | 组件 | 何时建 |
|---|---|---|---|
| 管线键 | RHIContext（同 `InstanceSlotCount`） | `PipelineKey{value}`；每帧写的 `KeyInstanceCount`（每个分类位一个数） | 编码第一次见到这个键 |
| 桶 | PassContext | `BucketOf{pass}`、`BucketVariant`、`BucketPSO`；每帧写的 `BucketSegment{index, first, capacity}` | 第一条分配指向它 |
| 分配 | PassContext | `BucketAssignment{pass, key, bucket}` | 新键或新 pass 出现 |
| pass（已有） | PassContext | `PassBucketRule`、`PassBucketTable`（GPU 表）；每帧写的 `PassBucketLayout{bucketCount, viewStride}` | pass 声明 |

分配关系建在键这一级，数量是键数 × 分桶的 pass 数，很小，也很少变。

**系统**（一帧内的先后；1 是现有的 `InstanceBindingSystem`，2 ~ 4 排在它之后、`ExecutePipeline` 之前）

1. **编码**：算出键写进记录；按（分类位，键）计数，用的是写进记录的 `m_drawMask` 与键，几何未就绪（`m_drawMask` 为 0）与容量溢出
   未编码的实例不计入，与第一段能挑出的实例是同一批。结束时把计数写到各个键实体的 `KeyInstanceCount` 上，没见过的键新建实体。
2. **分配**（低频，用 Exclude 发现新增，同 GlobalBuffer 发现新材质）：`GetView<PipelineKey>(Exclude<KeyAssigned>)` 对每个有规则的 pass
   求变体，在这个 pass 的桶里找变体相同的、没有就新建（同时建 PSO），建一条分配；`GetView<PassBucketRule>(Exclude<PassAssigned>)`
   对已有的全部键做同样的事。pass 的加入与移除整体重算。
3. **布局**（每帧）：遍历分配，`capacity += key 的计数[pass 的分类位]`；遍历桶，按 pass 依次给 `index` 与 `first`（前缀和），
   累计出 `PassBucketLayout`。
4. **投影**（每帧）：遍历分配，写 `table_pass[key.value] = {index, first}`；没有分配的键写"不画"。表是关系的投影，同 `g_Materials`。

渲染图里第二段读这张表，解析 `DrawIndirect` 读这个 pass 的桶（D10）。

**计数为什么每帧做、为什么按键数**：段起点与 `maxCount` 是 CPU 录制时填的，GPU 算出的拿不回来（D3），所以每个桶要有 CPU 知道的上界；
编码本来每帧经过每个实例，顺带数最简单。按键数而不按桶数，是因为编码不知道 pass，同一个键在不同 pass 里可能落进不同的桶。

**非 ECS 的部分，各有理由**

- 编码循环里的局部累加数组：逐实例按值找键实体太贵，先在系统内累加，结束时每个键写一次组件。
- 键值 → 实体的查找：发布计数时按值找键实体。键不超过 32 个，线性扫描；键变多时在系统内加索引。
- 规则回调：它就是 pass 的分组策略。
- GPU 表：GPU 要按键直接寻址，只能是数组。

**以后的加法**：PSO 异步编译时桶上加就绪标签；投影时一个键的分配有任一个桶没就绪，就在所有 pass 的表里写"不画"
（否则 DepthPre 画了、GBuffer 没画，`Equal` 下是空洞）；shader 编号进键；物体标志加位。

**不选的**

- 每帧对每个材质、每个 pass 求规则：材质可达数万。
- 分配建在材质级（材质，桶）：物体一侧影响 PSO 的因素进不来；GPU 表按材质槽位寻址，大小与空洞同 `g_Materials`。
- 分配建在物体级（物体，桶）：数量是物体数 × pass 数，加载帧批量建；GPU 表按物体槽位寻址，只在变化时局部更新，要一种还没有的
  "持久 buffer、局部更新"写法。
- 把 MaterialState 打包成键、shader 身份以后再加：shader 身份要去重成实体、经引用取得，加进来时获取方式整个变掉，不是加法。

### D9　Distortion 做不做　待定

材质模型没有折射输入，做它要先扩材质。倾向与 HeightFog 一样推迟。

### D10　绘制 pass 怎么"每个桶画一次"　✅ 选 B（2026-10-16）

pass 仍然只写一行 `DrawIndirect`，解析时读这个 pass 的桶实体（D8），展开成每个（视图，桶）一个 item：与 D13 按视图展开同一套机制，
pass 不关心有几个桶，就像它现在不关心有几个视图。

- 参数偏移 = 写参数的访问用 `IndirectArgumentsOf` 标出的视图起点 + `first` × 记录大小；视图之间相隔 `viewStride` 条。
- `maxCount` = `capacity`；count 偏移 = （视图 × `bucketCount` + `index`）× 4。
- PSO = 桶的 PSO。`capacity` 为 0 的桶不展开。

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

- **PSO 不是"第一次画时才知道"，而是"键和 pass 都已知时就能推出来"**：pass 对一个管线键用什么 PSO，是（键，pass 的固定配置）
  的确定函数（D8）。新键出现时分配系统就求出变体、建桶，同时发出预编译请求；平时只是查表。
- 因此 pass 的规则必须是"不画就能求值"的，输入只有键与 pass 自己的配置。
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
- ~~查找表由谁在什么时候算~~：D8 的分配系统，在新键或新 pass 出现时。

### D12　覆盖材质的状态也可以被覆盖　✅ 已定（用户 2026-10-16）

- 分桶只读"物体最终解析出的那个材质"的状态，来源是材质资产还是覆盖不影响分桶的设计。
- 要改的都在上层：覆盖组件除了参数还能带状态，`SyncOverrideMaterials` 建覆盖材质时把状态也写进去。覆盖组件的格式做到时再定，不在 P5 主线上。

---

## 三、步骤

### 1　两段式剔除 + 分桶 + item 级 PSO 切换，用 doubleSided 验证

两个小步：

- **1a 只拆段，只有一个桶**：管线键写进 `InstanceData` 并计数；D8 的实体与四个系统，所有 pass 的规则恒返回默认变体，每个 pass 一个桶；
  剔除 pass 改写可见列表，加每个绘制 pass 的第二段（写 draw 记录的代码从现在的剔除 shader 挪过去）；D10 的展开。两段用同一个函数
  枚举视图（`CollectViews` + `TryGetViewIndex`），D3 的视图布局先不另做机制。画面应与现在完全一样，验证拆段本身没有画错。
- **1b 分桶与 PSO 切换**：DepthPre、Shadow、GBuffer 的规则在双面时返回 `CullMode::None`；GBuffer 一律按 `SV_IsFrontFace` 翻转法线与
  切线框架（单面物体的背面被剔掉，不需要排列）；执行器的 item 级切换（D2），查后端换 PSO 是否保留绑定。变体 PSO 按（pass，变体）
  同步建，挂在桶上，拼描述的部分从 `CompilePipelineStates` 抽出来复用。这一步只换渲染状态，不需要新 shader，layout 不变。

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
- 新重载与现有 `DrawIndirect` 共用几何绑定（一段代码），记录里的几何偏移取 `MeshGeometry` 的 `m_firstIndex` / `m_vertexOffset`，
  `StartInstanceLocation` 取实例槽位，与 GPU 那条路同一个约定。这条路对几何的依赖与不透明的 GPU 路径完全相同，几何池以后怎么改都在
  同一处一起改，不以 `TODO_BufferRangePlan.md` 为前提。

### 5　半透明

- 材质格式与编辑器（D7）。
- 半透明标签、收集与排序、按 PSO 切段写参数（D6）。
- 前向着色库与 pass（D7）。

### 6　Distortion

待 D9。

---

## 四、验证

- **单元测试**：管线键的生成、按（分类位，键）计数、新键与新 pass 的分配、同变体的键并进同一个桶、每个桶的容量与段起点、
  投影出的"键 → 桶"表、D10 展开出的 item、两段共用的视图枚举、`ItemOfView` 的解析、CPU 排序与切段。
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
