# Descriptor 池的值语义 / 指针语义错配（留待重构）

`DescriptorHandle` / `DescriptorTable` 是**值语义**的小对象，却被塞进**指针语义**的
`ObjectPool`。这个错配已经反复产出同一类 bug（见下方历史）。本文记录根因和根治方向。
**现在不动代码** —— 已有的止血修复足够跑通，重构本身影响面到 Core 层。

---

## 1. 错配是什么

`DescriptorHandle` 是 8 字节，`DescriptorTable` 是 `(DescriptorHandle, uint32_t)`。
两者都按值到处拷贝，这是完全合理的用法：

```cpp
DescriptorTable AllocateTable(uint32_t count) { return *BasePool::Allocate(count); }  // 解引用，返回副本
D3D12_CPU_DESCRIPTOR_HANDLE GetCpuNativeHandleForTable(DescriptorTable table) const;  // 按值收
b.m_viewsDescriptorTable = descriptorCtx.CreateDescriptorTable(...);                  // 存副本
```

但 `ObjectPool` 是围绕指针建的：

```cpp
using StorageType   = DescriptorTable*;          // traits
void DeAllocate(ObjectType* object);
m_collectFunction = [](ObjectType& object) { ... };
```

于是每次「分配」实际上产生**两个对象**：

| | 谁拥有 | 生命周期 |
|---|---|---|
| **A** `m_tablePool[offset]` | 池 | 到 `DestoryObject` 里 `erase` 为止 |
| **B** 调用者手里的副本 | 如 `ShaderBindings::m_viewsDescriptorTable` | 随持有者析构 |

内容相同、内存不同。而**延迟回收队列必须持有 A**，因为回收发生在若干帧之后，那时 B
往往已经不存在了。

## 2. 为什么这个错配特别容易踩

从值拿回池内对象，需要一层「用 index/offset 反查」的桥接：

```cpp
DescriptorHandle* GetPooledHandle(uint32_t index) { return &m_handlePool[index]; }
DescriptorTable*  GetPooledTable(size_t offset);   // 2026-07-29 补上
```

问题在于**这层桥接不是类型系统强制的**：

```cpp
BasePool::DeAllocate(&table);   // 编译通过，运行时悬垂
```

`&table` 和 `GetPooledTable(...)` 类型完全一样，都是 `DescriptorTable*`。写错了没有任何
编译期信号，后果是几帧之后在另一个调用栈上的悬垂读 —— 离案发现场很远。每新增一种池
都要重新记得做一遍，漏一次就是一个潜伏 bug。

## 3. 历史（同一个根因的反复发作）

- **`ReleaseHandle` 悬垂**：队列里存了调用者的 `DescriptorHandle`（如 `ImageView` 的
  成员），持有者先于回收析构。修法是加 `GetPooledHandle` 桥接。
- **释放后句柄仍然有效**（commit `fix: null released descriptor handles so pooled views
  re-allocate`，2026-07-02）：`Release*` 只还了池槽位，调用者手里的值还是「有效」的，
  复用时指向已被回收的槽位。修法是在 `ReleaseDescriptor` /
  `ReleaseStaticDescriptor` / `ReleaseDescriptorTable` 里把调用者的值置空。
- **`ReleaseTable` 悬垂**（2026-07-29，本次）：table 路径从来没有做过第一条那个桥接。
  上面第二条的置空恰好把它从「靠调用者碰巧还活着而侥幸工作」变成了**必现**：
  `DeAllocate(&table)` 入队后，`table = DescriptorTable()` 立刻清空它，几帧后回收读到
  `m_index == NullIndex`，报 "Trying to deallocate a DescriptorTable that is not
  allocated from this factory"。

  值得注意的是：置空这个改动**没有引入 bug，而是让一个沉默的内存错误变得可见**。
  在此之前，回收读到的是野内存，`m_tablePool.find(随机 offset)` 有概率命中**别人的**
  table 并静默释放一段仍在使用的描述符范围 —— 那才是真正危险的形态。

三次都是同一件事：**值的副本被当成池内对象的替身**。

## 4. 根治方向：延迟回收队列存值，不存指针

`DescriptorTable` 8 字节、`DescriptorHandle` 4 字节，拷贝成本为零。队列存值以后：

- 生命周期问题**从根上消失** —— 队列自己拥有数据，不依赖任何外部对象存活
- `GetPooledHandle` / `GetPooledTable` 两个桥接可以删掉
- `DestoryObject` 改成收值（或收 offset），内部照常查表释放
- 写错的可能性没有了：没有指针可传

代价：`ObjectPool` 是 Core 层通用设施，`StorageType` 是 traits 的一部分。要确认其他
使用者是否真的依赖指针语义（比如需要对象复用、或对象本身很大不宜拷贝）。可能的落法是
给 traits 增加一个「值语义池」的变体，而不是改掉现有的。

## 5. 重构时顺带看的东西

- `DescriptorTableFactory::CreateObject` 里有一行没用的 `auto table = m_tablePool[result];`
  （多一次拷贝，且 `operator[]` 语义危险）
- `DescriptorTablePool::AllocateHandle` / `ReleaseHandle` 是 `ASSERT(false)` 的空壳实现，
  说明 `DescriptorPoolBase` 这个基类接口对两种池并不合身 —— 一半的方法对每种实现都是
  非法的。接口拆分可能比继承更合适。
- `DestoryObject` 对「同一个 table 释放两次」没有防护：第二次 `GetPooledTable` 仍会命中
  （`erase` 要等到 `Collect`），于是重复入队，第二次回收时 find 失败报错。当前没有已知
  的调用方会这么做，但值语义重构时应该一并想清楚。
