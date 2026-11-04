# I8 光追场景　实现方案

路线图 P6 的第二个基础项 I8（`TODO_RenderPipelineRoadmap.md` §三）的落地计划：渲染层每帧有一个 TLAS，场景里的静态网格
各有 BLAS，读 TLAS 的 pass 能直接用 inline ray query 追踪。RT 阴影、光追管线（`DispatchRays`）不在本文，见 §五。

前提是 I7（`TODO_RayTracingPlan.md`）已完成：加速结构对象、构建输入、大小查询、`AccelerationStructureBuildItem`、DX12 实现。

本文是草案，2026-10-30 与用户讨论后写下。现状一节来自同日读代码。

---

## 状态

| 步骤 | 内容 | 依赖 | 状态 |
|---|---|---|---|
| 1 | RHI 补缺：scratch 的地址对齐约定、瞬态池拒绝加速结构 buffer、几何池带构建输入用途 | — | 已落地；编译、单测通过，运行检查见 §四 |
| 2 | 渲染图：`AccelerationStructurePass` 与它的 scope，构建与追踪的访问声明 | 1 | 基础已实现；构建编译、提交与追踪读取绑定已接入，场景运行验证待后续 |
| 3 | 光追场景系统与 BLAS：存放 buffer、构建描述、BLAS 状态、构建预算、生命周期 | 2 | 已实现；全量 Debug 编译通过，2026-11-04 用户确认运行验证通过 |
| 4 | TLAS：几何记录中的 BLAS 地址、生成实例的 compute、TLAS 构建、绑进场景组 | 3 | 进行中；固定区间、场景描述和 BLAS 地址上传已实现，实例生成待实现 |
| 5 | 验证用的调试视图 | 4 | 未开始 |

D1 ~ D9 已定；BLAS 地址放入 `GeometryData`，复用 `GeometrySlot` 下标。

**步骤 4 TLAS 实现拆分（2026-11-04，逐步审阅）：**

1. **TLAS 存放区间与场景描述**（已实现）：在共享 AS buffer 内先为 TLAS 预留固定区间，按 65535 实例查询结果和 scratch 大小；发布 buffer 标签、TLAS view 和可用状态。
2. **BLAS 对象持有与地址上传**（已实现）：`RayTracingSceneSystem::Update` 在存放 buffer 就绪并分配区间后创建 RHI AS 对象，由 `RayTracingBottomLevel` 持有。attachment 必须通过 `.AccelerationStructure(...)` 显式传入对象，图不再创建 AS。下一帧 `GeometryBindingSystem` 将地址写入 `GeometryData`，复用现有增量上传；缺失或输入失效时为 0，不增加独立地址表或编译结果回填。
3. **GPU 实例生成**：compute 按活动列表读取 InstanceSlot，再读取 `InstanceData` 与 `GeometryData` 中的 BLAS 地址，生成 64 字节的 `RayTracingInstance`；实例号为 slot，无 BLAS 或 draw mask 为 0 时写无效实例，处理矩阵布局、0 实例与数量上限。
4. **TLAS 构建 scope**：扩展现有构建 Pass，先可选的 BLAS scope，再每帧 TLAS scope；使用实际实例数的 structured view，scratch 按两个阶段需求的最大值分配并复用，由图生成屏障。
5. **共享绑定与追踪入口**：给场景组增加 TLAS 输入，绑定场景系统持有的 TLAS 对象；追踪 scope 显式导入存放 buffer、传入对象并声明 TLAS 读取，普通场景组绑定不隐含 AS 依赖。

几何编码先于光追场景 Update，新 BLAS 的地址自然在下一帧上传；GPU 构建与后续使用仍由队列顺序和 AS 屏障保证，不等待 CPU 侧完成通知。
继续保留 Buffer / Image 图资源模型；AS 对象由外部持有，attachment 的组件仅持有本帧使用所需的引用。

**步骤 2 细分进展（2026-11-03）：**

| 子步骤 | 内容 | 状态 |
|---|---|---|
| 1 | `AccelerationStructurePassBuilder`、类型标签与注册宏，限制 Graphics / Compute 队列 | 已实现 |
| 2 | `AccelerationStructurePassScopes`、`AccelerationStructureScope`、资源导入与构建访问声明 | 已实现 |
| 3 | 构建描述、BLAS / TLAS item 记录与编译函数 | 已实现；2026-11-02 测试已由用户验证通过 |
| 4 | 主编译流程调用构建编译函数，executer 提交 RHI 构建 item | 已实现；2026-11-03 Debug 编译通过，运行验证待用户执行 |
| 5 | Compute / Render scope 的追踪访问接口与绑定 | 已实现；2026-11-03 Debug 编译通过，运行验证待用户执行 |
| 6 | 场景构建 pass 内先 BLAS scope、后 TLAS scope | 方案已定，场景系统接入时落实 |

AS 作为 buffer 的特殊用途，pass 直接导入存放 buffer；构建目标、追踪读取、构建输入和 scratch 的区间均从 attachment 的 view 读取。
渲染层 `AccelerationStructure` 组件只持有 RHI 对象，通过 `.AccelerationStructure(const Ptr<RHI::AccelerationStructure>&)` 显式传入使用它的 attachment，不使用对象缓存。
BLAS / TLAS 的输入组件分开，公共构建组件只引用目标和 scratch attachment。区间重叠校验按讨论推后，由调用方保证同一 scope 内的构建区间不重叠。

---

## 一、现状

- **几何**：`MeshGeometrySystem` 给每个带 `MeshComponent` 的世界实体建一份 `MeshGeometry`，是几何池里的顶点、索引区间。
  同一个模型放在多个实体上，池里就有多份，几何不共享。几何池是一块 `ReservedBuffer`，绑定标志
  `InputAssembly | CopyWrite | RayTracingBuildInput`（步骤 1 已补）；数据按每帧预算分帧拷入，拷完的实体挂
  `MeshGeometryReadyTag`（拷贝 pass 在本帧所有读池的 pass 之前）。几何池在 `RHIContext` 里是一个导入资源实体，带
  `MeshGeometryPoolTag`。顶点是标准格式，位置在最前。
- **几何记录**：`GeometryBindingSystem` 给每个 `MeshGeometry` 一个 `GeometrySlot`（`g_Geometries` 的下标，一个 `DeviceArray`）。
- **实例**：`InstanceBindingSystem` 给每个可渲染实体一个 `InstanceSlot`（`g_Instances` 的下标），记录 `InstanceData`：模型矩阵、
  `m_geometryIndex`（即 `GeometrySlot`，只在 `m_drawMask` 非 0 时有意义）等。活动列表（位置 → slot）以 `g_InstanceIDs` 上 GPU，
  个数在 `ActiveInstanceCount`。容量 2²⁰。
- **场景组**：`SceneBindingSystem` 持有 space0 的 `ShaderBindings`，标签 `MainSceneTag`。当前 `Uses` 经
  `AddScopeUses` / `TakeUploads` 为 scope 接收带相应标签的异步上传，处理 acquire；它不会导入普通资源或创建
  `ScopeAttachment`，因此不能据此生成图内构建到追踪的读依赖。图内资源的访问仍需 scope 显式声明，同 `InstancePoolTag`。
- **渲染图**：pass 类型按 scope 录的命令分（`CopyPass` / `ComputePass` / `RenderPass` / `AccelerationStructurePass`），一种类型一种 scope。
  `CreateBuffer` 建瞬态 buffer，由 RHI 的瞬态池放在共享堆里、与别的瞬态资源别名。`HasAccessConflict` 放过 shader storage 与 AS 的读写组合；
  构建 scope 内还允许 AS / scratch 的同用途重复访问声明，实际区间不重叠暂由调用方保证，跨 scope 仍按整个 buffer 同步。
- **区间分配**：`Suballocator` 在 `pageSize = 0` 时不按页计数，可用于普通 buffer；区间延迟 `frameCountMax` 帧归还。
- **RHI**：I7 的全部内容。步骤 1 已让瞬态池在 validation 开启时拒绝 `RayTracingAccelerationStructure`（与原有的
  reserved / `TileBacking` 校验并列）；scratch 的基地址对齐保证已写进 `BufferBindFlags` 与 `DeviceLimits`，因此构建校验
  只查偏移按 `m_accelerationStructureScratchOffsetAlignment` 对齐。

---

## 二、决策记录

### D1　构建是 `AccelerationStructurePass`，与以后的 `RayTracingPass` 分开　已定

- 新的 pass 类型 `AccelerationStructurePass`，scope 为 `AccelerationStructureScope`，宏 `SPARK_ACCELERATION_STRUCTURE_PASS`。
  scope 录对加速结构本身的操作：现在只有构建；I7 步骤 4 的原地更新、压缩（大小回读与拷贝）以后也在这里。不带 shader
  与绑定，同 `CopyPass`；不能并进 `CopyPass`，拷贝队列做不了加速结构的操作。
- `RayTracingPass` 留给光追管线的 `DispatchRays`：带管线、绑定与着色器表，同 `ComputePass` 一级，分散在各功能里（RT 阴影、
  反射、GI），每个功能一个 pass。
- 全场景一个构建 pass，每帧一次：先本帧要建的 BLAS，再 TLAS。
- 同一 pass 内分两个 scope：第一段构建互不依赖的 BLAS，第二段构建 TLAS，由图在 scope 之间生成 AS 依赖和 scratch 复用屏障。

### D2　几何来源只产出构建描述，BLAS 由光追场景系统构建　已定

- 光追场景系统 `RayTracingSceneSystem`（`Render/RayTracing/`，同其他绑定系统，由 `RenderSystem` 持有的普通类）负责构建、存放与
  TLAS；几何来源只在世界实体上挂一个构建描述组件。描述是来源与光追场景系统之间的约定，定义在 `Render/RayTracing/`，名字
  避开 RHI 的 `RayTracingGeometry`。
- 输入组件为 `RayTracingGeometryBuildInput`，顶点与索引区间使用 `BufferViewDescriptor`，位置格式单独保留；无索引时索引 buffer 句柄为空。
- 描述的内容：输入所在 buffer 的资源实体（`RHIHandle`，构建 pass 据此导入并声明构建输入读）、顶点偏移 / 步长 / 格式 / 个数、
  索引格式 / 偏移 / 个数、是否不透明。一个实体一个几何；一个 BLAS 含多个几何的表达（组件里不放数组）以后再定。
- 描述存在即表示：本帧构建 pass 运行时，输入已在 buffer 里。`MeshGeometrySystem` 在实体挂上 `MeshGeometryReadyTag` 的同一帧挂上
  描述，与 `MeshGeometry` 同去。
- 现在只有静态网格，描述出现后不变，BLAS 建一次。动态几何（蒙皮、地形）以后在描述里加"最后变化的帧号"之类的字段，光追
  场景系统与建 BLAS 时的帧号比较来决定重建，是加法。动态几何各有自己的系统产出描述，描述指向它们自己的输出 buffer。

### D3　生命周期每帧比较　已定

- 光追场景系统每帧比一遍：有 BLAS 而描述已不在（实体删除、网格换了），BLAS 撤掉。同 `RemoveStaleMeshGeometry` 的做法。
- 网格换了：旧 BLAS 撤掉，实例在新 BLAS 建好之前 mask 为 0（D7）。
- 以后多数系统基于"变化"执行时，再统一设计。

### D4　存放：一块固定容量的 buffer，BLAS 与 TLAS 都在里面　已定

- 一块 buffer，绑定标志只有 `RayTracingAccelerationStructure`，Device 堆，不是 reserved（Vulkan 不允许，I7 §二），容量固定，
  初始化时整块提交。在上面用 `Suballocator`（`pageSize = 0`）按 256 对齐分区间。满了就报错并跳过这个 BLAS，同几何池的处理；
  增长以后再做（块链，或另建更大的再重建）。
- 越少越好的原因：追踪读 TLAS 引用的每个 BLAS。Vulkan 的可见性按 buffer 走，TLAS 所在 buffer 的屏障只让 TLAS 的写可见，本帧
  新建的 BLAS 要它所在的 buffer 有"构建写 → 追踪读"的屏障；所以每个追踪 pass 要对每块存放 BLAS 的 buffer 声明 AS 读。
- TLAS 占一个固定区间，大小按实例上限 65535 查一次（查到的大小对实例数不超过它的构建都适用）。
- 存放 buffer 初始容量为 256 MiB，作为系统私有常量，后续按场景需求调整。

### D5　scratch：渲染图的瞬态 buffer，加每帧构建预算　已定

- scratch 构建前后内容都无关，用构建 pass 的 scope `CreateBuffer` 建瞬态 buffer，带 `RayTracingScratchBuffer`。
- 每帧构建预算：同 `UploadBytesPerFrame` 的做法，scratch 预算初值为 8 MiB，超出的任务留到下一帧。
  单个构建所需 scratch 超过预算时允许独占一帧，避免永久等待；各任务区间按设备要求对齐。
- 预算内的 BLAS 各用 scratch 里互不重叠的区间，彼此不加屏障；TLAS 复用 scratch，之前一道屏障。
- scratch 区间通过各自 attachment 的 `.View()` 声明，compiler 从 view 读取字节偏移，构建接口不再传 `scratchOffset`。
  同一 scope 内的 scratch 与 AS 存放区间暂由调用方保证不重叠；构建之间的区间重叠校验推后，不影响跨 scope 的整 buffer 同步。

### D6　BLAS 状态是世界实体上的组件　已定

- 组件持有存放 buffer 资源实体的 `RHIHandle`、其中的 `Suballocation` 和 `Ptr<RHI::AccelerationStructure>`，由 `RayTracingSceneSystem` 创建并管理。
  attachment 通过 `.AccelerationStructure(...)` 共享同一对象。BLAS 状态组件表示已构建或已安排在本帧构建，
  GPU 构建由本帧 pass 在任何读 TLAS 的 pass 之前执行；与描述分开，不复用 `MeshGeometryReadyTag`。
- 状态组件为 `RayTracingBottomLevel`，保存构建时的 `RayTracingGeometryBuildInput` 快照；输入消失或与快照不同即失效，覆盖同一实体在一帧内换网格并重新就绪的情况。
- 组件去掉或移动赋值替换旧值时：AS 对象进入 Device 延迟释放队列，区间延迟归还，还在 GPU 上的 TLAS 引用的对象与内存仍有效。attachment 销毁时仅释放其临时引用。

### D7　TLAS 实例由 compute 写，按活动列表紧排　已定

- 实例个数由 CPU 给：DX12 的构建没有间接形式，Vulkan 的间接构建依赖可选特性 `accelerationStructureIndirectBuild`。个数取
  `ActiveInstanceCount`，超过 65535 截断并警告一次。
- 第 i 个实例对应活动列表的第 i 个位置；实例号写 slot（路线图：TLAS InstanceID = InstanceBinding slot）。没有 BLAS 的实例
  （没有描述、BLAS 没建好、`m_drawMask` 为 0）写 mask 0，占位但不可见。
- BLAS 地址放入 `GeometryData::m_bottomLevelAddress`，占用原有 padding，记录保持 48 字节；HLSL 对应 `uint2 BottomLevelAddress`，偏移为 40。
  compute 经 `InstanceData::m_geometryIndex` 取地址，不增加独立地址 buffer、绑定或槽位。几何共享时 BLAS 跟着共享。
- `GeometryBindingSystem::Update` 从匹配当前构建输入的 `RayTracingBottomLevel` 对象取得地址；没有对象或输入变化时写 0。
  该 Update 早于 `RayTracingSceneSystem::Update`，新 BLAS 在下一帧才进入几何记录，复用 `DeviceArray` 原有变化检测和上传，不增加同帧回填。
- 生成实例的 compute pass 由光追场景系统注册：每线程一个活动位置，取 slot、取 `g_Instances` 的模型矩阵转成 3×4 行主序、
  取 BLAS 地址，写一个 `RHI::RayTracingInstance`。mask 先只有 0xFF / 0，标志先不填；以后按投影等属性分层、按双面材质关剔除。
- 实例 buffer 每帧整个重写，是瞬态 buffer：65535 × 64 字节，`ShaderWrite | RayTracingBuildInput`。

### D8　TLAS 绑进场景组，追踪 scope 显式声明 AS 读　已定

- `RayTracingSceneSystem` 管理存放 buffer 和 TLAS 区间；TLAS 仍按独立 AS 输入绑进 space0（I7 D3），系统间不经 Service。
  共享场景绑定使用场景系统持有的 AS 对象，具体接入在后续步骤实现。
- pass 直接导入存放 buffer，AS 访问通过 `BufferPassAttachment` 描述，`m_buffer` 与 `ScopeAttachment::m_resource` 都指向该 buffer 实体。
  构建目标和 shader 读取通过 `.View(CreateRaw(offset, size))` 声明 AS 区间；构建 item 只保存目标 attachment，编译时从其 AS 组件取得 RHI 对象。
- 所有 AS attachment（包括构建阶段读取）必须通过 `.AccelerationStructure(...)` 传入已初始化对象，scope 关闭时校验。
  Build 阶段检查对象已显式传入并初始化，编译阶段不再增加独立的 AS attachment 校验遍历；构建目标类型由构建 item 校验。
  组件只保存外部提供的 `Ptr<RHI::AccelerationStructure>`；图不创建 AS 对象，不新增 AS 图资源或缓存。
- 实际追踪的 pass 导入 TLAS 所在 buffer，每个追踪 scope 显式声明 `AccelStructRead`，阶段为
  实际执行 ray query 的 shader 阶段（本期为 `ComputeShader`）。依赖与屏障仍落在存放 TLAS 与 BLAS 的 buffer 上，不创建普通
  `BufferView`；shader 中的 TLAS 仍通过独立的加速结构绑定类型取得。
- Compute / Render scope 的 `ReadAccelerationStructure` 返回 `ShaderAttachment`；Compute 固定为 `ComputeShader`，Render 通过
  `.Stage(...)` 声明阶段，或由 `.Bind(...)` 按反射确定。`.Bind(...)` 写入 per-pass 组的独立 AS 输入，不生成普通 buffer view。
- 绑定与访问声明各司其职：绑定场景组本身不表示使用 TLAS。AS 存放 buffer 不加入 `MainSceneTag::Uses`，避免把不追踪的
  光栅 pass 也变成 AS 的读者。当前 `Uses` 只处理上传接收，不能替代这里的导入与访问声明。
- 可以用小接口统一导入和 AS 读声明，具体代码形状在实现时确定；本期不扩展 `Uses` 为普通图资源的自动依赖机制。

### D9　渲染图的访问声明　已定

- `AccelerationStructureScope` 声明：构建输入读（`AccelStructBuildInputRead`，几何池与实例 buffer）、存放 buffer 的 AS 写或
  AS 读写（TLAS 构建读 BLAS、写 TLAS）、scratch（`AccelStructScratch`），阶段都是构建。
- 构建输入的范围与布局通过 attachment 的 `.View(BufferViewDescriptor)` 声明，compiler 从中读取偏移、步长和数量；索引用
  `R16_UINT` / `R32_UINT` view，实例用 `RayTracingInstance` 的 structured view，数量为本次构建的实际数量。顶点位置格式与不透明属性单独保留。
- `HasAccessConflict` 要放过存放 buffer 上同一 scope 的 `AccelStructRead | AccelStructWrite`。`AccelStructScratch` 是写，两个
  scope 同时用一块 scratch 判为冲突，正确。
- 生成实例的 compute scope 声明实例 buffer 的 `ShaderStorageWrite`；到构建 scope 的 `AccelStructBuildInputRead` 由渲染图生成屏障。
- 追踪 scope 对 AS 存放 buffer 显式声明 `AccelStructRead`（D8），由渲染图生成构建写到 shader 追踪读的依赖与屏障。

---

## 三、步骤

1. **RHI 补缺**（小，代码已落地）：
   - 约定：带 `RayTracingScratchBuffer` 的 buffer 起始地址由后端保证按 `m_accelerationStructureScratchOffsetAlignment` 对齐，
     普通池与瞬态池都遵守，校验只查偏移因此成立。已写进 `BufferBindFlags` 与 `DeviceLimits` 的注释。DX12 现有普通池通过
     D3D12MA 创建独立 buffer 资源，瞬态池按 `GetResourceAllocationInfo` 对齐放置，现有 64 KB 对齐满足 256 字节的 scratch 要求，
     无需改分配路径。Vulkan 尚未实现 buffer 池：后续实现须显式保证基设备地址对齐，带设备地址用途的内存分配须有
     `VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT`，普通池与瞬态池同样遵守此契约。
   - 瞬态池拒绝带 `RayTracingAccelerationStructure` 的 buffer：它与别的资源重叠在 DX12 上是错误，同 `BufferPool` 的约束。
   - 几何池的 `ReservedBuffer` 已加 `RayTracingBuildInput`。已核对 Vulkan：
     [`vkGetBufferDeviceAddress` 的 03324](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetBufferDeviceAddress.html)
     允许 sparse buffer 取得设备地址；
     [`vkCmdBuildAccelerationStructuresKHR` 的 03673 / 03804 / 03806](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBuildAccelerationStructuresKHR.html)
     要求构建输入用途和有效的顶点 / 索引设备地址，没有禁止 sparse 构建输入。后端仍须具备 sparse / buffer device address 能力，
     为输入 buffer 设置相应用途，并保证实际输入区间已映射、上传与构建正确同步；AS 存放 buffer 的非 sparse 限制不套在输入上。
2. **渲染图**（中）：`AccelerationStructurePassBuilder`、`AccelerationStructureScope` 与构建的录入（D1）；构建与追踪的
   访问声明（D8、D9），以及 `HasAccessConflict`（D9）。
3. **光追场景系统与 BLAS**（中）：存放 buffer 与区间分配（D4）、构建描述及 `MeshGeometrySystem` 的产出（D2）、BLAS 状态组件
   （D6）、构建预算与 scratch（D5）、每帧比较（D3）、构建 pass 的注册。
4. **TLAS**（中）：几何记录中的 BLAS 地址、生成实例的 compute pass、TLAS 构建（D7）；TLAS 绑进场景组，存放 buffer 提供标签
   供追踪 pass 导入，通过 attachment view 选择 TLAS 区间（D8）。
5. **调试视图**（小）：一个 compute pass，每像素从相机打一条主光线，命中按实例号着色、未命中黑，可开关。也是读 TLAS 的
   第一个使用者：显式导入 TLAS 所在 buffer、声明区间和读取，验证 D8 的构建到追踪依赖。

每一步之后：全量编译，单元测试通过；编辑器运行无报错。

---

## 四、验证

**步骤 1 验证记录（2026-11-01）：**

- `cmake --build --preset debug --parallel 4` 全量构建通过；`ctest --test-dir build -C Debug --output-on-failure`
  的 5 个测试目标全部通过。
- `RayQueryTriangle` 完成 BLAS / TLAS 构建并运行、正常关闭，未记录 error；退出时有一条 D3D12 `Live ID3D12Device` 警告。
- 编辑器完成 DX12 初始化并进入资产预加载，日志有 `ModelAssetLoader` 的 glTF 必需扩展未启用错误。自动关闭未在时限内完成，
  已终止本次启动的进程；未完成进入场景后的验证，不视作「编辑器运行无报错」通过。该解析错误与退出行为本次未修改。
- 本次没有运行 Vulkan 后端，也没有开启 GPU-based validation；跨后端约束依据规范核对，当前运行检查使用 DX12 Debug。

**步骤 2 本轮验证记录（2026-11-02，进行中）：**

- `cmake --build --preset debug --target SparkRender --parallel 4` 的 Debug 构建通过，包含相关依赖；新接口的单独编译通过。
- 用户已完成本轮测试验证，并确认均通过。
- 步骤 1 的编辑器运行验证已由用户补充完成并确认通过；上面的 2026-11-01 记录保留为当次执行结果。
- 截至 2026-11-02，已完成 Pass / Scope、AS 实体导入、构建 item 记录与编译函数；主编译流程和命令提交接线、追踪访问入口仍待实现。

**步骤 2 子步骤 4 进展（2026-11-03）：**

- `ExecutePipeline` 在瞬态资源物化之后、生成提交列表之前调用 `CompileItemAccelerationStructures`，补齐 RHI 构建 item。
- executer 的 `SubmitItem` 识别 `RHI::AccelerationStructureBuildItem`，沿用现有提交序号调用 `CommandList::Submit`。
- 构建命令沿用 scope 的屏障、队列同步、提交列表和帧末清理；区间重叠校验继续推后。
- `cmake --build --preset debug --target SparkRender --parallel 4` 的 Debug 构建通过；本轮未运行单元测试或编辑器，运行验证待用户执行。
  追踪访问入口和场景系统接入仍在后续步骤。

**步骤 2 子步骤 5 进展（2026-11-03）：**

- Compute / Render scope 新增 `ReadAccelerationStructure`，返回 `ShaderAttachment`；访问依赖与屏障继续跟踪底层存放 buffer。
- `.Bind(...)` 匹配独立的 AS 反射输入，校验 TLAS 与 shader 阶段，并在编译绑定时解析资源实体、写入 AS 对象；未绑定的 per-pass AS 输入按现有规则清空。
- 共享场景组仍单独绑定，只有 scope 显式声明 AS 读取才产生图内依赖；`Binds<MainSceneTag>` 不隐含读取。
- `cmake --build --preset debug --target SparkRender --parallel 4` 的 Debug 构建通过；新接口的独立编译通过，覆盖 Compute `.Bind()`、Render `.Stage()` / `.Bind()`。
  本轮未运行单元测试或编辑器，运行验证由用户执行。

**步骤 2 AS 对象持有方式调整（2026-11-03）：**

- 删除 RHI 层 AS 资源组件和专用导入分支，AS 访问直接使用 `BufferPassAttachment`，构建目标和追踪读取通过 view 声明存放区间。
- 渲染层 `AccelerationStructure` 组件只保存 `Ptr<RHI::AccelerationStructure>`，在编译阶段创建并挂到 attachment 上，无对象缓存；attachment 销毁时释放引用。
- `ItemAccelerationStructureBuild` 只引用目标与 scratch attachment，构建 item 和 shader 绑定使用编译阶段创建的对象。
- 多个 scope 绑定同一 per-pass AS 输入时，按底层 buffer、区间和 AS 类型检查一致性，允许各 attachment 持有独立对象。
- 新接口独立编译通过，覆盖同一 buffer 中多个 AS 目标及 shader 读取；`cmake --build --preset debug --target SparkRender --parallel 4` 的 Debug 构建通过。
  本轮未运行单元测试或编辑器，运行验证待用户执行。

**步骤 3 几何构建描述进展（2026-11-04）：**

- 新增 `RayTracingGeometryBuildInput` 世界组件，记录顶点和索引 buffer 实体、view、位置格式与不透明属性。
- `MeshGeometrySystem` 在上传完成、本帧几何可用时与 `MeshGeometryReadyTag` 一起发布描述；更换或移除网格时清除，系统关闭时一并清理。
- 标准顶点使用 `R32G32B32_FLOAT` 位置格式，索引分别描述为 `R16_UINT` / `R32_UINT` view，支持无索引几何。
- `cmake --build --preset debug --target SparkRender --parallel 4` 的 Debug 构建通过；本轮未运行单元测试或编辑器。

**步骤 3 BLAS 状态组件进展（2026-11-04）：**

- 新增 `RayTracingBottomLevel` 世界组件，保存存放 buffer 资源句柄、拥有的分配区间，以及构建所用的几何输入快照。
- `RayTracingGeometryBuildInput` 支持按 buffer、view、位置格式与不透明属性比较；`RemoveStaleRayTracingBottomLevels` 清理输入消失或变化的 BLAS 状态。
- 状态移除或实体销毁时，区间通过现有 `Suballocation` 生命周期延迟归还。清理函数由 `RayTracingSceneSystem::Update` 调用。
- `cmake --build --preset debug --target SparkRender --parallel 4` 的 Debug 构建通过；本轮未运行单元测试或编辑器。

**步骤 3 系统资源与本帧任务准备进展（2026-11-04）：**

- 新增 `RayTracingSceneSystem` 的 `Init` / `Update` / `Shutdown`，设备不支持光追时不创建资源；存放 buffer 通过现有 RHI 资源流程创建。
- `Update` 清理失效 BLAS，等待存放和输入 buffer 就绪后，查询大小、筛选预算并分配结果区间；输入仍可在本帧通过图内上传就绪。
- 本帧构建数组保存输入描述、目标 view 和 scratch view；结果区间按 256 字节对齐，scratch 按设备要求对齐，构建标志统一为 `PreferFastTrace`。
- 选中的实体持有 `RayTracingBottomLevel` 状态；Shutdown 清理状态、资源实体和分配器。未引入 TLAS 或地址表。
- 本阶段准备构建任务；`SetUp`、构建 Pass 和 `RenderSystem` 接入见下方记录。
- `cmake --build --preset debug --target SparkRender --parallel 4` 的 Debug 构建通过；本轮未运行单元测试或编辑器。

**步骤 3 BLAS 构建 Pass 与系统接入进展（2026-11-04）：**

- `RayTracingSceneSystem::SetUp` 注册 `RayTracingSceneBuild`，在 Graphics 队列的一个 scope 内录入本帧所有 BLAS 构建，每个几何仍对应一个 BLAS。
- Pass 按需导入顶点和索引 buffer，名称由资源实体句柄生成；按本帧总需求创建瞬态 scratch，各目标和 scratch 区间使用任务记录的 view。
- `RenderSystem` 在几何上传 Pass 之后注册构建 Pass；每帧在几何、实例更新后准备任务，关闭时在渲染图等待 GPU 完成后释放资源。
- 构建 Pass 由 `SetUp` 注册，调用顺序由代码保证；没有任务的帧不声明 scope。输入上传到构建的依赖、屏障以及命令提交沿用现有渲染图流程。
- `cmake --build --preset debug --parallel 4` 全量 Debug 构建通过，包含运行时、编辑器、样例和测试目标的编译链接；本轮未运行单元测试或编辑器。

**BLAS 运行验证补充（2026-11-04）：**

- 用户确认当前 BLAS 构建流程运行验证通过。

**步骤 4 TLAS 固定区间与场景描述进展（2026-11-04）：**

- 初始化时按 `RayTracingInstanceCapacity = 65535` 和 `PreferFastTrace` 查询 TLAS 结果与 scratch 大小，保存查询结果。
- 在共享 AS buffer 的分配器中先预留 256 字节对齐的 TLAS 固定区间，后续 BLAS 使用剩余空间；Shutdown 释放该区间并重置查询结果。
- 存放 buffer 实体携带 `RayTracingSceneTag` 和 `RayTracingScene`，发布 TLAS raw view；`m_ready` 初始为 false，待实际 TLAS 构建接入后才标记可用。
- 本步只完成区间与描述，地址表、实例生成、TLAS 构建和共享绑定继续按后续步骤实现。
- `cmake --build --preset debug --parallel 4` 全量 Debug 构建通过；本轮未运行单元测试或编辑器。

**步骤 4 BLAS 对象持有与地址上传进展（2026-11-04）：**

- 场景系统在 Update 中创建 BLAS 对象，世界实体的 `RayTracingBottomLevel` 长期持有；构建请求和 attachment 共享引用。状态销毁或替换旧对象时延迟释放 AS，存放区间继续延迟归还。
- `Attachment` 与 `ShaderAttachment` 支持链式 `.AccelerationStructure(...)`；所有 AS attachment 必须显式传入对象并在 Build 阶段检查，Compile 不再创建对象或单独遍历校验 AS attachment。
- `GeometryData` 保持 48 字节，新增 BLAS 地址字段并同步 HLSL；下一帧正常增量上传，输入失效时清零。不增加独立地址表、上传 Pass、结果提取或延后回填接口。
- 实例生成、TLAS 构建及共享绑定尚未接入。
- `cmake --build --preset debug --parallel 4` 全量 Debug 编译与链接通过；未运行单元测试或编辑器。

**场景接入后的验证项：**

- 调试视图：场景里每个静态网格都在（BLAS 与 TLAS 的实例对得上）；与光栅画面重合（实例变换的转换）；实例号着色与
  `InstanceSlot` 一致；移动物体时跟着动（TLAS 每帧更新）；删除、换网格后正确消失与重建（D3）。
- 读依赖：调试视图的追踪 scope 有 AS 存放 buffer 的读 attachment，构建写到 `ComputeShader` AS 读的屏障由图生成；
  只绑定场景组、不追踪的 pass 不产生 AS 读依赖（D8）。
- 加载大场景时 BLAS 分帧建完，没有单帧尖峰超过预算（D5）。
- debug layer 与 GPU 验证层无报错。

---

## 五、不在本计划内

- 光追管线与 `RayTracingPass`（D1），RT 阴影（路线图 P6 第 3 项）。
- BLAS 压缩与原地更新（I7 步骤 4）；静态网格先不压缩。
- 动态几何（蒙皮、地形）的描述来源与重建信号（D2）。
- 存放 buffer 的增长（D4）。
- 几何共享（一个模型的多个实体共用一份几何与 BLAS）。
- 一个 BLAS 含多个几何。

---

## 关联文档

- `TODO_RenderPipelineRoadmap.md`：决策 3、5，§三 I8，P6。
- `TODO_RayTracingPlan.md`：I7，D1（加速结构对象）、D2（存放 buffer）、D3（绑定类型）、D5（访问位）、§六。
- `TODO_GPUDrivenPlan.md`：`g_Instances`、`g_Geometries`、活动列表。
- `TODO_BufferRangePlan.md`：区间分配在 RHI 之上、reserved 资源。
