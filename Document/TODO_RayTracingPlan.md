# I7 RHI 光追　实现方案

路线图 P6 的第一个基础项 I7（`TODO_RenderPipelineRoadmap.md` §三）的落地计划：RHI 有了加速结构，能构建、能在 shader 里用
inline ray query 追踪。I8（光追场景：BLAS 跟着几何、TLAS 每帧更新、进渲染图）与 RT 阴影不在本文，只在 §六 列出它们要从这里拿什么。

本文是草案，2026-10-22 与用户讨论后写下。路线图的决策 3（只用 inline ray query）、决策 5（接口按 Vulkan 语义设计，加速结构是
独立对象与独立绑定类型）是前提。现状一节来自 2026-10-22 读代码；原生语义一节按记忆写，**动手前要对照规范逐条确认**，确认过的标出来。

---

## 状态

| 步骤 | 内容 | 依赖 | 状态 |
|---|---|---|---|
| 1 | 能力位、管线阶段、shader 反射的绑定类型 | — | 未开始 |
| 2 | 加速结构对象与绑定类型，删掉 buffer 路径里的特例 | 1 | 未开始 |
| 3 | 构建：输入描述、所需大小、构建命令、屏障；一个 RHI 示例验证 | 2 | 未开始 |
| 4 | 压缩与原地更新 | 3 | 推后：I8 的静态网格先不压缩 |

决策 D1 ~ D8 都是**待定**，等用户逐条确认。

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
- 相关的已定规则：`TODO_BufferPoolPlan.md` 的 D3、D9：一个池可以在一个原生 buffer 里划分；DX12 的 buffer 屏障只能作用于整个
  原生 buffer，所以同一队列里写完再读的数据要"按底层 buffer 跟踪"（D9 的第二种用法），渲染图对底层 buffer 发屏障。

---

## 二、原生语义（按记忆，待逐条确认）

| | DX12 | Vulkan |
|---|---|---|
| 加速结构是什么 | buffer 里的一段，用 GPU 虚拟地址指称；没有独立对象 | `VkAccelerationStructureKHR`：在（buffer，偏移，大小，类型）上创建的对象；地址由 `vkGetAccelerationStructureDeviceAddressKHR` 取 |
| 存放它的 buffer | 资源要处于加速结构状态，且只能放加速结构（Enhanced Barriers 下是创建时的资源标志，待确认） | 用途 `ACCELERATION_STRUCTURE_STORAGE` + `SHADER_DEVICE_ADDRESS`；一个 buffer 可放多个 |
| 对齐 | 256 字节 | 偏移是 256 的倍数 |
| 构建输入 | 顶点 / 索引 / 实例 buffer 的 GPU 地址 | 设备地址；buffer 要带 `ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY` + `SHADER_DEVICE_ADDRESS` |
| 所需大小 | `GetRaytracingAccelerationStructurePrebuildInfo`（输入描述） | `vkGetAccelerationStructureBuildSizesKHR`（输入描述 + 每个几何的最大图元数） |
| scratch | UAV buffer，地址 256 对齐 | storage buffer + 设备地址，`minAccelerationStructureScratchOffsetAlignment` 对齐 |
| 构建 | `BuildRaytracingAccelerationStructure`（目标、输入、可选的源、scratch） | `vkCmdBuildAccelerationStructuresKHR`（可一次多个） |
| 实例 | `D3D12_RAYTRACING_INSTANCE_DESC`，64 字节 | `VkAccelerationStructureInstanceKHR`，64 字节，与 DX12 逐位相同 |
| 绑定 | SRV，维度 `RAYTRACING_ACCELERATION_STRUCTURE`，资源为空、填地址 | 描述符类型 `ACCELERATION_STRUCTURE_KHR`，经 `VkWriteDescriptorSetAccelerationStructureKHR` 写对象 |
| 同步 | 构建的 sync 阶段 + AS 读 / 写访问 | `ACCELERATION_STRUCTURE_BUILD` 阶段 + `ACCELERATION_STRUCTURE_READ / WRITE` 访问 |
| 压缩后的大小 | `EmitRaytracingAccelerationStructurePostbuildInfo` 写进 buffer | 写进查询池（可再拷进 buffer） |
| inline ray query | Tier 1.1 | `rayQuery` 特性 + `accelerationStructure` + `bufferDeviceAddress` |

更受限的一边：对象一侧是 Vulkan（要有对象、写描述符要对象），大小查询是 Vulkan（要最大图元数），压缩的回读是 Vulkan（查询池），
buffer 屏障的粒度是 DX12（只能整个原生 buffer）。

---

## 三、决策记录

### D1　加速结构是建在一段 buffer 上的对象　待定

**问题。** 现在它就是一块 buffer；Vulkan 要求一个对象，描述符写的也是对象。要定对象和存放它的内存是什么关系。

| 方案 | 做法 | 代价 |
|---|---|---|
| A. 对象建在（buffer，偏移，大小）上 | `RHI::AccelerationStructure` 由 Factory 创建，`Init(device, {type, buffer, offset, size})`；DX12 记下地址，Vulkan 创建对象。存放它的 buffer 来自普通的 `BufferPool`，可以在一个原生 buffer 里划分 | 上层要先分 buffer 再建对象，两步 |
| B. 对象自带内存 | 对象像 `Buffer` 一样是一种资源，自己分配 | 每个 BLAS 一次分配；小 BLAS 多时浪费放置对齐（64 KB）；状态要在对象上再记一份 |

倾向 A：它就是两边的原生形态（Vulkan 原文就是 buffer + 偏移 + 大小；DX12 是一个地址），像 `BufferView` 之于 `Buffer`，
不另起一种资源。内存与状态都留在 buffer 上，`TODO_BufferPoolPlan.md` 的池、延迟回收、屏障规则原样可用；
很多 BLAS 落在同一个原生 buffer 里是池的事。对象只是"这一段被解释为加速结构"。

### D2　存放加速结构的 buffer 只放加速结构，按底层 buffer 跟踪　待定

- DX12 的这种资源只能放加速结构，所以存放用的池单独一个，绑定标志只有 `RayTracingAccelerationStructure`，对齐 256。
- 构建是同一队列里写、之后的追踪再读，属于 `TODO_BufferPoolPlan.md` D9 的第二种用法：渲染图里的资源是池的底层 buffer，
  构建 pass 声明写、追踪 pass 声明读，屏障按普通 buffer 生成。各个加速结构只是分配与建对象用的句柄。
- 一次构建很多 BLAS 时，对整个底层 buffer 一条屏障正好就是需要的。

### D3　绑定类型独立　待定

- `ShaderInputType` 加 `AccelerationStructure`，有自己的输入类 `ShaderInputAccelerationStructure`（存 `const AccelerationStructure*`），
  删掉 `ShaderInputBufferType::AccelerationStructure`。
- 反射把 `D3D_SIT_RTACCELERATIONSTRUCTURE` 映射到它（以后 SPIR-V 反射同样）。
- DX12 后端在对象 Init 时建好 SRV（资源为空、填地址）；Vulkan 在写描述符时用对象。
- buffer 视图路径里为 AS 开的特例全部删掉（`BufferView`、`ConvertBufferView`、`DescriptorContext` 三处）。
- 先不进 bindless：TLAS 一帧一个，按名字绑进场景组就够。有了多个 TLAS 的需要再看（DX12 的 `ResourceDescriptorHeap` 能否取
  加速结构、Vulkan 的描述符数组怎么放，到时查）。

### D4　实例是一个 RHI 结构，与两边的原生布局逐位相同　待定

- `RHI::RayTracingInstance`：3×4 变换、24 位实例号 + 8 位 mask、24 位命中组偏移 + 8 位标志、64 位加速结构地址；`static_assert`
  大小 64，两个后端各自 `static_assert` 与原生结构的偏移一致。
- 这样实例可以由 CPU 写进上传 buffer，也可以由 compute 从 `g_Instances` 生成（I8 选），后端不转换。
- 地址由 `AccelerationStructure::GetDeviceAddress()` 给出。

### D5　同步：加阶段，scratch 的访问要查证　待定

- `AttachmentStage` 加 `AccelerationStructureBuild`（DX12 `SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE`，Vulkan
  `ACCELERATION_STRUCTURE_BUILD`），拷贝 / 压缩用的阶段到步骤 4 再加。
- 构建对目标写 `AccelStructWrite`，追踪读 `AccelStructRead`（compute 阶段），已有的访问位够用。
- **待查证**：构建输入（顶点 / 索引 / 实例）在构建阶段算哪种读访问；scratch 在两边是哪种访问（Vulkan 按记忆是 AS 读写，
  DX12 按记忆是 UAV）。现在 `ResourceState` 把 scratch 归到 `ShaderStorage*`，若两边不一致，scratch 要有自己的访问位。
- scratch 是一帧内的临时 buffer，可以从渲染图的临时资源分（I8 定）。

### D6　构建是一种 item，经 `CommandList::Submit` 提交　待定

- `AccelerationStructureBuildItem`：类型（底层 / 顶层）、构建标志（偏重追踪 / 偏重构建、允许更新、允许压缩）、模式（构建 / 更新）、
  目标对象、更新时的源对象、scratch（buffer + 偏移）、输入（底层：几何描述的 span；顶层：实例 buffer + 偏移 + 个数）。
- 几何描述 `RayTracingGeometry`：三角形；顶点 buffer + 偏移 + 步长 + 格式 + 个数，索引 buffer + 偏移 + 格式 + 个数，是否不透明。
  输入只收 buffer 句柄与偏移，不假设来自静态网格（路线图 I8 的动态 BLAS 接缝）。
- 所需大小：`Device::GetAccelerationStructureBuildSizes(type, flags, 几何描述或实例个数)` → 结果大小、构建 scratch、更新 scratch。
  按 Vulkan 的要求带上每个几何的最大图元数（就是描述里的个数），DX12 不用的忽略。
- 和 `DrawItem` / `DispatchItem` / `CopyItem` 同一种写法：渲染图的 scope 记录 item，执行时提交。

### D7　能力位分开　待定

- 新增 `DeviceFeatures::m_rayQuery`：DX12 为 Tier ≥ 1.1；Vulkan 为 `rayQuery`、`accelerationStructure`、`bufferDeviceAddress` 三个特性都在。
- `m_rayTracing` 留给光追管线（路线图决策 3 推后的部分），本计划不读它。
- 不支持时上层整条光追路径不建（I8 / P6 的回退）。

### D8　压缩与原地更新推后，接口先留位　待定

- 构建标志里留"允许更新""允许压缩"，模式里留"更新"，步骤 3 不实现、用到就断言。
- 压缩要：构建时把压缩后的大小写出（DX12 写 buffer，Vulkan 写查询池再拷进 buffer，抽象取"写进一块回读 buffer"）、下一帧读回、
  分一段更小的、拷贝、旧的延迟释放。路线图写的"延迟一帧回读"就是这条。等 I8 的显存占用值得优化时做。

---

## 四、步骤

1. **能力位、阶段、反射**（小）：D7 的 `m_rayQuery`；D5 的 `AccelerationStructureBuild` 阶段与两个后端的转换；
   反射认 `D3D_SIT_RTACCELERATIONSTRUCTURE`（类型落到步骤 2 的新类型之前，先报错而不是静默当 buffer）。
2. **对象与绑定类型**（中）：`RHI::AccelerationStructure`（D1）与 DX12 实现；`ShaderInputAccelerationStructure` 与编译器的写法（D3）；
   删掉 buffer 路径的三处特例和 `ShaderInputBufferType::AccelerationStructure`。
3. **构建**（中）：`RayTracingGeometry`、`RayTracingInstance`（D4）、大小查询、`AccelerationStructureBuildItem` 与 DX12 的提交（D6）、
   存放用的池（D2）。验证用一个 RHI 示例（同 `HelloTriangle`，不经过渲染图）：一个三角形的 BLAS、一个实例的 TLAS、
   一个 compute shader 每像素用 `RayQuery` 打一条光线，命中写颜色、没命中写背景，输出到屏幕。
4. **压缩与更新**（推后，D8）。

每一步之后：全量编译，`SparkRenderTest` 通过；步骤 3 在 debug layer 下跑示例，无报错。

---

## 五、验证

- 示例画面：三角形在正确的位置，边缘与光栅化的同一个三角形重合。
- debug layer 无报错；GPU 验证层下构建与追踪之间的屏障无报警。
- 设备不支持 Tier 1.1 时 `m_rayQuery` 为假，示例报告不支持并退出。

---

## 六、I8 要从这里拿什么（不在本计划内）

- 存放用的池（D2）由光追场景系统持有；BLAS 跟着几何的生命周期建与释放。
- 几何池的原生 buffer 要带构建输入的用途（Vulkan 的两个位；DX12 只需地址），在几何池创建时加，属于 `TODO_GPUDrivenPlan.md` 的几何池。
- 实例 buffer 由 CPU 写还是由 compute 从 `g_Instances` 生成，实例号取 InstanceBinding 的 slot（路线图 I8）。
- 渲染图：TLAS 与存放池的底层 buffer 作为资源；构建 pass；scratch 的来源；TLAS 绑进场景组。

---

## 关联文档

- `TODO_RenderPipelineRoadmap.md`：决策 3、5，§三 I7 / I8，P6。
- `TODO_BufferPoolPlan.md`：D3、D9（在一个原生 buffer 里划分、按底层 buffer 跟踪）。
- `TODO_GPUDrivenPlan.md`：几何池（BLAS 的输入）。
- `TODO_EnhancedBarriersPlan.md`：访问位与阶段的转换。
