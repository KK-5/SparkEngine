# I7 RHI 光追　实现方案

路线图 P6 的第一个基础项 I7（`TODO_RenderPipelineRoadmap.md` §三）的落地计划：RHI 有了加速结构，能构建、能在 shader 里用
inline ray query 追踪。I8（光追场景：BLAS 跟着几何、TLAS 每帧更新、进渲染图）与 RT 阴影不在本文，只在 §六 列出它们要从这里拿什么。

本文是草案，2026-10-22 与用户讨论后写下。路线图的决策 3（只用 inline ray query）、决策 5（接口按 Vulkan 语义设计，加速结构是
独立对象与独立绑定类型）是前提。现状一节来自 2026-10-22 读代码；原生语义一节按记忆写，**动手前要对照规范逐条确认**，确认过的标出来。

2026-10-29 按后来的定论改过：缓冲区间计划 R5 删了池在一个原生 buffer 里划分的做法，区间分配定在 RHI 之上（D1、D2 重写）；
光追硬件是最低要求、不做回退（D7 重写）。

2026-10-30：构建输入有了自己的绑定标志与访问位，不再复用 `ShaderSampledRead`；scratch 有了自己的访问位（D5）。

---

## 状态

| 步骤 | 内容 | 依赖 | 状态 |
|---|---|---|---|
| 1 | 最低要求检查、构建阶段 | — | 已完成 |
| 2 | 加速结构对象与绑定类型（含反射），删掉 buffer 路径里的特例 | 1 | 已完成 |
| 3 | 构建：输入描述、所需大小、构建命令、访问与屏障；一个 RHI 示例验证 | 2 | 已完成 |
| 4 | 压缩与原地更新 | 3 | 推后：I8 的静态网格先不压缩 |

D1 ~ D8 已定。

---

## 一、现状

O3DE 的写法留下了一些零件，形状是"加速结构是一块带特殊用途位的 buffer"：

- `DeviceFeatures::m_rayTracing`：DX12 判断 `RaytracingTier != NOT_SUPPORTED`。inline ray query 要 Tier 1.1，这个判断不够。
- `BufferBindFlags::RayTracingAccelerationStructure / RayTracingShaderTable / RayTracingScratchBuffer`；`RHIResourceSystem` 的
  device committed 池（注释是 "oversized / ray-tracing escape hatch"）允许这三个位。
- 加速结构走 buffer 视图：DX12 的 `BufferView` 在带 AS 位时建 SRV，`ConvertBufferView` 写 `RAYTRACING_ACCELERATION_STRUCTURE`
  维度，`DescriptorContext::CreateShaderResourceView` 为它把资源置空；但同一个函数接着按 raw 视图再建一份 bindless 的静态 SRV，
  对 AS 是错的（没有跑过）。`BufferViewDescriptor::CreateRayTracingTLAS` 被注释掉了。
- `ShaderInputBufferType::AccelerationStructure`：绑定类型是 buffer 的一个子类型。shader 反射（`ShaderAssetCompiler`）不认
  `D3D_SIT_RTACCELERATIONSTRUCTURE`，会落到 `Buffer` / `Unknown`。
- 屏障：`AccessFlags::AccelStructRead / AccelStructWrite` 已有，DX12 转换到 `RAYTRACING_ACCELERATION_STRUCTURE_READ / WRITE`；
  `ResourceState` 要求它们的 buffer 带 AS 位，并有 `ConvertToRayTracingAccelerationStructure`。scratch 被归到 `ShaderStorageRead /
  Write`（对不对见 D5）。`AttachmentStage` 有 `RayTracingShader`，**没有构建加速结构这个阶段**。
- 没有：加速结构对象、构建输入的描述、所需大小的查询、构建命令（`Device.cpp` 只在函数名表里列了 `BuildRaytracingAccelerationStructure`）、
  实例结构。`DispatchRaysItem.h` 是空文件。
- shader 已按 SM 6.6 编译（bindless 需要），`RayQuery` 只要 6.5，编译一侧不用改。
- Vulkan 后端只有 Instance 与 PhysicalDevice，可选扩展里列了 `VK_KHR_acceleration_structure`、`VK_KHR_ray_tracing_pipeline`、
  `VK_KHR_ray_query`，并取了属性。
- 相关的已定规则（`TODO_BufferRangePlan.md` §四）：RHI 的 `BufferPool` 只做资源分配，不在一个原生 buffer 里划分（R5 删了
  预算模式）；区间分配是 Core 的纯 CPU 算法，由渲染层的持有者在自己的大 buffer 上用，几何池就是区间池加 `ReservedBuffer`。
  延迟归还走 `QueueForRelease(Ptr<Object>)`。`RHIResourceSystem` 长期要去掉，它的 committed 池不作为加速结构的去处。
- 最低要求在 DX12 `Device` 初始化时检查，不满足就失败（Resource Heap Tier 2、Tiled Resources Tier 2、VS 写渲染目标数组层）。

---

## 二、原生语义

2026-10-29 对照规范确认：DX12 按 DirectX-Specs 的 `Raytracing.md`（v1.46）与 `D3D12EnhancedBarriers.md`，Vulkan 按
docs.vulkan.org 的参考页（`VkAccelerationStructureCreateInfoKHR`、`vkCmdBuildAccelerationStructuresKHR`、
`vkGetAccelerationStructureBuildSizesKHR`、`VkGeometryInstanceFlagBitsKHR`）。标"✓"的是逐字核对过的，其余仍是记忆。

| | DX12 | Vulkan |
|---|---|---|
| 加速结构是什么 ✓ | buffer 里的一段，用 GPU 虚拟地址指称；没有独立对象 | `VkAccelerationStructureKHR`：在（buffer，偏移，大小，类型）上创建的对象，不持有内存；地址由 `vkGetAccelerationStructureDeviceAddressKHR` 取 |
| 存放它的 buffer ✓ | 只能在 default 堆；Enhanced Barriers 下创建时带 `RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE`，并带 `ALLOW_UNORDERED_ACCESS`；终身只作加速结构，不能转出。与非加速结构的 buffer 重叠（放置资源）是 debug layer 错误 | 用途 `ACCELERATION_STRUCTURE_STORAGE`（不要求 `SHADER_DEVICE_ADDRESS`，草案原写错）；一个 buffer 可放多个 |
| reserved ✓ | 允许。一个 tile 不能同时映射进加速结构 buffer 和非加速结构的资源；映射或解除映射使该 tile 内容失效 | **不允许**：buffer 不能带 `SPARSE_RESIDENCY`（VUID-03615）；只带 `SPARSE_BINDING`（必须整个绑定）不受此限 |
| 对齐 ✓ | 256 字节 | 偏移是 256 的倍数 |
| 构建输入 ✓ | GPU 地址。顶点地址与步长按分量大小对齐（32 位分量 4、16 位 2），步长无上限；索引按索引大小；变换 16；实例 16 | 设备地址，对齐同左（顶点按格式、索引按类型、变换 16、实例数组 16）；buffer 带 `BUILD_INPUT_READ_ONLY`（与设备地址） |
| 顶点格式 ✓ | 基础：RG32F、RGB32F、RG16F、RGBA16F、RG16 / RGBA16 SNORM；Tier 1.1 加 UNORM16、RGB10A2、8 位 UNORM / SNORM | 由格式特性 `ACCELERATION_STRUCTURE_VERTEX_BUFFER` 报告（必支持的集合未核对） |
| 所需大小 ✓ | `GetRaytracingAccelerationStructurePrebuildInfo`：结果、构建 scratch、更新 scratch | `vkGetAccelerationStructureBuildSizesKHR`：同三个；忽略地址、源、目标与模式，要每个几何的最大图元数。之后的构建 / 更新只要几何数、类型、标志、顶点格式、索引类型相同，图元数与 `maxVertex` 不超过查询时的值，就够用 |
| 目标的大小 ✓ | — | 对象创建时的大小不能小于查询结果 |
| scratch ✓ | 地址 256 对齐；处于 `UNORDERED_ACCESS`（Enhanced Barriers 下即 UAV 访问） | storage buffer 用途 + 设备地址，按 `minAccelerationStructureScratchOffsetAlignment` 对齐 |
| 构建 ✓ | `BuildRaytracingAccelerationStructure`，一次一个；图形或 compute 命令列表，不能在 bundle | `vkCmdBuildAccelerationStructuresKHR`，一次多个；compute 队列能力；同一次调用里各目标、各 scratch 互不重叠，目标也不与任何 scratch 重叠 |
| 同步 ✓ | 目标 / 源：`SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE` + AS 写 / 读；输入：同一 sync + `ACCESS_SHADER_RESOURCE`（旧状态 `NON_PIXEL_SHADER_RESOURCE`）；scratch：UAV（旧状态 `UNORDERED_ACCESS`）；追踪读：`SYNC_COMPUTE_SHADING` 等 + AS 读 | 目标 / 源：`ACCELERATION_STRUCTURE_BUILD` 阶段 + AS 写 / 读；输入：同一阶段 + `SHADER_READ`；scratch：同一阶段 + **AS 读写** |
| 实例 ✓ | `D3D12_RAYTRACING_INSTANCE_DESC`，64 字节 | `VkAccelerationStructureInstanceKHR`，64 字节，紧密排列。标志位数值相同（剔除关闭 1、翻转正面 2、强制不透明 4、强制非不透明 8；Vulkan 的 `FLIP_FACING` 别名正是 `FRONT_COUNTERCLOCKWISE`） |
| 绑定 ✓ | SRV，维度 `RAYTRACING_ACCELERATION_STRUCTURE`，资源必须为空、地址写在视图描述里；也可作根描述符。地址 0 是合法的空加速结构，追踪必然未命中 | 描述符类型 `ACCELERATION_STRUCTURE_KHR`，经 `VkWriteDescriptorSetAccelerationStructureKHR` 写对象 |
| 压缩后的大小 | `EmitRaytracingAccelerationStructurePostbuildInfo` 写进 buffer | 写进查询池（可再拷进 buffer） |
| inline ray query | Tier 1.1 ✓ | `rayQuery` 特性 + `accelerationStructure` + `bufferDeviceAddress` |

更受限的一边：对象一侧是 Vulkan（要有对象、写描述符要对象），大小查询是 Vulkan（要最大图元数），压缩的回读是 Vulkan（查询池），
存放的 buffer 是否能 reserved 是 Vulkan（不能），buffer 屏障的粒度是 DX12（只能整个原生 buffer）。

- **Enhanced Barriers 规范自相矛盾**：`SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE` 一节说可配的访问只有 AS 读，
  兼容表又让 `SHADER_RESOURCE` 可配它；UAV 访问的兼容表不含它，旧 UAV 屏障的等价写法里却有。输入与 scratch 的屏障
  以步骤 3 示例在 debug layer 下的实际结果为准。2026-10-30 实测（`RayQueryTriangle`，RTX 3060）：输入 `SHADER_RESOURCE`、
  scratch `UNORDERED_ACCESS`、存放 buffer AS 写到 AS 读写，都配 `SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE`，debug layer
  与 GPU 验证层均无报错。

---

## 三、决策记录

### D1　加速结构是建在一段 buffer 上的对象　已定

`RHI::AccelerationStructure` 由 Factory 创建，`Init(device, {type, buffer, offset, size})`：建在一段 buffer 上，不持有内存。
持有 buffer 的引用，记下偏移、大小、类型；不是资源，不进池，不跟踪状态，内存、状态与屏障都在 buffer 上。
RHI 不记哪个 buffer 上建了哪些加速结构，多个加速结构放进同一个 buffer 是上层的事（D2）。

- DX12：地址（buffer 地址 + 偏移）与一个 SRV 描述符（资源为空、地址写在 `Location`）。描述符在 CPU 侧，绑定时拷进描述符表，
  销毁时直接释放，同 `BufferView`。
- Vulkan：创建并持有 `VkAccelerationStructureKHR`，销毁走延迟释放。

### D2　存放加速结构的 buffer 只放加速结构，由上层划分、按原生 buffer 跟踪　已定

- 存放用的 buffer 绑定标志只有 `RayTracingAccelerationStructure`，在 device 堆，不是 reserved 资源；加速结构的偏移按 256 对齐。
  DX12 创建时带 `RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE` 与 `ALLOW_UNORDERED_ACCESS`；Vulkan 用途为
  `ACCELERATION_STRUCTURE_STORAGE`。
- RHI 的池只分配整个 buffer。在一个 buffer 里给多个加速结构划区间，由持有者用 Core 的区间分配做；区间归还时连同
  加速结构对象经 `QueueForRelease` 延迟。
- 渲染图里的资源是这个原生 buffer：构建 pass 声明写、追踪 pass 声明读，屏障按 buffer 生成。

### D3　绑定类型独立　已定

- `ShaderInputType` 加 `AccelerationStructure`，有自己的输入类 `ShaderInputAccelerationStructure`（存 `const AccelerationStructure*`），
  删掉 `ShaderInputBufferType::AccelerationStructure`。
- 反射把 `D3D_SIT_RTACCELERATIONSTRUCTURE` 映射到它（以后 SPIR-V 反射同样）。
- DX12 后端在 `PipelineLayout` 里把它并进 SRV 范围；SRV 在对象 Init 时建好（资源为空、填地址）。Vulkan 在布局里是
  `ACCELERATION_STRUCTURE_KHR` 类型的独立 binding，写描述符时用对象。
- buffer 视图路径里为 AS 开的特例全部删掉（`BufferView`、`ConvertBufferView`、`DescriptorContext` 三处）。
- 不进 bindless，按名字绑进场景组。

### D4　实例是一个 RHI 结构，与两边的原生布局逐位相同　已定

- `RHI::RayTracingInstance`：3×4 行主序变换、24 位实例号 + 8 位 mask、24 位命中组偏移 + 8 位标志、64 位加速结构地址；`static_assert`
  大小 64，两个后端各自 `static_assert` 与原生结构的偏移一致。标志位取两边相同的数值（剔除关闭 1、翻转正面 2、强制不透明 4、
  强制非不透明 8）。
- 实例 buffer 里紧密排列，起始地址 16 字节对齐。CPU 或 compute 直接写，后端不转换。
- 地址由 `AccelerationStructure::GetDeviceAddress()` 给出。

### D5　同步　已定

一次构建涉及三类内存：输入（顶点 / 索引 / 实例，只读）、目标（存放加速结构的 buffer，D2）、scratch（驱动构建时的工作内存，
构建前的内容无关、构建后未定义，大小由大小查询给出；与目标是两块内存）。

- `AttachmentStage` 加 `AccelerationStructureBuild`（DX12 `SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE`，Vulkan
  `ACCELERATION_STRUCTURE_BUILD`），拷贝 / 压缩用的阶段到步骤 4 再加。
- 目标：构建阶段 `AccelStructWrite`（更新时的源为 `AccelStructRead`）；追踪：compute 阶段 `AccelStructRead`。
- 输入：构建阶段 `AccelStructBuildInputRead`，要求 buffer 带绑定标志 `RayTracingBuildInput`。访问 DX12 为
  `ACCESS_SHADER_RESOURCE`，Vulkan 为 `SHADER_READ`；绑定标志 DX12 清掉 `DENY_SHADER_RESOURCE`，Vulkan 为
  `ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY` + `SHADER_DEVICE_ADDRESS`。不复用 `ShaderSampledRead`：校验按访问位查
  绑定标志，它已对应 `ShaderRead`；让构建输入也只带 `ShaderRead`，Vulkan 就得给每个着色器读的 buffer 都加上构建的用途。
- scratch：构建阶段 `AccelStructScratch`（写的区间，读写一起），要求 buffer 带 `RayTracingScratchBuffer`。访问 DX12 为
  `ACCESS_UNORDERED_ACCESS`，Vulkan 为 `ACCELERATION_STRUCTURE_READ | WRITE`。现有的访问位都只适合一边：DX12 的 AS 访问
  只能用在创建时标成加速结构的资源上，Vulkan 的 storage 访问不能配构建阶段。scratch 不再归到 `ShaderStorage*`。
- 串行共用一块 scratch 的构建之间要屏障；同时进行的构建各用互不重叠的 scratch。scratch 的来源由 I8 定。

### D6　构建是一种 item，经 `CommandList::Submit` 提交　已定

- `AccelerationStructureBuildItem`，与 `CopyItem` / `DispatchItem` 同级：`CommandList::Submit` 一次提交一个构建。渲染图的 scope
  记录 item，执行时提交。
- 内容：类型（底层 / 顶层）、构建标志（偏重追踪 / 偏重构建、允许更新、允许压缩）、模式（构建 / 更新）、目标对象、更新时的源对象、
  scratch（buffer + 偏移）、输入（底层：几何描述的 span；顶层：实例 buffer + 偏移 + 个数）。
- 几何描述 `RayTracingGeometry`：三角形；顶点 buffer + 偏移 + 步长 + 格式 + 个数，索引 buffer + 偏移 + 格式 + 个数，是否不透明。
  输入只收 buffer 与偏移，不限来源。
- 所需大小：`Device::GetAccelerationStructureBuildSizes(type, flags, 几何描述或实例个数)` → 结果大小、构建 scratch、更新 scratch。
  每个几何的图元数作为 Vulkan 要的最大图元数，DX12 忽略。查到的大小对几何数、类型、标志、顶点格式、索引类型相同，
  图元数不超过查询值的构建与更新都适用。

### D7　inline ray query 是最低要求　已定

- 不加能力位，上层不分支。DX12 `Device` 初始化时检查 Raytracing Tier ≥ 1.1，与 Resource Heap Tier 等并列，不满足就失败；
  Vulkan 要求 `rayQuery`、`accelerationStructure`、`bufferDeviceAddress`。
- `m_rayTracing` 留给光追管线（路线图决策 3 推后的部分），本计划不读它。

### D8　压缩与原地更新推后，接口先留位　已定

- 构建标志里留"允许更新""允许压缩"，模式里留"更新"，步骤 3 不实现、用到就断言。
- 压缩后大小的取出、拷贝与回读在步骤 4 设计。

---

## 四、步骤

1. **最低要求、阶段**（小）：D7 的初始化检查（DX12；Vulkan 后端还没有创建设备，到时加）；D5 的
   `AccelerationStructureBuild` 阶段与 DX12 的转换。
2. **对象与绑定类型**（中）：`RHI::AccelerationStructure`（D1）与 DX12 实现；`ShaderInputAccelerationStructure`、反射把
   `D3D_SIT_RTACCELERATIONSTRUCTURE` 映射到它、编译器的写法（D3）；删掉 buffer 路径的三处特例和 `ShaderInputBufferType::AccelerationStructure`。
3. **构建**（中）：`RayTracingGeometry`、`RayTracingInstance`（D4）、大小查询、`AccelerationStructureBuildItem` 与 DX12 的提交（D6）、
   存放用的 buffer（D2）；D5 的访问：`RayTracingBuildInput` 与 `AccelStructBuildInputRead`、`AccelStructScratch`、
   `ResourceState` 的校验。交换链图像加 `CopyWrite`（DXGI 没有对应的用途，flip 模型的 back buffer 不声明也能作拷贝目标；
   Vulkan 后端建交换链时要求表面支持 `TRANSFER_DST`，不支持就失败，同 D7）。验证用一个 RHI 示例（同 `HelloTriangle`，不经过渲染图）：
   - 一个三角形的 BLAS；TLAS 两个实例引用它，各带不同的平移与实例号。BLAS 与 TLAS 放在同一个存放 buffer 里（验证偏移）。
   - 顶点在 Device 堆，从 upload 拷入（upload 堆在 DX12 上不发屏障，测不到输入的屏障）；实例 buffer 由 CPU 写。
   - 两次构建串行共用一块 scratch，中间有 scratch 与存放 buffer 的屏障。
   - 一个 compute shader 每像素用 `RayQuery` 打一条正交光线：命中按重心坐标着色（三个角各一种纯色），按实例号区分两个
     实例的色调；没命中写背景。结果写进一张图，拷进 back buffer。
4. **压缩与更新**（推后，D8）。

每一步之后：全量编译，`SparkRenderTest` 通过；步骤 3 在 debug layer 下跑示例，无报错。

---

## 五、验证

- 示例画面：两个三角形各在其平移处（实例变换的布局）；各自是自己实例号的色调（实例号与 mask 的打包）；三个角的颜色
  与顶点顺序一致（顶点的偏移、步长、格式）；两个都在（实例个数、BLAS 地址）。
- debug layer 无报错；GPU 验证层下构建与追踪之间的屏障无报警。
- 设备不支持 Tier 1.1 时 `Device` 初始化失败并报出原因。

---

## 六、I8 要从这里拿什么（不在本计划内）

- 存放用的 buffer 与其上的区间分配（D2）由光追场景系统持有；BLAS 跟着几何的生命周期建与释放。存放用的 buffer 不能
  reserved，增长的办法（多块 buffer 组成的块链，或另建更大的再拷贝 / 重建）由 I8 定。
- 几何池的 reserved buffer 要带 `RayTracingBuildInput`（D5），在几何池创建时加。构建的参考页
  没有禁止输入在 sparse residency 的 buffer 里（只禁止了存放加速结构的 buffer），但没有找到明说允许的条文，到 I8 再确认。
- 实例 buffer 由 CPU 写还是由 compute 从 `g_Instances` 生成，实例号取 InstanceBinding 的 slot（路线图 I8）。
- 渲染图：TLAS 与存放池的底层 buffer 作为资源；构建 pass；scratch 的来源；TLAS 绑进场景组。

---

## 关联文档

- `TODO_RenderPipelineRoadmap.md`：决策 3、5，§三 I7 / I8，P6。
- `TODO_BufferRangePlan.md`：§四（RHI 只分配、区间分配在上层）、§六（reserved 资源，几何池是 BLAS 的输入）。
- `TODO_EnhancedBarriersPlan.md`：访问位与阶段的转换。
