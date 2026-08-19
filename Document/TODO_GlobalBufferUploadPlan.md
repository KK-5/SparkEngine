# 全局 GPU 缓冲区:稳定槽位与数据上传

`g_Instances` / `g_Materials` / `g_Lights` / `g_Views` 这四块引擎级、shader 可见的数组,今天是同一段代码抄了四遍
(满配 staging → 每帧全量 scatter → `PendingBufferMap` → 每帧重绑 SRV),而槽位策略三种各不相同。

本文统一它们:**槽位稳定,编码增量,上传整体。**

---

## 一、范围

**管**:按稳定槽位寻址的全局数组——`g_Instances`(space4)、`g_Materials`(space3)、`g_Lights`(space0)、
`g_Views`(space1 的数组形态,吸收今天的 `g_ShadowViews`)。

**不管**:场景 CBV 单例(`g_LightCount`、环境常量)、per-view 的 CBV SRG、per-pass binding。它们不是数组,
几十字节整体重写,套模型是负收益。

---

## 二、数据结构

### 两个组件,归属刻意不对称

```cpp
// Core/CoreComponents/Tags.h —— 和 DeadTag / ActiveTag 同处
struct DirtyTag {};

// 渲染层
template<typename Tag> struct GlobalBufferSlot { uint32_t m_slot; };
```

| | 谁打 | 谁读 | 一个实体有几个 | 归属 |
|---|---|---|---|---|
| `DirtyTag` | **上层写入方** | 多个消费方 | 1 个 | **Core,不模板化** |
| `GlobalBufferSlot<Tag>` | 本系统 | 本系统 | **可能多个**(光源既占 `g_Lights` 又占 `g_Views`) | **渲染层,模板化** |

不对称是有理由的:标记模板化会逼着 `TransformSystem` 去 include 渲染层的 tag 类型,依赖方向是反的;而槽位上层
从不碰,模板化不引入任何反向依赖,且一个实体确实可能同时占几个数组的槽位,不模板化表达不了。

**标记共享、槽位分立。**

Tag 直接对上 shader 侧符号:`GlobalBufferSlot<Instances>` / `<Materials>` / `<Lights>` / `<Views>`。不与已有的
`InstanceBindingTag` / `MaterialBindingTag`(那些是 SRG 实体的 tag)撞名。

组件挂在**源实体**上,源在哪个 context 就挂哪:

| 数组 | 源实体所在 |
|---|---|
| Instances | world |
| Materials | material context |
| Lights | world(光源实体) |
| Views | RHIContext(view 实体) |

泛型需按 context 参数化。

### 每个数组两块内存

- **`staging[Capacity]`** —— CPU 侧**唯一真相**,普通可缓存 RAM,常驻,只有被编码时才改写
- **N 份 host-visible buffer**(`Component.h:239` 的 `BufferPerFrame`)—— staging 的完整快照

---

## 三、每帧四步

```
1. 回收   GetView<GlobalBufferSlot<Tag>, DeadTag>              → 槽位归还分配器
2. 分配   GetView<Src>(Exclude<GlobalBufferSlot<Tag>, DeadTag>)
                                → 取槽位, Add<GlobalBufferSlot>, Add<DirtyTag>
3. 编码   GetView<DirtyTag, Src, GlobalBufferSlot<Tag>>
                                → staging[slot] = Encode(src)          // 不 Remove
4. 上传   PendingBufferMap{ staging.data(), 0, 高水位 * sizeof(T) }
```

第 3 步的 `Encode` 是唯一的类型专属代码,其余三步全泛型。

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

**回收延迟参数 M**:死亡后 M 帧才可再分配。今天 M=0,行为与现状一致。接 GPU 剔除时(跨帧 GPU 状态按槽位
寻址)必须改成非零,否则「上一帧可见的 42 号」会变成另一个对象——只错一帧、随机复现,与
`TODO_MultiViewPlan.md` §五 决策 1 踩过的是同一族坑。

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

| | 今天 | 之后 | 连带 |
|---|---|---|---|
| **Instances** | 稳定 id + `InstanceSlotTable` **间接** | 槽位 = 稳定 id | 删 `InstanceSlotTable`、删 `DrawItemInstanceSlot`、`m_instanceOffset` 变成烘一次的常量 |
| **Materials** | **迭代序**,每帧回写 `MaterialGPUSlot` | 稳定槽位,分配一次 | |
| **Lights** | **迭代序** | 稳定槽位 | |
| **Views** | 只有 shadow view 有 tile 槽位 | 统一 `g_Views`,吸收 `g_ShadowViews` | |

**规模差异决定要不要接脏标记**:

| | 容量 | 每帧全量编码的成本 | 是否值得接 `DirtyTag` |
|---|---|---|---|
| Instances | 65536 | 每对象一次矩阵求逆 | **值得** |
| Materials | 1024 | 字段拷贝 | 否,第 3 步无条件全量即可 |
| Lights | 256 | 字段拷贝 | 否 |
| Views | 十几 | 相机每帧都在动,本来全脏 | 否 |

后三个可以先只拿稳定槽位那部分收益,第 3 步照旧全量。

**顺带解开一条时序依赖**:今天 `MaterialBindingSystem` 必须严格排在 `InstanceBindingSystem` 之前,因为后者要读
当帧写的 `MaterialGPUSlot`。槽位稳定后,材质下标只在**指派变化**时才需更新,per-frame 的 tick 顺序约束降级成
一次性的「槽位得先存在」。

---

## 九、前置条件(硬的)

**`Device::GetCurrentFrameIndex()` 收口**,见 `TODO_AsyncUpload_RemainingIssues.md`。

声明式上传的另一面是:调用方不知道数据落到了哪一份副本,「写哪份」和「绑哪份」只能靠约定一致。今天是两个独立
计数器隐式锁步——`ProcessBufferMaps` 用 `RHIResourceSystem::m_frameIndex`(`RHIResourceSystem.cpp:355`),
`BindFrameInstances` 用 swap chain 的 `GetCurrentImageIndex()`。

**这不是可选优化,是这个上传模型的完成件**:否则「我不管什么时候拷、拷到哪」这个承诺是空头的。那份文档里写着
「待 InstanceBindingSystem 跑通一遍后再做」,而这就是那个时刻。

---

## 十、明确留空的

| | 状态 |
|---|---|
| **谁打 `DirtyTag`** | 上层负责,不在本方案内。上层加之前,第 3 步无条件全量,方案照常成立 |
| **容量增长** | 今天固定,溢出丢弃(`Capacity` 溢出 `LOG_ERROR`)。稳定槽位让将来增长是平凡的(`slot k → slot k`) |
| **第 4 步增量化** | 满容量 9.4 MB/帧时再量。届时用第七节的区间数组形态 |
| **回收延迟 M 的取值** | 等 GPU 剔除落地时定 |
| **GPU 侧遍历空洞的有效性信号** | 同上 |

---

## 与其他文档的关系

- `TODO_DrawItemShapePlan.md` —— 讲提交路径的形状。交点只有一处:本方案让 `m_instanceOffset` 成为烘一次的
  常量,是那份文档「DrawItem 烘一次后只读」的最后一块。
- `TODO_AsyncUpload_RemainingIssues.md` —— 第九节的前置条件在那里。
- `TODO_MultiViewPlan.md` —— `g_Views` 吸收 `g_ShadowViews`;回收延迟与 §五 决策 1 同族。
