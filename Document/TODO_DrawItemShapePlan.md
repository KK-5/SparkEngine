# DrawItem 形态:瘦身、合并,与变体的边界

`TODO_MultiViewPlan.md` §八「DrawList 去持久化」落地之后的一次形态复核。结论集中在两件可以现在做的事
(RHI `DrawItem` 瘦身、Drawable 与 DrawItem 合并到同一实体),外加一组决定它们长期是否成立的边界条件。

**以已落地代码为准**,不引用任何文档里的将来方案。

> **落地状态(2026-08-20)**:三条决策**全部已实现**,顺序为 决策三 → 决策一 → 决策二。原分析中与代码或
> 事实不符的处已就地订正并标注(引用块或删除线小标题)。
>
> 实测效果:`DrawItem` **1400 → 616 字节**(−56%),每对象份数 **3 → 1**,等效每对象 4200 → 616 字节。
>
> 落地时另有两项文档未预见的改动:`Drawable` 已改名为 **`GeometrySpec`**(见第三节末),`DrawableTag` 已删除
> (与组件 1:1 冗余)。**下文仍用 `Drawable` 指代该类型**,读代码时对应 `GeometrySpec`。

---

## 一、现状里的两处冗余

### 1. `RHI::DrawItem` 的 1400 字节里,渲染层用 8 字节

> **订正**:本节原按内联负载估算,得出「四项合计 640、整体约 800」。实测(MSVC x64)两处都偏低,且漏算了
> 结构里最大的一项。下表为实测值。

`RHI/Command/DrawItem.h` 的四个 inline 缓冲区(`Limits::Pipeline` 的上界:`ShaderInputGroupCountMax = 8`、
`RootConstantByteCountMax = 256`、`AttachmentColorCountMax = 8`;`Viewport` 24 字节、`Scissor` 16 字节)。
`eastl::fixed_vector` 除内联缓冲外还带约 40 字节簿记(3 个指针 + pool 指针 + overflow allocator),所以按
内联负载估会系统性偏低:

| 字段 | 实测 | 内联负载 | 渲染层实际用量 |
|---|---|---|---|
| `m_shaderBindings`(8 × ptr) | 104 | 64 | **8**(一个 per-object SRG) |
| `m_viewports`(8 × 24) | 232 | 192 | 0 |
| `m_scissors`(8 × 16) | 168 | 128 | 0 |
| `m_rootConstants`(256 × 1) | 296 | 256 | 0 |
| 合计 | **800** | 640 | **8** |

**但这四项不是结构里最大的。** `m_vertexBufferView` 是 `fixed_vector<VertexInput, 12>`,`VertexInput` 40 字节
(`VertexInputView` 32 + slot 4 + padding),实测 **520 字节**。整个 `DrawItem` 约 **1400 字节**。

**真正的代价不是内存,是提交时的 cache。** `SubmitDrawBatch`(`RenderGraph/RenderGraphExecuter.cpp:645`)每个
draw 做一次 `Get<RHI::DrawItem>`,1400 字节意味着每次拖进约 22 条 cache line,而被读到的只有几何视图和 draw
参数那 60 来字节。删掉那四项后实测 616 字节 ≈ 10 条——**到不了 1~2 条,因为 `VertexBufferView` 还在**。

### 2. 一个 Drawable 派生 3 个逐字节相同的 DrawItem

`DrawItemRouter.cpp:272-293` 对**每个接受该 Drawable 的 pass** 各建一个实体:

| Drawable | 带的 tag | 接受它的 pass | DrawItem 数 |
|---|---|---|---|
| 网格 | `OpaqueTag` + `ShadowCasterTag`(`MeshDrawableComposer.cpp:165,168`) | DepthPre、GBuffer、Shadow | 3 |
| 全屏三角形 | `FullScreenTriangleTag` | Lighting、Skybox、Tonemap | 3 |

`BuildGeometryDrawItem(ctx, composed)`(`DrawItemRouter.cpp:124`)**不带 pass 参数**,三份数据完全一致。
per-object 的附加组件 `DrawItemObjectBinding` 同样与 pass 无关。(原文此处还列了 `DrawItemInstanceSlot`,
它已随 `InstanceSlotTable` 一起删除;`DrawItemObjectBinding` 也已随决策三删除。)

顶点布局的差异不构成反例:DepthPre 只读 POSITION、GBuffer 读四个通道,差异在 PSO 的 `InputStreamLayout` 里,
不在 `m_vertexBufferView` 里(`DepthPrePass.cpp` 里"POSITION 在 offset 0,不需要补 padding"那句注释说的正是
这件事)。同一份 VB view,不同 PSO 各取所需。

---

## 二、决策一:RHI `DrawItem` 瘦身

| 字段 | 处置 | 理由 |
|---|---|---|
| `m_viewports` / `m_scissors` + 两个 count | **已删**(400 字节) | viewport 已归 view,`ExecuteDrawListState` 设。消费端在 `CommandList::Submit` 的保存-覆盖-恢复三段一并删除 |
| `m_rootConstants` + `m_rootConstantSize` | **已注释保留**(296 字节) | 零生产者、`CommandList` 也不读;但它是能进 indirect 记录的绑定层,留作扩展 |
| `m_shaderBindings` + `m_shaderBindingsCount` | **已删**(104 字节) | 唯一内容是 per-object SRG,随决策三消失(见第四节) |
| `m_pipelineState` | **已删**(8 字节) | 零生产者,见下 |
| `m_stencilRef` | 保留(1 字节) | `CommandList.cpp:447` 无条件读;DX12 `OMSetStencilRef` / Vulkan `vkCmdSetStencilReference` 都是真正的 per-draw 状态 |
| `m_enabled` | **已删** | 确认零读取方 |

> **订正**:本节原主张保留 `m_pipelineState`,依据是「`SandBox/Program/RHI/BRDFLutGen.cpp:431` 在设它」。
> **那是引用错误**——那一行设的是 `DispatchItem::m_pipelineState`。全仓库五处 `m_pipelineState` 赋值
> (BRDFLutGen 一处、`EnvironmentBaker.cpp` 四处)**全部是 `DispatchItem`**;`RHI::DrawItem` 的三个构造点
> (`HelloTriangle.cpp:392`、`DrawShape.cpp:887`、`DrawItemRouter.cpp`)没有一处设它。
>
> 而两个真正的非 render-graph `DrawItem` 直用户,绑 PSO 走的是 `commandList->SetPipelineState(...)`
> (`HelloTriangle.cpp:389` / `DrawShape.cpp:880`)——字段在那儿,他们绕过去了。「留着免得 RHI 对直用户变
> 难用」这条理由被直用户自己的写法否定,故删除。

**root constant 已注释保留,不是删除。** 真回来时应当是几个 DWORD 的 inline,不要恢复成 256 字节内联——
DX12 的 64 DWORD 是 root signature 的总预算,不是单个 draw 该内联的量。

### 瘦身之后的形状

删完后 `DrawItem` = `{DrawArguments, DrawInstanceArguments, m_stencilRef, IndexBufferView, VertexBufferView}`,
实测 **616 字节**(1400 → 616,−56%)。配合决策三,**一个 SRG 指针都不剩**。

> **订正**:原文称这「**就是** indirect argument 记录的内容」。方向上靠近,但还不是——剩下 616 里有 520 是
> `m_vertexBufferView` 的 12 槽内联,而一条 indirect 记录不会内联 12 个顶点流视图。要真走到那个形状,
> `StreamCountMax` 在这条路上得降下来(今天 mesh 用 2 槽:几何 slot0 + ID 流 slot1;全屏三角形用 0 槽,
> 12 是纯 headroom)。**该步没有 profile 支撑,且是减能力,故未做。**

另修正一个常见说法:「indirect 记录里放不下绑定」**只对 descriptor table 成立**。
`D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT` / `_CONSTANT_BUFFER_VIEW` / `_SHADER_RESOURCE_VIEW` /
`_UNORDERED_ACCESS_VIEW` 都是合法的 command signature 条目,**唯独 descriptor table 不是**。所以 GPU-driven
关掉的是 descriptor table 的 per-draw 用法,root constant 与 root descriptor 是通的——这也是 root constant
值得注释保留的原因。

按「跟随更严格的后端」:基础 Vulkan 的 `vkCmdDrawIndirect` 只带 draw 参数,per-draw root constant 要靠
device-generated commands 扩展。两边取交集仍然只有 draw 参数——**终局结论不变,推理链换一条**。

---

## 三、决策二:Drawable 与 DrawItem 合并到同一实体

**合并的是实体这一层,不是组件这一层。** 两个组件挂在同一个实体上,`Drawable` 组件继续存在。

### 直接消失的东西

- `DerivedDrawItems`(每个 Drawable 上一个 `fixed_vector<RHIHandle, 8>`)
- `DrawItemsDerivedTag`(它存在只是因为 `DerivedDrawItems` 空建时不能兼作过滤器)
- `ReapDerivedDrawItems` 与 `MaxPassesPerDrawable` 这个上界
- router 里嵌套的建实体循环,变成"对每个接受的 pass 只打一个 tag"
- **另加:`DrawableTag`**。它在 6 处添加点全都紧挨 `Add<Drawable>`,从不单独出现,是与组件 1:1 的冗余。
  删掉后 `GetView<DrawableTag, Drawable>` 退化成单池迭代的 `GetView<GeometrySpec>`。

`m_markDrawItem` 改成打在 Drawable 实体上;`CollectPassDrawItems<PassTag>`(`Pass/PassCapabilities.h:164`)
**一个字不用改**——它查 `GetView<PassTag, DrawItem>`,合并后找到的就是 Drawable 实体。

派生过滤器由 `Exclude<DeadTag, DrawItemsDerivedTag>` 变为 `Exclude<DeadTag, RHI::DrawItem>`:
**`DrawItem` 在场 ⟺ 已派生,是结构保证而非靠额外 tag 维护的约定。** 这比原文给 `DrawItemsDerivedTag` 的
理由(「`DerivedDrawItems` 空建时不能兼作过滤器」)更直接,也是这个 tag 能整个消失的原因。

`Add<RHI::DrawItem>` **无条件执行**,放在 pass 循环之前——若改成「有 pass 接受才加」,一个零 pass 接受的
Drawable 会每帧重试到永远。这与原先无条件加 `DrawItemsDerivedTag` 是同一语义。

### 落地时踩到的一个坑:pass tag 兼作 draw tag 会撞 entt 断言

四个 SandBox sample 建 drawable 时把自己的 `SPARK_PASS_TAG(...)` 当分类 tag 打了上去(省事写法)。合并之前
`m_markDrawItem` 打在**新建的 DrawItem 实体**上,两者不碰面;合并之后它打在同一个实体上,于是同一类型
`emplace` 两次,触发 `sparse_set.hpp` 的 `ENTT_ASSERT(elem == null, "Slot not available")`。

引擎自身不受影响——它的 draw tag(`OpaqueTag` / `ShadowCasterTag` / `FullScreenTriangleTag`)与 pass tag 是
不同类型。修法是给 sample 各自的分类 tag(`SandBox/Program/RenderGraph/SampleDrawTag.h`),**而不是**把
`MarkPassTag` 放松成 `AddOrReplace`:「一个 pass 只标记一次」这条严格语义值得留着,正是它把这个偷懒写法
逼了出来。

**这项修复必须与合并同时或先于合并落地。**

### ~~合并后可再削的两个字段~~ —— 撤回

原文主张合并后可删 `Drawable::m_drawArgs` 与 `m_instanceCount`,视作 `DrawItem` 的纯重复。**这条是错的,
且与本节后半段自相矛盾。**

composer 写 `Drawable` 那一帧 buffer 通常还没落地,`DrawItem` 尚不存在——`m_drawArgs` 正是「未解析形态」
的一部分,删了生产者没地方写。这正是下一小节论证「`Drawable` 必须能承载尚未解析的状态」的直接推论。

合并后它确实变成派生后无人读的死重(约 36 字节/对象),但那是**必要的输入缓冲**,不是冗余。两个字段保留。

### 为什么不能更进一步,把 `Drawable` 组件也去掉

`Drawable` 和 `DrawItem` 不是同一份数据的两个粒度,是两个**形态**:

| | `Drawable` | `RHI::DrawItem` |
|---|---|---|
| 几何引用 | `RHIHandle` 指向 buffer **实体** + 字节范围 | `VertexInputView(*buf->m_buffer, ...)`——真实资源 |
| 写入时机 | 资源**还没创建**就能写 | 必须资源已就绪 |

`BuildGeometryDrawItem` 不是中转拷贝,是 **handle → resource 的解析**,而解析必须有一个承载「尚未解析」状态的
形态。三件 `DrawItem` 结构上做不到的事:

1. **就绪门控。** `DrawableReadyToDerive`(`DrawItemRouter.cpp:77`)逐个查
   `TryGet<Components::Buffer>(handle)->m_buffer` 是否落地。资源是延迟创建的,composer 写 Drawable 那一帧
   buffer 通常还不存在,靠「没打 `DrawItemsDerivedTag` ⇒ 下一帧重试」兜住。`DrawItem` 存的是
   `VertexInputView(*buf->m_buffer, ...)`,**构造时就要解引用一个已存在的 Buffer**,写不出「未就绪」。
   去掉 Drawable,每个 producer 都得自己实现重试和就绪判断。
2. **生命周期级联的边。** `DrawableTag` 的注释已写明:级联 reap 顺着 `m_streams` 的 buffer handle、
   `m_index.m_indexBuffer`、以及 instance 策略的依赖走——**那些 handle 就是依赖图的边**。`DrawItem` 里只有
   解析后的裸 `Buffer*`,没有实体身份,顺不回去;buffer 实体一死就是悬垂指针,且下一帧才炸。
3. **供给策略。** `m_instanceData` 的 variant(none / slot / direct)决定 startInstance 从哪来、per-object
   SRG 怎么取。DrawItem 上只剩解析**结果**,策略本身在解析时被消费掉了;资源重建要重新解析时策略得还在。

**这层不是可省的间接,是「资源延迟创建」的必然产物。** 削完之后两者分工可以一句话说清:
**`Drawable` = 依赖边 + 供给策略,`DrawItem` = 它的已解析缓存。**

### 落地时的追加改动:`Drawable` → `GeometrySpec`

合并**引入了一个命名歧义**:同一个实体上挂着 `Drawable` 和 `DrawItem` 两个组件,那么「drawable」指实体还是
指其中一个组件?合并之前 `Drawable` 是独立实体上的唯一组件,没有这个问题。

改名方向定为「几何」,依据有二:

- 代码里早有伏笔——解析函数**本来就叫 `BuildGeometryDrawItem`**,只是类型名没跟上。
- `Spec` 接上文件里已有的 `VertexStreamSpec` / `IndexStreamSpec`,而 `GeometrySpec` 正是这两者 + draw args 的
  聚合体。更重要的是 **`Spec` 承载了「未解析 / 按句柄 / 声明式」这层意思**,恰恰是它与 `DrawItem` 的唯一
  本质区别:`GeometrySpec` → `DrawItem` 读作「规格 → 已解析的产物」。

`m_instanceData` 不是这个命名下的例外:`m_idStream` → slot 1 顶点流、`m_slotRef` → `m_instanceOffset`,
**两项都进入几何产物**(见第四节末的分界线),所以它就是「每实例流和 start-instance 从哪来」。

连带改名:`MeshDrawableComposer` → `MeshGeometryComposer`,`DrawableReadyToDerive` →
`GeometryReadyToDerive`,sample 的 `BuildDrawable()` → `BuildGeometry()`。
`BuildGeometryDrawItem` / `DrawItemRouter` / `WorldComposedTag` / `DrawTag.h` 及各分类 tag **不变**。
目录 `Drawable/` 保留——它装的是 `GeometrySpec.h` + `DrawTag.h` + router + composer,作为「可绘制对象」这个
概念区仍然准确,改名要动 8 个 pass 文件的 include,换不到清晰度。

### 顺带的收益

合并后每个对象的全部信息在同一实体上(决策三落地后就是 `Drawable` / `DrawItem` 两件,
`DrawItemObjectBinding` 与 `DrawItemInstanceSlot` 都已不存在),submit 少一层实体跳转。而 §八 剔除方案里那条「draws 数组旁
并排的 objectIds」,合并后**就是实体句柄本身**——那条被列为前置条件的稠密对象索引直接不需要了。

两项改动落在同一片代码,建议一起做:瘦身让单份变小,合并让份数从 3 变 1。

### 实际收益(落地后回填)

净删约 170 行,**零新增概念**。最大的一块不在引擎里:四个 SandBox sample 各有一份手写的级联销毁遍历
(顺着 `DerivedDrawItems` 逐个 `DestoryEntity`),合计约 56 行,合并后每处塌成一行 `destroyIfValid(m_drawable)`
——那是四份可能各自漂移的重复逻辑,还写在别人会照抄的示例里。

要理解一个 Drawable 的生命周期,原先要同时握住三条事实(DrawItem 自包含 / 但销毁要顺着反向引用 / 而反向
引用空建所以另需幂等 tag),后两条都是第一条的代价。合并后塌成一条:**一个实体,`DrawItem` 在场即已派生,
一个 `DeadTag` 全完。**

---

## 四、决策三:删 `DirectInstanceBinding`,space4 提到 pass 级

### 它已经是死路径

`Drawable::m_instanceData` 的三个策略里,被构造过的只有两个:`SlotInstanceBinding`
(`MeshDrawableComposer.cpp:52`,所有网格)和 `NoInstanceBinding`(`RenderSystem.cpp:173` 的全屏三角形,
以及 4 个 SandBox sample)。`DirectInstanceBinding` **零生产者**,只出现在 `DrawItemRouter` 三处穷举 visit
的分支上(`:61` / `:111` / `:183`)。

### 它是 `DrawItemObjectBinding` 必须 per-draw 的唯一原因

`Drawable.h` 上的注释已经写明:

> Carried here (not a shared tag) because **direct's CBV is per-draw, not a singleton** — it is the only
> binding left on the DrawItem.

而 `InstanceBindingSystem.cpp:100` 只建**一个** `m_bindingsEntity` 并打 `InstanceBindingTag`,每个网格的
`slot.m_sharedBindings` 指的都是它。**slot 路径下 space4 本来就是单例**,只是被 direct 这个从未出现过的可能性
拖着,不得不按 per-draw 存。

### 链条

```
删 DirectInstanceBinding
 → 每对象绑定要么是全局 g_Instances SRG(slot),要么没有(none)
 → space4 恒为单例,可提到 pass 级
 → DrawItemObjectBinding 消失
 → DrawItem::m_shaderBindings + m_shaderBindingsCount 整个消失
 → UpdatePassBindings 每帧的 clear + push_back 消失
 → DrawItem 成为纯几何 + 参数记录
```

`UpdatePassBindings`(`Pass/PassCapabilities.h:117`)那段**每帧对每个 DrawItem 重建一个永不变化的 vector**
正是被这条挡着的——`TODO_MultiViewPlan.md` §二 预期它消失,它没消失,原因在此。

### 落地路径是现成的

`.Binds<InstanceBindingTag>()` 直接可用:那个实体同时带 `InstanceBindingTag`、`InstanceSlotTable` 和
`Components::ShaderBindings`,而 `ResolveSharedBinding<Tag>` 做的正是 `GetView<Tag, ShaderBindings>` 的 join。

- **DepthPre / GBuffer / Shadow** —— shader `#include <InstanceBindings.hlsl>`,声明 `.Binds<InstanceBindingTag>()`
- **Lighting / Skybox / Tonemap** —— 画全屏三角形,不用 space4,不声明

> **订正**:原文称「`UpdatePassBindings` 剩下的只有 startInstance 解析,应改成一趟全局
> `GetView<DrawItem, DrawItemInstanceSlot>`」。两点都不成立:`DrawItemInstanceSlot` 已随 `InstanceSlotTable`
> 一起删除(见 `TODO_GlobalBufferUploadPlan.md`),且 startInstance 早已在 `BuildGeometryDrawItem` 烘一次,
> 不再每帧解析。
>
> 实际结果比原文预期更干净:`UpdatePassBindings` 里那个 per-DrawItem 循环**整个消失**,函数退化成一行
> `ResolvePassSharedBindings`,遂将其删除,`m_updateBindings` 直接指向后者。上面的链条末端应为
> **「→ `UpdatePassBindings` 整个消失」**。

### 顺带:`SlotInstanceBinding::m_sharedBindings` 一并删

它今天有三处用途,决策三之后一处不剩:

| 用途 | 位置 | 决策三之后 |
|---|---|---|
| 解析成 `DrawItemObjectBinding` | `DrawItemRouter.cpp:182` | **消失**(space4 提到 pass 级) |
| 就绪门控 `srgReady(...)` | `:109` | **直接删**,无需替代(见下一小节) |
| 存活边 `Has<DeadTag>(...)` | `:44` | **冗余**——重复见证者 |

它是**一个全局单例的 per-Drawable 副本**:`InstanceBindingSystem.cpp:100` 只建一个 `m_bindingsEntity`,
`MeshDrawableComposer.cpp:100` 每帧用 `GetView<InstanceBindingTag>` 取到同一个再抄进每个 Drawable。而它与
`m_idStream.m_buffer` 是**同一个生命周期事件的两个见证者**——两个实体由 `InstanceBindingSystem` 一起创建、
一起在 `Shutdown`(`:313`)打 `DeadTag`。存活边留一个就够。

**分界线:进入产物的留,不进产物的删。** 删完后 `SlotInstanceBinding` = `{ m_slotRef, m_idStream }`,两项都
进入产物——`m_slotRef` → `DrawItem::m_drawInstanceArgs.m_instanceOffset`(在 `BuildGeometryDrawItem` 烘一次,
不是每帧解析),`m_idStream` → 同一处 `setStream(...)` 烘进 DrawItem 的 slot 1 顶点流。`m_idStream` 的字段值虽然也全是
全局常量(`{idBufferEntity, slot 1, {0, idBufferBytes, 4}}`),但它决定 DrawItem 长什么样,是配方的一部分;
`m_sharedBindings` 不再进入任何产物,只剩簿记。

这条线正好对齐第三节给 `Drawable` 的定义(**依赖边 + 供给策略**):`m_idStream` 是供给策略的内容,
`m_sharedBindings` 曾经是、现在不是。

注记:`InstanceBindingSystem` 今天**没有运行时重建路径**——容量固定,`:235` 溢出是 `LOG_ERROR` + 丢弃新对象
而非扩容,唯一的 `DeadTag` 在 `Shutdown`。所以「revision 快照」这套机制目前只在关闭时走到,那时全部 reap 本
来就对。将来 buffer 变成可增长的,`m_idStream.m_buffer` 仍是那个见证者,机制不受影响。

### ~~前置:共享绑定需要「声明了但没编译」的门控~~ —— 前提不成立,已取消

原文主张 `srgReady(s.m_sharedBindings)` 保证「g_Instances 的 SRG 编译好之前 DrawItem 根本不存在」,并据此
要求一个 pass 级门控作为本节删除的前置。**两点都不对。**

1. **`srgReady` 查不出那件事。** `Components::ShaderBindings::m_bindings` 是个 `Ptr`,从
   `InstanceBindingSystem::Init` 创建起就非空(SRG `Init` 失败会 early-return,实体压根不建),没有第三态。
   而 `Init` 在 `SetUp` 阶段跑,早于第 0 帧。所以它实际只等价于「实体存在」。

2. **「已编译门控」这件事不存在。** `CompileShaderInputs`(`RenderGraph/RenderGraph.cpp:184`)跑在 Compile
   阶段,executer 在其后启动——同一帧内编译一定早于提交。**RenderGraph 保证被用到的 ShaderBindings 在
   DrawItem 提交前编译好**,不存在「声明了但没编译就画」的窗口。

所以判据就是「是否成功构建」,即 `ResolveSharedBinding` 现有的 `if (comp.m_bindings)`。`srgReady` 直接删,
无前置改动。

附带澄清:今天真正挡住预热帧的是 `MeshDrawableComposer` 的 `idBufferByteCount != 0`——那是 ID buffer 不是
g_Instances。两者在 `InstanceBindingSystem::Init` 一起请求、同一个 `OnFrameBegin` 落地,所以**靠相关性成立,
不是靠构造成立**。这一点在决策三落地后没有改变。

### 代价:这等于彻底关掉 per-draw SRG 这条路

不是免费的。将来真要回来,得把 space4 从 pass 级降回 per-draw,不是加个 variant 分支那么简单。

但那条路本来就**该关**:indirect argument 记录里没有 SRG 指针的位置,per-draw SRG 是 GPU-driven 唯一表达不了
的东西——留着一个逃生出口,而出口通向架构正在离开的方向。

唯一像样的未来候选是蒙皮的骨骼调色板,而它也不该走 direct,该走「`InstanceData` 里存一个偏移、指向索引化的
骨骼 buffer」。**第二用例来了也不会用这个出口。**

---

## 五、per-batch PSO 与 Atom / UE 的关系

常见的说法是「Atom / UE 的 DrawItem 是完全自包含的,顺序无关,任何情况下提交都正确」。按源码看这只对一半:
Atom 的 `RasterPass::SubmitDrawItems` 是先 `SetSrgsForDraw` 绑 Scene / View / Pass 三个 SRG、再设 pass 级
viewport / scissor,然后才循环 `Submit`;DrawItem 上的 `m_viewportsCount` 常态为 0。UE 的
`FMeshDrawCommand` 不带 viewport,PSO 存的是 4 字节全局表下标而非指针,提交时由 `FMeshDrawCommandStateCache`
增量去重。

准确的说法是:**数据上自描述,执行上增量应用**,而且 view / pass 那两层本来就在 pass 上绑。对齐之后差异很窄:

| | Atom | 当前实现 |
|---|---|---|
| 持久对象 | DrawPacket(建于物体加入时) | DrawItem 实体(router 建于 Drawable 加入时) |
| 每帧结构 | DrawList = `{DrawItem*, sortKey, filterMask}` | `m_draws` = `RHIHandle` |
| view / pass 状态 | RasterPass 绑 | executer 绑 |
| PSO | **per-item** | **per-batch** |
| 顺序 | **按 sortKey 排序** | **不排序** |

**真正的差异只有 PSO 归属和顺序两条。** 其余部分已经一致。

PSO 归 batch 的选择是合身的:延迟渲染的 GBuffer pass 少 PSO 正是延迟的定义性收益;而 per-batch 是
`ExecuteIndirect` **唯一能表达**的形态(一次 indirect 调用内不能换 PSO)。per-item PSO 的自包含 item 无法翻译
成 indirect 记录——UE5 的 Nanite 路径干脆绕开 MDC,就是这个原因。

**per-item 是 per-batch 在 V = M 时的特例**(一个 batch 一个 item ⇒ 每 item 前设一次 PSO ⇒ 与状态缓存逐字
相同),通用机制吞掉了特例。这是渲染层不需要 per-item PSO 的正确理由。

(原文接着说「RHI 层保留该字段是为了非 render-graph 用户」——见第二节的订正:那些用户并不使用该字段,
`m_pipelineState` 已删。)

一个不影响结论的脚注:完全退化时 batch 路径**每个 batch 调一次 `funcs.m_executeFunction`**(间接调用 + span
构造 + `SetSubmitRange`),比状态缓存的一次比较略贵。该极端被下一节的 V 有界排除。

### 薄 DrawItem 的一项额外收益

自包含的隐含代价是「item 是完整的」这个不变式**没有任何地方强制**——某个生产者忘填一个 SRG 就是一次静默的
错误渲染,且离现场很远。当前形态把它换成了「**一个消费者在已知边界上建立状态**」,出错的地方从 N 个降到 1 个。

镜像成本:**DrawItem 脱离 executer 建立的状态就不可提交。** 存在别的提交路径(SkyboxPass 在自己的 hook 里调
`SubmitDrawBatch`,`.CustomPipeline()` 也开着),今天没问题是因为 hook 拿到的是 executer 已切好的
`work.m_drawHandles`。这条契约要当成契约维护,不是碰巧成立。

---

## 六、边界条件:变体、排序、PSO 数量

上面两项改动在三种规模下都成立,但支撑它们的前提各不相同,记录在此以免被无意推翻。

### 「CPU 不排序」等价于「变体集合编译期可枚举」

推理是闭合的:

```
不排序 ⇒ run 的连续性必须由查询结构给出(m_submitBegin/m_submitEnd 与 DrawRange 切片都要求 run 是连续区间)
       ⇒ 变体必须是可枚举的 tag / 独立 pool,CollectPassDrawItems 按变体依次追加,边界自然掉出来
       ⇒ 变体集合不能运行时无界
```

反过来,**变体一旦运行时无界(节点式材质图),顺序就不再是查询的副产品**,只剩 CPU 每帧排序或增量维护顺序两
条路。这两句是同一个决定的两面,不能都要。

### `ExecuteIndirect` 的收益是 M / V,不是绝对值

即便 V 到 500、M 到 10000,那也是 500 次 indirect 对 10000 次 Submit,仍是 20 倍;**平滑退化,不掉悬崖**,只有
V ≈ M 时归零。而 GPU-driven 的主要收益(draw 参数与 count 来自 GPU 内存,剔除不需要回读)与 V 完全无关。

另需分清:**PSO 数量随 shader permutation 与 render state 组合增长,不随材质数量增长。** 1000 个共用 uber-shader
的 PBR 材质是 1 个 PSO,差异全走 space3。

### 三段路

| 阶段 | V | 分组 + Indirect | 排序 |
|---|---|---|---|
| 今天(固定材质模型) | ~1-10 | 收益大 | 不需要 |
| 节点式材质 + GBuffer | 数百 | 收益仍大(M/V ≈ 20-50) | **必须** |
| visibility buffer | 1 | 收益最大 | 不需要 |

中间那段是唯一需要排序的,也是唯一 V 无界的。变体真爆炸时工业界的答案不是改进分组,而是**把几何提交与材质
求值解耦**(Nanite:几何 pass 只写 visibility buffer,一个 PSO;材质图求值挪到后面按材质做屏幕空间 pass)。
而那个终点恰恰是 per-batch 分组的最佳情形——本文档的两项改动在三段路上都成立。

排序真要回来时代价也不大:M = 10000、32 位 sort key 基数排序约 4 趟,几十微秒。**「不排序」换不来对等的东西,
不值得为它放弃节点式材质。**

---

## 七、不要踩的三条

1. **变体索引不要放在实体上。** 它是 (object, pass) 的函数(同一片树叶在 GBuffer 是「遮罩 + 法线图」、在
   DepthPre 是「遮罩」、在 Shadow 是「遮罩 + 双面」,三个 id 空间基数不同、轴也不同)。合并成一个实体之后,
   实体带 N 个 `PassTag` 就装不下 N 份索引。正确位置是 **pass 侧一张按材质寻址的表**,collect 时查一次——
   `CollectPassDrawItems<PassTag>` 天然知道自己是哪个 pass。往实体上放是最顺手的写法,也是错的那个。

   **决策二已落地,所以这条从「将来注意」变成了生效中的约束:任何 per-(object, pass) 数据现在都没有地方
   放。** 落地前评估过三类候选,风险都低——变体索引按上文走 pass 侧表;剔除结果是 per-(object, **view**),
   `TODO_MultiViewPlan.md` §八 的方案是 view 实体上按对象索引寻址的位图,合并只让它更容易(实体句柄本身
   就是那个稠密索引);排序键排的是 draws 数组,键在数组里不在实体上。
2. **「不排序」是不透明路径的性质,不是架构不变式。** `TransparentTag` 已声明、零消费者;alpha 混合落地时
   由后往前的顺序是**正确性**不是性能,且顺序 per-view,会同时打破「所有 replay 共享 `m_draws`」和「不排序」
   两条。别让后续设计(尤其剔除位图)建立在这两条上。
3. **ECS 查询给的是集合,不是顺序。** 它替掉了 Atom「生产者 push 进 DrawList」那半,替不掉 DrawList 有序的
   那半。第 2 条是这件事唯一咬人的地方。
