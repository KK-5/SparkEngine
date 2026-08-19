# 全局 GPU 缓冲区:稳定槽位与数据上传

`g_Instances` / `g_Materials` / `g_Lights` / `g_Views` 这四块引擎级、shader 可见的数组,今天是同一段代码抄了四遍
(满配 staging → 每帧全量 scatter → `PendingBufferMap` → 每帧重绑 SRV),而槽位策略三种各不相同。

本文统一它们:**槽位稳定,编码增量,上传整体。**

> 落地后修正:抄了四遍的**只是 buffer 管线那部分**,它对四个数组一律适用,现在是 `StagedArrayBuffer`。
> 「槽位稳定」则只对其中一部分成立——判据见第一节,`g_Lights` 不在其中。

---

## 落地状态(2026-08-19)

| | 状态 |
|---|---|
| `StagedArrayBuffer`(下层:buffer 管线) | **已落地** — `Binding/StagedArrayBuffer.h` |
| `GlobalBuffer`(上层:稳定槽位) | **已落地** — `Binding/GlobalBuffer.h`,组合下层 |
| `g_Instances` | **已落地并验证** — `Binding/Instance/`,旧 `Feature/Render/Instance/` 已从 CMake 摘掉但仍留在树上 |
| `g_Materials` | **已落地并验证** — `Binding/Material/`,旧 `MaterialBind/` 同样已摘未删 |
| `g_Lights` / `g_ShadowViews` | **已落地,但不走稳定槽位** — `Binding/Scene/`,只用下层 `StagedArrayBuffer`,保持稠密迭代序。理由见第一节判据 |
| `g_Views` | 未做,且与 `TODO_MultiViewPlan.md` 耦合,单独立项 |

相对本文原稿有两处实质改动:

1. **槽位引用补上了占用世代号**,见第二节。
2. **`GlobalBuffer` 拆成两层**,且 **`g_Lights` 被移出稳定槽位的适用范围**——原稿第十一节把「`g_Lights` 接入」列为下一步,那个立项是错的,见第一节。

其余各节按原稿落地。

---

## 一、范围

分两层,**适用范围不同**:

| | 管什么 | 谁在用 |
|---|---|---|
| **下层 `StagedArrayBuffer<Element>`** | 任何要每帧整块传给 GPU 的结构体数组 | `g_Instances`、`g_Materials`、`g_Lights`、`g_ShadowViews` |
| **上层 `GlobalBuffer<Tag, Element, Sources...>`** | 其中**需要稳定槽位**的那些 | `g_Instances`、`g_Materials`、(将来)`g_Views` |

**都不管**:场景 CBV 单例(`g_LightCount`、环境常量)、per-view 的 CBV SRG、per-pass binding。它们不是数组,
几十字节整体重写,套模型是负收益。

### 判据:什么时候该上稳定槽位

> **稳定槽位的收益,是让「别处存着的那个下标」不必每帧重写。没有那个「别处」,稳定槽位就是纯成本。**

| 数组 | 「别处」是谁 | 稳定槽位 |
|---|---|---|
| `g_Instances` | DrawItem 的 `StartInstanceLocation`——CPU 侧烘一次,到回收前不变 | 要 |
| `g_Materials` | `InstanceData.m_materialIndex`——**存在另一个 GPU 数组里** | 要 |
| `g_Lights` | **不存在**。没有任何东西持有光源下标,shader 自己遍历 `[0, g_LightCount)` | **不要** |
| `g_ShadowViews` | 有,但槽位归 `ShadowAtlasAllocator` 管,不归本文 | 不适用 |
| `g_Views` | `LightData.m_shadowIndex` 等 | 要 |

原稿把四个数组一律划进稳定槽位,是**错的**。`g_Lights` 一旦有了空洞,shader 那个 `for (i < g_LightCount)`
就会遍历过陈旧记录——场景里留下一盏删不掉的灯。要么补 GPU 侧有效性信号(第十节留空的那一项),要么另开一条
活跃索引数组,两条都是为了修一个自找的问题。

往前看也不成立:上了 clustered 之后「别处」确实会出现(cluster 的 light index list),但**那个 list 本身就是
每帧重建的**,恰恰不需要被它指向的下标稳定。这也是 UE / Unity HDRP / Frostbite / Atom 一致的做法——GPU 光源
数组每帧稠密重建,遍历走 index list,没有谁让 shader 去跳空洞。

---

## 二、数据结构

### 两个组件,归属刻意不对称

```cpp
// Core/CoreComponents/Tags.h —— 和 DeadTag / ActiveTag 同处
struct DirtyTag {};

// 渲染层
template<typename Tag>
struct GlobalBufferSlotRef
{
    const eastl::vector<uint32_t>* m_versions = nullptr;
    uint32_t                       m_id       = UINT32_MAX;
    uint32_t                       m_version  = 0;

    bool IsValid() const;   // m_id < size && (*m_versions)[m_id] == m_version
};
```

| | 谁打 | 谁读 | 一个实体有几个 | 归属 |
|---|---|---|---|---|
| `DirtyTag` | **上层写入方** | 多个消费方 | 1 个 | **Core,不模板化** |
| `GlobalBufferSlotRef<Tag>` | 本系统 | 本系统 + **下游拷贝方** | **原则上可能多个**(至今无实例,见第十一节) | **渲染层,模板化** |

### 为什么槽位引用要自带世代号

原稿写的是裸 `uint32_t m_slot`。落地时发现这不够——**去掉间接表之后,裸下标不足以回答"这个槽位还是不是我的"**。

id 会被回收再分配。下游(`Drawable.m_instanceData` 里的 `SlotInstanceBinding`)持有的是一份**拷贝**,而拷贝没法被上游置位。
于是"实体死了"和"这个 id 现在归别人了"在下游看起来一模一样:

```
帧 N  free:    释放 id 7
帧 N  alloc:   新实体拿走 id 7           ← 同一次 Update 里,紧挨着
帧 N  下游查:  "7 还有效吗" → 有效       ← 但不是它的了
```

旧实现踩的是同一个坑(`m_slots[7]` 当帧被新实体写回有效值),只是要同帧一死一生才触发。稳定槽位让它显式化。

**解法:让引用自我描述。** `GlobalBufferSlotRef` 同时带上"哪个数组"(版本数组指针)、"哪一格"(id)、"哪一次占用"(version)。
一份拷贝自己就能答 `IsValid()`,不需要任何指向上游的句柄——这是 `DrawItemRouter` 能对 instance 一无所知的前提。

两个副产品:

- 释放时 `++m_versions[id]` 会连**源实体自己那份组件**一起判失效,所以回收循环的重入护栏(`DeadTag` 实体活过一帧被重复回收)自己长出来了,不用另写哨兵。
- 指针指**容器**而非 `data()`,所以将来容量增长 resize 不会让已发布的引用集体悬垂。

代价:引用 16 字节;版本数组 capacity × 4B(65536 时 256 KB),CPU 侧,GPU 不可见。

不对称是有理由的:标记模板化会逼着 `TransformSystem` 去 include 渲染层的 tag 类型,依赖方向是反的;而槽位上层
从不碰,模板化不引入任何反向依赖,且一个实体确实可能同时占几个数组的槽位,不模板化表达不了。

**标记共享、槽位分立。**

Tag 直接对上 shader 侧符号:`GlobalBufferSlotRef<Instances>` / `<Materials>` / (将来)`<Views>`。不与已有的
`InstanceBindingTag` / `MaterialBindingTag`(那些是 SRG 实体的 tag)撞名。**没有 `<Lights>`**,理由见第一节。

组件挂在**源实体**上,源在哪个 context 就挂哪:

| 数组 | 源实体所在 |
|---|---|
| Instances | world |
| Materials | material context |
| Views | RHIContext(view 实体) |

泛型需按 context 参数化。`g_Materials` 落地时验证了这一条:`MaterialContext = BasicContext<MaterialHandle>`
直接代入,实体类型不是 `Entity` 也没有从任何缝里漏出来。

### 每个数组两块内存 —— 这就是 `StagedArrayBuffer`

- **`staging[Capacity]`** —— CPU 侧**唯一真相**,普通可缓存 RAM,常驻,只有被编码时才改写
- **N 份 host-visible buffer**(`Component.h:239` 的 `BufferPerFrame`)—— staging 的完整快照

这一层刻意**不提供 `Update`**,只暴露三样:`BindFrame(frameIndex)` / `operator[]` / `Upload(count)`。
「谁写进第几格」是调用方的策略,而那正是三个用户唯一不同的地方:

| | 填充策略 | `Upload` 长度 |
|---|---|---|
| `g_Lights` | 稠密迭代序 | 活跃数 |
| `g_ShadowViews` | 先整体清零,再按 atlas 行号散写 | `Capacity()` |
| `GlobalBuffer` | 回收 → 分配 → 编码 | `Size()`(高水位) |

把这一层和槽位捆在一起,正是原稿导致「`g_Lights` 接不进去」的直接原因。

---

## 三、每帧四步

```
0. 绑定   BindFrame(frameIndex)  → 失败(buffer 尚未 materialize)则整帧跳过    ┐ StagedArrayBuffer
1. 回收   GetView<SlotRef, DeadTag>              → ++version, 槽位归还分配器  ┐
2. 分配   GetView<Src>(Exclude<SlotRef, DeadTag>)                             │ GlobalBuffer
                                → 取槽位, Add<SlotRef>{versions, id, version} │
3. 编码   GetView<SlotRef, Src>(Exclude<DeadTag>)                             │
                                → process(entity, staging[id], src...)        ┘
4. 上传   Upload(Size()) → PendingBufferMap{ staging.data(), 0, ... }         ┘ StagedArrayBuffer
```

第 0、4 步属于下层,任何用 `StagedArrayBuffer` 的数组都走这两步;第 1~3 步是稳定槽位那一层独有的。

第 3 步的 `process` 回调是唯一的类型专属代码,其余全泛型。`DirtyTag` 尚未接入(见第十节),第 3 步目前无条件全量。

**第 4 步就是今天那一行**,唯一差别是长度从「活跃数」变成「高水位」。RHI 侧零改动。

---

## 四、三条关键性质

### 1. 增量只发生在第 3 步

贵的是编码——每对象一次 3×3 求逆 + 转置 + 材质句柄查找 + 几次稀疏集查找,量级在毫秒。跳过不动的对象就是
跳过这些运算。

第 4 步是纯字节顺序搬运,10000 个对象约 1.4 MB。**不做增量**,理由见下条。

### 2. staging 完整 ⇒ 副本天然完整

多副本「每份历史不同」的问题**只在部分写入时存在**:第 k 份上次被写是 N 帧前,只写本帧脏项就会缺中间几帧的
改动,表现为**物体在新旧位置之间以 N 帧为周期永久抖动**(它已经停下不动了)。

而 staging 是完整镜像,整体 memcpy 让副本 k 一次性完整。于是**不需要**倒计时、不需要 N 帧铺开、
`frameCountMax` 变化时也不需要全量重铺。

顺带解决预热:buffer 在 `Init` 时还不存在(延迟创建路径),这几帧编码照常进 staging,第一次成功上传就把全部
内容带过去,**不需要重放预热期的脏标记**。

**CPU 侧那份独立的完整镜像,就是「每帧整体拷一份」能成立的全部依据。** 省掉它、直接往映射指针写,完整镜像就
不存在了,立刻掉回上面那个抖动问题。

### 3. 写入模式对上了 upload heap

upload heap 通常是 write-combined:对**顺序流式写**优化,对散写、部分写、读改写很差(无读缓存)。

而编码恰好是散写 + 读改写(隔着空洞跳着写)。所以「在可缓存 RAM 里编码完 → 一次顺序流进 WC」比「直接在 WC
里散写」**更快**。staging 不是为了解耦多付的一次拷贝,是选对了写入模式。

---

## 五、`DirtyTag` 的生命周期:帧尾集中清

**消费方只读,不 `Remove`。**

因为共享之后一个实体可能有多个消费方(光源的变化,`g_Lights` 编码要看,shadow view 生成也要看),谁先跑谁
把 tag 拿走,后面的就漏。

| | 谁 |
|---|---|
| 打标记 | 上层写入方,`AddOrReplace`(幂等) |
| 读 | 所有消费方,只读 |
| 清除 | 一个系统在 `TICK_LAST` 附近 `Clear<DirtyTag>()` |

先例:`RenderGraphExecuter::End` 里那一串 `passContext.Clear<...>()` 就是帧尾集中清帧作用域组件。

**代价是过度标记**:实体因任何原因变化,它所有消费方都会重编码一次。代价是多一次 `Encode`,不是正确性问题。
这是有意识的取舍。

**时序约定**:所有源的写入方应排在消费它的 binding 系统之前。排在之后的系统自己承担延迟一帧的代价,不需要
机制兜底。

---

## 六、分配器

空洞在整套设计里**只在一处花钱**:第 4 步的 memcpy 范围是 `[0, 高水位)`,空洞是白拷的字节。第 1~3 步遍历的
都是 ECS view,空洞既不被编码也不被遍历。

两条纯策略,**不让任何对象搬家**:

- **最低空闲优先分配**(位图 find-first-zero 或有序空闲表),不用今天的 LIFO `pop_back` —— 活跃集合向 0 端聚拢
- **高水位可收缩** —— 释放的正好是 `高水位-1` 时,向下走到第一个占用位

**「不搬家」排除了另一条路,值得记下来。** Atom 的 `IndexedDataVector<T>` 做的是**稠密 + 句柄稳定**:数据永远
紧密排列,另维护 index↔data 双向重映射表,删除元素时把最后一个元素**搬进空洞**再修补映射。它同时拿到了稠密
(没有空洞、上传长度就是活跃数)和外部句柄稳定。

我们不能这么做,因为我们那个下标**已经烘进 DrawItem 了**;Atom 能这么做,因为它那个下标只活在 CPU 侧重映射表
里,GPU 侧每帧整块重传。两条路各自自洽,**但不能混**——搬家一次就废掉所有已烘的 `StartInstanceLocation`。

**已落地**:最低空闲优先(`eastl::*_heap` 上的最小堆)。高水位收缩没做——`Size()` 只增不减,代价是 memcpy 长度不回落。

**回收延迟参数 M**:死亡后 M 帧才可再分配。今天 M=0。

注意它**不再**是 CPU 侧消费者的正确性依赖——世代号(第二节)在结构上解决了 id 复用,不依赖任何时序约定。
M 仍然要为 **GPU 侧**加上:接 GPU 剔除时跨帧 GPU 状态按槽位寻址,GPU 那边没有世代号可比,「上一帧可见的 42 号」
会变成另一个对象——只错一帧、随机复现,与 `TODO_MultiViewPlan.md` §五 决策 1 踩过的是同一族坑。

**空洞里的陈旧数据不清零**——没人索引它,清零是白花的写。将来 GPU 侧要遍历 `[0, 高水位)` 时再补有效性信号。

---

## 七、上传接口保持声明式

`PendingBufferMap`(`Component.h:157`)是「给一块内存,后台自动拷过去」,不是「把映射指针交出来自己写」。
这个形状要保持,理由三条:

1. **生命周期安全** —— 映射指针不能越过 Unmap、不能被缓存、buffer 重建后悬垂;声明式请求的源内存是调用方
   自己的,没有这类隐患
2. **集中在 `RHIResourceSystem` 才有优化空间** —— 目的地互不重叠是**构造保证**的,所以并行拷贝**无需任何
   同步**;将来还可以子分配到一块大 buffer、持久映射,而这些对调用方完全透明
3. **写入模式** —— 见第四节第 3 条

将来第 4 步真要增量化时(见第十节),接口从「一段区间」扩成「一组区间」:

```cpp
struct PendingBufferMapRanges
{
    const void*                    m_src;      // staging 基址
    eastl::vector<BufferCopyRange> m_ranges;   // { srcOffset, dstOffset, byteCount }
};
```

**仍是声明式、目的地互不重叠、可无锁并行**——是加法,不是改造。

---

## 八、落到四个数组

| | 今天 | 之后 | 连带 | 状态 |
|---|---|---|---|---|
| **Instances** | 稳定 id + `InstanceSlotTable` **间接** | 槽位 = 稳定 id | 删 `InstanceSlotTable`、删 `DrawItemInstanceSlot`、`m_instanceOffset` 变成烘一次的常量 | **已完成** |
| **Materials** | **迭代序**,每帧回写 `MaterialGPUSlot` | 稳定槽位,分配一次 | 删 `MaterialGPUSlot`,解开与 `InstanceBindingSystem` 的 tick 顺序耦合(见下) | **已完成** |
| **Lights** | 迭代序 | **保持迭代序**,只换用下层 `StagedArrayBuffer` | 删 `BindFrameLights`,提取 `PackLightData` | **已完成** |
| **ShadowViews** | 按 atlas 行寻址 | **不变**,只换用下层 | 删 `BindFrameShadowViews` | **已完成** |
| **Views** | 只有 shadow view 有 tile 槽位 | 统一 `g_Views`,吸收 `g_ShadowViews` | | 待做,单独立项 |

Lights 那一行是本文最初判断错的地方,理由在第一节的判据。它拿到的是**buffer 管线的复用**,不是稳定槽位——
后者对它只有成本。

**规模差异决定要不要接脏标记**(只对上了稳定槽位的那些有意义):

| | 容量 | 每帧全量编码的成本 | 是否值得接 `DirtyTag` |
|---|---|---|---|
| Instances | 65536 | 每对象一次矩阵求逆 | **值得** |
| Materials | 1024 | 字段拷贝 | 否,第 3 步无条件全量即可 |
| Views | 十几 | 相机每帧都在动,本来全脏 | 否 |

**顺带解开一条时序依赖**:今天 `MaterialBindingSystem` 必须严格排在 `InstanceBindingSystem` 之前,因为后者要读
当帧写的 `MaterialGPUSlot`。槽位稳定后,材质下标只在**指派变化**时才需更新,per-frame 的 tick 顺序约束降级成
一次性的「槽位得先存在」。

---

## 九、遗留的隐式锁步(不是前置条件)

**`Device::GetCurrentFrameIndex()` 收口**,见 `TODO_AsyncUpload_RemainingIssues.md`。

声明式上传的另一面是:调用方不知道数据落到了哪一份副本,「写哪份」和「绑哪份」只能靠约定一致。今天是两个独立
计数器隐式锁步——`ProcessBufferMaps` 用 `RHIResourceSystem::m_frameIndex`(`RHIResourceSystem.cpp:355`),
`GlobalBuffer::BindFrame` 用 swap chain 的 `GetCurrentImageIndex()`。

> 原稿把这条写成本方案的**硬前置条件**,那是错的,已更正。它只对**增量上传**成立——增量上传下每份副本的历史
> 不同,选错副本会丢改动。而本方案是整体拷贝:staging 是完整镜像,任何一份副本被拷到的都是完整内容,选错副本
> 顶多是把这一帧的数据写进了另一帧的 buffer,下一帧又会被完整覆盖一次。**两个计数器同步递增时行为完全正确**,
> 不同步时表现为最多晚一帧,不会出现残缺数据。
>
> 所以它是一笔应该还的技术债(两个计数器锁步是隐式的,谁改动一边都会静默错位),但不阻塞第十节的任何一项。
> 真正把它变成硬前置条件的是「第 4 步增量化」。

---

## 十、明确留空的

| | 状态 |
|---|---|
| **谁打 `DirtyTag`** | 上层负责,不在本方案内。上层加之前,第 3 步无条件全量,方案照常成立 |
| **容量增长** | 今天固定,溢出丢弃(`Capacity` 溢出 `LOG_ERROR`)。稳定槽位让将来增长是平凡的(`slot k → slot k`);版本数组指容器而非 `data()`,增长时已发布的引用不悬垂 |
| **第 4 步增量化** | 满容量 9.4 MB/帧时再量。届时用第七节的区间数组形态,且第九节变成硬前置条件 |
| **回收延迟 M 的取值** | 等 GPU 剔除落地时定。CPU 侧已被世代号解决,这一项只为 GPU |
| **GPU 侧遍历空洞的有效性信号** | 同上。**注意这一项只对未来真的去遍历 `[0, 高水位)` 的 GPU 代码成立**——今天没有这样的代码,`g_Lights` 那个遍历循环走的是稠密数组,不是本文的槽位数组 |
| **高水位收缩** | `Size()` 只增不减。释放的正好是高水位-1 时可以回退,没做 |
| **SRG 创建收进泛型** | 三份 `Init`(Scene / Material / Instance)里的 shader 反射 + `PipelineLayoutDescriptor` + `ShaderBindings` + 建实体打 tag 是同构的,只差 shader 路径、spaceId、tag 类型三个参数。Scene 那处还额外做 `FindImageInput` 校验和 IBL 绑定,形态不同。三个用例都有了,**可以判了**,但要先决定 tag 类型走模板参数还是回调 |

---

## 十一、下一步(按顺序)

1. ~~**`g_Materials` 接入。**~~ **已完成**。第二用例证明了 `Ctx` 模板化是对的(`MaterialContext` 直接代入,
   实体类型 `MaterialHandle` 没有从任何缝里漏出来),且 `process` 回调确实是唯一的类型专属代码。连带删掉了
   `MaterialGPUSlot` 和那条 per-frame 的 tick 顺序依赖。
2. ~~**`g_Lights` 接入。**~~ **立项推翻**,见第一节判据。第三用例的真正产出不是"接进去",而是暴露了
   `GlobalBuffer` 把两层捆死了——拆出 `StagedArrayBuffer` 后 `g_Lights` / `g_ShadowViews` 拿到了想要的那半。
3. ~~**`PackLightData` 缺 `Exclude<DeadTag>`。**~~ **已修**。`EntityReaper` 在自己的 tick 里才销毁,所以标记
   死亡到真正销毁之间那盏灯还带着 `LightRenderData`,会继续照亮场景,且与 `ShadowViewSystem`(那边一直是排除的)
   对"这盏灯还在不在"判断不一致。
4. **删除旧 `Feature/Render/Instance/` 和 `MaterialBind/`。** 都已从 CMake 摘掉但仍在树上,等新实现稳定后删。
5. **`g_Views`。** 单独立项,与 `TODO_MultiViewPlan.md` 耦合。

> **「一个实体多个 `GlobalBufferSlotRef`」至今没有实例。** 原稿举的例子是「光源既占 `g_Lights` 又占 `g_Views`」,
> 而光源已经不占 `g_Lights` 槽位了。`g_Views` 落地时要重新找一个真实场景来验证这个性质,找不到就说明模板参数化
> 那条理由(第二节)只剩「依赖方向」一条支撑——那条本身仍然成立。

### 顺带暴露、但没做的

- **`UpdatePassBindings` 里剩下的每帧重建。** 拿掉 startInstance 之后,那个循环只剩
  `m_shaderBindings.clear() + push_back(objShaderBindings)`,而这个指针是 derive 时烘好的、到回收前不变。
  纯浪费。删它属于 `TODO_DrawItemShapePlan.md` 的 space4 上提那一步(`SlotInstanceBinding.m_sharedBindings`
  删除 + 各 pass 改走 `.Binds<InstanceBindingTag>()`),两件事一起做。

---

## 与其他文档的关系

- `TODO_DrawItemShapePlan.md` —— 讲提交路径的形状。交点只有一处:本方案让 `m_instanceOffset` 成为烘一次的
  常量,**已落地**,是那份文档「DrawItem 烘一次后只读」的最后一块。剩下的 space4 上提见第十一节末尾。
- `TODO_AsyncUpload_RemainingIssues.md` —— 第九节那笔债在那里(不是前置条件,见该节)。
- `TODO_MultiViewPlan.md` —— `g_Views` 吸收 `g_ShadowViews`;回收延迟与 §五 决策 1 同族。
