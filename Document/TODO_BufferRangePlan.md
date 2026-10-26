# 缓冲区间与 CPU→GPU 数据同步　设计思路

2026-10-15 讨论定下的方向；2026-10-23 补充池与分配器的分工、reserved 资源的增长方式、到 Image 的推广，以及统一的
区间对象和 CPU→GPU 的几种写入方式。只记大体思路，细节后面再设计。

它推翻了 `TODO_BufferPoolPlan.md` 的做法（池在一个原生 buffer 里分出 `RHI::Buffer`），也改动
`TODO_GPUDrivenPlan.md` D4 的落地方式。那份计划的实现与文档在 R5 一起删除，见 git 历史。

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
  - CPU 来源：CPU 写进主机可见、按帧分槽的暂存，图里一个 Pass 写进池的区间（Copy 或元素更新，见 §五）。
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
| Core | 纯 CPU 的区间分配算法，只认偏移和大小，不认 Buffer、不认帧（§五） |
| 系统 / 渲染层 | 区间池与区间对象（§五）。需要管理的系统自己持有池，合适时多个系统共用一个。`ReservedBuffer` = reserved Buffer + 后备 + 页映射，按报来的页占用映射 / 解除映射。几何池是一个区间池加 `ReservedBuffer`，`MeshGeometry` 持有区间而不是分段 Buffer。按帧分槽的暂存 + Copy / 元素更新 Pass 也在这一层 |

区间分配不进 RHI：它不是 DX12 / Vulkan 的原生概念（D3D12MA / VMA 的虚拟分配器是库附带的 CPU 工具），R5 删的
预算模式正是 RHI 里的这一套；延迟归还只要 `QueueForRelease`，上层就能做。先例：阴影图集（Core 的
`QuadTreeAllocator` + 渲染层 `ShadowAtlasAllocator` 持有图集并发 RAII tile）。

**`RHIResourceSystem` 长期去掉。** 资源的创建和 Map 由各自的系统负责：选池、Map 拷贝随之迁到使用方，按帧轮转
多份收在 `StagedArrayBuffer` 一类的工具里，不让每个系统各写一遍。`AsyncUploadSystem` 不再接收经它创建的资源，
"只收新资源"改为看资源自身的状态判断。

## 五、区间对象与 CPU→GPU 的写入方式

几何池之外，`GlobalBuffer` / `StagedArrayBuffer` 管的上传缓冲区是第二个用例，"Buffer 引用一个区间"抽成一个
对象，所有 CPU→GPU 的数据同步逐步统一到它上面。

**一个算法、一种池、一种区间，一律按字节。** 不另设定长槽：数组的一个元素就是大小为 `sizeof(Element)` 的区间，
下标 = 偏移 / 元素大小。数组池分配时对齐填 1：地址空间只在元素大小的整数倍处被切开，偏移自然是它的倍数，释放
留下的空洞恰好放下下一个元素；填元素大小的对齐反而让每次请求多出 `元素大小 - 1`，这种空洞就再也放不下。

**Core 的区间分配算法**：自写的两级分箱 TLSF（OffsetAllocator 的做法），不引第三方。

- 节点表按需扩容，没有最大分配数；偏移 64 位；支持任意对齐（非 2 的幂多分 `alignment - 1` 再上取）；给出最高
  使用位置。地址空间的大小建池时定。
- good-fit：请求先看自己那一档的链表头（最近释放的块，覆盖同尺寸的复用），不够再向上取档，取第一个非空档的
  任意块。末尾的大空闲块在最高的档里，所以至少大一档的空洞、同档最近释放的空洞都先于它被用到，分配集中在低
  地址；同档里更早释放的空洞留给更小的请求。一种元素大小的数组：恰好一个元素的空洞由"先看链表头"命中，合并出的
  更大空洞在比末尾低的档里，都先于末尾被用到，不另做"最小下标优先"。最高使用位置在末尾的释放并回大空闲块后会
  回落。
- 向上取档找不到时，再逐个查自己那一档：有空间就不会失败，只在快满时才不是 O(1)。

**区间池**（渲染层，`Suballocator`；区间对象是 `Suballocation`）：引用计数的 `Object`，包一个 Core 分配器，建池时
给定容量（字节）和 Device；用在 `ReservedBuffer`（§六）上时再给它的页大小。

- 分配器管理整个 reserved Buffer 的虚拟地址空间，不知道页。页跟着分配走：区间碰到未映射的页就映射它；页上的
  区间都真正归还后解除映射。物理内存按活着的区间碰到的页算，不按字节算。
- 页占用（给了页大小才开）：每页一个"碰到它的区间数"；分配时加一，真正归还时减一，有无变化的页记进变化
  列表。跨页的区间（包括跨页的数组元素）每页都计，不限制跨页。系统取变化时每页带上此刻是否被碰到，逐页与"是否
  映射着"比较，不一致才映射 / 解除映射；同一期间先空后占，不发任何映射。映射着哪个后备由 `ReservedBuffer` 记。

**区间对象**：池的引用 + 偏移 + 大小 + 分配器归还用的节点。**不带 Buffer**：它是池地址空间里的一段，背后是一个
reserved Buffer 还是每帧一份的多个 Buffer，是池的事；Pass 经 `p.Import` 导入整个池，绘制和元素更新只要偏移。

- 只能移动的唯一所有权（同 `UniqueRHIHandle`）；要共享时经持有它的实体共享（实例引用网格实体），不复制区间。
  满了返回空区间，调用方报错。
- 析构时新建一条释放记录（`Object`，持有池的引用和这段的位置），交给 `QueueForRelease`；记录析构时才把这段
  还给分配器、减页占用。主机池和设备池一律延迟：主机池必须延迟（CPU 会直接写复用的区间），设备池延迟只是晚几帧
  复用。记录在释放时才建，活着的区间不各占一个堆对象。
- 延迟归还的副作用：一个区间不会在同一帧里既被释放又被重新分配。
- 池不承担任何清理语义：释放后的空洞、新映射页里残留的内容都不清，由读的一方保证不读到它们（见下面的实例
  入口）。
- 记录的析构可能在 RHI 关闭的 `FlushReleaseQueue` 里，系统已经关了：池的回调只写池自己的内存，要用 RHI / 上下文
  的事记下来留给系统；池被记录持有，不会悬空。区间对象要在 RHI 关闭前全部析构（`Device::Shutdown` 之后排进来的
  不再回收），区间存在组件上，随实体先于池和 Device 清掉。

**实例入口与活跃列表**：会被整段遍历的只有 `g_Instances`，它是实例的入口；`g_Materials`、`g_Geometries` 等都由
实例记录按下标引用，从不被遍历，空洞里是什么无所谓。

- `g_Instances` 用区间池分持久的槽，下标跨帧稳定，要稳定引用实例的（历史数据、TLAS、拾取）记槽号。不做成
  swap-remove 的密集数组：那样每次删除都要挪数据，所有将来跨帧引用实例的地方都得跟着挪，没有理由放弃持久下标。
- `InstanceIDBuffer`（现在是恒等表）改为活跃列表：`位置 → 槽号`，只放有槽的实例，紧凑，swap-remove 维护。剔除
  遍历 `[0, 个数)`；绘制的 `FirstInstance` 填本帧的位置，VS 的 `InstanceIndex = InstanceIDBuffer[FirstInstance]`
  仍是槽号，shader 不变。空洞和新映射页的残留内容都没有位置指向，读不到，不需清零，也不需"空元素全 0"的约定。
- 位置只在本帧有效、不被任何人记住：列表的更新排在本帧剔除之前，CPU 剔除用应用完本帧删除后的位置生成命令。
  是否绘制仍由 `DrawMask` 定，几何未就绪的实例不会让列表来回增删。
- 逐绘制的通道只能是 `firstInstance`：Vulkan 的间接绘制不能逐条改 push constant；DX12 的 `SV_InstanceID` 不含
  它，所以经实例流的顶点属性取，这也是这张表存在的原因。
- 活跃列表只在末尾增减，放在 `ReservedBuffer` 上，不经区间池：读它的一方按编号算地址（`FirstInstance` 处在
  `位置 × 4`），所以条目和页都是 Buffer 的前缀，而区间池决定区间放在哪、归还又延迟，增减交错时会在前缀里留洞。
  系统按个数算需要的页数，与已映射页数比，多出的页映射、多余的页当帧解除映射：解除映射在图形队列上排在之前
  提交的所有读取之后，后备由池延迟释放。重新映射的页换了新后备，原来的条目没了，已写条数随之截到映射的范围内，
  再增长时重写。
- 将来的实例化绘制（`InstanceCount > 1`）要求一批实例在实例流里连续，而活跃列表的顺序任意、剔除后还有空隙：分箱
  按批写一份本帧压实的可见列表，绑为这类绘制的实例流，同是"位置 → 槽号"，是加法。

**系统的每帧处理**：取页的变化去映射，早于 `ExecutePipeline`。

**多线程**：分配和区间析构可在任意线程，归还在 Device 回收时，处理在系统更新里。

- 每个池一把锁，锁住分配器、页占用和变化列表；临界区是 O(1) 的分配加几页计数，分配不是每帧热点，不做每线程
  子分配器或无锁结构。
- 区间的偏移和大小创建后不变，读不加锁。析构只建记录交 `QueueForRelease`，不碰池的锁；记录析构时才拿池的锁，
  而 `ObjectCollector` 放开自己的锁后才析构对象，两把锁不嵌套。
- 系统**先取自己的写入队列，再取池的页变化**：工作线程总是先分配（页变化在池锁内登记）再交数据，所以取到的
  每项写入，它碰到的页都在这次变化里或早已映射；之后才分配的，下一帧写。

**写入方式**：区间对象是同一个，区别只在分配自哪个池、怎么写进去。

| | 主机池，每帧一份（Map 直接写） | 设备池，Copy | 设备池，元素更新 |
|---|---|---|---|
| 写入 | 每帧整份重写进本帧那份 | 暂存 + Copy Pass | 暂存 + 元素更新 Pass |
| 每帧上传量 | 整个数组 | 新数据 | 只有变化的条目 |
| 内存 | `frameCountMax` 份 | 1 份 | 1 份 |
| GPU 读取 | 走 PCIe | 显存 | 显存 |
| 适合 | 小数组，或每帧几乎全变的数据：`g_Views`、`g_Lights`、每帧常量、CPU 剔除的间接参数 | 写一次读很多帧的连续大块：几何 | 大数组、每帧只变一小部分：`g_Instances`、`g_Materials` |

主机池只放一份、只改变化的几条是不成立的：CPU 领先 GPU 若干帧，Map 改写时 GPU 可能还在读前一帧的值，同一帧的
不同 Pass 会读到不同版本；Map / Unmap 不提供同步。每帧一份可以避开，但每份上次被写已是 `frameCountMax` 帧前，
只能整份重写。设备池的 Copy / 元素更新把 CPU 的写入排进 GPU 队列：CPU 只写本帧专属的暂存槽，改写那一份数据的
是图里的 Pass，排在前一帧所有读取之后、本帧所有读取之前。

**元素更新 Pass**（即 scatter 写入）：

- `ElementUpdateBuffer`（`Render/Staging/`）：系统持有，内含一个 `StagingBuffer`，每帧 `Begin(条数)` 后逐条
  `Add(下标)` 填内容，暂存里是两段：下标（`uint`）和对应的新内容（紧密排列）。变化检测按帧记，同一下标一帧只
  出现一次。它只管暂存，不碰 Pass。
- 一个通用的 compute（`ElementUpdate.hlsl`），按 4 字节一块拷，不认元素类型，所有使用方共用：线程 `id` 处理第
  `id / elementWords` 条更新的第 `id % elementWords` 个 `uint`，`g_Elements[index * elementWords + word] = ...`。
  相邻线程写同一条记录的相邻字，写入连续。UE 的 `ScatterCopyCS` 同形。元素大小须是 4 的倍数。
- Pass 由拥有数组的系统自己声明，内容写在声明处：读暂存（raw view，主机堆，后端不发屏障）、读写数组（raw
  view，ReadWrite：没更新的元素保留原值）、设四个常量、按 条数 × 每条字数 dispatch。没有更新的帧不声明任何
  东西，Pass 被跳过。数组的 Buffer 带 `ShaderWrite`。
- 池是 reserved 资源，扩容只映射新页、已有数据不动，所以稳定下标和不定容量能同时成立。

**迁移**：

- `StagedArrayBuffer` 即"主机池每帧一份"，小数组保持这种写法，改用区间对象。
- `GlobalBuffer` 的大数组（`g_Instances`、`g_Materials`）迁到设备池 + 元素更新。要新加变化检测：现在的
  `process` 每帧重写所有实体；空洞也不再每帧整体重置，剔除经活跃列表读，读不到空洞。
- 变化检测先用镜像比较：CPU 留一份上次传上去的记录，每帧照常编码，和镜像 `memcmp`，不同才上传并更新镜像。只看
  结果，不必知道是哪个来源变了，不会漏。以后编码本身嫌慢时换变更帧号：源组件带最后变化的帧号，由生产它的系统
  原地写（不改 ECS 结构，可并行）；读取方先比帧号再编码，`prevModel` 用 `帧号 >= 当前帧 - 1` 覆盖，材质等共享源
  由实例去读它的帧号，不需要反查。不用 `on_update` 信号或标记组件：信号慢且不能并行，标记是结构改动，还要扇出。
  前提是 `TransformSystem` 只在变化时写世界矩阵。换成帧号后镜像留在 Validation 构建里当校验。
- `SlotPool` / `SharedHandle` / `HandlePool` 这类工具逐步由区间池和区间对象取代，不再新加；`WeakHandle` 随
  `GeometrySpec` 删除已无使用方，只剩测试。

## 六、池的增长：reserved 资源 + 同类后备

- 池的大块资源是 reserved 资源：一次保留足够的虚拟地址空间，按页映射物理内存。扩页时从池里取后备资源映射上去，
  缩页时解除映射。
- **页的大小**：硬件页（tile）只是映射的最小粒度，DX12 固定 64KB，Vulkan 由驱动给出，程序不能调，经
  `DeviceLimits::m_reservedPageSizeInBytes` 查询。管理用的页由持有者定，是硬件页的整数倍，按用途取（几何 2MB，
  小数组更小）：页越大，后备对象和映射调用越少，回收越粗。渲染层的持有者是 `ReservedBuffer`（以后的 Image 版本
  是 `ReservedImage`）。
- **后备与被映射的资源同类**：Buffer 由 Buffer 后备，Image 由 Image 后备。RHI 不暴露裸内存对象，后端从后备的
  分配里取底层内存（DX12 的堆 + 偏移，Vulkan 的 `VkDeviceMemory` + 偏移）。池只分配资源，页的管理在持有它的
  系统里。
- **reserved Buffer 由 `BufferDescriptor` 标记**，仍走池的 `InitBuffer`，池按标记直接调设备 API 创建
  （`CreateReservedResource` / 带 sparse 标志的 `vkCreateBuffer`，D3D12MA / VMA 都不创建 reserved 资源），注册在
  池上，释放走原有的延迟释放。标记存在 Buffer 的描述符上，映射时据此校验。约束：大小是页的整数倍、不能 Map
  （它没有自己的内存）、因而不带初始数据、瞬态资源不能是 reserved。不限池的堆类型：池的堆类型只决定池里普通分配
  的内存，reserved Buffer 不分配内存。
- **后备是池里普通分配的 placed Buffer**，不带 bind flags，没有专门的入口。池不知道自己是不是后备，约束由上层和
  后端各自保证：
  - 对齐：上层把池描述符的最小分配对齐设为页大小（`DeviceLimits` 给出）。
  - Vulkan 内存类型：后端给 `VmaPool` 选内存类型时，按"同 usage 加 sparse 标志"的 `memoryTypeBits` 选（规范保证它
    是不带 sparse 的子集，对普通 Buffer 也合法），所以任何池都能当后备；设备不支持 sparse 时按普通选。DX12 的
    文档没写堆类型的限制，实测（debug layer）DEFAULT / UPLOAD / READBACK 堆都能映射。
  - 映射时后端校验后备是 placed、偏移按页对齐、Vulkan 内存类型在 reserved 资源的 `memoryTypeBits` 内，不满足报错。
  - 一页一个后备 Buffer，大小即页的大小，映射一页是一条覆盖多个 tile 的 `BufferTileMapping`。后备池的块大小设为
    页的大小，每个后备正好占满一个 D3D12MA 块，必然 placed，块空了由 D3D12MA 释放。
- 后备与 reserved 资源共享内存，不能当普通资源用：它不带 bind flags，校验据此拒绝给它建 View、发屏障、导入图。
  Vulkan 要求 `usage` 非零，由后端自己补，RHI 层看不到。
- **映射是队列级操作**（DX12 `ID3D12CommandQueue::UpdateTileMappings`，Vulkan `vkQueueBindSparse`），不能录进
  CommandList，也不由图里的 Pass 做。RHI 只提供 `CommandQueue::UpdateTileMappings(span<const BufferTileMapping>)`
  （后备为空即解除映射）。映射什么、何时映射、何时解除由持有池的系统决定，提交由图做（见下）。
- 语义按 DX12 定：在队列 Q 上的映射排在 Q 上之前所有提交之后、之后所有提交之前。Vulkan 的 bind sparse 与 submit
  即使同队列也没有隐式顺序，后端内部用 timeline semaphore 补上（bind 等 Q 的上一次提交，下一次提交等 bind），
  接口不带同步参数。Vulkan 允许的"映射藏在后台、只在首次使用时等"现在不用；纹理流送需要时加一个返回 Fence 值的
  异步变体，是加法。
- 一次调用里每页最多出现一次，validation 检查重叠。按 Vulkan 定：同一批次里一段范围不能绑定两次，批次可能乱序
  完成；DX12 允许重叠、后者覆盖前者，不用这一点。一次调用可以含多个 reserved Buffer（Vulkan 一个批次绑多个
  Buffer、多块内存）；DX12 一次原生调用只有一个资源、一个堆，而一页一个后备即一页一个堆，后端一条映射一次调用。
- 上层的责任：系统在自己的更新里算出映射，经 `RHIEventContext` 的 `BufferTileMappingRequest` 交给图，和上传请求
  一样是渲染前的资源准备。一个请求一条映射，图在 Execute 开头、所有 Scope 之前对每个请求发一次
  `UpdateTileMappings` 在图形队列上，本帧即可用；系统拿不到图的队列，也不需要。`ReservedBuffer` 按页记未提交的
  请求：页只有两种状态，请求未提交时这页再变就是变回去，撤掉请求、什么都不发（先解除再占用，后备从请求里取回；
  先映射再变空，没映射过的后备随请求放掉），即 §五 的"同一期间先空后占，不发任何映射"。所以一页最多一个未提交
  的请求，请求之间无序。请求持有它映射或解除的后备，提交后才放掉，所以总是先解除映射再放掉后备。Vulkan 上一个
  请求一次 bind sparse 偏多时，图把请求收进跨帧复用的数组一次提交，请求不变。页上区间都归还后即可解除映射，图形队列之前的工作排在它前面。哪页映到哪个后备由持有池的系统记，
  它本来就管着页；RHI 不记映射，也不查后备释放时是否还映射着。
- 现在的边界：映射只在图形队列上排序，reserved Buffer 的共享队列只有 Graphics。将来别的队列（如异步计算）在本帧
  先用到这个 Buffer 时，扩展图的执行：映射发在本帧第一个用它的 Scope 的队列上（同上传的 acquire 记在第一个读它的
  Scope 里），或让那个队列等图形队列。请求的格式不变。
- 图导入整个 reserved 资源，按 §二 整资源同步；映射不进图。
- 特性要求（Tiled Resources Tier，与 Resource Heap Tier 是两套分级）是引擎的最低要求，设备初始化时检查，不支持
  的设备不能运行引擎，RHI 不设特性开关：DX12 Tiled Resources Tier 2（Tier 1 下读未映射页未定义，Tier 2 起读 0）；
  Vulkan `sparseBinding` + `sparseResidencyBuffer`（Image 再加 `sparseResidencyImage2D`）+
  `residencyNonResidentStrict`。移动端多数和 MoltenVK 不支持。

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
  区间池（每次分配一个物理页大小）"，与阴影图集同类。
- reserved 的 RT / DS 新映射的页内容未定义，首次使用前对该区域 Discard / Clear；只当拷贝目标、整页覆盖写的流送
  纹理不受影响。

## 八、对上层的影响

- **绘制**：区间偏移通过绘制参数（`vertexOffset` / `firstIndex`）传入。
- **DrawIndirect**：不再写死 `MeshGeometryBaseBufferTag`，改为和零散 Draw 一样接收顶点流附件，Pass 自己
  `p.Import<Tag>` + `s.InputAssembly`。`Uses<MeshGeometrySegmentTag>` 随之删除。
- **`Uses<Tag>`** 只留给"新资源经上传系统交接后、经绑定 / bindless 读取"的资源（材质贴图、IBL、查找表、图标）。
- **在图里被写的资源由 Pass 手动声明访问**（`p.Import` + `s.ReadBuffer(...)`，渲染 Pass 加 `.Stage`），共享绑定
  不替 Pass 声明。`Binds` 只声明 Pass 的着色器用哪些共享输入，以后可由着色器反射推导。漏声明由降低阶段校验：
  Scope 绑定的共享组里有视图指向图在跟踪的资源，而这个 Scope 没有声明对它的访问，就报错（L5d 起有可校验的对象）。
- **剔除与分箱**（§五 实例入口）：剔除按活跃列表的个数 dispatch，读 `g_Instances[InstanceIDBuffer[i]]`，可见时
  输出位置 `i`；分箱和 CPU 剔除的 `FirstInstance` 填位置而不是槽号。`InstanceSlotCount` / extent 不再定 dispatch
  大小。改动落在 `TODO_GPUDrivenPlan.md` 的剔除部分。

## 九、待设计

1. ~~暂存与拷贝 Pass 的形态~~（已定）：暂存由上传的系统自己持有（`StagingBuffer`），不走 `AsyncUploadSystem`。
   每个在途帧一个主机 Buffer，各是一个普通导入实体，由系统按 Device 帧号自己轮转（不用 `PerFrameTag` /
   `BufferPerFrame`），一直映射；复用安全由帧 Fence 保证。系统每帧先 `Begin(本帧字节数)` 再切分：总量写之前就
   已知（不能等的数据是变化项数，可以等的是额度），所以容量不够时在 `Begin` 换更大的 Buffer，此时其中没有数据，
   帧中途不增长。容量与速率分开：容量按需增长，不估；速率只限可以等的数据（几何、流送），不能等的（实例变换、
   材质修改）不限，否则画错。第一版可以等的额度是系统里定死的常量；以后换成一个全局的每帧上传预算（按 GPU 拷贝
   耗时调整），各系统只向它申请，是加法。拷贝由 Copy Pass（与 Render / Compute 并列的第三种 Pass）完成，由拥有
   暂存的系统登记。写入方式见 §五。
2. ~~区间分配算法的选型~~（已定）：Core 自写 TLSF，见 §五。
3. ~~映射发在哪个队列~~（已定）：映射只在同一队列内排序，队列由 `RenderGraph` 持有。不开放图的队列给系统（在
   系统边界上打洞），也不挪到 Device（Device 保持精简）。系统经事件把映射交给图，图在 Execute 开头于图形队列统一
   提交，见 §六。

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

- 加在 `RHI::Device` 上，实现在 RHI 基类：Core 的 `ObjectCollector`，延迟 `frameCountMax` 帧。Device 接
  `FrameEventBus`，在 `OnFrameEnd` 回收，和池遵守同一个帧约定，不依赖 RenderGraph（只链 SparkRHI 的样例自己广播）。
- 关闭时在 `RHIInterface::ShutdownInternal` 清空上下文之后全部回收（`FlushReleaseQueue`），早于 Factory 关闭所有池，
  排进来的对象可以间接持有 RHI 资源；`Device::Shutdown` 再兜底回收一次。
- 现有按类型分开的释放队列先不动。
- 暂无使用方，编译通过即可；渲染层的区间对象是第一个用户。

**R3　reserved Buffer**

- `BufferDescriptor` 加 `bool m_reserved`（Vulkan 对应 `SPARSE_BINDING | SPARSE_RESIDENCY`）。
- `DeviceLimits` 加 `m_reservedPageSizeInBytes`，DX12 在 `InitializeLimits` 填 `D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES`
  （64KB）；Vulkan 以后取一个 sparse Buffer 内存需求的对齐。上层据此设后备池的最小对齐、定 reserved Buffer 的大小。
  `DeviceFeatures` 不加字段：Tiled Resources Tier 2 是最低要求，`Device::InitInternal` 检查，不够就初始化失败。
  现有 `m_tiledResource` 保持原含义（`StreamingImagePool` / DX12 `Image` 在用）。
- `ID3D12DeviceX` 从 `ID3D12Device6` 改为 `ID3D12Device10`：建 reserved 资源并给初始 layout 要
  `CreateReservedResource2`。`InitInternal` 对 `ID3D12DeviceX` 的查询本就保证它，删掉单独查 Device10 的那段，只留
  增强屏障的检查。
- RHI 校验：reserved Buffer 大小是页的整数倍、不带初始数据；`MapBuffer` 拒绝 reserved Buffer（不论池在哪种堆）；
  RHI `TransientResourcePool` 拒绝 reserved 的瞬态 Buffer。
- DX12 `BufferPool`：`InitBufferInternal` 加第三个分支，reserved 用 `CreateReservedResource2`（初始 layout
  `UNDEFINED`）创建，不经 D3D12MA，`MemoryView(Memory*, Buffer, 0, size, 0)` 包起来——GPU 地址照常，`GetHeap()`
  为空，R4 据此区分它和后备。`ShutdownResourceInternal` 遇 reserved 没有 D3D12MA 分配可排，把 `ID3D12Resource`
  交给 `Service<ID3D12FactoryInterface>::Get()->QueueForRelease`，再清空内存视图。
- DX12 `CommandList::QueueBarrier`：reserved Buffer 跳过 `GetHeapProperties` 的上传 / 回读堆判断——那条规则针对
  建在这类堆里、状态固定为 `GENERIC_READ` 的资源；reserved 资源没有所属的堆，按自己的状态走屏障，与映射上的内存
  无关。
- 不用改：View、屏障、导入图照常（它就是一个原生资源）；后备不需要新代码，上层建一个最小分配对齐为页大小的池，
  `InitBuffer` 时传 `BindFlags::None`。
- 验证：暂无使用方，编译通过、编辑器照常即可；与 R4 一起在 SandBox 样例里实测。

**R4　`CommandQueue::UpdateTileMappings`**

- RHI 基类加接口和校验：目标是 reserved Buffer，后备不是 reserved、是 placed，页范围不越界。两侧的偏移和大小都按
  字节给（同 Vulkan 的 `VkSparseMemoryBind`），按页对齐；DX12 后端换算成 tile。
- DX12：后备的堆和起始页从它的 `D3D12MA::Allocation`（`GetHeap()` / `GetOffset()`）取，调
  `ID3D12CommandQueue::UpdateTileMappings`，解除映射用 NULL range flag。
- Vulkan 对应：`vkQueueBindSparse`，用 timeline semaphore 排进队列的时间线（§六）。
- R3、R4 的验证：`SandBox/Program/` 加一个最小样例——建 reserved Buffer、映射两页、拷贝写入、compute 读回检查，
  再解除映射、放掉后备。

**之后：渲染层**（区间分配器、区间池、`ReservedBuffer` 与图提交映射已完成）。L1 ~ L3 是两个使用方共同的前提，
先迁几何池（只用 Copy，验证整条链路，并解锁 R5），再迁实例。

- ~~**L1　帧号统一到 Device**~~（已完成，§十二）：按帧分槽的暂存靠"每份上次被用在 `frameCountMax` 帧前"保证
  复用安全，选槽要一个不会错开的帧号，所以先于暂存。
- **L2　系统 Pass 单独登记**（延后，§十二）：现在系统的 Pass 和管线的 Pass 一样登记在唯一的 PassContext 里，暂不
  支持自定义管线，改管线就改引擎代码。以后再把两类分开，让用户能控制管线。
- ~~**L3　暂存与 Copy Pass**~~（已完成，§九 第 1 条）：Copy Pass 是第三种 Pass，Scope 声明 CopySource /
  CopyDestination 并拷贝字节区间；`StagingBuffer` 是系统持有的每帧一份主机暂存。
- ~~**L4a　删掉旧的 DrawItem 派生路径**~~（已完成，§十二）：`MeshGeometryComposer` → `DrawItemRouter` →
  `GeometrySpec` 做出的 DrawItem 只有 `.Accepts<>()` 取得到，而没有 Pass 调它，连同 `ScopeSelections` 一起删。
- ~~**L4b　几何池迁移**~~（已完成）：`MeshGeometrySystem` 改用 `Suballocator` + `ReservedBuffer`（虚拟容量
  1GB，页 2MB），顶点与索引共用一块 reserved Buffer，`MeshGeometry` 持有两个区间（顶点按步长、索引按 4 字节对齐）；
  DrawIndirect 接收顶点流附件，几何池与 `InstanceIDBuffer` 都由 Pass 导入，删 `MeshGeometrySegmentTag` /
  `MeshGeometryBaseBufferTag` 一套（§八）。
  - 上传走 L3，不再经 `AsyncUploadSystem`：每帧定量（8MB），装进池按帧进行，这是正确性要求（池每帧被读，写要排在
    同帧读之前），业界同（UE 的 `r.VT.MaxUploadsPerFrame`、Nanite 每次更新的页安装上限）。准备数据（解析、解码）
    不与帧绑定，由准备好的字节入队。
  - 进度在实体上（`MeshGeometryUpload`，随 `MeshGeometry` 增删，几何移除即不再写），不另设队列：每帧先传完已开始
    的，再开始新的，到额度为止，所以任何时刻最多一个传了一半。传完最后一块的那帧即可画：拷贝 Pass 排在所有读池的
    Pass 之前。
  - 多线程：系统线程只做按额度挑选和记区间；切好的暂存区间互不重叠，memcpy 可拆给工作线程，在图 Build 前汇合。
  - 几何仍按实体，同一网格多实例各占一份；按网格共享是以后的事。
- ~~**R5**　删预算模式~~（已完成，见下）。
- **L5　`g_Instances`**：设备池 + 元素更新 Pass，加变化检测；`InstanceIDBuffer` 改为活跃列表；剔除与分箱按位置
  （§八，连同 `TODO_GPUDrivenPlan.md`）。分四步，先把槽位从 SharedHandle 换下来，后面各步只建在新组件上：
  - ~~**L5a　元素更新**~~（已完成）：`ElementUpdateBuffer` + `ElementUpdate.hlsl`（§五）。第一个使用方是
    `InstanceIDBuffer`：改为 `ReservedBuffer`，按条数映射前缀的页（增减都有，见 §五 活跃列表），新槽的恒等 ID 经
    `InstanceIDUpdate` Pass 写入，排在所有绘制 Pass 之前，新槽当帧可画；`InstanceSlotCount` 取槽数与已写 ID 数
    的较小者，去掉等上传完成的门槛。内容仍是恒等表，渲染不变。
  - ~~**L5b　槽位换成区间**~~（已完成）：`InstanceSlot { Suballocation }` 取代 `InstanceSlotRef`，从只管地址
    空间的 `Suballocator`（`Capacity × 208` 字节，不计页）按一条记录分，槽号 = 偏移 / 208。`InstanceBindingSystem`
    不再经 `GlobalBuffer`，直接用 `StagedArrayBuffer`，`g_Instances` 仍是主机上每帧一份、整份重写；空洞不再靠
    `SlotPool::IsHeld` 认，每帧先把 `[0, extent)` 清成 `InstanceData{}` 再编码。剔除照旧按槽，渲染不变。
  - ~~**L5c　活跃列表**~~（已完成）：位置 = `InstanceSlot` 存储里的下标（entt 的紧凑数组，增在末尾、删时 swap-remove），每帧
    从存储生成 `位置 → 槽`，与镜像逐位置比较，不同的经元素更新写入；正确性不依赖存储顺序，顺序只决定每帧差异
    多少。剔除、分箱、CPU 剔除按位置；列表长度用新组件 `ActiveInstanceCount`，删 `InstanceSlotCount`。剔除和
    分箱各自导入 `InstanceIDBuffer` 并以 `g_InstanceIDs` 读（手动声明，§八）。GPU 上的列表由 `ActiveInstanceList`
    持有（`ReservedBuffer`、页、元素更新、镜像），系统每帧把本帧的 `位置 → 槽` 数组交给它；暂存涨不上去时这一帧
    个数为 0，镜像清空，下一帧整份重写。容量 `InstanceCapacity` 移到 `InstanceBinding.h`，剔除与分箱的视图按它定。
  - **L5d　`g_Instances` 迁到设备池**：`ReservedBuffer` + 元素更新 + 镜像比较的变化检测，去掉空洞清零；读它的
    Pass 手动声明读取，加上漏声明的校验（§八）。
- **L6　其余数组**：`g_Materials` 等 `GlobalBuffer`、`StagedArrayBuffer` 的小数组迁到区间对象，`SlotPool` 一类
  工具逐步删除；`RHIResourceSystem` 随各系统迁移一起拆。

~~**R5　删预算模式**~~（已完成）

- 删 `ResourcePoolDescriptor::m_budgetInBytes`、`GetBaseBuffer`、`InitBaseBuffer`、`InitSubAllocatedBuffer`、
  `VirtualBlockAllocation` 与其释放队列、`BufferMemoryType::Shared`、`CommandList::QueueBarrier` 里 `Shared` 的分支、
  `RHI::BufferPool::Init` 里注册基础 Buffer 的那段。
- `BufferMemoryType` 只剩 `Unique`，整个枚举删掉；`BufferMemoryView` 暂留为 `MemoryView` 的薄壳，并入 `MemoryView`
  另做。`ResourcePoolDescriptor` 留作空基类。示例 `IndirectDraw` 的划分开关一并删除，四个 Buffer 各自独立。

**Image 的 reserved 资源**随纹理流送再做。

## 十一、前瞻

- **Nanite 类虚拟几何**：页池 = 一块被 GPU 写、被多个 Pass 读的大缓冲，按整资源同步；页从区间池分配
  （每次分配一页大小，流式系统持有）；淘汰的页经 `QueueForRelease` 延迟复用；页数据经暂存 + 转码 Pass 写入；页池以 SRV
  读，走 `ReadBuffer` 附件。与本方案一致。
- **API 趋势**：同步变粗（DX12 缓冲屏障整资源 + global barrier，Vulkan 推荐无所有权转移时用 global memory
  barrier）、内存地址化（buffer device address、bindless）。"大缓冲 + 偏移"顺应这个方向。
- **若将来需要区间级并行**：扩展点是让图里的缓冲访问带可选区间、按区间判冲突，是加字段，不需要把区间重新做成
  Buffer。

## 十二、相关但单独实施

零散 Draw（已完成）：`RenderScope::Draw(arguments, span<const Attachment> vertices[, IndexStream])`，
缓冲经 `p.Import` + `s.InputAssembly` 成为附件，步长取自 Pass 的输入布局；`MeshGeometryComposer`、
`DrawItemRouter`、`GeometrySpec`、`Accepts` 已删（L4a）。场景内容走 indirect，Draw 只用于零散用途。

帧号统一到 Device（L1，已完成）：

- 原先的计数器：交换链的图像索引（`RenderSystem` 取来传给各系统和 `ExecutePipeline`，经 `Device::BeginFrame`
  转述）、`CommandQueueContext::m_currentFrameIndex`（选 CPU 等的帧 Fence）、DX12 `TransientResourcePool` 的
  `m_currentSlot`（选瞬态槽）。交换链 Resize 把图像索引归 0，另外的不归，从此错开。
- Device 在 `OnFrameEnd` 推进唯一的帧号——不在 `OnFrameBegin` 推进，因为总线不定处理者顺序，`OnFrameBegin` 里
  读帧号的会读到新旧不定的值；也因此 `OnFrameEnd` 的处理者不读它。`CommandQueueContext::End` 排在 `OnFrameEnd`
  之前，给当前帧号的 Fence 发信号、等下一帧号的。RenderSystem、各绑定系统、图、瞬态池都读它；删掉 `BeginFrame`
  和 `ExecutePipeline` 的 `frameIndex` 参数。
- 交换链不是"每帧一份"：用哪一张由呈现引擎定（DXGI Resize 归 0，Vulkan acquire 不保证轮转），张数与在途帧数
  无关，复用由 Present / acquire 保证而非帧 Fence。原先它是一个带 `PerFrameTag` 的实体、按帧号取图和视图，只因
  帧号恰等于图像索引才成立。现在每张后备缓冲一个普通导入 Image 实体（`SwapChainImageTag`，`BackingImage`
  建时写定，普通视图缓存），Resize 时整组重建，每帧按交换链的图像索引把当前那张交给 builder。`PendingSync`
  随之记在各自的图上，是这张图上次被用的位置。

系统 Pass 与管线分开登记（方向已定，延后；在此之前系统 Pass 与管线的 Pass 登记在同一处，不支持自定义管线）：

- 问题：池的 Copy / 元素更新 Pass 若写在 `SetUpDefaultPipeline` 里，用户自定义管线时就没了（`InstanceCullingPass` /
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
