# 缓冲区间与 CPU→GPU 数据同步　设计思路

2026-10-15 讨论定下的方向；2026-10-23 补充池与分配器的分工、reserved 资源的增长方式、到 Image 的推广，以及统一的
区间对象和 CPU→GPU 的几种写入方式。只记大体思路，细节后面再设计。

它推翻了 `TODO_BufferPoolPlan.md` 的做法（池在一个原生 buffer 里分出 `RHI::Buffer`），也改动
`TODO_GPUDrivenPlan.md` D4 的落地方式。

---

## 一、问题

`BufferPool` 的预算模式把大缓冲里的一段当成一个 `RHI::Buffer`。`RHI::Buffer` 隐含的约定是"状态、屏障、
队列所有权的单位"，一段区间给不了：

- DX12 的缓冲屏障只能覆盖整个资源，分段只能靠后端兜底（`Shared` 不发原生屏障，断言没有写）。
- 分段和大缓冲各有一份 `ResourceState`，指向同一个原生资源。大缓冲那份初始化后没人维护。
- 使用时又必须回到大缓冲（DrawIndirect 绑的是整块），分段的独立性只在上传时成立。
- GPU 写入池（蒙皮输出、虚拟几何的页池）出现时，按分段记状态的做法整个不成立。

## 二、核心结论

**一个 `RHI::Buffer` 就是一个原生资源，是状态的唯一单位。区间不是 Buffer，只是"Buffer + 偏移"。**

两条规则：

1. **状态以原生资源为准。** 区间没有状态。图里对池的访问都是对整块 Buffer 的访问，按整资源走屏障。
2. **按访问模式拆池。** 状态按整块算，同一个池里读写不同区间也会被图串起来，所以只读的静态几何和 GPU 每帧写的
   数据分开放。

## 三、写入：上传系统和图各管一块，不交叉

讨论中试过"往池区间的上传不进状态模型"，结果上传系统里要分交接式 / 原位式、放开"只上传一次"、放开已导入
资源、按请求记就绪，处处特判。根源是**上传系统和渲染图同时在管同一个资源的状态**。只要一个资源先上传、
再进图，两边各管一段就不冲突；池同时在图里使用、又在被上传，就冲突了。所以划清地盘：

- **`AsyncUploadSystem` 只负责新资源。** 现有规则全部保留，它们就是这个系统的定义：只上传进新资源、只上传
  一次、已导入的资源不收、上传完交接（release / acquire）给图。之后资源归图，上传系统不再碰。
- **图在用的资源（池）只由图里的 Pass 写。**
  - CPU 来源：CPU 写进主机可见、按帧分槽的暂存，图里一个 Pass 写进池的区间（Copy 或 scatter，见 §五）。
    Nanite 的页也属于这类，只是 Pass 是转码（磁盘编码 → GPU 格式）+ fixup。
  - GPU 来源：蒙皮、粒子这类每帧由 GPU 算出的数据，本来就是 Pass。
  - 池的状态只有图写，两种来源是同一种"对整块 Buffer 声明写"。

UE 的 GPUScene（scatter upload）和 Nanite 页池（上传 + 转码）都是这个做法。

带来的简化：

- 上传系统不用改，也不需要池与拷贝队列共享。
- **就绪是平凡的**：几何系统在第 N 帧声明了拷贝，图保证它排在同帧的剔除和绘制之前，网格从第 N 帧起可用，
  不需要计数。
- 池的写入只有一种机制。

代价：几何数据不走异步拷贝队列，图里多一次 GPU 拷贝；暂存缓冲有限，要有每帧上传预算，大网格分几帧传完。

拷贝留在图形队列。池每帧都在被读，状态按整块算，放到拷贝队列上也得等它写完才能读，换不来并行，只多一次跨
队列交接和 Fence 等待。池能被哪些队列访问（如蒙皮池由异步计算写、图形读）由持有它的系统按用途定，跨队列交接
是图已有的能力。

## 四、分层分工

| 层 | 负责 |
|---|---|
| RHI | `BufferPool` / `ImagePool` 退回纯粹的资源分配：D3D12MA / VMA 的包装，按描述符分配资源，不再提供管理功能（删预算模式：大缓冲、VirtualBlock、`BufferMemoryType::Shared`、`GetBaseOffset`）。D3D12MA / VMA 的 Allocator 每个 Device 一个，每个池一一对应一个自定义池（`D3D12MA::Pool` / `VmaPool`），描述符对应过去：堆级别 + 主机访问 → 堆类型 / 内存类型，块大小，最小分配对齐（通用参数，不是 reserved 专用）；原来设在 Allocator 上的标志（如 `ALWAYS_COMMITTED`）下放到池或单次分配。代价：自定义池各开堆块，空闲内存不跨池共享，块不宜过大、空块及时释放。最低要求 Resource Heap Tier 2（Turing / GCN 起），池不按 Buffer / 纹理 / RT-DS 拆，`TransientResourcePool` 的 Tier 1 分支删掉。延迟释放仍留在各池：D3D12MA 要求池的分配先于池释放，而共用的 `ObjectCollector` 强制回收不保序，没法只先放某个池的分配；Device 只持有 Allocator，所有池都在它之前关闭。新增后端无关的 `QueueForRelease(Ptr<Object>)`：RHI 多持有这个对象直到已提交的 GPU 工作完成再放掉，要延迟的工作写在对象的析构里；延迟帧数只在 RHI 内部。不收函数：函数能捕获任何东西，不可控。新增 reserved 资源与页映射（见 §六） |
| Core | 纯 CPU 的区间分配算法：变长区间（自写空闲链表 / TLSF，或引入 OffsetAllocator）和定长槽，不依赖 D3D12MA |
| 系统 / 渲染层 | 统一的区间对象（§五）。需要管理的系统自己持有池，合适时多个系统共用一个；页式管理由需要的系统自己扩展。几何池 = 一个 reserved Buffer + 区间分配器，`MeshGeometry` 持有区间而不是分段 Buffer。按帧分槽的暂存 + Copy / scatter Pass 也在这一层 |

先例：阴影图集（Core 的 `QuadTreeAllocator` + 渲染层 `ShadowAtlasAllocator` 持有图集并发 RAII tile）。

**`RHIResourceSystem` 长期去掉。** 资源的创建和 Map 由各自的系统负责：选池、Map 拷贝随之迁到使用方，按帧轮转
多份收在 `StagedArrayBuffer` 一类的工具里，不让每个系统各写一遍。`AsyncUploadSystem` 不再接收经它创建的资源，
"只收新资源"改为看资源自身的状态判断。

## 五、区间对象与 CPU→GPU 的写入方式

几何池之外，`GlobalBuffer` / `StagedArrayBuffer` 管的上传缓冲区是第二个用例，"Buffer 引用一个区间"抽成一个
对象，所有 CPU→GPU 的数据同步逐步统一到它上面。

**区间对象**：`Ptr<RHI::Buffer>` + 偏移 + 大小 + 分配器的引用。

- 只能移动的唯一所有权（同 `UniqueRHIHandle`）；要共享时经持有它的实体共享（实例引用网格实体），不复制区间。
- 区间对象持有一个引用计数的分配记录（`Object`，析构时把区间还给分配器，并持有分配器的引用），析构时把它交给
  `QueueForRelease`。先例是现在的 `VirtualBlockAllocation`。主机池和设备池一律延迟，不按池类型区分：主机池必须
  延迟（CPU 会直接写复用的区间），设备池延迟只是晚几帧复用。分配器活到最后一条记录放掉。
- 延迟归还的副作用：一个区间不会在同一帧里既被释放又被重新分配。
- 整段被读的数组（GPU 剔除遍历 `g_Instances`）在释放当帧就要读成"空"：析构时立即把区间记进分配器的待清列表
  （只写分配器自己的内存），系统下次更新时把 `Element{}` 写进去；归还照样延迟。

**写入方式**：区间对象是同一个，区别只在分配自哪个池、怎么写进去。

| | 主机池，每帧一份（Map 直接写） | 设备池，Copy | 设备池，scatter |
|---|---|---|---|
| 写入 | 每帧整份重写进本帧那份 | 暂存 + Copy Pass | 暂存 + scatter Pass |
| 每帧上传量 | 整个数组 | 新数据 | 只有变化的条目 |
| 内存 | `frameCountMax` 份 | 1 份 | 1 份 |
| GPU 读取 | 走 PCIe | 显存 | 显存 |
| 适合 | 小数组，或每帧几乎全变的数据：`g_Views`、`g_Lights`、每帧常量、CPU 剔除的间接参数 | 写一次读很多帧的连续大块：几何 | 大数组、每帧只变一小部分：`g_Instances`、`g_Materials` |

主机池只放一份、只改变化的几条是不成立的：CPU 领先 GPU 若干帧，Map 改写时 GPU 可能还在读前一帧的值，同一帧的
不同 Pass 会读到不同版本；Map / Unmap 不提供同步。每帧一份可以避开，但每份上次被写已是 `frameCountMax` 帧前，
只能整份重写。设备池的 Copy / scatter 把 CPU 的写入排进 GPU 队列：CPU 只写本帧专属的暂存槽，改写那一份数据的
是图里的 Pass，排在前一帧所有读取之后、本帧所有读取之前。

**scatter Pass**：

- CPU 往本帧暂存槽写两段：目标槽下标（`uint`）和对应的新记录（紧密排列）。释放的槽也是一条更新，数据为
  `Element{}`。变化检测按帧记，同一槽一帧只出现一次。
- 一个通用的 compute，按 16 字节一块拷，不认 `Element` 类型，所有使用方共用一个管线：线程 `id` 处理第
  `id / ElementSize16` 条更新的第 `id % ElementSize16` 块，`Dest[slot * ElementSize16 + chunk] = Data[...]`。
  相邻线程写同一条记录的相邻块，写入连续。UE 的 `ScatterCopyCS` 同形。
- 图里声明读暂存槽（主机堆，后端不发屏障）、写整个池（ShaderWrite）；之后的剔除和绘制读池。没有更新的帧不
  dispatch。池的 Buffer 带 `ShaderWrite`，以 raw view 绑定。
- 要求 `sizeof(Element)` 是 16 的倍数；做不到的按 4 字节一块，线程数 ×4。
- 池是 reserved 资源，扩容只映射新页、已有数据不动，所以稳定下标和不定容量能同时成立。

**迁移**：

- `StagedArrayBuffer` 即"主机池每帧一份"，小数组保持这种写法，改用区间对象。
- `GlobalBuffer` 的大数组（`g_Instances`、`g_Materials`）迁到设备池 + scatter。要新加变化检测：现在的
  `process` 每帧重写所有实体；空洞也不再每帧整体重置，改为上面的待清列表。
- `SlotPool` / `SharedHandle` 由区间对象 + Core 的定长槽分配取代；`WeakHandle` 只有将删的
  `MeshGeometryComposer` / `GeometrySpec` 在用。

## 六、池的增长：reserved 资源 + 同类后备

- 池的大块资源是 reserved 资源：一次保留足够的虚拟地址空间，按页（64KB）映射物理内存。扩页时从池里取后备
  资源映射上去，缩页时解除映射。
- **后备与被映射的资源同类**：Buffer 由 Buffer 后备，Image 由 Image 后备。RHI 不暴露裸内存对象，后端从后备的
  分配里取底层内存（DX12 的堆 + 偏移，Vulkan 的 `VkDeviceMemory` + 偏移）。池只分配资源，页的管理在持有它的
  系统里。
- **reserved Buffer 由 `BufferDescriptor` 标记**，仍走池的 `InitBuffer`，池按标记直接调设备 API 创建
  （`CreateReservedResource` / 带 sparse 标志的 `vkCreateBuffer`，D3D12MA / VMA 都不创建 reserved 资源），注册在
  池上，释放走原有的延迟释放。标记存在 Buffer 的描述符上，映射时据此校验。约束：大小是页的整数倍、不带初始数据、
  只在设备堆、瞬态资源不能是 reserved。
- **后备是池里普通分配的 placed Buffer**，不带 bind flags，没有专门的入口。池不知道自己是不是后备，约束由上层和
  后端各自保证：
  - 对齐：上层把池描述符的最小分配对齐设为页大小（`DeviceLimits` 给出）。
  - Vulkan 内存类型：后端给 `VmaPool` 选内存类型时，按"同 usage 加 sparse 标志"的 `memoryTypeBits` 选（规范保证它
    是不带 sparse 的子集，对普通 Buffer 也合法），所以任何池都能当后备；设备不支持 sparse 时按普通选。
  - 映射时后端校验后备是 placed、偏移按页对齐、Vulkan 内存类型在 reserved 资源的 `memoryTypeBits` 内，不满足报错。
  - 后备按大块分配，映射引用"后备 + 页偏移"。
- 后备与 reserved 资源共享内存，不能当普通资源用：它不带 bind flags，校验据此拒绝给它建 View、发屏障、导入图。
  Vulkan 要求 `usage` 非零，由后端自己补，RHI 层看不到。
- **映射是队列级操作**（DX12 `ID3D12CommandQueue::UpdateTileMappings`，Vulkan `vkQueueBindSparse`），不能录进
  CommandList，也不由图里的 Pass 做。RHI 只提供 `CommandQueue::UpdateTileMappings(span<const BufferTileMapping>)`
  （后备为空即解除映射），映射什么、何时映射、何时解除都由持有池的系统决定。
- 语义按 DX12 定：在队列 Q 上的映射排在 Q 上之前所有提交之后、之后所有提交之前。Vulkan 的 bind sparse 与 submit
  即使同队列也没有隐式顺序，后端内部用 timeline semaphore 补上（bind 等 Q 的上一次提交，下一次提交等 bind），
  接口不带同步参数。Vulkan 允许的"映射藏在后台、只在首次使用时等"现在不用；纹理流送需要时加一个返回 Fence 值的
  异步变体，是加法。
- 上层的责任：系统在自己的更新里（早于 `ExecutePipeline`）映射，本帧即可用；页上区间都归还后即可解除映射，同
  队列不需额外等待，别的队列用过该页时用现有 `Signal` / `Wait` 等它；先解除映射再放掉后备 Buffer，后端在后备
  释放时检查它是否还有页映射着。
- 图导入整个 reserved 资源，按 §二 整资源同步；映射不进图。
- 特性要求（Tiled Resources Tier，与 Resource Heap Tier 是两套分级）：DX12 Tiled Resources Tier 1 下读未映射页
  未定义，Tier 2 起读 0；Vulkan 需要 `sparseBinding` +
  `sparseResidencyBuffer` / `sparseResidencyImage2D`，移动端多数和 MoltenVK 不支持。

## 七、推广到 Image

- Image 的状态本来按子资源记（`ImageSubresourceStates`），图按子资源区间合并屏障，写 mip k 的同时采样其他 mip
  不会整块串行。
- **纹理流送是第一个用户**：mip 驻留 = 映射对应的 tile。需要：
  - tile 形状和 mip tail 由后端查询（DX12 已有 `ImageTileLayout`，Vulkan `vkGetImageSparseMemoryRequirements`），
    mip tail 整体映射；
  - 格式逐个查询 sparse 支持；
  - 未驻留的 mip 靠最小 LOD 限制（DX12 SRV 的 `ResourceMinLODClamp`，Vulkan `VK_EXT_image_view_min_lod`）。改它
    就是改描述符，bindless 下描述符更新随帧延迟。
- **虚拟纹理通常不走硬件 sparse**：物理页图集纹理 + 间接表，多张纹理共享一个物理页缓存，属于"普通大 Image +
  Core 的定长槽分配器"，与阴影图集同类。
- reserved 的 RT / DS 新映射的页内容未定义，首次使用前对该区域 Discard / Clear；只当拷贝目标、整页覆盖写的流送
  纹理不受影响。

## 八、对上层的影响

- **绘制**：区间偏移通过绘制参数（`vertexOffset` / `firstIndex`）传入。
- **DrawIndirect**：不再写死 `MeshGeometryBaseBufferTag`，改为和零散 Draw 一样接收顶点流附件，Pass 自己
  `p.Import<Tag>` + `s.InputAssembly`。`Uses<MeshGeometrySegmentTag>` 随之删除。
- **`Uses<Tag>`** 只留给"新资源经上传系统交接后、经绑定 / bindless 读取"的资源（材质贴图、IBL、查找表、图标）。

## 九、待设计

1. ~~暂存与拷贝 Pass 的形态~~（已定）：暂存由上传的系统自己持有，不走 `AsyncUploadSystem`。按帧分槽
   （`frameCountMax` 个），槽大小就是每帧预算，超出的留在系统自己的队列里下一帧接着传；复用安全由帧节奏保证。
   不做环形缓冲：每帧预算是前提，单帧多占违背它。写入方式见 §五。
2. 区间分配算法的选型。

先不做（都是加法，后面加上不破坏现在的设计）：

- 不支持 sparse 的平台：池换一种增长方式（固定大块，或扩容时新建更大的资源 + 图里拷贝），区间分配器、句柄、
  图的用法不变。
- 区间碎片整理：一页里只要还有区间在用就还不回去；整理是图里 GPU 搬数据 + 更新 GPU 侧记的偏移。

## 十、实施步骤：RHI

R1 ~ R4 只加不删，每步做完引擎照常渲染；预算模式等几何池在渲染层迁移完才删（R5）。Vulkan 后端现在只有
Instance / Device 骨架，各步的 Vulkan 部分只约束接口，不写代码。

**R1　Allocator 归 Device，池对应自定义池，最低 Resource Heap Tier 2**（纯后端重构，上层不改）

- DX12 `Device` 持有唯一的 `D3D12MA::Allocator` 和 `D3D12MAReleaseQueue`，帧末回收；`Device::ShutdownInternal`
  先清空释放队列、销毁 Allocator，再 `ReportLiveDeviceObjects`。`RHISystem` 现有的关闭顺序（实体 → Factory
  里的池 → Device）已保证 Device 活得比池久。Resource Heap Tier 低于 2 时 Device 初始化失败。
- `BufferPool` / `ImagePool` 各自建一个 `D3D12MA::Pool`，描述符对应过去：堆级别 + 主机访问 → 堆类型，
  `m_largestPooledAllocationSizeInBytes` → 块大小，新增的最小分配对齐 → `MinAllocationAlignment`。删掉各池自己的
  Allocator 和释放队列。`ImagePool` 用 `POOL_FLAG_ALWAYS_COMMITTED`，行为不变。
- `TransientResourcePool`、`ConstantBufferContext` 改用 Device 的 Allocator；删掉 `TransientResourcePool` 的
  Tier 1 分支。
- 预算模式的基础 Buffer 改在池的 `D3D12MA::Pool` 里分配（committed），到 R5 前照常可用。
- Vulkan 对应：Device 持有唯一的 `VmaAllocator`，池对应 `VmaPool`，内存类型按 §六 的规则选。
- 验证：编译，引擎渲染不变，退出时 live objects 干净。

**R2　`QueueForRelease(Ptr<Object>)`**

- 加在 `RHI::Device` 上，实现在 RHI 基类：Core 的 `ObjectCollector`，延迟 `frameCountMax` 帧，帧末回收；Device
  关闭时全部回收，之后才销毁 Allocator。
- 现有按类型分开的释放队列先不动。
- 暂无使用方，编译通过即可；渲染层的区间对象是第一个用户。

**R3　reserved Buffer**

- `BufferDescriptor` 加 reserved 标记；池的 `InitBuffer` 见标记就调设备 API 创建（DX12 `CreateReservedResource`），
  不经 D3D12MA，注册在池上，释放走 `QueueForRelease(ID3D12Object)`。
- 校验：大小是页的整数倍、不带初始数据、只在设备堆、设备支持 tiled resources；瞬态资源不能是 reserved。
- `DeviceLimits` 加页大小，`DeviceFeatures` 记 Tiled Resources Tier。
- 后备不需要新代码：上层建一个最小分配对齐为页大小的池，`InitBuffer` 时传 `BindFlags::None`。

**R4　`CommandQueue::UpdateTileMappings`**

- RHI 基类加接口和校验：目标是 reserved Buffer，后备不是 reserved、是 placed、偏移按页对齐，页范围不越界。
- DX12：后备的堆和起始页从它的 `D3D12MA::Allocation`（`GetHeap()` / `GetOffset()`）取，调
  `ID3D12CommandQueue::UpdateTileMappings`，解除映射用 NULL range flag。后端记录每个后备映射着的页数，后备释放
  时还有页映射着就报错。
- Vulkan 对应：`vkQueueBindSparse`，用 timeline semaphore 排进队列的时间线（§六）。
- R3、R4 的验证：`SandBox/Program/` 加一个最小样例——建 reserved Buffer、映射两页、拷贝写入、compute 读回检查，
  再解除映射、放掉后备。

**之后：渲染层**——区间对象、按帧分槽的暂存与 Copy Pass、几何池迁到 reserved Buffer、系统 Pass 阶段，
`RHIResourceSystem` 的拆除随各系统迁移一起做。到时另列步骤。

**R5　删预算模式**（依赖几何池迁移完）

- 删 `ResourcePoolDescriptor::m_budgetInBytes`、`GetBaseBuffer`、`InitBaseBuffer`、`InitSubAllocatedBuffer`、
  `VirtualBlockAllocation` 与其释放队列、`BufferMemoryType::Shared`、`CommandList::QueueBarrier` 里 `Shared` 的分支、
  `RHI::BufferPool::Init` 里注册基础 Buffer 的那段。

**Image 的 reserved 资源**随纹理流送再做。

## 十一、前瞻

- **Nanite 类虚拟几何**：页池 = 一块被 GPU 写、被多个 Pass 读的大缓冲，按整资源同步；页按槽分配（Core 的定长
  槽分配器，流式系统持有）；淘汰的页经 `QueueForRelease` 延迟复用；页数据经暂存 + 转码 Pass 写入；页池以 SRV
  读，走 `ReadBuffer` 附件。与本方案一致。
- **API 趋势**：同步变粗（DX12 缓冲屏障整资源 + global barrier，Vulkan 推荐无所有权转移时用 global memory
  barrier）、内存地址化（buffer device address、bindless）。"大缓冲 + 偏移"顺应这个方向。
- **若将来需要区间级并行**：扩展点是让图里的缓冲访问带可选区间、按区间判冲突，是加字段，不需要把区间重新做成
  Buffer。

## 十二、相关但单独实施

零散 Draw（已定，先做）：`RenderScope::Draw(arguments, span<const Attachment> vertices[, IndexStream])`，
缓冲经 `p.Import` + `s.InputAssembly` 成为附件，步长取自 Pass 的输入布局；同时删除 `MeshGeometryComposer`、
`DrawItemRouter`、`GeometrySpec`、`Accepts`。场景内容走 indirect，Draw 只用于零散用途。

系统 Pass 与管线分开登记（方向已定，做法到时再设计）：

- 问题：池的 Copy / scatter Pass 若写在 `SetUpDefaultPipeline` 里，用户自定义管线时就没了（`InstanceCullingPass` /
  `InstanceBinningPass` 现在也有同样的问题）。这类 Pass 跑不跑由数据的主人决定：系统记着"区间第 N 帧写好"，
  不跑就是假记录；它们只读暂存、只写池，不依赖管线里的任何 Pass，也不能因"没人读"被剔除。
- 单一 PassContext：图从建图到执行处处靠 `PassExecuteContext::Current()` 查 Pass，`Pass` 的 id 只在所属
  context 里有意义，所以不能另开一个 context 放系统 Pass。PassContext 改由 RenderSystem 持有，不再属于
  `Pipeline`，示例不再 Push 自己的 context。
- 系统 Pass 由拥有池的系统在 `Init` 里登记，排在管线的 Pass 之前；图先遍历系统 Pass，再遍历管线的。
- 启动时选一条管线：引擎配置里放管线的 SetUp 函数，默认是 `SetUpDefaultPipeline`，只调用选中的那一个，默认
  管线不会和自定义管线同时存在。画质档位、特性开关用 `ActivePassTag` 激活 / 停用已声明的 Pass。
- 先不做（加法）：运行中热切换管线、多条管线并存（编辑器多视口）。要做时给管线的 Pass 加归属标签和销毁
  路径；现在没有任何销毁 Pass 的路径，挂在 Pass 上的每 Pass SRG、`DrawGroupSystem` 的绘制组、`PassDrawList`
  都要能随之清理。`InstanceCullingPass` / `InstanceBinningPass` 归系统还是归管线也到时再定。
