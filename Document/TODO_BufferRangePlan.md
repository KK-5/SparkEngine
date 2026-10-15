# 缓冲区间与几何池　设计思路

2026-10-15 讨论定下的方向，只记大体思路，细节后面再设计。

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
  - CPU 来源：CPU 写进主机可见、按帧轮转的暂存缓冲，图里一个 Pass 写进池的区间。普通几何是 Copy；Nanite 的页
    也属于这类，只是 Pass 是转码（磁盘编码 → GPU 格式）+ fixup。
  - GPU 来源：蒙皮、粒子这类每帧由 GPU 算出的数据，本来就是 Pass。
  - 池的状态只有图写，两种来源是同一种"对整块 Buffer 声明写"。

UE 的 GPUScene（scatter upload）和 Nanite 页池（上传 + 转码）都是这个做法；仓库里 `StagedArrayBuffer`
（主机每帧一份）是同类。

带来的简化：

- 上传系统不用改，也不需要池与拷贝队列共享。
- **就绪是平凡的**：几何系统在第 N 帧声明了拷贝，图保证它排在同帧的剔除和绘制之前，网格从第 N 帧起可用，
  不需要计数。
- 池的写入只有一种机制。

代价：几何数据不走异步拷贝队列，图里多一次 GPU 拷贝；暂存缓冲有限，要有每帧上传预算，大网格分几帧传完。
以后图支持把 Copy Scope 放到拷贝 / 异步队列上，这次拷贝可以挪过去，机制不变。

## 四、分层分工

| 层 | 负责 |
|---|---|
| RHI | `BufferPool` 只保留"一个 Buffer 一个原生资源"，删预算模式（大缓冲、VirtualBlock、`BufferMemoryType::Shared`、`GetBaseOffset`）；它仍是内存域（D3D12MA、堆类型、Map、延迟释放）。新增后端无关的 `QueueForRelease(function)`：等已提交的 GPU 工作完成后执行，延迟帧数只在 RHI 内部 |
| Core | 纯 CPU 的区间分配算法（自写空闲链表 / TLSF，或引入 OffsetAllocator），不依赖 D3D12MA |
| 渲染层 | 几何池 = 一个普通 Buffer + 区间分配器，发 RAII 区间句柄；句柄析构经 `QueueForRelease` 归还。`MeshGeometry` 持有区间而不是分段 Buffer。暂存环形缓冲 + 拷贝 Pass 也在这一层。第二个用例出现前不抽通用类 |

先例：阴影图集（Core 的 `QuadTreeAllocator` + 渲染层 `ShadowAtlasAllocator` 持有图集并发 RAII tile）。

## 五、对上层的影响

- **绘制**：区间偏移通过绘制参数（`vertexOffset` / `firstIndex`）传入。
- **DrawIndirect**：不再写死 `MeshGeometryBaseBufferTag`，改为和零散 Draw 一样接收顶点流附件，Pass 自己
  `p.Import<Tag>` + `s.InputAssembly`。`Uses<MeshGeometrySegmentTag>` 随之删除。
- **`Uses<Tag>`** 只留给"新资源经上传系统交接后、经绑定 / bindless 读取"的资源（材质贴图、IBL、查找表、图标）。

## 六、待设计

1. 暂存环形缓冲与拷贝 Pass 的形态：大小、每帧预算、超预算时如何分帧，Copy 还是 scatter compute。
2. 区间分配算法的选型。
3. 池放在哪个队列上被读写，以及图的 Copy Scope 将来能否上拷贝 / 异步队列。

## 七、前瞻

- **Nanite 类虚拟几何**：页池 = 一块被 GPU 写、被多个 Pass 读的大缓冲，按整资源同步；页按槽分配（Core 的定长
  槽分配器，流式系统持有）；淘汰的页经 `QueueForRelease` 延迟复用；页数据经暂存 + 转码 Pass 写入；页池以 SRV
  读，走 `ReadBuffer` 附件。与本方案一致。
- **API 趋势**：同步变粗（DX12 缓冲屏障整资源 + global barrier，Vulkan 推荐无所有权转移时用 global memory
  barrier）、内存地址化（buffer device address、bindless）。"大缓冲 + 偏移"顺应这个方向。
- **若将来需要区间级并行**：扩展点是让图里的缓冲访问带可选区间、按区间判冲突，是加字段，不需要把区间重新做成
  Buffer。

## 八、相关但单独实施

零散 Draw（已定，先做）：`RenderScope::Draw(arguments, span<const Attachment> vertices[, IndexStream])`，
缓冲经 `p.Import` + `s.InputAssembly` 成为附件，步长取自 Pass 的输入布局；同时删除 `MeshGeometryComposer`、
`DrawItemRouter`、`GeometrySpec`、`Accepts`。场景内容走 indirect，Draw 只用于零散用途。
