# 上传资源的交接　实现方案（取代 Static 资源路径）

把"资源上传完、交给使用它的队列"这件事整个归上传系统，渲染图不认识这类资源。
渲染图里的 Static 路径（`StaticImportTag`、挂在资源实体上的 attachment、图开头的等待与屏障、帧末盖 fence）全部删除。

决定都是用户确认过的。§三 标了哪些是查过原文的、哪些是凭记忆的。

---

## 状态

| 步骤 | 内容 | 状态 |
|---|---|---|
| 1 | `RHIEventContext`；上传请求是独立实体，带状态；就绪在 CPU 上判断；读的一方都问就绪 | 完成 |
| 2 | 上传系统把接回的屏障写到请求上，队列持有者在帧开头执行；上传线程录的屏障由请求给出；删除渲染图 Static 路径的屏障与等待 | 完成 |
| 3 | 清理资源上的 attachment、`StaticImportTag`；改名；上传与导入分开，`PendingSync` 收回渲染层（§五） | 完成 |

---

## 一、为什么做

Static 路径是为顶点、纹理这类"数量多、用法固定"的资源开的：绕过渲染图逐 pass 的状态检查，同步点只在图的开头。它的问题：

- **用"边"的组件表达"交接后的状态"。** 资源实体上挂的 `BufferPassAttachment` / `ImagePassAttachment` 只被读了 usage、access、stage
  去拼一个目标状态，它不是 pass 到资源的边。后果是渲染图里凡是遍历 attachment 的地方都要 `Exclude<StaticImportTag>`，有四处。
- **buffer 的同步其实只有 fence。** 共享模式的 buffer 不发屏障；独占模式下池里的段在 DX12 上不发原生屏障。
  真正需要屏障的只有 Vulkan 上独占资源的所有权交接，和 image 的 layout。
- **每帧全量扫描。** 每帧遍历全部静态资源算屏障，稳定之后也是；帧末再遍历一遍给每个静态资源盖 fence。
- **"使用方要记得登记"是一类出过的错。** IBL 那几张图曾经没有挂 attachment，结果既没有状态转换也没有等上传的 fence，
  只是 DX12 上碰巧没出问题。
- **上传线程写资源状态、主线程在读。**

几何已经进了同一个原生 buffer，这类 buffer 的数量在变少；材质纹理不是，每张纹理仍是独立的 image，pass 也不知道材质引用了哪些。

---

## 二、现在的代码（步骤 1、2 之后）

**上传系统**（`RHI/System/AsyncUploadSystem.{h,cpp}`）

- 自己建了一个 copy 队列和一条上传线程。图形、计算队列在渲染图的 `CommandQueueContext` 里，上传系统拿不到。
- `OnFrameBegin`（主线程）：先 `FinishUploads`（fence 完成的请求往下走），再 `SubmitBatch`（收走新请求）。
- 上传线程 `ProcessBatch`：录 image 的拷贝前屏障、从请求的 `m_data` 拷进 staging 并拷到目标、录释放的屏障、提交、signal。
  屏障都是主线程在 `SubmitBatch` 里算好放进 batch 的。

**帧的入口**（`RenderGraph::ExecutePipeline`）

```
m_device->BeginFrame(frameIndex)
FrameEventBus::Broadcast(OnFrameBegin)     // 上传系统在这里；处理函数之间的顺序是任意的
m_commandQueueContext.Begin()
RHI::ExecuteUploadAcquires(...)            // 执行"等接回"的请求
RefreshPerFrameBackings
Build（各 pass 的 Build 函数）
Compile
Execute
```

广播 `OnFrameBegin` 的还有 `SandBox/Program/RHI/` 下三个示例（`HelloTriangle`、`DrawShape`、`IndirectDraw`），它们和测试都不用上传系统。

**谁在上传，谁在读**

全部是图形独占。buffer 的绑定标志都是 `InputAssembly | CopyWrite`，image 都是 `ShaderRead | CopyWrite`。

| 资源 | 建在哪 | 读的一方怎么问就绪 |
|---|---|---|
| 网格的顶点、索引（池里的段） | `MeshGeometrySystem` | 就绪后加 `MeshGeometryReadyTag`，画的一方看它 |
| `InstanceIDBuffer` | `InstanceBindingSystem` | 间接绘制：就绪前 `InstanceSlotCount` 写 0，`InstanceCullingPass` 不写绘制记录。逐个绘制：`MeshGeometryComposer` 没就绪不生成 `GeometrySpec` |
| 材质纹理 | `MaterialTextureSystem` | `MaterialBindingSystem` 的 `ResolveTexIndex` 没就绪返回 `InvalidTextureIndex`，着色器走"没有贴图"的分支 |
| IBL、BRDF LUT、LTC 两张表 | `SceneBindingSystem` | 它自己问 |
| 天空盒立方体 | `SkyboxSystem` | `SkyboxPass` 就绪后才 `p.Import` |
| UI 图标 | `IconManager` | `UIProcessFeature` 看 `PendingUploads` |
| 示例的顶点、索引 | `SandBox/Program/RenderGraph/` 下四个示例 | Build 里就绪了才 `Accepts<SampleDrawTag>()` |
| 示例的贴图（`DrawCube`、`MultiView`） | 同上 | 就绪后才 `p.Import` |

- 这些资源都用 `RHI::CreateBuffer` / `RHI::CreateImage` 创建（网格的段由几何系统直接从池里建），就绪用 `RHI::IsResourceReady` 问。
- 材质记录每帧重新编码，纹理下标随就绪变化不需要额外触发。
- "先上传、再导入"的有三处：天空盒立方体和两个示例的贴图（D12）。pass 作用域里按名字绑定 image 只有 `p.Import` 加
  `ReadImage(...).Bind(...)` 这一条路，没有直接绑视图的接口。`p.Import` 会给还没有 `ImportedTag` 的资源加上它。
- 每一处都是一个资源对象一次传完。

**后端与状态记录**

- DX12 后端用 enhanced barrier。`AttachmentStage::Any` 换算成 `D3D12_BARRIER_SYNC_ALL`。
- 录屏障的地方更新资源记录的状态，**换队列屏障的释放那一半除外**：接回那一半是交接后状态的唯一来源。
  这份记录与原生 API 无关，是给渲染图编译读起点用的。
- 换队列的屏障在 DX12 上，释放只用源一侧，接回只用目标一侧。
  - image：copy 队列上一律是 COMMON，copy 队列不能转 layout。copy 队列上的"未定义"不算丢弃。
  - 独立的 buffer：释放录成"源访问到不可访问"，接回录成"不可访问到目标访问"。所以接回的目标访问要包含之后的全部读法，
    否则 debug layer 会报。这是后端翻译换队列屏障的写法，不是 DX12 的要求。
  - 池里的段、upload / readback 堆上的 buffer：不发原生屏障。
- 普通池和 transient 池里独占内存的 image 创建时的初始 layout 是 COMMON，所以 copy 队列可以直接写新 image。
  transient 池里复用内存的 image 是 UNDEFINED，它们不会被上传。
- image 的状态按子资源记，但每张 image 只有一份表（`ImageSubresourceStates`）：全部一致时折叠成一个状态；
  写局部范围会展开成数组，写完整个扫一遍，又一致了就折叠回去。写不同的子资源也会碰到同一份存储。buffer 的状态每个对象一份。
- `BufferBarrier` 没有偏移和长度，指的是整个 `RHI::Buffer` 对象；池里的段各是一个 `RHI::Buffer`。`ImageBarrier` 有子资源范围。
- Debug 构建开着 debug layer，D3D12 的 error、warning 以 `[D3D12]` 前缀进日志。
- 流送的骨架在 RHI 里：`StreamingImagePool::ExpandImage` / `TrimImage`、`Image::m_residentMipLevel`（只搜到接口，没有读实现）。

---

## 三、原生语义

**查过原文或源码的**

- 独占模式与队列族所有权交接是 Vulkan 1.0 的核心功能，独占是默认模式；规范的"遗留功能"附录不包括它们。
- 交接是两条屏障：源队列上释放，目标队列上接回，两条都要写源、目标队列族。应用要保证释放先于接回执行。
- layout 转换可以搭在交接上：两条写同样的新旧 layout，转换只执行一次。
- 独占资源被另一个队列族不经交接地接手是允许的，但内容变成未定义。所以新资源被 copy 队列第一次写之前不需要交接。
- 共享模式不需要交接。
- `VK_KHR_maintenance9`：启用后 buffer 与线性排布的 image 被别的队列族隐式接手时内容一定保留，显式交接变成可选。交接在放宽，不是被废弃。
- Atom 的 Vulkan `AsyncUploadQueue`：copy 队列上拷贝，并在同一个命令列表里把 image 转到 `SHADER_READ_ONLY_OPTIMAL`；完成靠 fence 加回调。
- Granite 的一条路径：image 建成图形与传输两个队列共享，拷贝与前后两次 layout 转换都在传输队列，图形队列用 semaphore 等。
- 官方教程（dedicated transfer queue）：渲染器在 CPU 上查传输的 timeline semaphore，完成前不使用新纹理。
- D3D12 enhanced barrier 规范（2026-10-12 用工具取回的摘录，不是逐字核对的全文）：
  - **屏障里的同步和访问只在一次 `ExecuteCommandLists` 之内有意义。** 一次提交结束时所有缓存都已落到内存，
    前一次提交里的写在后一次提交用到资源时一定已经完成。跨提交留下来的状态只有 layout。
    规范原话：跨提交的 split barrier"本质上是只有 layout 的屏障"，两侧的访问值"实际上被忽略"。
  - **只含屏障的提交是规范认可的用法**（"JIT latch resource state"），写法是两侧都"不同步、不可访问"。
    `SyncAfter` 写"不同步"的条件是同一次提交里之后不再碰这个资源。
  - **buffer 在一次提交里第一次被访问不需要屏障**；每次提交开始时 buffer 都隐含处于通用状态。
  - **纹理在一次提交里第一次被访问也可以不要屏障**，条件是 layout 与访问方式相容、压缩元数据已初始化。
  - **COMMON layout 下可以直接当着色器资源、拷贝源、拷贝目标用**，不需要转 layout。
  - 按队列区分的 layout 是优化：转到不分队列的 `SHADER_RESOURCE` 意味着之后计算队列也可能读它；
    只用一个图形队列时规范建议用 `DIRECT_QUEUE_…`。
  - copy 队列不支持转 layout 的屏障，它用过的子资源结束时仍在 COMMON。

**凭记忆、没有查原文的**

- 拷贝、所有权交接、layout 转换的单位是"一段"而不是整个资源：buffer 到字节范围，image 到子资源。D6 的多段上传建立在这一条上。
- DX12 的 buffer 屏障只能覆盖整个资源。
- 往深度格式的 image 里拷贝，DX12 有额外限制。

**没有查的**

- `maintenance9` 的设备支持率，以及它是否进了某个核心版本。
- UE 的 Vulkan 后端。

---

## 四、决定

### D1　上传是完整的交接，渲染图不认识这类资源

一次上传是四步，全部归上传系统：

1. copy 队列拷贝并释放（上传线程）。
2. 主线程在帧开始时查 fence，完成了才往下走。
3. 在资源所属的队列上接回，落到 D7 的状态（D5）。
4. 请求结束：目标资源上"还没结束的上传"的计数减一，减到零，读它的一方就能用了（D6）。

"使用方要记得登记 attachment"这一步不存在。

### D2　就绪在 CPU 上判断

上传的 fence 完成之前内容不算就绪，不被绑定和绘制；渲染队列不在 GPU 上等 copy 队列。代价是资源晚一两帧出现。

### D3　保留独占模式

这类资源上传一次、一直使用。独占是 Vulkan 的核心能力，方向是放宽（§三）。

- 现在的 Vulkan 设备上，独占的 buffer 和 image 都要在使用方队列上接回。
- `maintenance9` 普及后 buffer 的那一条可以不发，要不要发由后端按设备能力决定，上层的形状不变。
- image 的 layout 转换不会消失，所以"在使用方队列上的那一步"这个位置必须有。

### D4　`RHIEventContext` 与 `RHIEvent`

一个统一的 Context，实体类型 `RHIEvent`，RHI 层定义，由 `RHIInterface` 持有并压入。现在里面只有一种实体：上传请求。

- 事件本质上是组件：产生方在 Context 里建实体、写上数据；接收方 `GetView` 取出处理。实体只是句柄。
- 不引入 ECS 之外的另一套数据，以后的合并、多线程与访问控制是现成的路。

照此实现要留意的：

- **遍历顺序不等于产生顺序。** 以后放进来的事件若讲先后，要自己带序号。
- **事件里引用别的 Context 的东西，生命周期要自己保证**（D6"目标资源被销毁"）。
- **实体的句柄不要留过它的销毁。** 槽位会被复用，版本号只有 12 位。请求由上传这条线自己销毁，所以不把它的句柄交给外面。

### D5　接回的屏障是请求在"等接回"阶段带的数据，队列持有者在帧开头执行

- **写上**：上传系统在 `OnFrameBegin` 里发现某个请求的 fence 完成，就把接回的屏障加到这个请求实体上
  （`RHI::BufferBarrier` / `RHI::ImageBarrier` 直接当组件），状态从 `UploadingTag` 换成 `UploadAcquirePendingTag`。
  上传系统不需要命令录制器，也不拿任何队列。
  - 屏障里的目标是裸指针。请求上的 `UploadBatch`（带目标的 `Ptr`）留到请求结束，裸指针因此始终有效。
- **执行**：`RHI::ExecuteUploadAcquires(Device&, CommandQueueContext&)`（声明在 `AsyncUploadSystem.h`）。
  取出全部"等接回"的请求，有请求的队列各开一个命令列表，录完、提交，然后请求结束。
  - 它在 RHI 层，队列持有者只调一行。现在的持有者是渲染图，在 `ExecutePipeline` 里 `m_commandQueueContext.Begin()` 之后调。
    自己广播 `OnFrameBegin` 的程序也能调。
- **为什么在编译之前**：这条屏障就是把资源转成可用状态，这时渲染图还没开始运行。屏障在录的时候更新资源状态，
  所以 Build 看到的是就绪的资源，编译读到的是落点状态。
- **不需要 GPU 上的等待**：fence 已经在 CPU 上确认完成，这段命令又排在这一帧该队列的所有提交之前。
- **代价**：有上传完成的那些帧，每个涉及的队列多一次小的提交。
- **依赖的前提**：写上屏障（`OnFrameBegin` 里）与执行之间，别的帧开始处理函数不读这些资源的状态。
  `FrameEventBus` 的处理函数顺序是任意的；现在的几个都不读。调用处的注释写了这条依赖。
- **没有执行方时**：请求停在"等接回"，资源上的计数不会归零，读的一方不会把它当成就绪，目标资源也不释放。
  这是配置错误（用了上传系统却没有人调 `ExecuteUploadAcquires`），没有报错，表现是资源一直不出现。

### D6　一次上传是一个请求实体，状态在请求上；就绪从资源上问

**多段上传要支持。**

- 原生的单位是"一段"而不是"一个资源"（§三）。实际的用法也是分段的：一个大 buffer 里很多网格、纹理流送逐级补 mip、图集里逐个填图标。
- 模型里不假设一个资源同时只有一个上传；每个请求只交接它自己写的范围（D8）；读一段的同时可以上传另一段。
- 没有"一个资源同时只能有一个请求"的限制，也没有拦它的检查。重叠的范围同时上传是使用方的错（D8 末尾的契约）。

**请求是贯穿全程的事件，状态挂在请求实体上。**

请求是 `RHIEventContext` 里的实体，带目标资源、范围、数据指针（`BufferUploadRequest` / `ImageUploadRequest`）。
事件实体上的组件在 `RHI/Component/EventComponent.h`，资源实体上的在 `Component.h`。
三个互斥的状态标记，各方只按标记查自己该处理的那一批：

| 状态 | 标记 | 实体上还有 | 谁把它转到下一个 |
|---|---|---|---|
| 已请求 | `UploadRequestedTag` | — | 上传系统 `SubmitBatch` 收走 |
| 上传中 | `UploadingTag` | `UploadBatch`：这一批的 fence 值、目标对象的 `Ptr` | 上传系统在帧开头发现 fence 完成 |
| 等接回 | `UploadAcquirePendingTag` | 要执行的屏障，`UploadBatch` 留着 | 队列持有者在帧开头执行 |
| 结束 | 实体销毁 | — | — |

- 不需要屏障的（共享模式的 buffer）fence 完成就结束，不经过"等接回"。
- 请求只在上传这条线内部流转：`Request…Upload` 不返回值，调用处只给资源句柄。

**就绪从资源上问：资源实体上挂"还没结束的上传"的计数。**

- 需要知道就绪的是读的一方，不是发请求的一方（§二的表）。功能层发完请求就没事了；读的一方手里只有资源句柄。
- `PendingUploads { m_count }`：发请求时目标资源的计数加一（没有就挂上）；一个请求结束时减一，减到零就摘掉。
- **就绪 = 资源已建好，并且没有 `PendingUploads`**，也就是 `IsResourceReady`。
- 用计数而不是标记：一个资源可以同时有多个请求，而且请求在不同的地方结束（fence 完成时、接回执行完之后、校验不过时），
  计数的规则对它们都一样。
- 它的含义是"这个资源上还有上传没落地"，不是"这个资源不能用"。现在这两件事是一回事，做流送时会分开（D11）。

**失败没有单独的状态。** 校验不过或目标实体在被收走前已销毁：报错，请求直接结束，资源带着没初始化的内容被当成可用。校验不过的情况：

- 队列掩码为空；独占却只有 copy 队列；独占的 buffer 已经在使用；目标是每帧一份的 host 资源；
- image 是共享模式的（D11）；image 请求范围里有已经用过的子资源。

这些都是写代码时的错；出现运行时的失败（显存不够、流送被取消）时再加一个状态。

**目标资源被销毁**：请求上的 `Ptr` 保证上传线程和屏障用到的对象活着；fence 完成时目标实体已经不在，就不写屏障，直接结束。
`Ptr` 放在请求上而不是 batch 里：batch 在上传线程上析构，握着最后一个引用的话资源会在上传线程上销毁。

**数据的生命周期**：`m_data` 要活到这次上传结束，因为上传线程在 batch 提交之后才从它拷进 staging。
现有的使用方给的是资产数据或成员、全局数据，没有逐个核对资产在上传完成前是否一定不被释放。

### D7　接回时的落点是固定的

接回的屏障要写目标状态（范围是请求写的范围，D8）：

| | 接回后停在 |
|---|---|
| image，绑定标志有 `ShaderRead` | 着色器只读，所有阶段 |
| image，没有 `ShaderRead` | 不转 layout，停在拷贝写，等渲染图接手 |
| buffer | 绑定标志允许的全部读，所有阶段；一种都没有就停在拷贝写，等渲染图接手 |

一句话：有可落的只读状态就落过去，没有就原样停着等渲染图。

**为什么 image 是固定的一种。** 上传的资源渲染图不认识（D1），而渲染图不认识的 image 只有一种用法：通过着色器绑定被采样。

- 写、拷贝源、attachment 都得出现在某个 pass 里，pass 要声明它，它就成了导入资源，layout 归渲染图。
- 核过两点：运行时里在图形、计算队列上录命令的只有渲染图执行器（另有上传系统的 copy 队列，和烘焙工具 `EnvironmentBaker`
  自己的录制器，后者没有读）；不经 pass 声明能碰到资源的途径只有只读的 bindless 下标，渲染图之外只有 `MaterialBindingSystem` 在取。
- "着色器只读"是接回时的**落点**，不是所有 image 的归宿：只被采样的停在这里不再变；之后被 pass 声明的，
  渲染图第一次碰它时从这里出发再转一次。
- 没有 `ShaderRead` 的 image 连这个落点都不合法，它一定会被某个 pass 声明。按正常写法不带这个标志的有：只当拷贝源的中转 image、
  shading rate 图。现在都没有。

**为什么 buffer 取全集。** buffer 不经 pass 声明的读法有好几种（绘制项绑顶点流、索引流，着色器绑定，间接参数），没有唯一的一个；
它没有 layout，这些访问位可以并在一起，取全集没有代价。

- 这组位在 Vulkan 上不是关键（接回主要是换所有权）；在 DX12 上因为后端那座桥（§二），之后的读法必须都在里面。
- 实现上没有另写一张"标志对访问"的表：`RHI::GetReadOnlyAccess` 把算作只读的几种访问逐个拿去问已有的 `IsBufferAccessSupported`，
  通过的并起来。

**边界**：上传过、没有导入渲染图的资源只能按上表的落点用。拿去写、通过可写绑定读、当 attachment，都不会有屏障，
DX12 的 debug layer 会报，Vulkan 上是错的 layout。没有检查拦着：上传系统看不到资源之后被怎么用。

### D8　上传线程录的屏障由请求给出，范围是请求写的范围

主线程在 `SubmitBatch` 里算好屏障放进 batch，上传线程只管录。上传线程不读任何资源的状态。

| 屏障 | 谁录 | 源 | 目标 |
|---|---|---|---|
| image 拷贝前 | 上传线程 | 未定义，copy 队列 | 拷贝写，copy 队列 |
| 释放（独占的 buffer、image） | 上传线程，拷贝之后 | 拷贝写，copy 队列 | D7 的落点，所属队列 |
| 接回 | 队列持有者，帧开头 | 同上 | 同上 |

- 释放与接回是同一个函数算出的同一条屏障（`MakeBufferHandoff` / `MakeImageHandoff`），调用两次。
  符合"layout 转换搭在交接上、两条参数一致"（§三）。
- **buffer 没有拷贝前屏障**：新资源被 copy 队列第一次写之前不需要交接（§三）。共享模式的 buffer 连释放也没有。
  独立的 buffer 因此在 DX12 上是释放屏障声称"从拷贝写出发"、而没有哪条屏障把它转成过拷贝写；debug layer 不报（用户看过日志）。
- **image 拷贝前那条从"未定义"出发**，是 copy 队列内的转换。它说的是严格后端上的事实：Vulkan 上新 image 的 layout 是未定义。
  DX12 上 copy 队列不能转 layout，后端把 copy 队列上的"未定义"当作没有东西可丢（§二）。
- **只接受新的子资源**：`SubmitBatch` 按请求的范围看状态，有任何一段用过就拒绝。
- **范围**：image 是请求的子资源范围；buffer 在 `BufferBarrier` 有范围之前是整个 `RHI::Buffer` 对象（D11）。
- **上传线程写资源状态只剩一处**：image 拷贝前那条屏障（它不是释放）。
  - 会出事的情况：同一张 image 上有两个时间上重叠的请求。主线程在帧开头执行前一个的接回，上传线程同时在为后一个录拷贝前屏障，
    两边写的子资源不重叠，但撞的是它们共用的那份状态表（§二）。
  - 现在没有：每张 image 一个请求时，主线程只在收走之前和 fence 完成之后碰它的状态。
  - 第一个对同一张 image 发多个请求的使用者（流送）出现时，给 image 的状态表加锁：写在 `Image::SetResourceState`，
    读在 `GetResourceState` 与取状态表的访问函数，后者现在返回引用，要改成锁内拷一份。

仍由使用方保证的契约（`TODO_GPUDrivenPlan.md` D12）：同一时刻只有一个队列写；读的字节与正在写的字节不相交；
写完的字节等 fence 之后才读。D6 的计数是第三条的落点。

### D9　读的一方都问就绪

就绪改成 CPU 判断后，读上传资源的每一处都要问 `IsResourceReady`，否则会读到没传完的数据。各处怎么问见 §二的表。

`InstanceIDBuffer` 的就绪放在 `InstanceSlotCount` 上（就绪前写 0），所以 `InstanceSlotCount` 除了"`g_Instances` 用到哪"之外还带着"ID buffer 能不能读"。
`RenderScope::DrawIndirect` 另有一道"几何 buffer 与 ID buffer 都存在才画"的判断，它只看存在，不看就绪。

### D10　`CreateStaticImage` / `CreateStaticBuffer` 改名为 `CreateImage` / `CreateBuffer`

`StaticImportTag` 删掉后"Static"不对应任何机制，这个函数也不再加任何标记：它就是默认的那种，要渲染图管的才叫 `CreateImported…`。

- 同名的：`Factory::CreateBuffer()` 是成员函数、不带 Context、返回 `Ptr`；pass 作用域里的 `p.CreateImage` / `p.CreateBuffer`
  建的是 transient 资源。调用处都看得出区别。
- `MeshGeometrySystem` 里原来叫 `CreateBuffer` 的私有函数改成了 `CreateGeometryBuffer`。

`CreateImportedImage` 含义不变：渲染图跟踪它的状态，pass 要声明对它的访问。

### D11　不做的

- **`BufferBarrier` 的偏移与长度。** 定了要加，不在这份方案里。
  - 在加之前，一个 buffer 请求交接的是整个 `RHI::Buffer` 对象。现有的使用方都是一个对象一次传完；
    同一个独占的 `RHI::Buffer` 对象上有两个时间上重叠的请求时会整块交接两次，现在没有这样的使用者。
  - 它不只是加两个字段：buffer 记录的状态每个对象一份，屏障有了范围之后"接回时把状态写成什么"要一起定。
    真正用到范围的是 Vulkan 后端，现在验证不了。
- **纹理流送本身。** 模型支持同一张 image 上多个请求各管一段子资源（D6）；没做的是流送这个功能。做的时候要带上：
  - D8 说的那把锁。
  - 读流送纹理的一方改成问流送的状态（可读的 mip 级数），不能再问 `PendingUploads`：补 mip 期间它不为零，但纹理是可用的。
- **原地覆盖正在被读的内容**（再次上传）。独占资源现在不允许（报错）。推演过怎么做，没有写代码验证：
  - 使用方先停用；请求在上传之前多一个"等释放"的状态，带一条释放的屏障，队列持有者在帧开头执行并 signal，
    上传线程等到它再拷贝。这条释放排在该队列此前所有提交的后面，等到它就等于等到旧的使用全部跑完。
  - 要加的：那个状态、执行方把 signal 的 fence 值写回请求、放开"独占且已在使用就报错"、使用方的就绪标记能撤回。
  - 这期间内容不可用，画面上会消失几帧。这类需求通常是上传到没用过的范围，或换一个新资源。
- **共享模式 image 的上传。** 现在拒绝（报错并丢弃请求）。共享模式的 buffer 不受影响：它上传完没有任何后续操作，
  fence 完成就对所有队列就绪。
  - `ImageDescriptor::m_sharedQueueMask` 的默认值是 `All`，所以建 image 时没设这个字段就是共享的，上传会被拒。
    图标原来就是这样（`IconManager` 没设），步骤 2 之后图标消失，改成显式设 `Graphics`。默认值不改：以后会支持共享。
  - 共享的 image 不用换所有权，但 layout 还是要转一次。做法：和独占一样由图形队列在帧开头转，只是屏障的源、目标队列相同。
  - 要加的两样，都是独占那条路白得、共享没有的：
    - **等执行完才算就绪。** 独占的屏障一提交请求就能结束，之后只有同一个队列用它。共享的别的队列也用，而队列并行：
      `CommandQueueContext::End` 等的是若干帧之前的 fence，图形队列可以落后最多 `m_frameCountMax - 1` 帧，
      计算队列上的读可能先于那条屏障执行。所以屏障后面要 signal 一个 fence、把值记到请求上，CPU 查到完成才结束。
      和上面"原地覆盖"是同一个机制。
    - **DX12 上落到不分队列的 layout。** 后端现在按队列选 `DIRECT_QUEUE_SHADER_RESOURCE` / `COMPUTE_QUEUE_SHADER_RESOURCE`，
      图形队列转过去计算队列用不了。
- **DX12 后端对 buffer 的换队列屏障一律不发原生屏障**，像对池里的段那样。那样接回的目标访问在 DX12 上不起作用。
  规范支持这样做（§三：buffer 在一次提交里第一次被访问不需要屏障）。随"拆开"一起做（§七）。
- **pass 声明读大 buffer。** 渲染图以后若要写几何 buffer（GPU 蒙皮、压缩整理），它必须变成 pass 声明的导入资源，那时是必需的。

### D12　上传与导入分开；`PendingSync` 收回渲染层

- **规则**：目标已经带 `ImportedTag` 的上传请求被拒绝（`SubmitBatch` 里报错，请求直接结束）。上传系统不读 `PendingSync`。
  - 一个资源同时被上传、又被渲染图跟踪，就有两个系统在写它的状态。
  - 拒绝放在收走的时候而不是发请求的时候：资源可能在发请求之后才被导入。
- **`PendingSync` 只在渲染图内部**（`Render::PendingSync`，`Pass/Component/RHIComponents.h`）：帧末给被 pass 碰过的导入资源盖，
  之后某一帧别的队列接手它时等。
- **要被 pass 声明、又要上传内容的资源，先上传再导入**：天空盒立方体、`DrawCube` 与 `MultiView` 的贴图。
  用 `CreateImage` 创建（创建时不带 `ImportedTag`），就绪后 pass 才 `p.Import` 再 `ReadImage(...).Bind(...)`，
  `ImportedTag` 由 `p.Import` 在那时加上。
  - 请求在 pass 导入之前已经结束，两个系统先后写状态，不同时。
  - 导入之后它是渲染图跟踪的资源，不能再上传。
  - 渲染图第一次碰它时，状态是 D7 的"所有阶段只读"，会多一条同队列、只读到只读的屏障，一次。

---

## 五、步骤 3 做了什么

- 删除了 `CreateStaticBufferAttachment` / `CreateStaticImageAttachment` 及全部调用处、`StaticImportTag`、四处 `Exclude<StaticImportTag>`。
  渲染图里已经没有任何 Static 路径的东西。
- `IsResourceReady` 挪到了 RHI 层（`RHI/ResourceBuilder.h`）。
- 改名（D10）。
- 上传与导入分开（D12）。
- `TODO_GPUDrivenPlan.md`、`TODO_BufferPoolPlan.md` 里描述旧的独占路径、静态 attachment、`PendingSync` 的段落各加了一句说明，原文没有改。

---

## 六、验证

分工：我保证编译和单元测试通过；画面和日志由用户看。编辑器要从仓库根目录启动，并打开一个存好的场景。

- 三个步骤的画面与日志用户都确认过。
- **已知的一条告警**：`[D3D12] #1356 … have recorded only Barrier commands`，有上传完成的帧会出现。它是性能提示，
  说的是只含屏障的提交里屏障不该带同步和访问（§三）。没有单独处理，见 §七"换所有权和改状态拆开"。
- **单元测试**（`Test/Render/UploadRequestTest.cpp`）覆盖：请求建立时的状态与数据、资源上的计数（每个请求加一、最后一个结束才摘、
  目标先没了也能结束）。收走、校验（包括"往已导入的资源上传被拒"）、fence 完成、执行接回要有真的 copy 队列，单元测试测不了。

**验证不到的**

- 独占资源的所有权交接在 DX12 上没有对应物。这份方案在 Vulkan 规则下是否正确，只能靠读代码与 §三 对照，跑不出来。
- 同一个资源上多个请求、读一段传一段，现在没有使用者，跑不到。
- 校验失败、目标先于请求销毁这几条分支，正常运行走不到。

---

## 七、留着的缺口

各自的决定里写了，这里汇总：

- 同一张 image 多个请求时状态表要加锁（D8）。
- 做流送时，读流送纹理的一方不能再问 `PendingUploads`（D11）。
- `BufferBarrier` 没有范围（D11）。
- 上传过、没有导入的资源被拿去按落点之外的方式用，没有检查（D7）。
- 共享模式 image 的上传不支持（D11）。
- 所属队列是计算队列、绑定标志里却有 `InputAssembly` 或 `Constant` 的 buffer，落点里有计算队列不支持的访问，DX12 后端会断言。
  这种描述本身是矛盾的，现在没有，没有为它加处理。
- `RHIEventContext` 里以后还放什么没有定。回读、延迟释放的通知这类"一方产生、另一方过一会处理"的东西是候选。

**换所有权和改状态拆开**（设计在 `TODO_QueueTransferPlan.md`）

现在的交接屏障一条里装了三样：从哪个队列到哪个队列、现在是什么状态、以后怎么用。第三样引出了三处麻烦：

- D7 的落点牵动上传线程：释放要和接回一样，提交 batch 时就得知道"以后是什么状态"。
- DX12 上 buffer 接回要声明之后的全部读法。规范说 buffer 跨提交根本不需要屏障（§三），那座桥是后端自己加的。
- DX12 的那条告警：同步和访问放在了没有任何使用的提交里。

两个 API 跨提交要交接的东西其实只有：D3D12 是 layout（纹理），Vulkan 是所有权加 layout。访问方式和阶段在两边都不是交接的内容。
拆开后交接只带所有权和 layout，改状态是同一个队列上的普通屏障，一次上传是"做哪几步"的组合（共享 image 就是只做后一步）。
它不只动上传系统：`BufferBarrier` / `ImageBarrier` 上"源、目标队列不同就是交接"的约定渲染图处理跨队列资源时也在用。
另外 D3D12 上采样用的纹理停在 COMMON 就能用，连 layout 都可以不转；按队列区分的 layout 是性能上的取舍。

---

## 参考

- [Queue Family Ownership: The Handshake（Vulkan Documentation Project）](https://docs.vulkan.org/tutorial/latest/Synchronization/Pipeline_Barriers_Transitions/03_queue_family_ownership.html)
- [Non-Blocking Data Uploads: Utilizing the Dedicated Transfer Queue](https://docs.vulkan.org/tutorial/latest/Synchronization/Transfer_Queues_Streaming/02_non_blocking_uploads.html)
- [VkImageMemoryBarrier2 refpage](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageMemoryBarrier2.html)、[VkSharingMode refpage](https://registry.khronos.org/vulkan/specs/latest/man/html/VkSharingMode.html)
- [Legacy and Superseded Functionality（规范附录）](https://docs.vulkan.org/spec/latest/appendices/legacy.html)
- [VK_KHR_maintenance9 refpage](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_maintenance9.html)、[proposal](https://github.khronos.org/Vulkan-Site/features/latest/features/proposals/VK_KHR_maintenance9.html)
- [O3DE Atom Vulkan AsyncUploadQueue.cpp](https://raw.githubusercontent.com/o3de/o3de/development/Gems/Atom/RHI/Vulkan/Code/Source/RHI/AsyncUploadQueue.cpp)
- [Granite vulkan/device.cpp](https://raw.githubusercontent.com/Themaister/Granite/master/vulkan/device.cpp)
- 仓库内：`TODO_GPUDrivenPlan.md`（步骤 2、D12、§六 `PendingSync`）、`TODO_BufferPoolPlan.md`（D3、D6、§三）
