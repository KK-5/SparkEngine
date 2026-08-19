# 实体句柄生命周期追踪设计

> 记录「实体句柄(MaterialHandle / RHIHandle / World Entity)缺乏引用追踪」这一问题的**背景、症状与方向选择**,避免日后重推。
> 本文只记结论与论证,不含实现细节 —— 具体机制(`Ref<H>` 的 move/copy 语义、跨 registry decrement、teardown 序)留到真正动手时再定。
> **优先级:不急做。** 先记录,后落地。

---

## 1. 问题本质:句柄没有生命周期

引擎里有若干「句柄」类型 —— `MaterialHandle`、`RHIHandle`、World `Entity` —— 在很多场景下它们只是一个**整数,指代一份资源**。但这个整数:

- **不会因为引用它的宿主(某个组件)被销毁而失效**;
- 被引用的目标实体也**不知道自己还有没有人引用**。

于是「这个句柄还需不需要保留」只能靠两条烂路判断:

1. **每帧全量扫描 + 比对**(遍历所有引用方,重建活集,标记-清扫);
2. **手动有序清理**(在关闭时按依赖次序人工拆引用链)。

这两者是**同一个缺失能力的两个症状**,不是两个独立问题。

---

## 2. 症状现场

### 2.1 上层的全量扫描(本次切入点)

两处「引用方 → 句柄」的关系:

- 实体的 `MaterialComponent` → 引用 `MaterialHandle`;
- 材质的 `MaterialParams`(纹理槽)→ 引用纹理 `RHIHandle`。

现在为了回收,`MaterialTextureSystem` 维护了一个 `AssetId → RHIHandle` 的 pool,并用 **generation-GC 每帧全量扫描**判断谁还被引用。这个 pool 是「做在 ECS 系统成员里的临时办法」,本质是**一个位置放错、做得别扭的引用计数**。

### 2.2 关闭时的手动有序清理(暂不碰)

`RHIResourceSystem::ShutdownInternal` 必须**手动**、**按序** `Clear` 掉持有资源引用的组件(ViewCache、`ShaderBindings` 等):ImageView/BufferView 会被塞进 `ShaderBindings`,导致底层资源 refcount 不归零,只能人工先拆。**这一处涉及资源引用,场景复杂,本轮不作为切入点。**

---

## 3. 两种所有权模型:引用方管 vs 被引用方管

### 3.1 引用方负责释放(现状:SkyboxSystem)

`SkyboxSystem` 监听组件销毁事件,主动清理实体上的 `SkyboxGPUComponent`,并手动 `DeadTag` 掉其中的 `m_cubemap`(RHIHandle)。**机制做在引用方 —— 谁引用谁负责释放。**

- 它**能成立的前提是独占**:1 个 skybox : 1 个 cubemap,没有别人引用,所以宿主销毁时直接释放绝对安全。

### 3.2 被引用方自管(目标方向)

目标是把机制移到**被引用方 —— 谁被引用,谁自己管理释放时机**:目标实体持有引用计数,引用方只负责发 +1/−1 信号,计数归零时目标自己销毁(`DeadTag`)。

### 3.3 为什么必须选后者:共享语义

决定性理由不是「后者更优雅」,而是**本次要处理的两个引用都是多对一共享的**:

- 多个实体的 `MaterialComponent` → 同一个 `MaterialHandle`(材质被多个 mesh 共用);
- 多个 `MaterialParams` → 同一个纹理 `RHIHandle`(贴图被多个材质共用)。

一旦共享,「引用方负责释放」就崩溃:某宿主销毁时**若直接释放 → 误杀他人还在用的资源;若不敢释放 → 泄漏**。为绕开它只能让引用方之间凑一个「大家一起数」的机制 —— **这正是 §2.1 那个 pool + generation-GC 的来历**。所以被引用方引用计数在共享语义下是**唯一正确**模型,Skybox 那套只在独占时侥幸成立。

---

## 4. 关键佐证:`Ptr<T>` 已经是这个模型

`SkyboxGPUComponent` 一个结构体内部的不对称,就是缺失能力的铁证:

```
Ptr<Resource::ImageAsset> m_cubemapAsset;   // 组件销毁自动释放 —— 免管
RHI::RHIHandle            m_cubemap;         // 组件销毁需手动 DeadTag —— 要管
```

`m_cubemapAsset` 是 `Ptr<T> = intrusive_ptr`,**堆资源早就是「被引用方自管、refcount 归零自销毁」了**。我们要的东西精确地说就是:**让 `RHIHandle` / `MaterialHandle` 表现得和 `Ptr<Resource>` 一样。** 同一结构体里一个字段免管、一个要管,证明句柄缺的就是 `Ptr` 那一层。

---

## 5. 方向选择:A —— 侵入式智能句柄 `Ref<H>`

引用计数挂在**被引用实体**上(其所在 context 里一个 `RefCount` 组件:`MaterialHandle` 放 MaterialContext、`RHIHandle` 放 RHIContext),归零 → 自身 `DeadTag`(走现有延迟销毁)。引用方如何发信号,考虑过三条:

| 方案 | 做法 | 取舍 |
|---|---|---|
| **A(选定)** | 组件里裸句柄换成 `Ref<H>`,构造 incref / 析构 decref,由 entt 组件生命周期自动驱动 | **无事件、无扫描**;是「让句柄像 `Ptr`」的完全体。代价:entt 会搬移组件,move/copy 语义要严谨 |
| B(过渡) | 复用 ComponentEventBus,construct→incref / destroy→decref,句柄保持裸整数 | 改动小、复用已验证管线;但没消灭「引用方要显式挂事件」 |
| C(否决) | 反射给句柄字段打 trait,中心 GC 追踪式 mark-sweep | 非侵入,但**保留了扫描**(正是要消灭的东西),且要定义 roots、追踪比引用计数难写对 |

**选 A。** 它把「引用是否存活」绑定到 entt 已经权威掌管的**组件寿命**,直接命中问题陈述里那条性质(宿主组件销毁 → 引用自动释放),并同时干掉每帧扫描和事件挂接。B 保留作风险更低的过渡选项。

### 5.1 分工澄清

「被引用方自管」**不等于**「引用方什么都不做」。目标实体无法凭空知道自己的 refcount,引用方仍须发 +1/−1 信号。真正的变化是:**引用方只负责 signal,「何时真正销毁」的决定权移到被引用方** —— 与 `Ptr<T>` 完全同构(持有即 +1,析构即 −1,释放时机归对象自己)。

---

## 6. 范围与非目标

- **切入点**:先在 `SkyboxGPUComponent::m_cubemap` 这一个字段上把 `Ref<RHIHandle>` 语义做出来验证,可直接对照旁边的 `m_cubemapAsset` 观察行为是否对齐;语义确认后再推广到 `MaterialHandle`,`MaterialTextureSystem` 的 pool + generation-GC 随之消失。
- **非目标(本轮不碰)**:`RHIResourceSystem` 关闭时的手动有序 `Clear`(§2.2)。它涉及资源引用链与整表 teardown 次序,复杂度高,单列。
- **已知遗留**:引用计数解决**稳态**回收(宿主销毁即释放、无扫描),但**整机 shutdown 的 registry 销毁次序是另一个问题** —— 引用计数不自动解决,届时可能仍需一条显式有序拆解,或一个抑制 `Ref` 副作用的 teardown 模式。这条不阻塞稳态收益。

---

## 7. 待动手时再定的实现问题(仅备忘,不在本文展开)

- `Ref<H>` 的 move / copy 语义(entt 组件搬移下不能双重释放 / 漏 incref)。
- 跨 registry decrement(MaterialContext 组件持 `Ref<RHIHandle>` 要伸进 RHIContext;context 已销毁时安全空转)。
- refcount 与现有 `DeadTag` + N 帧延迟销毁如何衔接。
- teardown 模式(§6 遗留)。
