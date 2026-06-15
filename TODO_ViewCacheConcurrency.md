# GetOrCreateXXXView 的多线程方案(留待 parallel compile 时实现)

承接 [TODO_ViewCache.md](TODO_ViewCache.md)。view cache 的四个原语
(`GetOrCreateImageView` / `GetOrCreateImageViewPerFrame` /
`GetOrCreateBufferView` / `GetOrCreateBufferViewPerFrame`)目前是**单线程**形态。
等 compile 真正并行时,按本文改造。现在不动代码。

---

## 1. 何时需要 / 并发模型

compile 以后会按 pass 并行(各 pass 填自己的 DrawItem/CopyItem,互不干涉)。
那时多个 pass 可能同时对**同一个 resource** 调 `GetOrCreate*` 取 view。模型:

- **读并行、建串行(per-resource)** 是当前架构下最好的并发模式。
- view 是「冷启建几次、之后全是命中读」的典型 **read-mostly**,所以优化要落在
  **命中读路径无锁**,只有罕见的首次 miss 才同步。
- 同步粒度是 **per-resource**(每个 resource 一份 cache 组件),不同 resource 天然
  无竞争,不需要全局锁表。

## 2. 当前为什么不安全(三个隐患)

```cpp
auto* cache = ctx.TryGet<ImageViewCache>(resource);
if (!cache) { ctx.Add<ImageViewCache>(...); cache = ctx.TryGet<...>(); } // ① 结构性写
for (auto& e : cache->m_entries) if (e.m_descriptor == desc) return ...; // ② 读
cache->m_entries.push_back(...);                                         // ③ 写
```

1. **① 懒建 = `ctx.Add` = entt 结构性写**:动整个 component pool 的 sparse set,与
   *任何* 对该 pool 的并发访问冲突 —— registry 级,最危险。
2. **③ 同资源 `push_back` 竞争**:两线程对同一 cache append,撕裂 entries/size。
3. **②③ 同资源读写竞争**:一个命中读、一个 miss 写同一 cache。

不同 resource 的 cache 各自独立(distinct component 实例),只要没有 ①,跨资源并发
本就安全 —— 这正是当初选 cache 而非 view-entity 的并发理由。

## 3. 选定方案:无锁读 + per-component 内联自旋(写者互斥)

两个原子,各管一轴,**职责正交,合不到一个变量里**:

- `std::atomic<uint32_t> m_count` —— **读者轴**:已发布条数,release/acquire 配对,
  让读者无锁安全扫描(看到 count=n+1 时 entry[n] 内容也一定可见)。
- `std::atomic_flag m_lock` —— **写者轴**:只在 miss 时序列化写者。

> `m_count` 为什么不能兼任写者互斥:写者「占第 n 槽」必须在写**之前**、「发布 n+1」
> 必须在写**之后**,一个计数器只能待在一端。`fetch_add` 占槽会在写前就发布 → 读到
> 垃圾;「先写再 CAS」则两写者同时写同一 entry[n] → 撕裂。所以仍需独立的 `m_lock`。

### 组件布局(用定长数组 + 原子计数,替掉 fixed_vector)

无锁读要求一个**原子发布点**,而 `fixed_vector::size()` 不是原子的、`push_back` 不是
publish-safe,所以 MT 形态把 `fixed_vector` 换成定长数组 + 原子计数:

```cpp
struct ImageViewCache
{
    static constexpr uint32_t MaxViews = 8;
    ImageViewCacheEntry   m_entries[MaxViews];
    std::atomic<uint32_t> m_count { 0 };               // 读者轴:已发布条数
    std::atomic_flag      m_lock = ATOMIC_FLAG_INIT;   // 写者轴:miss 时互斥(C++17 须显式 INIT)

    // entt 要求组件可移动构造/赋值(Add 时 move 临时对象、销毁实体 swap-and-pop);
    // 而 std::atomic* 不可移动 → 必须自定义 move,只搬数据,原子不传递(留默认/清零)。
    // 安全前提:move 只在串行的结构性操作(资源创建/销毁)发生,绝不与加锁窗口重叠。
    ImageViewCache() = default;
    ImageViewCache(ImageViewCache&& o) noexcept
    {
        uint32_t n = o.m_count.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < n; ++i) { m_entries[i] = eastl::move(o.m_entries[i]); }
        m_count.store(n, std::memory_order_relaxed);
        // m_lock 留默认清零
    }
    ImageViewCache& operator=(ImageViewCache&& o) noexcept
    {
        uint32_t n = o.m_count.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < n; ++i) { m_entries[i] = eastl::move(o.m_entries[i]); }
        m_count.store(n, std::memory_order_relaxed);
        return *this;   // m_lock 保持目标自身(清零)状态
    }
};
```

### 函数形态(内联 flag,单一出口;不用 RAII guard)

临界区有多个出口(锁内复查命中 / 创建失败),内联时统一算出 `result` 再 `clear`,
避免漏解锁:

```cpp
RHI::ImageView* GetOrCreateImageView(ctx, resource, image, viewDesc)
{
    auto& c = ctx.Get<Components::ImageViewCache>(resource);   // 已预加,无 ctx.Add

    // 读:无锁
    uint32_t n = c.m_count.load(std::memory_order_acquire);
    for (uint32_t i = 0; i < n; ++i)
        if (c.m_entries[i].m_descriptor == viewDesc) { return c.m_entries[i].m_view.get(); }

    // 写:miss 才自旋拿锁,单一出口处 clear
    while (c.m_lock.test_and_set(std::memory_order_acquire)) { /* spin */ }

    RHI::ImageView* result = nullptr;
    n = c.m_count.load(std::memory_order_relaxed);            // 锁内复查
    for (uint32_t i = 0; i < n; ++i)
        if (c.m_entries[i].m_descriptor == viewDesc) { result = c.m_entries[i].m_view.get(); break; }
    if (!result)
    {
        auto view = Service<Factory>::Get()->CreateImageView();
        if (view && view->Init(image, viewDesc) == ResultCode::Success)
        {
            result = view.get();
            c.m_entries[n] = { viewDesc, eastl::move(view) };
            c.m_count.store(n + 1, std::memory_order_release);   // 先写后发布
        }
    }
    c.m_lock.clear(std::memory_order_release);               // 单一解锁点
    return result;
}
```

命中(暖 cache,绝大多数情况)= 一个 acquire load + 扫描,**零锁**;只有冷启 miss 才上
那把 1 字节锁。

## 4. 前置:资源创建处预加空 cache

`Get<ImageViewCache>(resource)` 要求 cache 已存在,且要把 ① 的结构性写彻底挪出热路径
(并行段绝不能有 `ctx.Add`)。所以在**所有资源创建点**预加空 cache(串行段),并行段
只 `Get` + 无锁读 / 锁内写,不再有任何 Add/Remove。

需要预加的点(按需对应单帧/per-frame 版本):
- transient:`CreateTransientImageResource` / `CreateTransientBufferResource`
- static:`CreateStaticImage` / `CreateStaticBuffer`
- imported / swapchain:`ImportSwapChain` 等导入点(swapchain 是 per-frame → 预加
  `ImageViewCachePerFrame`)

## 5. 纪律(并行落地时一并保证)

- **结构性写(CreateEntity / Add / Remove / DestroyEntity)留在串行段**;并行段只对
  *已存在* 的组件做「无锁读 + 锁内原地写」。
- cache 组件预加后,`GetOrCreate*` 内不再有 `ctx.Add`。
- entt 默认 paged storage:并行段无 Add/Remove → 不会搬动组件 → `Get` 拿到的引用和
  其中的原子在持锁期间稳定。move 那套只在串行创建/销毁走。

## 6. per-frame 变体的额外注意

`ImageViewCachePerFrame` / `BufferViewCachePerFrame`:entry 按 descriptor 找,entry 内
`m_views[frameIndex]` 按帧懒填。并发上比单帧多一层:

- **entry 列表**:find-or-append entry,同单帧(无锁读 + 锁内 append + 原子发布)。
- **槽位填充**:同一 descriptor 的不同 frameIndex 槽在不同帧填;同帧同 descriptor 的
  两个线程会争同一 `m_views[frameIndex]` → 该填充也要在锁内,且读路径要 per-slot 的
  发布(每个槽一个 ready,或 entry 上一个按帧的 ready 位图),才能无锁读判断某帧槽是否
  就绪。实现时细化;原则(read-mostly、写上 per-component 锁、预加 cache)与单帧一致。

## 7. 否决的备选

- **分片全局锁表(hash entity → 锁)**:会把不相干的 entity 撞同一把锁产生假竞争;
  per-component 锁无此问题,且 1 字节/资源不算浪费。
- **`fetch_add` 占槽 + per-entry ready 的纯无锁版(连写者锁都不要)**:可行,但
  ① 同 descriptor 并发 miss 会产生**重复 view**(无害但浪费);② 丢了 `fixed_vector`
  的 overflow 兜底,定长数组越界要自己管。综合不如「无锁读 + 1 字节写锁」干净。
- **让 `m_count` 兼任写者锁**:见第 3 节,占槽/发布在计数两端,单计数器做不到。
