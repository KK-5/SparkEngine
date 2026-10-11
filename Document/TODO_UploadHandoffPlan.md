# 上传资源的交接　实现方案（取代 Static 资源路径）

把"资源上传完、交给使用它的队列"这件事整个归上传系统，渲染图不再认识这类资源。
渲染图里的 Static 路径（`StaticImportTag`、挂在资源实体上的 attachment、图开头的等待与屏障、帧末盖 fence）全部删除。

方向由用户在 2026-10-10 提出并逐条讨论；2026-10-11 用户读完后又定了几处（当时待确认的四处、改名、`PendingSync` 的处理）。
2026-10-12 用户要求抛开已有的方案和限制、只看最终目标重新想了一遍上传的模型，D5、D6、D8 因此重写：
一次上传是一个贯穿全程的请求实体，状态在请求上，同一个资源可以有多个请求各管一段。
标 ✅ 的决定是用户确认过的，标"待确认"的是我在写文档时补的，没有讨论过。
现状一节来自 2026-10-10 读代码，2026-10-11、12 对着代码又核了几处；原生语义一节标了哪些是查的原文、哪些不是。
实现从 2026-10-11 开始，进度见下表。

---

## 状态

| 步骤 | 内容 | 依赖 | 状态 |
|---|---|---|---|
| 1 | `RHIEventContext`；上传请求改成独立实体，带状态；就绪改成 CPU 判断；补上现在靠 GPU 等待兜底的三处使用方 | — | 进行中：`RHIEventContext`、请求实体与两个状态、资源上的 `PendingUploads` 已写并跑过，用户在编辑器里确认过画面（§六）；D9 的三处还没补，用户定先不做（D9 写了这期间的状况） |
| 2 | 上传系统把接回的屏障写到请求上，队列持有者在帧开头执行；上传线程录的屏障由请求给出；删除渲染图 Static 路径的屏障与等待 | 1 | 未开始 |
| 3 | 清理资源上的 attachment、`StaticImportTag`；改名；上传与导入分开，`PendingSync` 收回渲染层 | 2 | 未开始 |

---

## 一、为什么做

Static 路径当初是为顶点、纹理这类"数量多、用法固定"的资源开的：绕过渲染图逐 pass 的状态检查，同步点只在图的开头。
用户 2026-10-10 回头看，指出两点不足，读代码后又多出三点。

- **用"边"的组件表达"交接后的状态"**（用户指出）。资源实体上挂的 `BufferPassAttachment` / `ImagePassAttachment`
  只被读了 usage、access、stage 三个量去拼一个目标状态，它不是 pass 到资源的边。后果是渲染图里凡是遍历 attachment 的地方
  都要 `Exclude<StaticImportTag>`，现在有四处。
- **buffer 的同步其实只有 fence**（用户指出）。共享模式的 buffer 步骤 2（`TODO_GPUDrivenPlan.md`）起就不发屏障；
  独占模式的 buffer 在 DX12 上三条换队列的屏障全部被后端丢弃。真正需要屏障的只有 Vulkan 上独占资源的所有权交接，和 image 的 layout。
- **每帧全量扫描**。`CompileStaticResourceBarriers` 每帧遍历全部静态资源，稳定之后也是；帧末再遍历一遍，
  给每个静态资源盖 fence（因为不知道哪些被用了，只能全盖）。
- **"使用方要记得登记"是一类已经出过的错**。`SceneBindingSystem.cpp` 的注释记着：IBL 那几张图曾经没有挂 attachment，
  结果既没有状态转换也没有等上传的 fence，只是因为 DX12 会把 copy 队列用过的资源回落成 COMMON、首次读时再提升才没出问题，
  Vulkan 两样都没有。
- **上传线程写资源状态、主线程在读**（`TODO_GPUDrivenPlan.md` §六 `PendingSync` 一条里记过，没有观察到出事）。
  独占路径上，上传线程录屏障时更新资源的状态，渲染图在主线程编译时读它。

用户还指出一个趋势：几何已经进了同一个原生 buffer，这类资源的数量在变少，让 pass 自己声明并不难。
这一条对 buffer 成立；对材质纹理不成立——每张纹理仍是独立的 image，pass 也不知道材质引用了哪些。
"pass 声明读大 buffer"这次不做（D11）。

---

## 二、现状（2026-10-10 读代码，2026-10-11、12 补）

说的是这份方案动手之前的代码。

**Static 路径做的四件事**

1. 就绪判断：`IsResourceReady`（`Render/RenderGraph/RenderGraphUtils.h`），资源已建好且没有 `UploadPendingTag`。
2. 等上传的 fence：`CompileStaticResourceBarriers` 把资源身上的 `PendingSync` 收进每个队列的表，执行器在图开头 `queue.Wait`。
3. 一条一次性的屏障：从资源当前状态转到 attachment 算出的目标状态，同样在图开头录。
4. 帧末给全部静态资源盖本队列的 fence（`RenderGraph.cpp`），供上传系统判断"还在用，不能再次上传"。

第 2、3 件对每个静态资源每帧都做，不管这一帧有没有人用它；要不要接回是从资源的状态判断的（状态里的队列不是所属队列），不是从 `PendingSync`。

**上传系统**（`RHI/System/AsyncUploadSystem.{h,cpp}`）

- 上传请求是资源实体上的组件：`PendingBufferUpload` / `PendingImageUpload` 加 `UploadPendingTag`。
  一个实体同一时间只能有一个请求——这是"组件挂在资源上"这种写法的副作用，不是谁定的规则。
- `OnFrameBegin`（主线程）→ `SubmitBatch`：把请求收成一个 batch，给资源盖 `PendingSync{上传 fence, 值}`，去掉 `UploadPendingTag`。
  - 目标带着还没完成的 `PendingSync` 时这一帧不收，留到下一帧。这是上传系统读 `PendingSync` 的唯一一处。
    它排在"独占且已在使用就报错"的校验后面。
- 上传线程 `ProcessBatch`：录接手的屏障（`ConvertToCopyWrite` 读资源状态）、拷贝、录释放的屏障、提交、signal。
  共享模式的 buffer 两条屏障都不录。
  - 接手那条的源队列取自资源记录的状态，新资源是图形队列，所以它录成了"图形到 copy 的接回"，没有与之配对的释放。
  - 上传线程从请求的 `m_data` 拷进 staging 也在这里，在 `SubmitBatch` 之后。
- **image 的屏障都是整张的**：请求带着子资源范围（`m_range`），但释放那条屏障没有填它，拷贝前那条用的是
  "整张 image 必须处于同一状态"的辅助函数；`SubmitBatch` 校验读的也是整张的状态，各子资源不一致时这个读法会断言。
- 它自己建了一个 copy 队列。**图形、计算队列在渲染图的 `CommandQueueContext` 里**，上传系统拿不到。

**谁在用**

| 资源 | 建在哪 | 共享模式 | 就绪判断 |
|---|---|---|---|
| 网格的顶点、索引（池里的段） | `MeshGeometrySystem` | 图形独占 | 有，`MeshGeometryReadyTag` |
| `InstanceIDBuffer` | `InstanceBindingSystem` | 图形独占 | **没有**，靠图开头的等待 |
| 材质纹理 | `MaterialTextureSystem` 建，`MaterialBindingSystem` 登记 attachment | 图形独占 | **没有**：image 一建好就把 bindless 下标写进材质记录 |
| IBL、BRDF LUT、LTC 两张表 | `SceneBindingSystem` | 图形独占 | 有 |
| 天空盒立方体 | `SkyboxSystem`，是导入资源（`ImportedTag`），`SkyboxPass` 声明 | 图形独占 | 有 |
| UI 图标 | `IconManager` | 图形独占 | 有，但直接读 `PendingSync`（`UIProcessFeature.cpp`） |
| 示例的顶点、索引 | `SandBox/Program/RenderGraph/` 下 `DrawCube`、`MultiView`、`TrianglePassFeature`、`MSAAPassFeature` | 图形独占 | 没有 |
| 示例的贴图（`DrawCube`、`MultiView` 的 `m_baseColor`） | 同上，是导入资源，pass 声明 | 图形独占 | 有 |

每一处都是一个资源对象一次传完。绘制读的是整个原生 buffer（用 `firstIndex`、`vertexOffset` 定位到某一段），
所以"一段在传、一段在读"今天就存在，靠的是每一段有自己的 `RHI::Buffer` 和实体。

材质记录每帧重新编码（`GlobalBuffer::UpdateMirror` 对每个持有槽位的实体都调一次），所以纹理下标随就绪变化不需要额外触发。

"上传且导入"的资源有三处：天空盒立方体和两个示例的贴图。它们是导入资源，是因为 pass 用 `p.Import` 加 `ReadImage(...).Bind(...)` 绑定；
pass 作用域里按名字绑定 image 只有这一条路，没有直接绑一个视图的接口（`Render/RenderGraph/PassScopes.h`）。
`p.Import` 会给还没有 `ImportedTag` 的资源加上它（`RenderGraphBuilder::ImportResource`）。

引擎里上传 image 的四个文件（材质纹理、`SkyboxSystem`、`SceneBindingSystem`、图标）给的绑定标志都是 `ShaderRead | CopyWrite`。

**帧的入口**（`RenderGraph::ExecutePipeline`）

```
m_device->BeginFrame(frameIndex)
FrameEventBus::Broadcast(OnFrameBegin)     // 上传系统的 SubmitBatch 在这里；处理函数之间的顺序是任意的
m_commandQueueContext.Begin()
RefreshPerFrameBackings
Build（各 pass 的 Build 函数）
Compile
Execute
```

广播 `OnFrameBegin` 的除了这里，还有 `SandBox/Program/RHI/` 下三个示例（`HelloTriangle`、`DrawShape`、`IndirectDraw`）；
它们和测试都不用上传系统。pass 的 Build 在 `OnFrameBegin` 之后，`SkyboxPass` 就是在 Build 里判断就绪的。

**后端与状态记录**

- DX12 后端用的是 enhanced barrier。`AttachmentStage::Any` 换算成 `D3D12_BARRIER_SYNC_ALL`，
  `AccessFlags::ShaderSampledRead` 换算成 `ACCESS_SHADER_RESOURCE`，所以"所有着色器阶段可读"是可以表达的（D7 用到）。
- 录屏障的地方更新资源的状态（`DX12/Command/CommandList.cpp` 里 `SetResourceState`），**换队列屏障的释放那一半除外**：
  它不更新，接回那一半是交接后状态的唯一来源。上传线程录的拷贝前那条不是释放，所以会在上传线程上更新状态。
  这次记账与原生 API 无关（两个 API 都不记录资源状态），是给渲染图下一帧编译读起点用的。
- 换队列的屏障在 DX12 上，释放只用源一侧，接回只用目标一侧；copy 队列上的 image 一律是 COMMON（`ConvertBarrierLayout`）。
  copy 队列上带"未定义"的 image 屏障会触发断言（copy 队列不能转 layout）。
- **image 的状态按子资源记，但每张 image 只有一份表**（`RHI/Resource/Image/ImageSubresourceStates.cpp`）：全部一致时折叠成一个状态；
  写局部范围会先展开成每个子资源一项的数组，写完把整个数组扫一遍，又全部一致了就清空折叠回去。
  所以写不同的子资源也会碰到同一份存储。buffer 的状态每个对象一份。
- `BufferBarrier` 没有偏移和长度，指的是整个 `RHI::Buffer` 对象；池里的段各是一个 `RHI::Buffer`，所以段可以单独表达。
  段是共享原生资源的一部分，DX12 上不发原生屏障。`ImageBarrier` 有子资源范围。
- 流送的骨架已经在 RHI 里：`StreamingImagePool::ExpandImage` / `TrimImage`、`Image::m_residentMipLevel`
  （2026-10-12 只搜到这些接口，没有读它实现到哪、有没有人用）。按这个模型，一张 image 是在可读的同时继续往里补更高的 mip。

---

## 三、原生语义

**2026-10-10 查的原文或源码**

- 独占模式与队列族所有权交接是 Vulkan 1.0 的核心功能，独占是默认模式。规范的"遗留功能"附录列了 render pass 对象等十二项，
  **不包括**共享模式与所有权交接。附录对"遗留"的定义是仍然可用、不建议新代码使用。
- 交接是两条屏障：源队列上释放，目标队列上接回，两条都要写源、目标队列族。应用要保证释放先于接回执行。
- **layout 转换可以搭在交接上**：两条写同样的新旧 layout，转换只执行一次（`VkImageMemoryBarrier2` refpage）。
- 独占资源被另一个队列族不经交接地接手是允许的，但内容变成未定义。所以**新资源被 copy 队列第一次写之前不需要交接**。
- 共享模式不需要交接；官方教程说对 buffer 的性能影响可以忽略，对 image 在某些硬件上比独占慢。
- `VK_KHR_maintenance9`：启用后 buffer 与线性排布的 image 被别的队列族隐式接手时内容一定保留，显式交接变成可选；
  optimal 排布的 image 由每个队列族的 `optimalImageTransferToQueueFamilies` 说明哪些队列族可以隐式接手。
  即交接在放宽，不是被废弃。
- Atom 的 Vulkan `AsyncUploadQueue`：copy 队列上拷贝，并在同一个命令列表里把 image 转到 `SHADER_READ_ONLY_OPTIMAL`，
  屏障的队列族写 `IGNORED`（不交接），记下"归 copy 队列"；完成靠 fence 加回调，回调里才把 image 的 layout 记成可读。
- Granite 的一条路径（`unmap_linear_host_image_and_sync`）：image 建成图形与传输两个队列共享，
  拷贝与前后两次 layout 转换都在传输队列，图形队列用 semaphore 等。
- 官方教程（dedicated transfer queue）：渲染器在 CPU 上查传输的 timeline semaphore，完成前不使用新纹理。

**凭记忆、没有查原文的**

- 拷贝、所有权交接、layout 转换的单位是"一段"而不是整个资源：buffer 的屏障带偏移与长度、所有权按范围算；
  image 的屏障带子资源范围，layout 与所有权按子资源算。D6"支持多段上传"建立在这一条上（2026-10-12）。
- DX12 的 buffer 屏障只能覆盖整个资源；image 的 layout 按子资源算。

**没有查的**

- `maintenance9` 的设备支持率，以及它是否进了某个核心版本。
- Atom 里所有权交接那一半具体由谁、在哪发。
- Granite 创建普通纹理的主路径。
- UE 的 Vulkan 后端。我的记忆是在图形队列上用专门的上传命令缓冲，没有出处。
- DX12 上非同时访问的纹理从 copy 队列回来后的隐式提升规则。本方案在图形队列上显式转换，不依赖它。

---

## 四、决定

### D1　上传是完整的交接，渲染图不认识这类资源　✅ 已定

一次上传是四步，全部归上传系统：

1. copy 队列拷贝并释放（上传线程）。
2. 主线程在帧开始时查 fence，完成了才往下走。
3. 在资源所属的队列上接回，并转到最终状态（D5）。
4. 请求结束：目标资源上"还没结束的上传"的计数减一，减到零，读它的一方就能用了（D6）。

渲染图里的静态屏障表、图开头的等待、帧末给静态资源盖章、四处 `Exclude<StaticImportTag>` 全部删除。
"使用方要记得登记 attachment"这一步不存在了。

### D2　就绪在 CPU 上判断　✅ 已定

上传的 fence 完成之前内容不算就绪，不被绑定和绘制；渲染队列不再在 GPU 上等 copy 队列。
代价是资源晚一两帧出现，用户 2026-10-10 确认不是问题。

### D3　保留独占模式　✅ 已定

这类资源上传一次、一直使用，用户倾向独占。它是 Vulkan 的核心能力，方向是放宽（§三）。

- 现在的 Vulkan 设备上，独占的 buffer 和 image 都要在使用方队列上接回。
- `maintenance9` 普及后 buffer 的那一条可以不发，和 DX12 后端丢弃它的行为一致。要不要发由后端按设备能力决定，上层的形状不变。
- image 的 layout 转换不会消失，所以"在使用方队列上的那一步"这个位置必须有。

没有选"上传的资源一律共享模式"：它能让 buffer 只剩 fence，但 image 仍要转一次 layout，位置省不掉，还放弃了独占。

### D4　`RHIEventContext` 与 `RHIEvent`　✅ 已定

一个统一的 Context，实体类型 `RHIEvent`，RHI 层定义。现在里面只有一种实体：上传请求（D6），接回的屏障是它带的数据（D5）。

- 用户的思路（2026-10-10）：事件本质上是组件。产生方在 Context 里建实体、写上数据；接收方 `GetView` 取出处理。
  实体在这里只是句柄，相当于向池要一个位置存数据，放在同一个 Context 里不代表是同一类东西；
  有没有某种事件，`GetView` / `GetStorage` 直接能判空。
- 好处是不引入 ECS 之外的另一套数据（专门的事件列表、互相持有对象），并且以后的合并（`ContextMerge`）、多线程与访问控制是现成的路。
- 它由 `RHIInterface` 持有并压入，和 `RHIContext` 一样，只要 RHI 在它就在。
- 我先提的是屏障与上传请求各一个 Context，用"取不取得到 Context"判断有没有人执行屏障。用户定成一个，理由如上。

这个思路本身要留意的（照此实现会出错的）：

- **遍历顺序不等于产生顺序。** 屏障之间互相独立，没有影响；以后放进来的事件若讲先后，要自己带序号。
- **事件里引用别的 Context 的东西，生命周期要自己保证。** 屏障带的是 `RHI::Buffer*` / `RHI::Image*` 裸指针，
  见 D6"目标资源被销毁"。
- **实体的句柄不要留过它的销毁。** 槽位会被复用，版本号只有 12 位（`RHIEvent` 照 `RHIHandle` 定义）：
  槽位被复用 4096 次后旧句柄会重新 `Valid`。请求由上传这条线自己销毁，所以不把它的句柄交给外面（D6）。

### D5　接回的屏障是请求在"等接回"阶段带的数据，队列持有者在帧开头执行　✅ 已定（2026-10-12 改过）

- **写上**：上传系统在 `OnFrameBegin` 里发现某个请求的 fence 完成，就把接回的屏障加到这个请求实体上
  （现有的 `RHI::BufferBarrier` / `RHI::ImageBarrier`，范围取自请求，最终状态按 D7，目标队列它们自己带着），
  状态从 `UploadingTag` 换成 `UploadAcquirePendingTag`。上传系统不需要命令录制器，也不拿任何队列。
  - 2026-10-11 定的是屏障另建一个事件实体、请求当时就销毁。2026-10-12 并进请求：一次上传从头到尾只有一个实体。
- **执行**：队列的持有者（现在是渲染图）在 `ExecutePipeline` 里、`m_commandQueueContext.Begin()` 之后、Build 之前，
  取出全部"等接回"的请求，有请求的队列各开一个命令列表，录完、提交，然后请求结束：目标资源的计数减一，请求销毁（D6）。
  - 做这件事的函数放在 RHI 层，参数是 `RHI::CommandQueueContext`，渲染图只调一行（2026-10-11 定）。
    这样"队列持有者"不等于渲染图，自己广播 `OnFrameBegin` 的程序也能调。
- **为什么在编译之前**（用户 2026-10-10 定）：这条屏障本来就是把资源转成可用状态，这个时候渲染图还没有开始运行，
  算不上破坏它的结构。屏障在录的时候更新资源状态，所以编译读到的已经是最终状态。
  - 我先提的是录在渲染图每个队列的第一个命令列表开头（执行阶段）。那样编译时状态还是旧的，被 pass 声明的导入资源
    （天空盒立方体）会被渲染图自己再生成一条接回；为了躲开它我又提了"屏障执行后的下一帧才就绪"加一轮确认。
    改到编译之前后这些都不需要。
- **不需要 GPU 上的等待**：fence 已经在 CPU 上确认完成，这段命令又排在这一帧该队列的所有提交之前。
- **代价**：有上传完成的那些帧，每个涉及的队列多一次小的提交。
- **依赖的前提**：写上屏障（`OnFrameBegin` 里）与执行之间，别的帧开始处理函数不读这些资源的状态。
  `FrameEventBus` 的处理函数顺序是任意的；现在的几个（`RHIResourceSystem` 等）按我读到的都不读状态。执行的位置要写明这条依赖。
- **没有执行方时**：请求停在"等接回"，资源上的计数不会归零，读的一方不会把它当成就绪。
  上传系统在 `OnFrameBegin` 里发现还有上一帧留下的 `UploadAcquirePendingTag` 就报错：它提示的是配置错误
  （用了上传系统却没有队列持有者来执行），正确性不靠它。现在会广播 `OnFrameBegin` 而不执行的只有三个不用上传系统的 RHI 示例（§二）。
  - 没有选"执行方在 Context 里放一个标记实体，上传系统查它"：多一样东西。

这实际上是把用户先选的"上传系统自己向使用方队列提交"换成了"队列持有者留一个口子"，口子是 Context 而不是互相持有对象。
用户否掉了前一种的实现方式：上传系统要拿 `RenderSystem` 内部的队列，`RenderUI::Bind` 那个先例本身是渲染内部的系统，而且以后会被优化掉。

### D6　一次上传是一个请求实体，状态在请求上；就绪从资源上问　✅ 已定（2026-10-12 重写）

用户 2026-10-12 让我抛开已有的方案和限制，只看最终目标（上传事件化、去掉 Static 路径）回答两个问题：
多段上传该不该支持，就绪怎么判断。下面是定下来的；就绪那一半当天又改过一次，见末尾"走过又放弃的形状"。

**多段上传要支持。**

- 原生的单位是"一段"而不是"一个资源"（§三，凭记忆）：buffer 到字节范围，image 到子资源。一个子资源内部的一小块不能单独交接。
- 实际的用法都是分段的：一个大 buffer 里很多网格、纹理流送逐级补 mip、图集里逐个填图标。
- "支持"的意思：模型里不假设一个资源同时只有一个上传；每个请求只交接它自己写的范围（D8）；读一段的同时可以上传另一段。
- **没有"一个资源同时只能有一个请求"的限制，也没有任何拦它的检查。** 重叠的范围同时上传是使用方的错（D8 末尾的契约）。

**请求是贯穿全程的事件，状态挂在请求实体上。**

请求是 `RHIEventContext` 里的实体，带目标资源、范围、数据指针（`BufferUploadRequest` / `ImageUploadRequest`）。
三个互斥的状态标记，各方只按标记查自己该处理的那一批：

| 状态 | 标记 | 实体上还有 | 谁把它转到下一个 |
|---|---|---|---|
| 已请求 | `UploadRequestedTag` | — | 上传系统 `SubmitBatch` 收走 |
| 上传中 | `UploadingTag` | `UploadBatch`：这一批的 fence 值、目标对象的 `Ptr` | 上传系统在帧开头发现 fence 完成 |
| 等接回 | `UploadAcquirePendingTag` | 要执行的屏障（D5） | 队列持有者在帧开头执行 |
| 结束 | 实体销毁 | — | — |

- 不需要屏障的（共享模式的 buffer）fence 完成就结束，不经过"等接回"。
- **请求只在上传这条线内部流转**：发请求的一方建它，上传系统与队列持有者推进它，走完由它们销毁。
  没有人在外面拿它的句柄，`Request…Upload` 不返回值，调用处只给资源句柄，和这份方案动手之前一样。

**就绪从资源上问：资源实体上挂"还没结束的上传"的计数。**

- **需要知道就绪的是读的一方，不是发请求的一方**（用户 2026-10-12 问出来的）：

  | 内容 | 谁发上传请求 | 谁需要知道它能不能用 |
  |---|---|---|
  | 材质纹理 | `MaterialTextureSystem`（功能层） | `MaterialBindingSystem`，往材质记录里写纹理下标时 |
  | 天空盒立方体、IBL | `SkyboxSystem`（功能层） | `SkyboxPass`、`SceneBindingSystem` |
  | 图标 | `IconManager`（功能层） | `UIProcessFeature`，向 ImGui 登记纹理时 |
  | 三张表 | `SceneBindingSystem` | 它自己 |
  | 网格 | `MeshGeometrySystem` | 画的一方，通过 `MeshGeometryReadyTag` |

  功能层的三个系统发完请求就没事了，它们连每帧的更新都没有。这和 D7 是同一个道理：功能层只产出资源，什么时候能用由渲染层决定。
  读的一方手里只有资源句柄，所以就绪要能从资源句柄问出来。
- **`PendingUploads { m_count }`**：发请求时目标资源的计数加一（没有就挂上）；一个请求结束时减一，减到零就摘掉。
  资源实体已经不在了就不用管，组件随实体一起没了。
- **就绪 = 资源已建好，并且没有 `PendingUploads`**，也就是 `IsResourceReady`。原来在问它的地方不用改。
- **用计数而不是标记**：一个资源可以同时有多个请求，而且请求在不同的地方结束（fence 完成时、接回执行完之后、校验不过时）。
  标记要在摘的时候去查还有没有别的请求；计数的规则对它们都一样，结束就减一。
- **它的含义是"这个资源上还有上传没落地"，不是"这个资源不能用"。** 现在这两件事是一回事。做流送时会分开（D11）：
  补 mip 期间纹理是可用的，读的一方要改成问流送的状态，不能再问这个计数。
- **它是资源实体上唯一挂的东西。** 原来的 `UploadPendingTag`（请求还没被收走）被它取代：计数存在的时间段包住了那个 tag 的时间段。
  Static 路径在它被删掉之前也是通过 `IsResourceReady` 看这个计数，于是它晚到上传完成后才去接回，那时资源的状态仍然记着"在 copy 队列"，
  接回照常发，图开头的等待是空的（2026-10-12 对着 `CompileStaticResourceBarriers` 核过）。

**失败没有单独的状态**（用户 2026-10-12 定）。校验不过（队列掩码为空、独占却只有 copy 队列、独占且已在使用、目标是每帧一份的 host 资源）
或目标实体在被收走前已销毁：报错，请求直接结束。和现在一样，资源带着没初始化的内容被当成可用。
这些都是写代码时的错；出现运行时的失败（显存不够、流送被取消）时再加一个状态。

**目标资源被销毁**：已经在途的，请求上的 `Ptr` 保证上传线程用到的对象活着；fence 完成时目标实体已经不在，就不写屏障，直接结束。
`Ptr` 放在请求上而不是 batch 里：batch 在上传线程上析构，握着最后一个引用的话资源会在上传线程上销毁。

**数据的生命周期**：`m_data` 要活到这次上传结束。上传线程在 `ProcessBatch` 里才从 `m_data` 拷进 staging，
所以"活到被 `SubmitBatch` 收走"是不够的（原来的注释与这份文档 2026-10-10 的写法都是这样写的，2026-10-11 写代码时发现）。
这次不改行为；现有的使用方给的是资产数据或成员、全局数据，我没有逐个核对资产在上传完成前是否一定不被释放。

**走过又放弃的形状**（都在 2026-10-11、12 的讨论里）

- **就绪 = 请求已销毁，使用方问完把句柄置空**（2026-10-11 定过，写进过代码）。上传系统销毁请求，使用方手里的句柄就悬空，
  要靠"问完必须置空"的规则躲开槽位复用。
- **同一个独占资源同时只能有一个在途请求**（2026-10-11 我写成了检查）。它的来源是"原来的组件挂在资源上"和
  "`BufferBarrier` 现在没有范围"，两样都不是原生的要求；后一样已经定了要加（D11）。用户：已经定了要补的东西不该拿来当约束。
- **三个状态标记挂在资源上。** 它是为了守住单请求才提出来的。限制没了，资源上只需要回答"还有没有"，不需要知道走到哪一步。
- **发起方存句柄，取走完成的请求后自己记就绪**（2026-10-12 定过，写进过代码：`TakeUploadDone`，完成的请求留到发起方来取）。
  我当时的理由是"就绪属于上传不属于资源"：资源上的标记是个合计值，补一级 mip 整张纹理就成了未就绪。放弃它的原因：
  - 使用方本来就存着资源句柄，现有的用法里一份内容对应一个资源实体（一张纹理、一张表、网格的一段子 Buffer），
    再存一个请求句柄是同一件事记两遍。
  - 发请求的一方并不关心就绪（上面那张表）。按这个形状，`SkyboxSystem`、`IconManager` 的组件要带着句柄，再由渲染层越过去取。
  - 合计值的问题只落在流送上，而流送真做的时候纹理的创建和读取本来就要重做。为它让现在的每个使用方都多背一个句柄，是把这一种情况的权重放得太高。
  - 它还留下一个缺口：发起方中途没了，请求没人取。现在没有了。
- **完成也加一个标记。** 不需要；现在请求结束就销毁。

### D7　最终状态从资源的绑定标志推出来　✅ 已定

上传后转到"绑定标志里声明的全部只读用途，所有阶段"，由上传系统推，不由任何一方给。范围是请求写的范围（D8）。

- 没有选"作为上传请求的字段"：发请求的是功能层（材质纹理系统等），那等于又让产出方决定用途，
  和"功能层只产出资源，用途由渲染层决定"相反。
- 顺带修正一处：现在材质纹理的目标阶段写死是像素着色器，别的阶段采样它在严格的后端上不对。
- DX12 后端能表达（§二）。
- 2026-10-11 核过：引擎里上传的 image 绑定标志都是 `ShaderRead | CopyWrite`（§二），推出来只有着色器只读一种。
- 留意（我补的）：绑定标志里有两种以上只读用途的 image（比如着色器读加拷贝读），DX12 后端有对应的 layout（GENERIC_READ），
  Vulkan 没有同时满足两者的专用 layout，只能是 GENERAL。这是后端换算的事，规则不变；现在没有这样的资源。

### D8　上传线程录的屏障由请求给出，范围是请求写的范围　✅ 已定（2026-10-12 改过）

- **buffer**（用户 2026-10-10 同意方向）：buffer 没有 layout，屏障对它唯一的含义是"这一段的所有权换队列"，
  这个信息上传请求里就有（哪一段、所属队列）。所以屏障的两端都从请求算，不看 buffer 的当前状态。
  - 新资源被 copy 队列第一次写之前不需要交接（§三），所以 buffer 在上传线程上只剩拷贝之后的释放一条。
  - 释放那一半后端本来就不更新状态（§二）。所以上传线程对 buffer 的状态不读也不写，不需要 RHI 改动。
  - 交接的范围：`BufferBarrier` 有范围之前是整个 `RHI::Buffer` 对象（D11）。
- **image**：上传线程的两条屏障（转成拷贝目标、释放）与主线程执行的接回，两端都由请求给出，范围都是请求的子资源范围；
  接回直接到最终状态。
  - 起点是确定的：只接受请求范围内的子资源是新的。这个检查在主线程的 `SubmitBatch` 里按范围做
    （现在读整张状态的写法在各子资源状态不一致时会断言，§二）。
  - 释放与接回由同一个请求生成，两条写同样的新旧状态，符合"layout 转换搭在交接上、两条参数一致"（§三）。
    现在的写法是释放转到"无"、接回从渲染图读到的状态再转一次，两半并不一致，只是没有 Vulkan 后端所以没有暴露。
  - 拷贝前那条改成 copy 队列内的转换。现在它是"图形到 copy 的接回"而没有配对的释放（§二），在 Vulkan 上不合法。
  - DX12 上成立（2026-10-11 读代码）：释放只用源一侧、接回只用目标一侧，copy 队列上 image 一律是 COMMON，
    所以释放那条带着最终状态也不影响它。
- **上传线程与资源状态**：改完之后上传线程不读任何资源的状态；写只剩一处，image 拷贝前那条屏障（它不是释放）。
  - **什么时候会出事**：同一张 image 上有两个时间上重叠的请求时。主线程在帧开头执行前一个的接回（写这张 image 的状态表），
    上传线程同时在为后一个录拷贝前屏障（也写）。两边写的子资源不重叠，逻辑上互不影响，先写后写结果一样；
    撞的是它们共用的那份表在展开、扫描、折叠（§二）。
  - **现在没有**：每张 image 一个请求时，主线程只在收走之前（校验）和 fence 完成之后（接回）碰它的状态，都不在上传线程录屏障的那段时间里。
    唯一会在中间读它的是 Static 路径，步骤 2 删掉。
  - **到时怎么办**（用户 2026-10-12 定）：第一个对同一张 image 发多个请求的使用者（流送）出现时，给 image 的状态表加锁：
    写在 `Image::SetResourceState`，读在 `GetResourceState` 与取状态表的访问函数，后者现在返回引用，要改成锁内拷一份。
    现在不做——在那之前没有并发访问，加了等于每次状态读写白过一次锁。
  - 没有选"上传系统的命令录制器不更新资源状态"（我提的，用户否掉）：它要给 RHI 加一个开关；场景少，锁就够。
- **步骤 2 要核对的**：
  - image 拷贝前那条的起点写"无"还是"未定义"（待确认，§七）。
  - 独立的 buffer（`InstanceIDBuffer`、示例的顶点与索引）去掉拷贝前屏障后，释放仍写"从拷贝写出发"，
    而没有哪条屏障把它转成过拷贝写。在 debug layer 下看报不报。池里的段不发原生屏障，不受影响。

仍由使用方保证的契约不变（`TODO_GPUDrivenPlan.md` D12）：同一时刻只有一个队列写；读的字节与正在写的字节不相交；
写完的字节等 fence 之后才读。D6 的计数是第三条的落点。

### D9　补上现在靠 GPU 等待兜底的三处　✅ `InstanceIDBuffer` 一条已定，其余是推论

改成 CPU 判断后，这三处不补就会读到没传完的数据：

- **材质纹理**：未就绪时写无效下标（`InvalidTextureIndex`），就绪后写真实下标。材质记录每帧重编码，不需要触发。
- **`InstanceIDBuffer`**：就绪前 `InstanceSlotCount` 报 0，剔除 pass 不写任何记录（用户 2026-10-10 同意）。
- **示例的顶点与索引**：四个示例发完上传就画，没有任何判断。

这三处要补上对 `IsResourceReady` 的判断（D6）。其余使用方已经在问它，不用改。
UI 图标原来直接读资源上的 `PendingSync`，改成看 `PendingUploads`（已改）。

**还没有做**（用户 2026-10-12 定先不做）。2026-10-12 读代码时列过具体要改的地方：

- 材质纹理：`MaterialBindingSystem.cpp` 的 `ResolveTexIndex`，纹理上还有 `PendingUploads` 时返回 `InvalidTextureIndex`。
- `InstanceIDBuffer`：`InstanceBindingSystem.cpp` 里就绪前 `InstanceSlotCount` 报 0。
  另有一处上面没列：`MeshGeometryComposer.cpp` 给逐个绘制的那条路准备数据时也把它当顶点流用，现在只等它建出来、没等它传完，
  要把那里的"还没建好就先返回"扩成"还没就绪就先返回"。
- 四个示例：Build 里顶点、索引 buffer 都就绪了才 `Accepts<SampleDrawTag>()`。

**不补的这段时间里的状况**：Static 路径现在是等上传完成后才去接回（D6），这三处原来靠它在图开头的 GPU 等待兜底，现在没有了。
它们在上传完成之前的那几帧会读到没传完的数据：贴图内容不全、实例号不对、示例画出没传完的顶点。上传完成后自己恢复。
用户 2026-10-12 在编辑器里看过现在这版，没有发现问题。

### D10　`CreateStaticImage` / `CreateStaticBuffer` 改名为 `CreateImage` / `CreateBuffer`　✅ 已定

`StaticImportTag` 删掉后"Static"不对应任何机制，这个函数也不再加任何标记：它就是默认的那种，
要渲染图管的才叫 `CreateImported…`（用户 2026-10-11 定）。

- 重名的两处：`Factory::CreateBuffer()` 是成员函数、不带 Context、返回 `Ptr`，调用处看得出区别；
  `MeshGeometrySystem::CreateBuffer` 是私有函数，改名（改成什么待确认，§七）。
- 没有选的：`CreateReadOnly…`（我先倾向它，用户否掉）；`CreateUploaded…`（上传完的资源也可以被 pass 导入，区分不出来）；
  `CreateUntracked…`（用否定来命名）；`CreatePersistent…`（导入资源也长期存在）；`CreateExternal…`（和"导入"太近）。

与之相对的 `CreateImportedImage` 含义不变：渲染图跟踪它的状态，pass 要声明对它的访问。

### D11　这次不做的

- **`BufferBarrier` 的偏移与长度。已定要加**（用户 2026-10-12：底层的 buffer 可能还是需要这样的屏障），这次不放。
  - 在加之前，一个 buffer 请求交接的是整个 `RHI::Buffer` 对象。现有的使用方都是一个对象一次传完，结果是对的；
    同一个独占的 `RHI::Buffer` 对象上有两个时间上重叠的请求时会整块交接两次，现在没有这样的使用者。
  - 它不只是加两个字段：buffer 记录的状态每个对象一份，屏障有了范围之后"接回时把状态写成什么"要一起定。
    DX12 的 buffer 屏障只能覆盖整个资源，真正用到范围的是 Vulkan 后端，现在验证不了。
- **纹理流送本身。** 模型支持同一张 image 上多个请求各管一段子资源（D6）；没做的是流送这个功能：
  谁决定补哪一级、读的一方怎么只用已经到的 mip。RHI 里有骨架（§二）。做的时候要带上两件事：
  D8 说的那把锁；读流送纹理的一方改成问流送的状态（可读的 mip 级数），不能再问 `PendingUploads`——
  补 mip 期间它不为零，但纹理是可用的（D6）。流送那一方要知道"我这次补的落地没有"：一次只补一批的话看计数归零，否则那时再加按请求问的办法。
- **原地覆盖正在被读的内容**（再次上传）。独占资源现在就不允许（报错）。用户 2026-10-11 问这条路通不通，下面是当时的推演，没有写代码验证：
  - 使用方先停用；上传系统让所属队列先释放（请求在上传之前多一个"等释放"的状态，带一条释放的屏障，队列持有者在帧开头执行并 signal），
    上传线程等到它再拷贝。这条释放排在该队列此前所有提交的后面，等到它就等于等到旧的使用全部跑完，不需要逐资源盖章。
    image 在 DX12 上这一步省不掉：copy 队列只认 COMMON。
  - 要加的：那个状态、执行方把 signal 的 fence 值写回请求、放开"独占且已在使用就报错"、
    使用方的就绪标记能撤回（`MeshGeometryReadyTag` 现在加上就不摘）。
  - 这期间内容不可用，画面上会消失几帧。这是原生限制；这类需求通常是上传到没用过的范围，或换一个新资源。
  - 释放后重新分配的范围（池里回收的段）不属于这一种：延迟释放保证旧的使用跑完了才回到分配器
    （按 `CLAUDE.md` 写的规则说的，没有为此读池的代码）。
  - 渲染图跟踪的资源要更新内容，写成图里的一个拷贝 pass，由渲染图自己排同步（推断，没有对着具体需求验证）。
- **共享模式 image 的上传。** 它在所属队列转完状态的同一帧，别的队列可能先读到。现在没有这种资源，加断言拦住。
- **pass 声明读大 buffer。** 用户定以后再说。渲染图以后若要写几何 buffer（GPU 蒙皮、压缩整理），
  它必须变成 pass 声明的导入资源，那时是必需的。

### D12　上传与导入分开；`PendingSync` 收回渲染层　规则 ✅ 已定，三处资源的改法待确认

- **规则**（用户 2026-10-11 定）：目标已经带 `ImportedTag` 的上传请求被拒绝（报错，请求直接结束，D6）；
  `SubmitBatch` 里"目标带着没完成的 `PendingSync` 就推迟"那段检查删掉，上传系统不再读 `PendingSync`。
  - D1 说上传的资源渲染图不认识。一个资源同时被上传、又被渲染图跟踪，就有两个系统在写它的状态。
  - 那段检查在 Static 路径删掉之后本来也只对导入资源有意义：给普通资源盖章的两处（上传系统自己、帧末全量）都没有了。
    它对现有的资源也从不触发：第一次上传时渲染图还没碰过资源；再次上传的独占资源在它前面的校验就被拒掉。
  - 不能只删检查不加拒绝：共享模式的导入资源再次上传时就没有任何保护。
- **`PendingSync` 此后只在渲染图内部**：帧末给被 pass 碰过的导入资源盖，下一帧换队列时等。
  组件从 `RHI/Component/Component.h` 挪到渲染层；那段"所有系统统一遵守"的协议说明重写
  （它写的"使用方等完后移除"实际没有任何地方做）。
- **现在"上传且导入"的三处怎么改**（待确认，我写文档时查到的）：天空盒立方体、`DrawCube` 与 `MultiView` 的 `m_baseColor`，
  都是 `CreateImportedImage` 加上传。
  - 我 2026-10-11 对用户说的是"只有天空盒立方体一处，改成只读后直接绑视图"，两点都不对：示例里还有两处；
    pass 作用域里没有直接绑视图的接口（§二）。用户同意的是"立方体改成普通资源"这个方向。
  - 打算：三处都改用 `CreateImage` 创建（创建时不带 `ImportedTag`），pass 一侧不动——就绪后照旧 `p.Import`
    再 `ReadImage(...).Bind(...)`，`ImportedTag` 由 `p.Import` 在那时加上。
    - 这是"先上传，交接完再交给渲染图"：请求在 pass 导入之前已经结束，两个系统先后写状态，不同时。
    - 导入之后它是渲染图跟踪的资源，不能再上传（上面的规则）。
    - 渲染图第一次碰它时，状态是 D7 的"所有阶段只读"，会多一条同队列、只读到只读的屏障，一次。
  - `SkyboxSystem` 里 `CreateAndUploadImage` 的 `staticImport` 参数随之去掉。
  - 另一种是给 pass 作用域加一个直接绑视图的接口，资源始终不导入。多一个接口，这次不做。

---

## 五、步骤

步骤只是实现时的切分，要做的事由 §四 定，不随切分改变。要删除的路径不为中间状态做设计：
它和新的做法冲突时，把删除提前（用户 2026-10-11）。当前实现里的缺口，定了要补的，也不当作设计的约束（用户 2026-10-12）。

### 1　请求成为实体，就绪改成 CPU 判断

- 新建 `RHIEventContext` / `RHIEvent`，`RHIInterface` 持有并压入。**已写**（2026-10-11）。
- `RequestBufferUpload` / `RequestImageUpload` 在 `RHIEventContext` 里建请求实体，给目标资源的 `PendingUploads` 加一；
  `SubmitBatch` 把"已请求"的收走，转到"上传中"；fence 完成后请求结束（这一步还没有"等接回"）。
  `IsResourceReady` 看 `PendingUploads`，资源上原来的 `UploadPendingTag` 删掉。
  **已写**（2026-10-11、12 写过两版别的形状，见 D6 末尾；这是第三版）。
- 补 D9 的三处。**还没做**，用户 2026-10-12 定先不做。
- 渲染图的 Static 路径这一步不动，接回仍由它发，只是它通过 `IsResourceReady` 晚到上传完成后才发；上传后盖的 `PendingSync` 照旧。

### 2　上传系统发接回，Static 路径的屏障与等待删除

- 上传线程：buffer 只录释放；image 的两条由请求给出、按请求的范围，拷贝前那条是 copy 队列内的转换（D8）。
- `SubmitBatch` 里 image 的校验按请求的范围看状态（D8）。
- `OnFrameBegin`：还留着上一帧的"等接回"就报错（D5）；fence 完成的请求写上接回的屏障，转到"等接回"；最终状态从绑定标志推（D7）。
- RHI 层加执行"等接回"请求的函数，执行完请求结束；渲染图在 `ExecutePipeline` 里 `Begin()` 之后调它（D5）。
- 删除 `CompileStaticResourceBarriers`、`StaticPreBarrierTable`、执行器开头的那段、帧末给静态资源盖章；
  上传系统不再盖 `PendingSync`。

### 3　清理、改名、上传与导入分开

- `CreateStaticBufferAttachment` / `CreateStaticImageAttachment` 及全部调用处；`StaticImportTag`；四处 `Exclude<StaticImportTag>`。
- `IsResourceReady`（已建好且没有 `PendingUploads`）和渲染图无关，挪到 RHI 层。
- 改名（D10），`MeshGeometrySystem` 的私有函数一并改。
- 上传与导入分开（D12）：三处改用 `CreateImage`；拒绝往带 `ImportedTag` 的目标上传；删掉 `SubmitBatch` 里读 `PendingSync` 的检查；
  `PendingSync` 挪到渲染层并重写说明。
- 同步 `TODO_GPUDrivenPlan.md`（D12、§六 `PendingSync` 一条）、`TODO_BufferPoolPlan.md` 里提到 Static 路径与独占路径三条屏障的地方。

---

## 六、验证

按仓库的分工，画面由用户确认；我保证能跑、测试通过、validation 无报错。编辑器要从仓库根目录启动，并打开一个存好的场景。

- 每一小步：全量 Debug 编译；`SparkRenderTest`。有行为变化的再跑示例或编辑器，只确认能跑、不报错。
- 第 1 步额外看：场景加载的前几帧，没有就绪的纹理显示为没有贴图而不是别的纹理的内容；物体不提前出现。
- 第 2 步额外看：多个纹理同一帧完成时每个队列只多一次提交；debug layer 对独立 buffer 的释放屏障不报错（D8）。
- 第 3 步额外看：天空盒与两个示例的贴图照常显示。
- 可以写单元测试的：请求建立时的状态与数据、资源上的计数（每个请求加一、最后一个结束才摘、目标先没了也能结束）、
  往已导入的资源上传被拒。收走、fence 完成、执行接回要有 copy 队列和 fence，单元测试测不了。
- 四个示例至少编译通过并各跑一次。

**跑过什么**

- 2026-10-11，`RHIEventContext`：全量 Debug 编译通过，`SparkRenderTest` 109 个通过。
- 2026-10-11，请求实体的第一版：全量 Debug 编译通过，`SparkRenderTest` 115 个通过（含新加的 6 个）；
  `DrawCube` 跑 6 秒后关窗，退出码 0，日志只有 info。没有确认这个示例是否开着 debug layer。
  新加的"在途请求"报错与"目标已死"两条分支那次运行没有走到。这一版后来返工了。
- 2026-10-12，按 D6 返工后：全量 Debug 编译通过，`SparkRenderTest` 117 个通过（上传请求的 8 个是重写的）；
  `DrawCube` 跑 6 秒后关窗，退出码 0，日志 10 行全是 info。仍然没有确认这个示例是否开着 debug layer。
  校验失败与"目标已死"的分支没有走到；收走与 fence 完成后转到完成这一段没有单元测试，只由示例能正常运行间接说明。
  这一版后来又返工了。
- 2026-10-12，改成"资源上挂计数、不存句柄"之后（现在的代码）：全量 Debug 编译通过，`SparkRenderTest` 116 个通过
  （上传请求的 7 个是重写的）；`DrawCube`、`MultiView` 各跑 6 秒后关窗，退出码 0，日志各 10 行全是 info。
  - 编辑器跑了一次，**没有覆盖到网格、材质纹理、天空盒**：临时代码在第 30 帧就打开 `Car.scene`，那时模型和天空盒的资产还没加载完，
    72 个网格报"Model asset not ready"，天空盒同样没建。这次运行只说明编辑器能启动、界面的图标与三张表走新的就绪判断没有出事、
    运行 20 秒后关窗退出码 0。要覆盖场景，得等资产加载完再打开，运行要更久，还没有做。
  - 关闭时的 4 条 `There is garbage that wasn't collected` 和一条 `Live ID3D12Device ... Refcount: 5` 是改动之前就有的
    （`TODO_GPUDrivenPlan.md` 里对照过，条数相同）。日志里还有一条 glTF 解析报错，是某个资产缺扩展，和这次改动无关。
  - 临时改的 `Editor.cpp`、`WelcomeScreen.cpp` 当时只还原了源码，二进制没有重新编译（见下一条末尾）。
  - 没有走到的：校验失败、目标先于请求销毁、同一个资源多个请求（只有单元测试覆盖计数）。
- 2026-10-12，用户在编辑器里看到"天空盒一开始没有，换一下才出现"，为此加临时日志跑了两次（都是 `Car.scene`，都已还原）：
  - **资产加载完再开场景**（第 28 秒打开，共 55 秒）：天空盒的三个请求在开场景后约 60 毫秒被收走，再过 64 毫秒 fence 完成、
    计数归零，`SkyboxPass` 看到立方体就绪并导入。整次运行 64 个 image 请求全部收走并结束。日志里没有网格或天空盒的报错。
    画面我没有看，"天空确实画出来了"是从 pass 的代码推的。
  - **资产没加载完就开场景**（第 2 秒打开，共 45 秒）：`SkyboxSystem` 报"Skybox image not ready yet"并跳过，
    之后资产加载完了也没有人再触发它，立方体实体从头到尾没有被建出来（`SkyboxPass` 一直拿不到立方体），所以没有任何上传请求。
    72 个网格同样报"Model asset not ready"，它们后来有没有补上我没有查。
  - **用户看到的现象是我留在二进制里的临时代码造成的，不是引擎的行为。** 用户指出编辑器正常打开是空场景，现在却一打开就有场景。
    查下来：我用 `Copy-Item` 从备份还原临时改过的文件，它保留了备份的旧修改时间，MSBuild 认为源码比目标文件旧，一个文件都没有重编；
    构建照样打印了 `SparkEditor.exe` 并返回 0。我两次报告"已还原并重新编译"，而 `SparkEditor.exe` 里一直留着
    "自动打开 `Car.scene`、自动跳过欢迎页"的代码（后一次还带着临时日志）。用户是在这个二进制上验证的：
    场景在资产加载完之前被自动打开，于是出现上面第二种情况。
    2026-10-12 01:34 把四个文件的修改时间改成当前、重新编译，构建输出里确认它们被编译了，
    并在 `bin/Debug` 的全部可执行文件里搜过临时代码的字符串，都没有了。
  - 清掉之后用户在编辑器里按正常流程重新看了一遍（2026-10-12）：都正常。这是对现在这版代码的画面确认。
  - 由此留下的一条观察（不是这份方案的事）：场景在资产加载完之前被打开时，天空盒不会在资产就绪后自己补上。
    正常流程有欢迎页挡着，走不到这里。

**验证不到的**

- 独占资源的所有权交接在 DX12 上没有对应物，后端会丢弃换队列的 buffer 屏障。这次改动在 Vulkan 规则下是否正确，
  只能靠读代码与 §三 对照，跑不出来。
- 同一个资源上多个请求、读一段传一段，现在没有使用者，跑不到。

---

## 七、未决

- **待确认的三处**，都是 2026-10-11 写文档时补的：
  - **三处"上传且导入"的资源改用 `CreateImage`、pass 一侧不动**（D12）。
  - **image 拷贝前那条屏障的起点**（D8）。Vulkan 上新 image 的 layout 是未定义，要从它转成拷贝目标；
    DX12 后端对 copy 队列上带"未定义"的 image 屏障有断言。两种写法：
    - 起点写"未定义"，DX12 后端在 copy 队列上把它当成不需要转换（放开那条断言）。
    - 起点写"无"，由 Vulkan 后端把新 image 的"无"对应到未定义。
    前一种说的是原生的事实，我倾向它。它依赖"DX12 上新 image 建出来就能被 copy 队列直接写"，
    这一点我是从现有路径能跑推断的，没有去读 image 创建时给的初始 layout。
  - **`MeshGeometrySystem::CreateBuffer` 改成什么名字**（D10）。
- **已知留着的缺口**（各自的决定里写了，这里汇总）：
  - 同一张 image 多个请求时状态表要加锁（D8）。
  - 做流送时，读流送纹理的一方不能再问 `PendingUploads`（D6、D11）。
  - `BufferBarrier` 没有范围（D11）。
- **`RHIEventContext` 里以后还放什么。** 现在只有上传请求。回读、延迟释放的通知这类"一方产生、另一方过一会处理"的东西是候选，这次不动。

---

## 八、改动清单（草案）

| 步骤 | 文件 |
|---|---|
| 1 | 新建 `RHI/Context/RHIEvent.h`、`RHIEventContext.h`；`RHI/RHIInterface.{h,cpp}`；`RHI/Component/Component.h`（请求的数据组件、三个状态标记里的前两个、`UploadBatch`、`PendingUploads`；删 `UploadPendingTag`）；`RHI/ResourceBuilder.h`（两个 `Request…Upload`、`FinishUploadRequest`）；`RHI/System/AsyncUploadSystem.{h,cpp}`；`Render/RenderGraph/RenderGraphUtils.h`（`IsResourceReady`）；`Render/Feature/UI/UIProcessFeature.cpp`；`Test/Render/UploadRequestTest.cpp`。D9 的三处：`Render/Binding/Material/MaterialBindingSystem.cpp`、`Render/Binding/Instance/InstanceBindingSystem.{h,cpp}`、`SandBox/Program/RenderGraph/` 下四个示例 |
| 2 | `RHI/Component/Component.h`（`UploadAcquirePendingTag`）；`RHI/System/AsyncUploadSystem.{h,cpp}`（写上接回的屏障、上传线程的屏障、按范围校验；执行"等接回"请求的函数放在这里或它旁边的新文件）；`Render/RenderGraph/RenderGraph.cpp`（调用它；删帧末给静态资源盖章）、`RenderGraphCompiler.{h,cpp}`、`RenderGraphExecuter.{h,cpp}`（删 Static 路径的屏障与等待）；DX12 `Command/CommandList.cpp`（只在 image 拷贝前屏障的起点定成"未定义"时，放开 copy 队列上的断言） |
| 3 | `Render/RenderGraph/RenderGraphCompiler.cpp`、`RenderGraphExecuter.cpp`（四处 `Exclude`）；`Render/Pass/Component/RHIComponents.h`；`RHI/Component/Component.h`（删 `StaticImportTag`，`PendingSync` 挪走）与渲染层接收它的头文件；`RHI/ResourceBuilder.h`；`RHI/System/AsyncUploadSystem.cpp`（拒绝导入的目标、删 `PendingSync` 检查）；`Render/Geometry/MeshGeometrySystem.{h,cpp}`（私有函数改名）；`Feature/Skybox/SkyboxSystem.cpp`、`DrawCube.cpp`、`MultiView.cpp`（改用 `CreateImage`）；第 1 步列的各使用方（去掉 attachment、改名）；三份方案文档 |

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
