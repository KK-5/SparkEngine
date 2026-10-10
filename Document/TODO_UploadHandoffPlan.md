# 上传资源的交接　实现方案（取代 Static 资源路径）

把"资源上传完、交给使用它的队列"这件事整个归上传系统，渲染图不再认识这类资源。
渲染图里的 Static 路径（`StaticImportTag`、挂在资源实体上的 attachment、图开头的等待与屏障、帧末盖 fence）全部删除。

方向由用户在 2026-10-10 提出并逐条讨论；标 ✅ 的决定是用户当天确认的，标"待确认"的是我在写这份文档时补的，没有讨论过。
现状一节来自 2026-10-10 读代码，没有为这份方案运行过任何东西。原生语义一节标了哪些是当天查的原文、哪些不是。
还没有开始做。

---

## 状态

| 步骤 | 内容 | 依赖 | 状态 |
|---|---|---|---|
| 1 | `RHIEventContext`；上传请求改成独立实体；就绪改成 CPU 判断；补上现在靠 GPU 等待兜底的三处使用方 | — | 未开始 |
| 2 | 上传系统发接回的屏障事件，队列持有者在帧开头执行；上传线程录的屏障不再读写资源状态 | 1 | 未开始 |
| 3 | 删除渲染图的 Static 路径、资源上的 attachment、`StaticImportTag`；改名 | 2 | 未开始 |

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

## 二、现状（2026-10-10 读代码）

**Static 路径做的四件事**

1. 就绪判断：`IsResourceReady`（`Render/RenderGraph/RenderGraphUtils.h`），资源已建好且没有 `UploadPendingTag`。
2. 等上传的 fence：`CompileStaticResourceBarriers` 把资源身上的 `PendingSync` 收进每个队列的表，执行器在图开头 `queue.Wait`。
3. 一条一次性的屏障：从资源当前状态转到 attachment 算出的目标状态，同样在图开头录。
4. 帧末给全部静态资源盖本队列的 fence（`RenderGraph.cpp`），供上传系统判断"还在用，不能再次上传"。

**上传系统**（`RHI/System/AsyncUploadSystem.{h,cpp}`）

- 上传请求是资源实体上的组件：`PendingBufferUpload` / `PendingImageUpload` 加 `UploadPendingTag`。
  一个实体同一时间只能有一个请求。
- `OnFrameBegin`（主线程）→ `SubmitBatch`：把请求收成一个 batch，给资源盖 `PendingSync{上传 fence, 值}`，去掉 `UploadPendingTag`。
- 上传线程 `ProcessBatch`：录接手的屏障（`ConvertToCopyWrite` 读资源状态）、拷贝、录释放的屏障、提交、signal。
  共享模式的 buffer 两条屏障都不录。
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
| 示例 | `SandBox/Program/RenderGraph/` 下 `DrawCube`、`MultiView`、`TrianglePassFeature`、`MSAAPassFeature` | — | 部分有 |

材质记录每帧重新编码（`GlobalBuffer::UpdateMirror` 对每个持有槽位的实体都调一次），所以纹理下标随就绪变化不需要额外触发。

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

**后端**

- DX12 后端用的是 enhanced barrier。`AttachmentStage::Any` 换算成 `D3D12_BARRIER_SYNC_ALL`，
  `AccessFlags::ShaderSampledRead` 换算成 `ACCESS_SHADER_RESOURCE`，所以"所有着色器阶段可读"是可以表达的（D7 用到）。
- 录屏障的地方直接更新资源的状态（`DX12/Command/CommandList.cpp` 里 `SetResourceState`）。
- `BufferBarrier` 没有偏移和长度，指的是整个 `RHI::Buffer` 对象；池里的段各是一个 `RHI::Buffer`，所以段可以单独表达。

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
4. 请求销毁，资源就绪（D6）。

渲染图里的静态屏障表、图开头的等待、帧末给静态资源盖章、四处 `Exclude<StaticImportTag>` 全部删除。
"使用方要记得登记 attachment"这一步不存在了。

### D2　就绪在 CPU 上判断　✅ 已定

上传的 fence 完成之前资源不算就绪，不被绑定和绘制；渲染队列不再在 GPU 上等 copy 队列。
代价是资源晚一两帧出现，用户 2026-10-10 确认不是问题。

### D3　保留独占模式　✅ 已定

这类资源上传一次、一直使用，用户倾向独占。它是 Vulkan 的核心能力，方向是放宽（§三）。

- 现在的 Vulkan 设备上，独占的 buffer 和 image 都要在使用方队列上接回。
- `maintenance9` 普及后 buffer 的那一条可以不发，和 DX12 后端丢弃它的行为一致。要不要发由后端按设备能力决定，上层的形状不变。
- image 的 layout 转换不会消失，所以"在使用方队列上的那一步"这个位置必须有。

没有选"上传的资源一律共享模式"：它能让 buffer 只剩 fence，但 image 仍要转一次 layout，位置省不掉，还放弃了独占。

### D4　`RHIEventContext` 与 `RHIEvent`　✅ 已定

一个统一的 Context，实体类型 `RHIEvent`，RHI 层定义。屏障事件（D5）与上传请求（D6）都放在里面。

- 用户的思路（2026-10-10）：事件本质上是组件。产生方在 Context 里建实体、写上数据；接收方 `GetView` 取出处理。
  实体在这里只是句柄，相当于向池要一个位置存数据，放在同一个 Context 里不代表是同一类东西；
  有没有某种事件，`GetView` / `GetStorage` 直接能判空。
- 好处是不引入 ECS 之外的另一套数据（专门的事件列表、互相持有对象），并且以后的合并（`ContextMerge`）、多线程与访问控制是现成的路。
- 它由 `RHIInterface` 持有并压入，和 `RHIContext` 一样，只要 RHI 在它就在。
- 我先提的是屏障与上传请求各一个 Context，用"取不取得到 Context"判断有没有人执行屏障。用户定成一个，
  理由如上。这带来两处要写明的规则，见 D5"清理"与"没有执行方"。

这个思路本身要留意的（照此实现会出错的）：

- **遍历顺序不等于产生顺序。** 屏障之间互相独立，没有影响；以后放进来的事件若讲先后，要自己带序号。
- **事件里引用别的 Context 的东西，生命周期要自己保证。** 屏障带的是 `RHI::Buffer*` / `RHI::Image*` 裸指针，
  见 D6"目标资源被销毁"。

### D5　接回的屏障是事件：上传系统发，队列持有者在帧开头执行　✅ 已定

- **发**：上传系统在 `OnFrameBegin` 里发现某个请求的 fence 完成，就在 `RHIEventContext` 里建实体，
  组件直接用现有的 `RHI::BufferBarrier` / `RHI::ImageBarrier`（目标队列它们自己带着）。上传系统不需要命令录制器，也不拿任何队列。
- **执行**：队列的持有者（现在是渲染图）在 `ExecutePipeline` 里、`m_commandQueueContext.Begin()` 之后、Build 之前，
  取出全部屏障事件，有事件的队列各开一个命令列表，录完、提交。
- **为什么在编译之前**（用户 2026-10-10 定）：这条屏障本来就是把资源转成可用状态，这个时候渲染图还没有开始运行，
  算不上破坏它的结构。屏障在录的时候更新资源状态，所以编译读到的已经是最终状态。
  - 我先提的是录在渲染图每个队列的第一个命令列表开头（执行阶段）。那样编译时状态还是旧的，被 pass 声明的导入资源
    （天空盒立方体）会被渲染图自己再生成一条接回；为了躲开它我又提了"屏障执行后的下一帧才就绪"加一轮确认。
    改到编译之前后这些都不需要。
- **清理**：执行完后销毁带屏障组件的实体。**不能清空整个 Context**，里面还有上传请求。
- **不需要 GPU 上的等待**：fence 已经在 CPU 上确认完成，这段命令又排在这一帧该队列的所有提交之前。
- **代价**：有上传完成的那些帧，每个涉及的队列多一次小的提交。
- **依赖的前提**：发事件（`OnFrameBegin` 里）与执行之间，别的帧开始处理函数不读这些资源的状态。
  `FrameEventBus` 的处理函数顺序是任意的；现在的几个（`RHIResourceSystem` 等）按我读到的都不读状态。执行的位置要写明这条依赖。
- **没有执行方时**（待确认）：统一成一个 Context 后，不能再用"取不到 Context"判断。打算这样查：
  上传系统在 `OnFrameBegin` 发新事件之前，如果 Context 里还留着上一帧的屏障事件，说明没有人执行，报错。
  它晚一帧才报，而且那时请求已经销毁；它查的是配置错误（用了上传系统却没有队列持有者来执行），不是运行时会恢复的状态。
  另一种写法是执行方在 Context 里放一个标记实体，上传系统发事件前查它；多一样东西，但当帧就能报。

这实际上是把用户先选的"上传系统自己向使用方队列提交"换成了"队列持有者留一个口子"，口子是 Context 而不是互相持有对象。
用户否掉了前一种的实现方式：上传系统要拿 `RenderSystem` 内部的队列，`RenderUI::Bind` 那个先例本身是渲染内部的系统，而且以后会被优化掉。

### D6　上传请求是独立实体；就绪就是请求已经不存在　✅ 方向已定，持有句柄的方式待确认

- 请求不再是资源实体上的组件，而是 `RHIEventContext` 里自己的实体，带目标资源、范围、数据指针。
  `RequestBufferUpload` / `RequestImageUpload` 返回 `RHIEvent`。
- **"上传中"标在请求上，不标在资源上。** 资源实体上什么都不挂，`UploadPendingTag` 与上传后盖的 `PendingSync` 都不需要了。
- 请求的生命周期：建立 → `SubmitBatch` 收走（记下这一批的 fence 值）→ fence 完成 → 发屏障事件并销毁。
  不需要屏障的（共享模式的 buffer）fence 完成就销毁。
- **就绪 = 请求已销毁**，同一帧的屏障在编译前执行（D5），所以请求销毁的那一帧资源就可用。
- 一个 buffer 上同时有多个请求是自然的。用户 2026-10-10 说明以后要支持"同一个 buffer 一段在上传、一段在读"，
  这是"标在请求上"的原因：标在资源上的话，一段在传整个 buffer 都变成未就绪。
  - 现在网格的每一段各是一个 `RHI::Buffer`、各有实体，所以这次的行为与"标在资源上"没有区别；规则按请求定，以后是加法。

**持有句柄的方式（待确认）**：发请求的一方把 `RHIEvent` 存在资源句柄旁边，用 `!eventContext.Valid(request)` 问就绪。

- 要加字段的地方：`MeshGeometry`（顶点、索引各一个）、材质的 GPU 纹理组件（每个槽位一个）、图标的 GPU 组件、
  天空盒与 IBL 的句柄、`SceneBindingSystem` 的三张表、`InstanceBindingSystem`、四个示例。
- 另一种写法是不存句柄，`IsResourceReady(resource)` 去 Context 里找有没有以它为目标的请求。调用处不用改，
  但每次查询要扫一遍在途的请求，而且以后按段问就绪时还是得回到句柄。我倾向存句柄。

**目标资源被销毁**：请求还没被收走时，`SubmitBatch` 发现目标实体已死就销毁请求。已经在途的，batch 里现在存的是裸指针
（现有问题，不是这次引入的）；打算顺手让 batch 持有 `Ptr`，这样上传线程用到的对象一定活着。

**数据的生命周期**不变：`m_data` 要活到请求被 `SubmitBatch` 收走。

### D7　最终状态从资源的绑定标志推出来　✅ 已定

上传后转到"绑定标志里声明的全部只读用途，所有阶段"，由上传系统推，不由任何一方给。

- 没有选"作为上传请求的字段"：发请求的是功能层（材质纹理系统等），那等于又让产出方决定用途，
  和"功能层只产出资源，用途由渲染层决定"相反。
- 顺带修正一处：现在材质纹理的目标阶段写死是像素着色器，别的阶段采样它在严格的后端上不对。
- DX12 后端能表达（§二"后端"）。

### D8　上传线程录的屏障不读也不写资源状态；状态只在接回处更新一次　buffer 部分 ✅，推广到 image 待确认

- **buffer**（用户 2026-10-10 同意方向）：buffer 没有 layout，屏障对它唯一的含义是"这一段的所有权换队列"，
  这个信息上传请求里就有（哪一段、所属队列）。所以屏障的两端都从请求算，不看 buffer 的当前状态，录的时候也不更新它。
  - 新资源被 copy 队列第一次写之前不需要交接（§三），所以 buffer 在上传线程上只剩拷贝之后的释放一条。
  - buffer 的状态于是只有主线程读写。"上传线程写、主线程读"对 buffer 不存在了，而且不依赖"上传完之前没人碰它"。
    我先说的是"主线程只在 fence 完成后才碰这个资源，所以竞争消失"，用户指出分段上传时这不成立。
  - "这一段能不能传"的检查留在主线程的 `SubmitBatch`（读状态没有问题）。
- **image**（我写文档时补的，待确认）：同样让上传线程的两条屏障（转成拷贝目标、释放）两端都由请求给出、不更新状态，
  只有主线程执行的接回更新一次，直接到最终状态。
  - 上传只接受新 image（现有断言），所以起点是确定的，不需要读状态。
  - 好处：上传线程对任何资源的状态都不读不写；释放与接回由同一个请求生成，两条写同样的新旧状态，
    符合"layout 转换搭在交接上、两条参数一致"（§三）。现在的写法是释放转到"无"、接回从渲染图读到的状态再转一次，
    两半并不一致，只是没有 Vulkan 后端所以没有暴露。
  - 要核对：DX12 后端现有的换队列映射（copy 队列上 image 停在 COMMON）在两端都显式给出时是否仍然成立。
- **RHI 要加的**：录屏障时可以选择不更新资源状态。现在 `CommandList` 在录的地方直接写。

仍由使用方保证的契约不变（`TODO_GPUDrivenPlan.md` D12）：同一时刻只有一个队列写；读的字节与正在写的字节不相交；
写完的字节等 fence 之后才读。D6 是第三条的落点。

### D9　补上现在靠 GPU 等待兜底的三处　✅ `InstanceIDBuffer` 一条已定，其余是推论

改成 CPU 判断后，这三处不补就会读到没传完的数据：

- **材质纹理**：未就绪时写无效下标（`InvalidTextureIndex`），就绪后写真实下标。材质记录每帧重编码，不需要触发。
- **`InstanceIDBuffer`**：就绪前 `InstanceSlotCount` 报 0，剔除 pass 不写任何记录（用户 2026-10-10 同意）。
- **UI 图标**：`UIProcessFeature` 不再读 `PendingSync`，改问请求。

其余使用方已经有就绪判断，只是换成问请求。

### D10　`CreateStaticImage` / `CreateStaticBuffer` 改名　方向 ✅，名字待定

`StaticImportTag` 删掉后"Static"不对应任何机制，用户倾向改名，名字没定。候选：

| 名字 | 说的是什么 | 问题 |
|---|---|---|
| `CreateReadOnlyImage` / `CreateReadOnlyBuffer` | GPU 上只读：内容只来自上传，之后没有人写它。这正是它可以不进渲染图的原因 | 以后支持再次上传、分段上传时，"只读"指的是"渲染图不写"，要在注释里说清 |
| `CreateUploadedImage` / `CreateUploadedBuffer` | 内容来自上传 | 导入资源也可以上传（天空盒立方体），区分不出来 |
| `CreateImage` / `CreateBuffer` | 不加限定词，它就是默认的那种；要渲染图管的才叫 `CreateImported…` | 和 `Factory::CreateBuffer()`、`MeshGeometrySystem::CreateBuffer` 重名，读代码时容易混 |

我倾向第一种。与之相对的 `CreateImportedImage` 含义不变：渲染图跟踪它的状态，pass 要声明对它的访问。

### D11　这次不做的

- **再次上传。** 独占资源现在就不允许（报错），共享模式的资源引擎里没有上传的使用者。帧末盖章删掉后不补替代机制；
  纹理流送这类需求出现时再做，那时用一个全局的帧 fence（"最后一次可能用到它的那一帧跑完了没有"）。
  被渲染图跟踪的导入资源仍由渲染图帧末盖 `PendingSync`，上传系统对它的"还在用就推迟"检查保留。
- **共享模式 image 的上传。** 它在所属队列转完状态的同一帧，别的队列可能先读到。现在没有这种资源，加断言拦住。
- **pass 声明读大 buffer。** 用户定以后再说。渲染图以后若要写几何 buffer（GPU 蒙皮、压缩整理），
  它必须变成 pass 声明的导入资源，那时是必需的。
- **同一个 `RHI::Buffer` 对象内按字节范围发屏障**（给 `BufferBarrier` 加偏移与长度）。"段是自己的 buffer 对象"已经能表达。
- **image 的分段上传**（部分 mip 在传、其余在读）。它是纹理流送的事。

---

## 五、步骤

每一步做完都能跑。

### 1　请求成为实体，就绪改成 CPU 判断

- 新建 `RHIEventContext` / `RHIEvent`，`RHIInterface` 持有并压入。
- `RequestBufferUpload` / `RequestImageUpload` 在 `RHIEventContext` 里建请求实体并返回句柄；`SubmitBatch` 从那里取。
- `OnFrameBegin` 在 `SubmitBatch` 之前加一步：fence 完成的请求销毁（这一步先不发屏障事件）。
- 各使用方存请求句柄、改问请求；补 D9 的三处。
- 渲染图的 Static 路径原样保留：它等的 fence 已经完成，等待是空操作；接回的屏障仍由它在图开头录。
  - 这一步里上传系统仍给资源盖 `PendingSync`，否则 Static 路径不知道要接回。

### 2　上传系统发屏障事件，上传线程不再碰状态

- RHI：录屏障时可以不更新资源状态。
- 上传线程：buffer 只录释放；image 的两条两端由请求给出；都不更新状态（D8）。
- `OnFrameBegin`：fence 完成的请求先发接回的屏障事件再销毁；最终状态从绑定标志推（D7）。
- 渲染图：`ExecutePipeline` 里 `Begin()` 之后执行屏障事件并销毁它们（D5）。
- 上传系统不再盖 `PendingSync`。渲染图的 Static 路径此时看到的已经是稳定状态，不再产生等待和屏障。

### 3　删除与改名

- `CompileStaticResourceBarriers`、`StaticPreBarrierTable`、执行器开头的那段、帧末给静态资源盖章、四处 `Exclude<StaticImportTag>`。
- `CreateStaticBufferAttachment` / `CreateStaticImageAttachment` 及全部调用处；`StaticImportTag`；`UploadPendingTag`。
- `IsResourceReady` 剩下的含义只有"资源已建好"，和渲染图无关，挪到 RHI 层或并进请求的查询。
- 改名（D10）。
- 同步 `TODO_GPUDrivenPlan.md`（D12、§六 `PendingSync` 一条）、`TODO_BufferPoolPlan.md` 里提到 Static 路径与独占路径三条屏障的地方。

---

## 六、验证

按仓库的分工，画面由用户确认；我保证能跑、测试通过、validation 无报错，并用脚本抓编辑器窗口对比
（做法见 `TODO_GPUDrivenPlan.md`"步骤 5 第 1、2 小步跑过什么"）。编辑器要从仓库根目录启动，并打开一个存好的场景。

- 每一步：全量 Debug 编译；`SparkRenderTest`；`ModelTest.scene`、`Room.scene` 在 debug layer 下运行，
  与改动前的截图对比（摆好相机，两个场景的默认视角看不到多少东西）。
- 第 1 步额外看：场景加载的前几帧，没有就绪的纹理显示为没有贴图而不是别的纹理的内容；物体不提前出现。
- 第 2 步额外看：日志里每批上传只执行一次屏障事件；多个纹理同一帧完成时只多一次提交。
- 可以写单元测试的：请求的生命周期（建立、收走、完成后销毁、目标已死时销毁）、"就绪 = 请求已销毁"、
  屏障事件执行后只销毁屏障实体而请求还在。
- 四个示例至少编译通过并各跑一次。

**验证不到的**

- 独占资源的所有权交接在 DX12 上没有对应物，后端会丢弃换队列的 buffer 屏障。这次改动在 Vulkan 规则下是否正确，
  只能靠读代码与 §三 对照，跑不出来。
- "上传线程不再读写状态"是没有竞争的保证，不是可以观察到的现象；原来的竞争也从没有观察到出事。

---

## 七、未决

- **待确认的四处**（见各节）：没有执行方时怎么查（D5）；请求句柄由使用方存还是按目标查（D6）；
  image 的上传屏障也不更新状态（D8）；改成什么名字（D10）。
- **`PendingSync` 剩下的用途。** 改完后它只在渲染图内部：帧末给导入资源盖、下一帧跨队列时等，以及上传系统对导入资源的
  "还在用就推迟"。`Component.h` 里那段"所有系统统一遵守"的协议说明要重写；它写的"使用方等完后移除"实际没有任何地方做。
- **`RHIEventContext` 里以后还放什么。** 现在只有两种。回读、延迟释放的通知这类"一方产生、另一方过一会处理"的东西是候选，
  这次不动。

---

## 八、改动清单（草案）

| 步骤 | 文件 |
|---|---|
| 1 | 新建 `RHI/Context/RHIEventContext.h`（与 `RHIEvent`）；`RHI/RHIInterface.{h,cpp}`；`RHI/Component/Component.h`（请求的组件带目标）；`RHI/ResourceBuilder.h`（两个 `Request…Upload` 返回句柄）；`RHI/System/AsyncUploadSystem.{h,cpp}`；`Render/Geometry/MeshGeometry.h`、`MeshGeometrySystem.cpp`；`Render/Binding/Instance/InstanceBindingSystem.{h,cpp}`；`Render/Binding/Material/MaterialBindingSystem.cpp`、`Feature/Material/MaterialTextureSystem.cpp` 与材质的 GPU 纹理组件；`Render/Binding/Scene/SceneBindingSystem.{h,cpp}`；`Feature/Skybox/SkyboxSystem.cpp` 与它的组件；`Feature/UI/ImGui/IconManager.cpp` 与图标的 GPU 组件、`Render/Feature/UI/UIProcessFeature.cpp`；`Render/RenderGraph/RenderGraphUtils.h`；`SandBox/Program/RenderGraph/` 下四个示例；测试 |
| 2 | `RHI/Resource/ResourceState.h`、`RHI/Command/CommandList.{h,cpp}`、DX12 `Command/CommandList.cpp`（不更新状态的录法）；`RHI/System/AsyncUploadSystem.{h,cpp}`；`Render/RenderGraph/RenderGraph.cpp`（执行屏障事件） |
| 3 | `Render/RenderGraph/RenderGraphCompiler.{h,cpp}`、`RenderGraphExecuter.{h,cpp}`、`RenderGraph.cpp`；`Render/Pass/Component/RHIComponents.h`；`RHI/Component/Component.h`、`RHI/ResourceBuilder.h`；第 1 步列的各使用方（去掉 attachment、改名）；三份方案文档 |

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
