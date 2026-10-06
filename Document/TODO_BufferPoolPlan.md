# BufferPool 在一个原生 buffer 内分配　实现方案

让 `BufferPool` 可以在初始化时自己建好一个大的原生 buffer，之后从这个池申请的每个 `RHI::Buffer` 都是它里面的一段，
释放的段延迟几帧后再分出去。不指定这个行为的池和现在一样，一个 buffer 一个原生对象。

它是 `TODO_GPUDrivenPlan.md` 步骤 3（几何进共享 buffer）的基础：有了它，每个网格仍然是一个自己的 `RHI::Buffer`，
落在同一个原生对象里是池的事，上层不需要再做一层分配与回收。池由使用它的系统持有（D6），本计划只做 RHI 这一层。

方向由用户在 2026-10-06 提出；D1–D6、D8 已由用户逐条确认，D7 只是记录。
现状一节来自 2026-10-06 读代码，没有运行验证。原生语义一节标了哪些是当天读的原文、哪些不是。

---

## 状态

| 步骤 | 内容 | 依赖 | 状态 |
|---|---|---|---|
| 1 | RHI 与 DX12 后端：池的新行为（建底层 buffer、划分、对齐、延迟回收、屏障），一个 RHI 示例验证 | — | 完成（2026-10-06），见下；画面用户已确认 |
| 2 | 改写 `TODO_GPUDrivenPlan.md` 的 D4、D12 的一句与步骤 3 | 1 | 完成（2026-10-06） |

接入引擎不在本计划内：持有这个池的几何系统是 `TODO_GPUDrivenPlan.md` 步骤 3 的内容（§七）。

下文把"池初始化时建的那个大的原生 buffer"叫**底层 buffer**（代码里是 base buffer，划出来的 buffer 是 sub-allocated）。

**步骤 1 跑过什么、没跑过什么**

- 全量 Debug 编译通过；`SparkRenderTest` 89 个用例通过，没有新增用例。
- 示例 `IndirectDraw` 加了 `kSubAllocateBuffers`（默认关）。开、关各在 debug layer 下跑 10 秒，无报错无断言。画面由用户确认：开着和关着一样。
  打开时它是这些东西的第一次执行：池建底层 buffer、从里面划分、buffer 从不发原生屏障而直接当顶点 / 索引 / indirect 参数用、
  `ExecuteIndirect` 的参数与 count 带非零偏移。
- 用临时代码确认过（已删）：
  - 四个 buffer 相对第一个的偏移是 count +0、顶点 +28、索引 +704、参数 +776。顶点的步长 28 不是 2 的幂，
    它从 4 被取整到 28，走的是"多要一点再向上取整"那条路。
  - 延迟回收：建一个 48 KB 的 buffer 再释放，之后每帧申请同样大小的，第 0、1、2 帧被拒绝，第 3 帧成功。
    示例的 `m_frameCountMax` 是 2，即释放后经过 3 次帧结束才归还，和 `ObjectCollector` 对独立分配的时机相同。
  - 放不下的报错："15489 of its 65536 bytes are free, the largest free range is 15488 bytes"。差的 1 字节是顶点与索引
    之间对齐留下的空洞，数字对得上。
  - 对一个划出来的 buffer 发"拷贝写入"屏障，D3 的断言触发，信息里有访问与队列。
- 没有执行过的：
  - 换队列的交接屏障被丢弃的那一行（示例只有一个队列）。
  - 划出来的 buffer 经过上传系统与渲染图（示例直接用 RHI）。
  - 大量 buffer 反复申请、释放，碎片。
  - host 内存的池打开这个行为。
  - 池先关、buffer 后析构时 `VirtualBlockAllocation` 自己归还的那条路。
  - GPU-based validation。

---

## 一、为什么做

- **池退化成了分配器。** 现在从池申请一个 buffer 就是建一个原生对象，池只决定它落在哪种 heap 上，
  和直接创建资源差别很小。D3D12MA 在 buffer 内部再分配的能力没有用上。
- **上层因此要自己再管一层。** `TODO_GPUDrivenPlan.md` 的 D4 原方案是渲染层持有一个共享 buffer、自己写一个变长的
  分配器、自己数帧做延迟回收，上传系统还要学会"往某个 buffer 的一段里写"。这些本来是池的事。
- **延迟回收应当在 RHI 层。** 仓库里现有的延迟回收都在 RHI（两个释放队列、transient 池），渲染层没有一处自己数帧。
- **I11 需要它。** 一次 indirect 调用里所有 draw 共用一个 VB / IB 绑定，所以它们的几何必须在同一个原生 buffer 里。

---

## 二、现状

**池现在做什么**

- `DX12::BufferPool::InitBufferInternal` 对每个 buffer 调一次 `CreateResource3`（`BufferPool.cpp`），一个 buffer 一个
  `ID3D12Resource`。D3D12MA 只决定它落在哪个 heap 块里。
- 池自己的管理只有两件：按描述选 heap 类型；释放时把分配放进 `m_releaseQueue`，每帧结束 `Collect` 一次，
  延迟是设备的 `m_frameCountMax`。

**后端已经按"buffer 是原生对象里的一段"写的地方**

- DX12 的 `Buffer` 持有 `BufferMemoryView`，里面有偏移。拷贝（`CommandList.cpp` 的 `Submit(CopyItem)`）、顶点与索引的绑定
  （`SetVertexBuffers` / `SetIndexBuffer`）、常量缓冲、SRV / UAV 的起始元素（`Conversions.cpp`）、`ExecuteIndirect`
  都加了这个偏移。它现在恒为 0。
- `BufferMemoryType::Shared` 的注释是"和别的 buffer 共用一个内存资源"。现在它被用来表示"这个分配落在一个 heap 块里"，
  和注释的意思不同。
- SRV / UAV 要求偏移是元素大小的整数倍，否则报错（`Conversions.cpp`）。

**只认整个原生对象的地方**

- 屏障：`CommandList::QueueBarrier` 填的是 `Offset = 0`、`Size = UINT64_MAX`。
- 资源状态：`RHI::Buffer::m_resourceState`，每个 `RHI::Buffer` 一份。
- 释放队列：排的是 `D3D12MA::Allocation`。

**现成可用的**

- D3D12MA 的 `VirtualBlock`：只记账、不建 API 对象。`TransientResourcePool` 已经在用。它的对齐必须是 2 的幂
  （`D3D12MemAlloc.h` 里 `VIRTUAL_ALLOCATION_DESC::Alignment` 的注释）。
- `BufferDescriptor::m_alignment`：现在只是原样传进 `MemoryView`，没有人按它分配。
- `ResourcePoolDescriptor::m_budgetInBytes`：没有人读。
- `BufferPoolDescriptor::m_largestPooledAllocationSizeInBytes`：只用来抬高 D3D12MA 的块大小。

**谁在用池**

- `RHIResourceSystem` 持有四个 buffer 池（device、device 独占给光追、host 上传、host 回读），`SelectBufferPool` 按 heap
  类型与绑定标志选：它不知道资源是干什么的，靠标志决定用哪个池。
- 自己持有池的系统已经有：`RenderGraph`（transient 池，以及给跨帧图像的 `ImagePool`，构建时直接从池里同步建，
  关闭时先销毁自己的实体再关池）、`AsyncUploadSystem`（staging 池）。
- 走上传的 device buffer 只有三种：网格的 VB、IB（共享模式，图形与 copy 两位）和 `InstanceIDBuffer`（独占，图形一位）。
- buffer 指向池的是裸指针（`Resource::m_pool`），析构时回调池的 `ShutdownResource`。
- Vulkan 后端里没有 buffer 池的实现。

---

## 三、原生语义

- **在 buffer 内部再分配是 D3D12 文档里写明的用法。** 微软文档有一页 "Suballocation Within Buffers"，原话是
  "Buffers, through suballocation, have all the features necessary for low-level memory management."
  （2026-10-06 读的原文。）
- **D3D12 的 buffer 屏障只能作用于整个 buffer。** Enhanced Barriers 规范："For now, `Offset` must be zero and `Size`
  must be either the buffer size in bytes or `UINT64_MAX`."结构体里有这两个字段是"to facilitate future buffer subregion
  barriers"；现在不支持的理由是别的 API 有子区间屏障，但"how these barriers work with various memory, caches or whether
  they guarantee multi-writer support is unclear"；驱动接口目前不接收这两个值。（2026-10-06 读的原文。）
- **buffer 没有 layout，屏障只管缓存刷新与同步。** 同一份规范："Unlike textures, buffers have only a single subresource
  and do not have a transitionable layout." "Buffer Barriers control cache flush and synchronization for buffer
  resources."（2026-10-06 读的原文。）
- **一次提交里对 buffer 的第一次访问可以不带屏障**，顶点、索引、拷贝写入都在列（规范 Barrier-Free Access 一节，
  2026-10-06 读的原文）。同一节还有一句："with the exception of `D3D12_BARRIER_ACCESS_RENDER_TARGET`, barriers MUST be
  used to flush sequential writes to the same resource."它对"一次提交里往同一个 buffer 拷多段"是否适用，读不出确定答案，
  见 §八。
- **Vulkan 的 buffer 屏障带偏移与大小**（`VkBufferMemoryBarrier` 的 `offset` / `size`）。同步这一半 RHI 按更严格的一边
  （DX12）设计，在抽象层仍按整个原生对象处理。绑定顶点 / 索引 buffer 用的是 `VkBuffer` 加偏移，这一点是凭已有的了解写的。
- **Vulkan 独占模式的所有权按 buffer 的区间算，不是按整个 buffer。**（2026-10-06 读的 refpage：`VkSharingMode`、
  `VkBufferMemoryBarrier`。）
  - "VK_SHARING_MODE_EXCLUSIVE specifies that access to any range or image subresource of the object will be exclusive
    to a single queue family at a time."
  - "Ranges of buffers ... created using VK_SHARING_MODE_EXCLUSIVE must only be accessed by queues in the queue family
    that has ownership of the resource."
  - 屏障的释放与接手都是 "for the specified buffer range"。
  - "A queue family can take ownership of an image subresource, tensor subresource, or buffer range without an ownership
    transfer, however, taking ownership in this way has the effect that the contents are undefined."新分出来、正要被
    上传覆盖的一段正是这种情况。
  - "VK_SHARING_MODE_CONCURRENT may result in lower performance access to the buffer or image than
    VK_SHARING_MODE_EXCLUSIVE."差多少没有数据。
- DX12 没有所有权这个概念。

由此：

- 从一个池出来的多个 `RHI::Buffer` 共用一个原生对象，在两个 API 上都是原生的用法。
- 需要同步屏障的访问（GPU 写了之后同一队列再读或再写）不能放进这样的池，因为 DX12 的屏障没法只作用于其中一段。
- 所有权交接可以按段做：一个独占的底层 buffer，图形队列一直拥有并读着大部分区间，copy 队列只接手新的那一段、写完、
  交还。这一半 Vulkan 有要求而 DX12 没有，抽象层要带着它，DX12 丢弃。

---

## 四、决策记录

### D1　池的新行为由 `m_budgetInBytes` 打开　✅ 已定

`ResourcePoolDescriptor::m_budgetInBytes` 给出底层 buffer 的字节数。不为 0，池在初始化时建好它，之后的申请都在里面划分；
是 0 就是现在的行为。

- 这个字段现在没人读，含义就是"这个池一共管多少"，不另加字段。
- 不引入"页"：这一版只有一个底层 buffer，没有"一块满了开下一块"。以后怎么增长见 D5。

### D2　对齐用 `BufferDescriptor::m_alignment`　✅ 已定

一个 buffer 在底层 buffer 里的偏移必须是它元素大小的整数倍：indirect 记录里的 `vertexOffset` / `firstIndex` 以元素为单位，
后端建结构化 view 时也要求偏移整除元素大小（`Conversions.cpp`，现在偏移恒为 0 所以没触发过）。
申请方在 `m_alignment` 里填元素大小（顶点填步长、索引填索引大小），池保证偏移是它的倍数。

- 对齐由"这段数据怎么被用"决定，属于 buffer，不属于池。D3D12MA 的文档对这个用法说的也是每一段各自声明对齐。
- `m_alignment` 已有、没人用：现在只被存进 `MemoryView`，不传给 D3D12（2026-10-06 查过），填了不影响一对一的池。
  值为 0 按 1 处理。
- `VirtualBlock` 的对齐只接受 2 的幂，48 不是。不是 2 的幂时池里多申请不到一个对齐的字节、再用 `AlignUpNPOT` 把起点
  向上取整，每个 buffer 最多浪费 `m_alignment - 1` 字节。
- 顶点与索引可以放在同一个底层 buffer 里，不同步长的顶点布局也可以共存。`TODO_GPUDrivenPlan.md` 的 D4 原来定的
  "顶点、索引各一个数组，按元素分配"不再需要。
- 没有选"池初始化时定一个分配单位"（`VirtualBlock` 允许大小与偏移用字节以外的单位）：它省掉的只是取整那几行，
  换来池上多一个选项和"步长必须整除单位"的约束，48 容纳不了 32、40、64 字节的布局。
- **申请方要填**：顶点 buffer 填步长，索引 buffer 填索引大小。

### D3　只是原生 buffer 的一段的 buffer 不发原生屏障　✅ 已定

一个 `RHI::Buffer` 只是某个原生 buffer 的一段时，DX12 后端不为它发原生屏障，状态的更新规则和现在一样。

- 依据：DX12 的屏障只能作用于整个原生对象（§三），一段没法单独发。
- 这样的池只放"上传一次、之后只读"的数据。它需要的访问（copy 队列写入、顶点 / 索引读）都在 §三 "首次访问可以不带屏障"
  的列表里，跨队列的先后靠 fence。每次提交开始时 buffer 隐式回到初始状态，所以现在"首次使用时发一次"的那条屏障
  本来就没有持续的效果。
- 每个 `RHI::Buffer` 的状态留着，包括"当前所有者队列"：各自在第一次使用后变成稳定状态。它和 Vulkan 按区间算的
  所有权一一对应。**渲染图与上传系统不用改**，独占路径（copy 队列接手、释放，图形队列等 fence 后接回）对每一段各走一遍。
- 后端有先例：host 内存的 buffer 在 `CommandList::QueueBarrier` 里直接跳过。这里是它旁边一个同类的判断。
- 屏障按种类分开处理：

  | 屏障 | DX12 后端 | 以后的 Vulkan 后端 |
  |---|---|---|
  | 换队列的交接（源队列与目标队列不同） | 丢弃：没有所有权，没有东西要做 | 按这一段的偏移与大小发所有权交接 |
  | 同一队列内，两边有任何一边是写 | 断言失败：需要真的同步，而给不了一段 | 同样不支持，抽象层跟着更严格的一边 |
  | 同一队列内，两边都是读 | 丢弃 | 丢弃 |

  断言抓的是"同一个队列里先写这样的 buffer、再读它"。上传走 copy 队列，正常路径碰不到。
- **限制**：GPU 会写的 buffer 不能进这样的池，仍然一个 buffer 一个原生对象；由池的绑定标志限定。
- 没有选"状态记在底层 buffer 上、共用一份"（这一条原来的写法）：渲染图先编译整帧的屏障再执行，同一帧首次使用的
  网格读到的都是"还没人用过"，会对同一个原生 buffer 各生成一条"之前无访问"的屏障，第二条起违反规范（读规范得出，
  没有跑过）；渲染图的状态游标又是按实体记的，两个实体背后是同一个原生 buffer 时各走各的。要做对就得把
  "哪些 buffer 共用一个原生对象"带进渲染图的状态跟踪。
- 没有核实的：DX12 上一个 buffer 从不发屏障、直接当顶点和索引用，debug layer 是否干净（步骤 1 的示例里跑）；
  Vulkan 上跨队列靠 semaphore 就够、不需要屏障，是凭已有了解写的。

### D4　延迟回收在池里　✅ 已定

释放这样一个 buffer 时，它那一段等 `m_frameCountMax` 帧后才还给 `VirtualBlock`，和独立 buffer 的分配晚释放的时机一样。
池关闭时全部还回去，不等。

不延迟会出的事：CPU 已提交、GPU 还没执行的帧仍在画旧网格，此时这一段被分给新网格并由 copy 队列写入，
那几帧就会从这一段读到新网格的数据。

- **把 `VirtualAllocation` 补成现有体系里的一员**（用户 2026-10-06 定）。它只是一个句柄，进不了 `Ptr` 和
  `ObjectCollector`；`VirtualBlockAllocation` 把它包成带引用计数的对象，最后一个引用放掉时把那一段还给
  `VirtualBlock`——它对 `VirtualBlock` 就是 `D3D12MA::Allocation` 对 heap。之后回收用的就是 `ObjectCollector`，
  `ReleaseQueue.h` 里照现有写法多一个队列类型。
- **数据在 `BufferMemoryView` 上。** `Unique` 的 buffer 分配在基类里（不变）；`Shared` 的 buffer 基类里没有分配，
  `BufferMemoryView` 多出来的那个 `Ptr` 指向它的 `VirtualBlockAllocation`。
- **池里两个队列、同一套机制。** 独立的分配进原来那个，划出来的一段进新的那个，一个池实际只用到其中一个。
  没有合成一个队列：那要两种对象有共同的类型，而 `D3D12MA::Allocation` 唯一的公共基类是 COM 的 `IUnknown`，
  包装类就得手写成 COM 对象。
- 每一段自己拿着 `VirtualBlock` 的引用，所以池先关、buffer 后析构也能安全归还，池关闭时不用先清空 `VirtualBlock`。
- **待回收的段仍算已用**。删掉一批物体后立刻加载新的，那几帧里新旧两份同时占着容量，
  定容量时的余量要覆盖它。
- **线程。** `VirtualBlock` 不是线程安全的。归还发生在 `VirtualBlockAllocation` 析构的线程：经过释放队列时是主线程的
  帧结束，分配也在主线程；不经过队列（池已关）时池不再分配。队列自己带锁，同现有的释放队列。
- 没有选"用 fence 判断在途的帧跑完"：更准，能早一两帧归还，但现有释放队列都是数帧，要改就一起改。
- 换掉的三版写法：
  - 池里手写一个"分配句柄 + 帧计数"的列表，带自己的锁与帧计数（这一条最初的写法，步骤 1.2 实现过）。理由是
    "给句柄包一层对象更绕"，结果是在现有释放队列旁边多出一整套平行的东西。
  - 另起一个类同时表示两种分配、一个队列：和 `BufferMemoryView` 表达的是同一件事。
  - `BufferMemoryView` 只带句柄、池里一个手写列表装 view：`BufferPool` 不再用 `ObjectCollector`，
    绕开了而不是解决"句柄进不了现有体系"。

### D5　容量在初始化时定死，放不下就失败　✅ 已定

底层 buffer 在池初始化时建好。放不下时 `InitBuffer` 失败并报错，怎么处理由持有池的系统决定。

- **不退回独立的 buffer**（`TransientResourcePool` 的 `m_allowCommittedFallback` 那种）：用这个池的场景依赖的就是
  "都在一个 buffer 里"，退回去的那个是另一个原生对象，进不了同一次 indirect 调用。
- 报错要分清两种情况（见 D8）：总量不够，还是总空闲够、但没有一段连续的够用。
- 以后要增长，是开第二个底层 buffer 还是搬迁扩容，这次不预设。多于一个之后 indirect 要按它分组，
  那是 I5 的 bucket 做出来之后的事。
- 容量是持有池的系统的配置。几何的容量没有数据：`TODO_GPUDrivenPlan.md` 步骤 3 接入时打一行用量的日志
  （几何系统累加自己申请过的字节数，不需要池的统计查询），用户加载最大的场景跑一次，按实际用量的两倍左右取。

### D6　池由使用它的系统持有；放进去的 buffer 要和池的共享模式、绑定标志一致　✅ 已定

**谁持有。** 这样的池不进 `RHIResourceSystem`，由分配这类资源的系统自己建、自己持有，没有"按标志选池"的规则。

- 仓库里走的本来就是"一类资源放到一起"：`RenderGraph`、`AsyncUploadSystem` 已经各自持有池（§二）。
  `RHIResourceSystem` 是唯一靠标志猜用途的地方，集中起来特殊情况只会越来越多。
- 容量、统计、报错，以及以后的增长与碎片整理，都由知道这些数据是什么的系统来用。
- 持有者的责任：
  - **关闭顺序。** buffer 指向池的是裸指针，池先没了、buffer 还活着就是悬空指针。持有者关闭时先销毁自己建的所有
    buffer 实体，再关池，照 `RenderGraph::Shutdown` 的顺序。所以这些 buffer 实体要由持有者自己建、自己销毁。
  - 建 buffer 的那几行（建对象、设名字、从池初始化、挂组件）在持有者里手写（用户 2026-10-06 定）。
    这一条原来写的是放进 `RHI/ResourceBuilder.h` 一个带池参数的函数、各系统共用；现在只有几何系统一个使用者，不提前抽。
- 每个池有自己的 D3D12MA 分配器，各自向系统要 heap 块。自己持有池适合有一定体量的一类资源；零散的资源仍然走
  `RHIResourceSystem`，它留着，只是不再往里加特殊规则。
- 没有选"`RHIResourceSystem` 加一个这样的池、`SelectBufferPool` 加一条按绑定标志的规则"（这一条原来的写法）：
  隐式，任何标志相符的 buffer 都会落进这个容量固定的池；以后标志一变（比如几何要被 shader 读）规则就要跟着扩。

**一致性。** 底层 buffer 按池的 `BufferPoolDescriptor::m_sharedQueueMask` 创建。`InitBuffer` 时 buffer 的掩码与池的
不一致就报错返回；绑定标志要被池的包含，这条校验现在已经有。

- 两种模式都支持。独占：每一段各自交接所有权（D3）。共享：只靠 fence（`TODO_GPUDrivenPlan.md` 步骤 2）。
- **几何的池用图形独占**（用户 2026-10-06 定）：顶点这类数据本来就该归图形队列，copy 队列只在上传时用一下，
  几次交接换来独占更划算。
- 没有选"这样的池只能是共享模式"（这一条的上一版写法）：依据是"独占要对整个原生对象交接"，这是错的，
  Vulkan 的所有权按区间算（§三）；那样定会替使用方把独占这条路堵死。

### D7　以后 RHI 要暴露"底层 buffer"与"在其中的偏移"　记录，不在本计划内

本计划用不到：CPU 提交路径每个 draw 绑自己的 buffer，后端自动加偏移。`TODO_GPUDrivenPlan.md` 步骤 4 把多个 draw
合成一次 indirect 调用时，需要知道一个 buffer 落在哪个底层 buffer 上、偏移多少，并能绑定整个底层 buffer。
接口留到那一步定。它意味着 `RHI::Buffer` 不再等于一个原生对象。

### D8　池这一版不多暴露任何东西；统计查询有了使用者再加　✅ 已定

- **统计查询先不做**（用户 2026-10-06 定，改掉了这一条原来"暴露统计"的写法）：现在没有使用者。
  - 原来设想的形状留作记录：`BufferPool` 上一个只读查询，返回容量、已用字节、分配个数、最大的一段连续空闲，
    只对 `m_budgetInBytes` 不为 0 的池有意义。`VirtualBlock` 给得出这四个数，VMA 的虚拟分配器给的是同一组。
  - 原来以为的第一个使用者是 `TODO_GPUDrivenPlan.md` 步骤 3 定几何的容量（D5）。那一步定下来不需要它：
    几何系统累加自己申请过的字节数就够。
- **放不下时的报错直接在后端里问 `VirtualBlock`**，不经过 RHI 的接口：日志写出还空多少字节、最大的一段空闲多大，
  空的总量小于申请的是容量不够，大于是不连续（D5）。
- **不暴露分配算法。** 分配策略（省内存 / 省时间 / 尽量靠前）在千这个量级、加载时才分配的数据上看不出差别，后端固定用默认的。
  线性算法（一次全放、栈、环形缓冲）适合"每帧写一批、整批作废"的数据，这次没有这样的使用者，见 §八。
- **不暴露碎片整理。** 见 §八。

---

## 五、步骤

### 1　RHI 与 DX12 后端

1. 池的描述按 D1 打开新行为；初始化时按池的共享模式建底层 buffer 与一个 `VirtualBlock`。
2. `InitBufferInternal`：按 D2 划一段，buffer 的 `MemoryView` 指向底层 buffer 加偏移；`InitBuffer` 按 D6 校验掩码。
3. 释放按 D4：`VirtualBlockAllocation` 与它的释放队列，每帧结束时回收；池关闭时全部还回去。
4. `CommandList::QueueBarrier` 按 D3 的表处理这样的 buffer 的屏障。
5. 放不下时的报错写出空闲总量与最大的一段空闲（D8）。
6. 示例 `IndirectDraw` 加代码里的开关。它本来就自己持有一个 device 池，顶点、索引、参数、count 四个 buffer 都从里面分，
   打开后四个落在同一个原生 buffer 里，`ExecuteIndirect` 的参数与 count 也因此带上非零偏移。
   它现在在图形队列上传、并给这四个 buffer 发"拷贝写入"与之后的读屏障，这是同一队列内带写的屏障，按 D3 会断言。
   开关打开时不发这些屏障：上传在它自己的一次提交里并且等完了才开始绘制（`FlushCommands`），之后是新提交里的首次访问。

### 2　文档

改写 `TODO_GPUDrivenPlan.md` 的 D4、D12 的一句与步骤 3（见 §七），路线图跟着改。那份文档步骤 3 的具体方案
（几何系统的形状、销毁怎么被发现）在那边定，不在这里。

---

## 六、验证

按仓库的分工，画面由用户确认，我只保证能跑、测试通过、validation 无报错。

- **步骤 1**：示例开关两边画面一致；debug layer 无报错。它是"一个 buffer 从不发原生屏障、直接当顶点和索引用"的第一次执行。

**验证不到的**

- **在引擎里的行为**（运行中加载、删除网格，延迟回收，一次提交里往同一个原生 buffer 拷多段，独占路径的屏障被丢弃）
  要到 `TODO_GPUDrivenPlan.md` 步骤 3 接入之后才出现。RHI 示例直接用 RHI，不经过上传系统与渲染图。
- **host 内存的池打开这个行为。** 没有加限制（映射、回读的代码本来就按偏移工作，按代码读是通的），
  但本计划的示例用的是 device 池，host 这条路要等第一个使用者出现才算跑过。
- **Vulkan。** 没有 Vulkan 的 buffer 池；D3 表里 Vulkan 那一列没有代码。
- **延迟回收防住的那个错**（旧网格的帧读到新网格的数据）是偶发的，没有它也未必看得到；只能验证"到期前不会被再分出去"。
- **GPU-based validation。** 它是编译期常量，Debug 只开 debug layer。

---

## 七、对 `TODO_GPUDrivenPlan.md` 的影响

那份文档已在 2026-10-06 按下面的清单改写（本计划的步骤 2），细节以它的 D4 与步骤 3 为准。清单留作记录，
改写时定下来的两处标在对应的条目里：

- **D4 作废的部分**：渲染层自己持有一个共享 buffer 并在里面分配（现在持有的是池，分配是池的事）；租约；
  区间实体与区间组件；上传系统按区间找目标与基偏移；
  "实体和原生对象一对一"；"就绪跟着租约走"；顶点与索引各一个数组、按元素分配。
- **D4 里"否掉的做法"第二条**（`BufferPool` 在 buffer 对象内再分配）就是本计划。当时的两条理由：
  "把使用场景带进了 `BufferPool`"不成立，在 buffer 内再分配是分配策略；"要把资源状态挪到底层对象上"按本文 D3 不需要挪，这样的 buffer 不发原生屏障。
- **步骤 3 变成"渲染层的几何系统持有这个池、替 Mesh 申请"**（用户 2026-10-06 定的方向）：
  - Mesh 模块只描述引用了哪个网格资源，不碰 RHIContext；`MeshGPUComponent` 删掉，`MeshSystem` 只剩填统计数字。
  - 几何系统扫描带 `MeshComponent` 的世界实体，从自己的池里建 buffer（一个网格仍是两个 `RHI::Buffer`、两个实体，
    `m_alignment` 按 D2 填）、提交上传，把 `RHIHandle` 存在世界实体上一个渲染层的组件里。仓库里同形状的是
    `MaterialOverrideRef` 与 `SyncOverrideMaterials`。
  - `InstanceBindingSystem`、`MeshGeometryComposer` 从读 `MeshGPUComponent` 改成读这个组件。上传系统与
    `DrawItemRouter` 不用改。
  - Mesh 组件或世界实体被销毁时，几何系统怎么发现并释放那两个 buffer：列清单时没定，改写时定了——世界实体上的组件
    把两个 buffer 实体的句柄各包在一个 RAII 对象里（`UniqueRHIHandle`），析构时给实体打 `DeadTag`，之后的回收是现成的。
- **网格 buffer 的掩码改回图形独占**（D6）。那份文档步骤 2 把它改成图形与 copy 两位，是为了让共享路径有使用者；
  改回去后共享路径在引擎本体里暂时没人走，只剩示例 `IndirectDispatch` 打开 `kArgsOnComputeQueue` 时。
- **D12 里"共享 buffer 用共享模式创建"那句作废**，依据的是被 §三 纠正的理解。
- **独占路径的旧问题重新适用于网格**：上传线程发屏障时写 buffer 的状态，渲染图在主线程读。这是那份文档步骤 2 之前
  所有网格一直处于的情况，没有观察到出过事，记在那份文档未决项的 `PendingSync` 一条里。
- **`InstanceIDBuffer` 不动**，仍然走 `RHIResourceSystem`。
- **就绪判断**：图形队列在 GPU 上等每个 buffer 自己的上传 fence，不变。改写时加了一条：compose 要等到上传已经提交
  （buffer 实体没有 `UploadPendingTag`）。在那之前渲染图没有 fence 可等，而划出来的一段可能是刚回收的，
  会把上一个网格的数据画出来（读代码得出，没有观察过）。
- **`g_Geometries`** 挪到步骤 5（第一个读它的是剔除 shader），每一行的偏移来自本文 D7 的接口。

---

## 八、未决

- **一次提交里往同一个原生 buffer 拷多段。** §三 引的那句"连续写必须用屏障刷新"是否适用，读不出确定答案；
  按惯例这是常规用法。`TODO_GPUDrivenPlan.md` 步骤 3 接入时第一次真正出现，用 debug layer 确认。它也是那份文档 D12
  "没有覆盖的"第二条。
- **`RHIResourceSystem` 的其他使用方迁到各自的池。** 是方向，不是任务：`g_Instances` 这类 host buffer、材质贴图等
  以后逐步由分配它们的系统持有自己的池。这次只做几何。
- **容量以后怎么增长，以及碎片整理。** 两件事是同一个问题：一个 buffer 换了位置之后怎么通知。它的偏移已经烘进
  `DrawItem` 里的顶点 / 索引地址、buffer view，以后还有 indirect 记录里的 `firstIndex` / `vertexOffset` 与 `g_Geometries`。
  `VirtualBlock` 没有碎片整理（库的那个功能只在真实分配器一层，搬迁本来也要调用方做）。等放不下的报错显示容量或碎片
  确实成了问题再一起设计。
- **线性分配算法。** `VirtualBlock` 支持，适合每帧整批作废的数据，比如 `AsyncUploadSystem` 的 staging buffer
  （现在是手写的偏移累加）。有了这样的使用者再在池的描述里加算法选项，是纯增量。
- **D7 的接口**，留到 `TODO_GPUDrivenPlan.md` 步骤 4。
- **`BufferMemoryType::Shared` 现在的用法**（表示落在 heap 块里）和它的注释不一致。新行为要用它表示注释里的意思。
  2026-10-06 查过：`BufferMemoryView::GetType()` 没有任何调用方，改它的含义不影响现有代码。
- **非 48 字节步长的网格。** 资产加载器按有没有法线、UV 决定步长，而所有 pass 的输入布局写死为 48 字节那一种，
  步长不同的网格现在就画不对。它和本计划无关（D2 的对齐按各自的步长），但进了同一个底层 buffer 之后仍然画不对，单独记一笔。

---

## 涉及的文件

| 步骤 | 文件 |
|---|---|
| 1 | `RHI/Resource/Buffer/BufferPool.{h,cpp}`（D6 的校验）；DX12 `Resource/Buffer/BufferPool.{h,cpp}`、`BufferMemoryView.{h,cpp}`、新建 `VirtualBlockAllocation.h`、`ReleaseQueue.h`（D4）、`Command/CommandList.cpp`（D3）；一个 RHI 示例 |
| 2 | `Document/TODO_GPUDrivenPlan.md`、`Document/TODO_RenderPipelineRoadmap.md` |
