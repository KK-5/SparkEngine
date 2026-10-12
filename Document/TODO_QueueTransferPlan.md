# 换队列与改状态拆开　实现方案

把"资源从一个队列换到另一个队列"从屏障里拿出来，做成独立的操作；屏障只表示同一个队列上的状态变化。
起因是 `TODO_UploadHandoffPlan.md` §七 留下的方向。

§四 的决定都是用户确认过的。§五 是还没定的。§三 标了哪些是查过原文的、哪些是凭记忆的。

---

## 状态

还没有开始写代码。步骤见 §六（草案）。

---

## 一、为什么做

现在的约定是"屏障的源队列和目标队列不同，就是交接，同一条屏障在两个队列上各录一次"。它有这些问题：

- **一个约定装了两件不同的事。**
  - 上传：copy 队列把独占资源交给它的所属队列。这是 Vulkan 的所有权交接，全仓库只有这一处。
  - 渲染图：禁止独占的导入资源换队列，transient 池又都是共享的，所以渲染图里跨队列的 image 屏障全部发在**共享** image 上。
    共享资源在 Vulkan 上没有所有权，这些屏障不是交接，只是 DX12 上换队列类型时要重写 layout。
- **共享 image 不能上传。** 它不需要换所有权，但要转一次 layout；现在"队列不同"就等于交接，表达不了"只转状态"。
- **多次上传很难做。** `BufferBarrier` 没有范围；加了范围，"接回时把每对象一份的状态写成什么"又要一起定（`TODO_UploadHandoffPlan.md` D11）。
- **DX12 后端的 `QueueBarrier` 里全是特殊判断**：是不是跨队列、是释放还是接回、释放不写状态记录、copy 队列上的未定义不算丢弃。
  动一处容易带错另一处。
- **交接屏障带着"以后怎么用"。** 上传系统要在提交 batch 时就知道落点；独立 buffer 在 DX12 上接回时要声明之后的全部读法；
  帧开头那次只含屏障的提交触发 DX12 的 #1356 告警。
- **pass 不声明对这类资源的访问。** 几何的大 buffer 是 `DrawIndirect` 靠标记找到后直接绑上的，贴图是着色器按 bindless 下标采样的，
  渲染图都不知道。上传系统手里只有资源的队列掩码：不知道这一帧到底哪个队列、哪个 pass 会用它，
  只能按最保守的做：接回和落点放在帧开头单独一个阶段，共享的资源还要 CPU 等它执行完。上一条里的告警也是这样来的。

---

## 二、现在的代码

**屏障**（`RHI/Resource/ResourceState.h`）

- `BufferBarrier` / `ImageBarrier` 带源、目标两侧的访问、阶段、队列。`ImageBarrier` 有子资源范围，`BufferBarrier` 指整个对象。
- 结构体上的注释规定：队列不同即所有权交接，必须以相同参数录两次；DX12 经 COMMON 中转。
- `ValidateBufferBarrier` / `ValidateImageBarrier` 里的 `IsCrossQueueAllowed`：跨队列要求资源的队列掩码同时含两个队列。

**DX12 后端**（`Backend/DX12/Command/CommandList.cpp` 的两个 `QueueBarrier`）

- 用"屏障的队列是否不同"和"本命令列表是哪个队列"判断这次录的是释放还是接回：释放只用源一侧，接回只用目标一侧。
- image：释放把 layout 转到 COMMON，接回从 COMMON 转出。独立的 buffer：释放录成"到不可访问"，接回录成"从不可访问"。
- 释放不写资源的状态记录，接回写。
- layout 由（访问，队列）推出（`ConvertBarrierLayout`）：copy 队列恒为 COMMON；图形、计算队列用各自专用的 layout。

**状态值**（`RHI/Resource/AccessFlags.h`）

- `None`：`ResourceState` 的默认值。普通池、交换链新建的资源记成它（`ImagePoolBase.cpp`、`BufferPool.cpp`、`SwapChain.cpp`）。
  DX12 上对应 COMMON，普通池的 image 也确实建成 COMMON。`TODO_EnhancedBarriersPlan.md` D2、D3 把它定义为"当前无访问，内容保留"。
- `Undefined`：内容不用保留，只能当屏障的起点；旁边的读写位是这块内存上一次的访问。transient 池每次分配时写上。
  DX12 上对应未定义 layout，渲染目标、深度再加丢弃标志。
- 没有屏障以 `None` 为目标（`ImageBarrierMerge.cpp` 有断言），所以 `None` 实际只在创建时出现。

**上传**（`RHI/System/AsyncUploadSystem.cpp`）

- `MakeBufferHandoff` / `MakeImageHandoff` 算出一条屏障，释放和接回各用一次。
- image 拷贝前那条屏障手写了"从 `Undefined` 出发"，而普通池的记录是 `None`。它在 copy 队列上不产生原生屏障。
- "只接受新的子资源"的校验看的是记录里的访问是不是 `None`。

**渲染图**

- 编译（`RenderGraphCompiler.cpp` 的 `CompileBufferAccess` / `CompileImageAccess`）：源队列和本 Scope 的队列不同时，
  同一条屏障挂两处：消费方 attachment 上的 `Pre…Barrier`，生产方最后一个 attachment 上的 `Post…Barrier`，并记一次跨队列等待。
- 共享 buffer 换队列不发屏障，只记等待。
- 执行（`RenderGraphExecuter.cpp`）：Scope 开头录 Pre，结尾录 Post。

---

## 三、原生语义

**查过原文的**

- Vulkan，`vkCmdPipelineBarrier2`：屏障的同步范围作用于同一队列上提交顺序在它之前、之后的操作，跨多次提交。
  提交顺序本身不产生任何依赖。
- Vulkan，所有权交接：释放的目标一侧、接回的源一侧不生效，也不受"阶段必须是本队列族支持的"这条检查。
  搭在交接上的 layout 转换只执行一次。
- Vulkan：屏障的阶段必须是录制它的队列族支持的（VUID 09673–09676）。layout 不受队列族限制，只和 image 的 usage 标志挂钩。
- Vulkan：屏障的旧 layout 只能是"未定义"或"当前 layout"。从未定义出发等于不要旧内容。
- Vulkan：`VkImageCreateInfo::initialLayout` 必填，只能是未定义、预初始化（仅线性排布）、零初始化（扩展）。
- D3D12：三个创建函数都有 `InitialLayout`；纹理可以填任意 layout，buffer 只能填未定义。
- D3D12：copy 队列不支持转 layout 的屏障，它用过的子资源结束时仍在 COMMON。
- D3D12：COMMON 可以不转 layout 直接当着色器资源、拷贝源、拷贝目标；在任何队列类型上可读。
- D3D12：带队列名字的 layout 只能在对应的队列类型上用。给出的理由是压缩数据：有些硬件的图形队列能直接读渲染目标的压缩数据，
  计算队列不能；用专用 layout，驱动可以不解压。规范"强烈建议尽量使用"。
- D3D12：丢弃标志只能在起点是未定义 layout 时使用。
- D3D12：未定义 layout 的定义是"之前的 layout 无关或未定义时使用"。调试层会检查屏障的起点与它已知的 layout 一致。
- Vulkan：独占模式的所有权按 buffer 的字节范围、image 的子资源范围算。原文是"acquired ownership of a buffer range or
  image subresource range"；接回的范围必须和之前释放的范围完全一致。`TODO_BufferPoolPlan.md` §三 有更多原文。
- D3D12：buffer 的屏障只能作用于整个 buffer（偏移必须是 0，长度必须是整个 buffer），出处同上。
- D3D12：buffer 在一次提交里第一次被访问不需要屏障。只含屏障的提交，规范认可的用法只有给纹理转 layout，
  两侧写"不同步、不可访问"。

**来自仓库里已有的记录**

- #1356 由只含屏障的命令列表触发。之前的来源是单独提交的交换链 Present 转换，改成随最后一个 Scope 录制后消失
  （`TODO_EnhancedBarriersPlan.md` D7、§四）。

**凭记忆、没有查到原文的**

- Vulkan 共享模式的资源，屏障里的队列族字段必须忽略；照"两边各录一次"的约定会变成两次独立的转换。

**没有答案的**

- D3D12 上从 COMMON 采样和从只读 layout 采样有没有性能差别。规范没有比较。
- D3D12 的调试层是否接受"起点写未定义、实际是 COMMON"。transient 池的 committed 回退路径现在是这样写的，没有专门确认过。
- #1356 的完整文本，以及只发 layout 的写法会不会仍然触发它。

---

## 四、决定

### D1　上层把要做的事写清楚，后端怎么做是后端的事

上层（上传系统、渲染图）按逻辑写出每一步：释放、接回、改状态。某一步在某个后端上不用做，由后端自己省掉。

- 上层不因为"DX12 上这一步是空的"而去掉或挪动这一步。
- 后端的具体规定（某个 layout 的性能、某种队列的限制）不作为上层设计的理由。
- 没有查清的后端细节不挡上层的设计。

### D2　换队列是独立的操作

```cpp
// ResourceState.h
struct ImageQueueTransfer
{
    Image*                m_image     = nullptr;
    ImageSubresourceRange m_range;
    AccessFlags           m_srcAccess = AccessFlags::None;
    AttachmentStage       m_srcStage  = AttachmentStage::Uninitialized;
    HardwareQueueClass    m_srcQueue  = HardwareQueueClass::Graphics;
    HardwareQueueClass    m_dstQueue  = HardwareQueueClass::Graphics;
};
struct BufferQueueTransfer { /* m_buffer，其余同上，没有 m_range */ };

// CommandList.h
virtual void QueueRelease(const BufferQueueTransfer& transfer) = 0;
virtual void QueueRelease(const ImageQueueTransfer& transfer)  = 0;
virtual void QueueAcquire(const BufferQueueTransfer& transfer) = 0;
virtual void QueueAcquire(const ImageQueueTransfer& transfer)  = 0;
```

- 一次换队列录两次：源队列上 `QueueRelease`，目标队列上 `QueueAcquire`。两次之间的先后由调用方的 fence 保证，和现在一样。
- **凡是换队列都发**，独占、共享、buffer、image 都一样。哪一半在哪个后端上为空，由后端决定（D9）。
- **换完之后资源停在 `None`**（D3）。释放方只管把自己的状态收掉，接收方只管从 `None` 转到自己要的，
  两边都不需要认识对方的状态。
  - 没有选"换队列之后状态不变"：那样要求交接时的状态两边队列都支持（计算队列写不出渲染目标），
    渲染图得判断屏障该发在哪一边。
- `m_srcAccess` / `m_srcStage` 是释放前的状态，由调用方给：上传线程不读资源的记录。
- **内容不用保留的资源不交接。** 状态是 `Undefined`，或是没人用过的 `Initial`，接手的队列直接从那个状态发屏障。

### D3　三个"没有访问"的状态

三个值都表示现在没有人在访问，区别是里面的内容。

| | `Initial`（新增） | `Undefined` | `None` |
|---|---|---|---|
| 含义 | 新建的，自己的内存，从没被访问过 | 内容不用保留，内存可能是别的资源用过的 | 内容要保留，任何队列都可以接手 |
| 谁写上去 | 池在创建时 | transient 池每次分配时 | `QueueAcquire` |
| DX12 | COMMON | 未定义 layout，渲染目标和深度加丢弃 | COMMON |
| Vulkan | 从未定义出发 | 从未定义出发 | 从保留下来的 layout 出发 |

- 三者都只能当屏障的起点，不能当终点。校验里加上"目标不能是 `Initial`、`None`"。
- **为什么是三个**：情况有三种，两个后端的归类不同。Vulkan 把"新建"和"复用内存"归成一类（都是未定义），
  DX12 把"新建"和"空闲保留"归成一类（都是 COMMON）。只有两个值时总有一个后端分不出来。
  - `Initial` 与 `None` 的区别是 Vulkan 需要的：前者没有内容，后者必须从真实的 layout 出发，否则内容被丢掉。
  - `Initial` 与 `Undefined` 的区别是 DX12 需要的：丢弃标志只能配未定义 layout，而普通池的 image 建成 COMMON。
- 上层也用到 `Initial` 与 `None` 的区别：上传"只接受新的子资源"看的是 `Initial`，换过队列的资源不会被当成新的。
- **`None` 的含义不变**，还是 `TODO_EnhancedBarriersPlan.md` D2、D3 定的"当前无访问，内容保留"。新增的是"新建"的名字。
  - 没有反过来（`None` 表示新建，给"空闲保留"起新名字）：`ResourceState` 默认构造出来是 `None`，
    默认值应该落在"内容保留"一侧。忘了写初始状态的新资源在 Vulkan 上是校验报错；反过来的话，
    有内容的资源误带默认值会被悄悄丢掉内容，那份方案的步骤 3 出过这类错。
- `Initial` 放在 `AccessFlags` 里"代表 layout 的状态"那一组，和 `Present`、`Undefined` 并列。
  没有用 `Uninitialized` 这个名字：`AttachmentStage` 里已经有一个，意思不同。
- buffer 也用这三个值。它没有 layout，三者在两个后端上都不产生原生屏障，区别只体现在上层的判断里。
- **它不是给 Vulkan 留的占位，现在的代码就要改**：
  - 创建处写 `Initial`：`ImagePoolBase.cpp`、`BufferPool.cpp`（两处）、`SwapChain.cpp`。
  - 上传的两处"是不是新的"校验改成看 `Initial`。拆开之后接回写的是 `None`，校验还看 `None` 的话，
    换过队列、又没有只读落点的资源会被当成新的再传一次。
  - 渲染图里"跨帧换队列"的断言对"没用过的资源"的豁免，从看 `None` 改成看 `Initial`。
  - DX12 的转换：`Initial` 和 `None` 一样是 COMMON、不同步、不可访问。原生行为和现在没有区别。

### D4　屏障只在一个队列内

`BufferBarrier` / `ImageBarrier` 去掉 `m_srcQueue` / `m_dstQueue`。屏障的起点应该等于记录里的状态。

- 上传拷贝前那条屏障的起点从手写的 `Undefined` 改成 `Initial`。
- `IsCrossQueueAllowed` 从屏障的校验挪到换队列的校验，并且把 copy 队列当作访客：它现在要求两个队列都在资源的队列掩码里，
  而独占 `Graphics` 的资源上传时是从 copy 队列换过来的，copy 不在掩码里。改成"除 copy 之外的队列都在掩码里"。

### D5　记录什么时候写

- `QueueBarrier`：和现在一样，录的时候把记录写成目标状态。
- `QueueAcquire`：把记录写成 `{None, 本队列, 无阶段}`。
- `QueueRelease`：不写。换队列是一件事录两次，记录只在接回时变一次。上传线程因此仍然只在拷贝前那条屏障上写记录。
- `ResourceState::m_queue` 的含义是"资源现在在哪个队列上"，由屏障和接回写。

### D6　上传的步骤

| 步骤 | 在哪 | 做什么 |
|---|---|---|
| 1 | copy 队列（上传线程） | image：屏障 `Initial` 到拷贝写。然后拷贝 |
| 2 | copy 队列（上传线程） | `QueueRelease`，交给所属队列 |
| — | 主线程 | 帧开头查 fence，完成了才往下走 |
| 3 | 声明访问的 Scope 所在的队列 | `QueueAcquire`，资源到 `None` |
| 4 | 同上 | 屏障：`None` 到那次访问要的状态 |

- **上传系统只做到查 fence 为止**：拷贝、释放、CPU 查 fence。第 3、4 步由渲染图在 pass 声明的访问处做（D11），
  不再有帧开头的 `ExecuteUploadAcquires`。
- **落点由声明给出**，不再靠绑定标志去猜。`TODO_UploadHandoffPlan.md` D7 那张固定落点的表不再需要。
- batch 里的两个释放屏障列表换成两个换队列列表。`MakeBufferHandoff` / `MakeImageHandoff` 只剩释放那一半要用的换队列。

### D7　共享资源的上传

- **共享的资源也走第 2、3 步。** 它没有所有权要换，但"copy 队列用完、另一个队列接着用"仍然是换队列，
  后端各自决定发什么（D9）。
- **共享 image 放开上传。** 落点屏障发在一个队列上，别的队列也可能读它，而队列之间是并行的。
  读它的 Scope 都声明了访问（D11），所以这个先后由渲染图用现成的跨队列等待保证，不需要 CPU 等它执行完，请求也不用多一个状态。

### D8　渲染图

- **编译**：一次访问的源队列和本 Scope 的队列不同、且有内容要保留时，产生三样：生产方最后一个 attachment 上的释放、
  消费方 attachment 上的接回、消费方上从 `None` 出发的屏障。跨队列等待照旧。
- **组件**：`Pre/PostImageBarrier`、`Pre/PostBufferBarrier` 旁边加放换队列的组件。
- **执行**：Scope 开头先录接回、再录屏障；结尾录释放。
- **共享 buffer 换队列**：现在只记等待、不发屏障；改成和别的资源一样发释放和接回，两个后端上都为空。
- **不动的**：独占的导入资源不能换队列；image 不跨帧换队列的断言；transient image 不在 copy 队列上首次使用的断言；
  等待与 `PendingSync`。

### D9　各后端把它变成什么

| 操作 | Vulkan | DX12 |
|---|---|---|
| 释放 image | 独占：交接的释放一半，顺带转到公用 layout。共享：一条普通屏障转到公用 layout | 当前 layout 转到 COMMON；已经是 COMMON 就为空（copy 队列） |
| 接回 image | 独占：交接的接回一半。共享：空 | 空 |
| 释放、接回 buffer | 独占：交接一对。共享：空 | 空 |
| 从 `None` 出发的屏障 | 公用 layout 转到需要的 | COMMON 转到需要的 |

- Vulkan 的"公用 layout"是哪一个由后端定：统一转到一个（比如 GENERAL），或者保留原 layout 并记住它。上层不关心。
  代价是比现在多一条屏障，用户确认可以接受。仓库里还没有 Vulkan 后端，这一列是照着 §三 核对过的预期。
- **DX12 后端要改的**：
  - `QueueBarrier` 删掉全部跨队列分支：释放、接回的判断，"释放不写状态"的例外。
  - 留下的规则：upload / readback 堆上的 buffer 不发；池里的段不发；copy 队列的 layout 恒为 COMMON；`Undefined` 的丢弃处理。
  - `ConvertBarrierLayout`：`Initial` 和 `None` 一样是 COMMON。
  - buffer 的屏障从 `Initial` 或 `None` 出发时不发原生屏障（规范：一次提交里第一次访问 buffer 不需要屏障）。
    调试层对之后的屏障会不会有意见，跑的时候确认。

### D10　创建时的 layout 不改

- 普通池仍然把 image 建成 COMMON，记录从 `None` 改成 `Initial`（D3）。两者在 DX12 上都是 COMMON，记录和实际 layout 一致。
- 不在 DX12 的 image 上加"建出来时的 layout"这样的成员。
- 不加创建参数。两个 API 创建时都要给初始 layout，RHI 没有这个输入，是简化了；但能跨后端指定的只有 Vulkan 允许的那几个值，
  现在只有"没有内容"一个合法值。等有 CPU 预先写好内容的 image 时再加。
- DX12 把纹理建成哪个原生 layout 是后端的事。建成 COMMON 对谁先用都成立：copy 队列只认 COMMON，
  图形、计算队列可以从 COMMON 转到自己要的。后端不需要知道资源将来在哪个队列上用。

### D11　pass 按标签声明对这类资源的访问；接回和落点在声明处做

这一条只有大体的思路，细节在 §五。

**声明的单位是标签。** Scope 说的是"带某个标签的资源，我按某种访问来用"，比如带顶点 buffer 标签的当顶点输入，
带材质贴图标签的当着色器采样。标签后面是一个资源还是一千个，pass 不关心。

- 几何和贴图是同一类东西：上传一次，之后只读，数量多，pass 不逐个认识它们。区别只是几何碰巧都在一个大 buffer 里。
- 标签就是 RHI 实体上的组件，和现有的 `Accepts<标签>` 是同一种写法。不引入"组"这样的新实体，也不维护成员关系。
- 标签由渲染层和功能层定义，RHI 不认识它们。

**屏障由声明自动生成，每个资源只转一次。**

- 上传的拷贝完成后（CPU 查到 fence），上传系统给资源留一个"等落点"的标记。
- 渲染图按 Scope 的声明，取"带这个标签、又带等落点标记"的资源，在这个 Scope 开头录接回和落点屏障，然后摘掉标记。
- 这一帧后面再声明同一个标签的 Scope 取到的是空的，没有屏障。
- 每帧的开销只和这一帧新传完的数量有关，和带标签的资源总数无关。帧末没有东西要清。
- 持续上传时，每一帧第一个声明的 Scope 都会把新传完的那批带上。

**它替掉了什么**

| 之前 | 现在 |
|---|---|
| 帧开头单独一个接回阶段（`ExecuteUploadAcquires`） | 没有这个阶段 |
| 只含屏障的提交，触发 #1356 | 屏障在声明它的 Scope 的命令列表里 |
| 共享 image 要 CPU 等落点执行完 | 别的队列上声明同一标签的 Scope 等做转换的那个 Scope，用现成的跨队列等待 |
| 落点靠绑定标志猜 | 声明里写的访问就是落点 |
| 就绪要等接回执行 | 拷贝的 fence 完成就算就绪，使用一定排在声明处的转换后面 |
| 用了没登记的资源，悄悄出错 | 声明是 Scope 上的，漏了可以查（怎么查见 §五） |

**单个导入的资源**（天空盒立方体那种）走同一个规则：它上传完、被 `p.Import` 之后，第一次被声明访问时由渲染图做接回，
再转到那次访问要的状态。所以规则只有一条：所有接回和改状态都发生在某个 pass 声明的访问上，声明的粒度要么是一个资源，要么是一个标签。

**和 Static 路径的区别**（它在 `TODO_UploadHandoffPlan.md` 里被删掉，这里不是把它加回来）

- 声明挂在 Scope 上，是 pass 到资源的边；Static 路径把 attachment 挂在资源实体上，它不是边。
- 每帧只处理新传完的资源；Static 路径每帧扫全部静态资源。
- 同步点在声明它的 Scope；Static 路径的同步点固定在图的开头。

### D12　buffer 的单位是整个 `RHI::Buffer` 对象；偏移和长度以后加

`BufferQueueTransfer` 和 `BufferBarrier` 现在都不带偏移和长度。`TODO_UploadHandoffPlan.md` D11 的"定了要加"不变，
只是不在这份方案里做：现在换队列和屏障永远是整个对象，加了字段也没有区别。

- **要分段用的，每一段是一个自己的 `RHI::Buffer`**（池里的段，`TODO_BufferPoolPlan.md` D3）。它有自己的状态记录，
  DX12 对它不发原生屏障，Vulkan 后端按这一段在原生 buffer 里的偏移和大小发所有权交接。网格的顶点、索引就是这样。
- **同一个独占的 `RHI::Buffer` 对象只能上传一次**，目标必须是新的（`Initial`）。
- **新增一个校验**：`SubmitBatch` 收走独占 buffer 的请求时，目标实体上的 `PendingUploads` 计数大于 1 就拒绝。
  计数大于 1 说明它上面还有别的请求没结束，照收的话整个对象会被交接两次。不需要扫描，每个请求多读一次计数。
  - 之前的请求已经在传、之后又来一个：后来的被拒，之前的照常完成。
  - 两个请求进了同一个 batch：两个看到的计数都是 2，一起拒。分不出先后，也不去分：这是写错了才会出现的情况。
- **需要反复上传的建成共享模式**，只靠 fence。
- **以后真正加字段时要一起定的**：DX12 的 buffer 屏障只能作用于整个 buffer，状态记录又是每个对象一份，
  一段范围的接回不能把整个对象的记录写成 `None`（§三）。

### D13　DX12 按 image 的队列掩码选 layout；transient 的掩码是它实际用到的队列

**问题**：DX12 后端现在一律按录屏障的队列选专用 layout。共享 image 的落点由图形队列转过去，
得到的是图形队列专用的只读 layout，计算队列用不了。

**DX12 的 layout 推导多一个输入**：这张 image 的 `m_sharedQueueMask` 是否同时含图形和计算。

- 是：用通用版，图形、计算队列都能用。
- 否：按队列用专用版，就是现在的行为。
- 受影响的是有三个版本的那几个 layout：着色器采样、着色器读写、拷贝源、拷贝目标、多种只读。
  `Initial` 和 `None` 一直是通用的 COMMON；渲染目标、深度、resolve 没有队列版本；copy 队列恒为 COMMON，掩码里的 copy 位不参与。
- 这个输入必须是 image 自己的属性，不能是"这条屏障是谁发的"：layout 要能从状态唯一推出来，
  否则进入某个状态的屏障和离开它的屏障会推出不同的 layout。
- 真正用得上通用版的只有一种情况：两种队列都在用这张 image，中间又没有换队列，也就是按标签声明（D11）、
  被两种队列同时采样的共享 image。渲染图逐个跟踪的共享 image 每次换队列都经过 `None`，用哪个版本都是对的。
- 共享 image 的上传因此不需要对掩码设额外的限制。

**渲染图给 transient image 填实际用到的队列**

- 编译器已经算出每个 transient 这一帧被哪些队列用到（`LifetimeEnds::m_queueMask`），现在只传给了池的分配 fence。
  创建时把它写进描述符的 `m_sharedQueueMask`。
- 只在一个队列上用的自动是独占，被多个队列用到的自动是共享，不需要谁声明。Vulkan 上对应建成独占或共享模式。
- 不做的话，transient 的掩码是默认的 `All`，主路径上的渲染目标会全部换成通用 layout。
- 池按描述符复用资源对象的地方，掩码不同会不会算成不同的描述符，做的时候看。

**默认值不改**，`ImageDescriptor::m_sharedQueueMask` 仍是 `All`。两种设置都必须正确，默认值是什么不影响对错：

- 没写掩码的 image 算共享，用通用 layout，结果正确。
- 代价只在性能上：规范说渲染目标转成只读时，驱动不能假设计算队列读不到，可能多做一次解压。
  受影响的是没写掩码、又不是 transient 的 image（历史帧这类）。
- 默认值要不要改是另一件事，不在这份方案里。

---

## 五、没定的

1. **按标签声明（D11）的细节**，方向定了，下面这些都没定：
   - 声明的写法，以及画网格的调用（`DrawIndirect`、`Accepts<>`）要不要自动带上对几何的声明。
   - 同一个标签在一帧里被两种访问声明：buffer 没有 layout，可以取并集；image 不一致是不是直接断言。
   - 独占资源的标签这一帧只被别的队列上的 Scope 声明：所属队列没人做转换，是不是直接断言。
   - 这一帧没人声明某个标签：新传完的资源留着标记，等有人声明的那一帧再转。这时 CPU 一侧算不算就绪。
   - 用了但没声明怎么查。贴图只能靠看着色器有没有用 bindless 贴图。
   - "等落点"的标记带什么：image 的一次上传只写一段子资源范围，接回要知道是哪一段。
   - 屏障和跨队列等待在渲染图里挂在哪：现在都挂在 attachment 实体上，标签的声明没有逐个资源的 attachment。
   - 哪些资源用标签，哪些单个导入。材质贴图、几何、图标适合用标签；IBL、LUT 这几张数量固定的可以单个导入。
   - 渲染图以后要写带标签的资源（GPU 蒙皮写几何 buffer）时怎么声明。
2. **内容不经屏障就写进去的 image**（CPU 直接写的）。它们一建好就有内容，不能记成 `Initial`，
   对应的是 Vulkan 的"预初始化"。现在有没有这样的 image 还没有逐个查。
3. **transient 池的 committed 回退路径**现在建成 COMMON、记成 `Undefined`，记录和实际 layout 不一致。
   它是已有的问题；改成和复用内存的路径一样建成未定义就一致了。

---

## 六、步骤（草案）

| 步骤 | 内容 |
|---|---|
| 1 | RHI：`Initial` 与创建处的初始记录（D3）；两个换队列的结构、`QueueRelease` / `QueueAcquire` 与 DX12 的实现；校验 |
| 2 | 渲染图改成 D8；屏障去掉队列字段，DX12 的 `QueueBarrier` 删掉跨队列分支（D4、D9）；transient 填实际用到的队列（D13） |
| 3 | 按标签声明（D11）；上传系统只做到查 fence 为止（D6）；拷贝前屏障的起点和"是不是新的"校验改成 `Initial`；独占 buffer 重复上传的校验（D12） |
| 4 | DX12 按掩码选 layout（D13）；共享 image 的上传（D7） |

D11 的细节定下来之前，步骤 3、4 的内容还会变。

---

## 七、验证

分工不变：我保证编译和单元测试通过；画面和日志由用户看。

- 渲染图里跨队列的路径现在没有运行时覆盖（所有 pass 都在图形队列上），步骤 3 只能靠读代码和单元测试。
- 独占资源的所有权交接在 DX12 上没有对应物，Vulkan 那一列跑不出来，只能与 §三 对照。
- 通用 layout 在计算队列上当屏障起点、终点的各种组合（D13）同样跑不到，只能对照规范的兼容表。
- 要在日志里看的：DX12 调试层有没有新的报错；#1356 还在不在。

---

## 与旧文档的关系

- `TODO_UploadHandoffPlan.md`：§七"换所有权和改状态拆开"由本文展开。它的 D8（三条屏障的表）、
  D11 里"共享模式 image 的上传"和"DX12 后端对 buffer 的换队列屏障一律不发"由本文取代。
  本文的 D11 还取代了它的 D1 里"渲染图不认识这类资源"、D5（帧开头执行接回）和 D7（固定的落点）：
  拷贝和释放仍归上传系统，接回和落点回到渲染图，由 pass 的声明驱动。D2（就绪在 CPU 上判断）、D9（读的一方都问就绪）沿用，
  就绪的时刻提前到拷贝的 fence 完成。
- `TODO_EnhancedBarriersPlan.md`：D5（释放转到 COMMON、接回从 COMMON 转出）由本文的 D2、D9 取代，接回不再转出，
  由接收方的屏障转。D2、D3 对 `None` 的定义沿用。D4 里"`ImagePool` 新建资源的记录是 `None`"改成 `Initial`。
  D8、D9 的断言沿用，D8 里对"初始状态是 `None`"的豁免改成 `Initial`。
  D2 里"一律用专用版"由本文的 D13 取代；D8 末尾写的方向这里做了按掩码选 layout 那一半，默认值没有改。
- `TODO_SubresourceBarrierPlan.md`：D4、D7 里按子资源挂释放、接回的做法沿用，挂的东西从屏障换成换队列。

---

## 参考

- [vkCmdPipelineBarrier2 refpage](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdPipelineBarrier2.html)
- [VkImageMemoryBarrier2 refpage](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageMemoryBarrier2.html)
- [VkImageCreateInfo refpage](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageCreateInfo.html)
- [Vulkan spec: Synchronization](https://docs.vulkan.org/spec/latest/chapters/synchronization.html)
- [D3D12 Enhanced Barriers spec](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html)
- 仓库内：`TODO_UploadHandoffPlan.md`、`TODO_EnhancedBarriersPlan.md`、`TODO_SubresourceBarrierPlan.md`
