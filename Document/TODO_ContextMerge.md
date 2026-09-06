# 上下文合并（Context Merge）

> 机制本体放 `SparkCore`，**只依赖 entt**。不依赖反射系统——今天的反射是 `entt::meta` 的封装，
> 以后换库时这个机制不该跟着动。
>
> 场景加载是它的第一个使用者，但它不是为场景设计的。见「用例」。

## 它是什么

一句话：**把源上下文里指定组件类型的数据，写进目标上下文。**

```cpp
Merge<MergeMatch::Any, MergeMapping::NewEntity,
      TransformComponent, MeshComponent>(world, eastl::move(staging));
```

搬的是**组件**。实体集合是从组件类型推出来的结果，不是另一个独立输入。要「整个实体都过去」的
效果，调用方把类型列全就行——**那份类型表是使用者的事，不是机制的事**，用反射、用手写清单、用别的
手段都可以。

## 为什么值得单独做成机制

同一个操作今天已经有手写的副本，以后还会更多：

- `SpawnModel` 是一份手写的 merge。
- 场景加载：反序列化到一个临时上下文，再合进活世界。
- prefab 实例化、复制粘贴、编辑器 undo。

这些共享的不是「代码长得像」，而是**同一组语义问题**：编号怎么映射、组件怎么无声地插进去、目标
怎么被告知。每写一份手写副本就要把这三个问题重答一遍，而且大概率答错第三个。

---

## 契约

**方向固定，目标编号永不改变。** 重编号只发生在**进来的**那批实体身上，目标的编号一个都不动。

这一条使「实体编号被外部持有」按构造就不成问题：能被外部持有的只有目标侧的编号，而目标侧不动。
今天全仓的持有者逐个对得上——`EditorInputSystem::m_editorCamera`（世界=目标）、
`MaterialSystem::m_defaultMaterial`（material=目标）——没有一个持有源侧编号。

**不支持自合并**（`Merge(world, world)`）。插入会让正在遍历的 storage 重分配。需要「原地复制」时
走两趟：世界 → 暂存（Copy），暂存 → 世界（Consume）。顺带把「剪贴板」这个东西白送了。

---

## 三个轴

| 轴 | 取值 | 决定什么 |
|---|---|---|
| **Match** | `Any` / `All` | 源里哪些实体参与：带**任一** `Ts` / 带**全部** `Ts` |
| **Mapping** | `NewEntity` / `SameEntity` | 源实体 → 目标实体 |
| **Source** | `Consume`（移动，源死） / `Copy`（拷贝，源活） | 元素怎么过去 |

`Source` 用**重载**区分而不是第三个枚举——两者的源参数类型本来就不同（`UniquePtr<Ctx>` 与
`const Ctx&`），让类型系统去表达比多一个标志好。

### 写入语义是推出来的，不是第四个轴

- `NewEntity` → 目标实体是新建的，组件必然不存在 → `emplace`
- `SameEntity` → 组件很可能已经存在，而且覆盖正是目的 → `emplace_or_replace`

`SameEntity` 还顺带让 remap 退化成恒等，于是以后的引用重映射（见「扩展点」）在这个模式下是空
操作——**这正是对的**，undo 的数据本来就已经在目标的编号空间里。

---

## 用例

| 用例 | Match | Mapping | Source |
|---|---|---|---|
| 场景加载 | Any | NewEntity | Consume |
| 复制（世界 → 暂存） | Any | NewEntity | **Copy** |
| 粘贴（暂存 → 世界） | Any | NewEntity | Consume |
| prefab 实例化 | Any | NewEntity | Copy |
| Undo「删除实体」 | Any | NewEntity（`create(hint)` 恢复原编号） | Consume |
| Undo「批量改组件」 | Any / All | **SameEntity** | Consume |

「复制」不需要另造一个 extract 操作——它就是 `Copy` 模式的 merge，目标是一个空的暂存上下文。

---

## 三个机制

### 1. `ContextAccess<E>`：通到裸 registry 的一道口子

**为什么非有不可。** merge 要的三件事 `BasicContext` 一件都没暴露，而且都**故意绕过上下文的
契约**：

| 要做的 | entt 入口 | 为什么不能走公开 API |
|---|---|---|
| 按指定编号建实体 | `registry.create(hint)` | 未暴露 |
| 直取 storage | `registry.storage<T>()` | 未暴露 |
| **静默插入组件** | `storage.emplace(...)` | `Add<T>` 会发事件；事件必须统一在最后补 |

把它们加进 `BasicContext` 的公开 API 等于邀请别处使用。而两个上下文类
（`BasicContext<E>` 与 `BasicContext<Entity>` 特化）的成员都是 `entt::basic_registry<E> m_registry`，
所以**一道口子换来实现只写一份**，不用为特化再写一遍。

```cpp
template<typename E> struct ContextAccess
{ static entt::basic_registry<E>& Registry(typename ContextTraits<E>::ContextType&); };
```

两个类各加一行 `template<typename> friend struct ContextAccess;`，公开 API 不动。

### 2. remap 表

**为什么非有不可。** `create(hint)` 在槽被占时**静默换号**（`storage.hpp:1151`），所以目标编号
必须拿返回值，不能假设恒等。

形状：按源实体 id 部分索引的 `vector`（源是新建的，编号稠密）。`SameEntity` 模式下退化成恒等。

作为返回值交给调用方——它常常要知道对应关系（粘贴后要选中新实体，undo 要记录）。

### 3. `OnExternalWrite<Ts...>` 钩子

**为什么非有不可。** 搬运直写 storage，绕过了目标的事件派发——**merge 破坏了目标的事件契约就
得修**。但「派发是什么」只有上下文类型知道：`BasicContext<E>` 根本没有事件，`WorldContext` 有
两条总线。所以 merge 只负责把名单交出去。

```cpp
// BasicContext<E>：空模板
// WorldContext：
for (E t : entities) { EntityEventBus::Broadcast(OnEntityCreate, t); }
(DispatchIf<Ts>(entities), ...);   // if constexpr 判 ComponentTraits<T>::componentEvents
```

全编译期，**零注册面**——不需要 `RegisterEventOnEntityRemove` 那样的运行期 TypeId 集合，因为
`Ts` 在调用点就是已知的。

三条约束：

- **必须是全部搬完之后一次**。逐组件发会让 handler 拿到半成品实体去查还没到的组件——`Hierarchy`
  那类问题的翻版。
- **每个 `T` 自己用 `pool.contains(t)` 过滤**。`Any` 模式下一个实体未必带全部 `Ts`，不过滤就会给
  没有 Mesh 的实体广播 Mesh 的构造事件。
- `SameEntity` 模式下要区分「原来没有 → Construct」和「原来有 → WillUpdate / Updated」，而
  `contains` 事后问不出来，所以搬运时得记一份「哪些是新构造的」。这不是新语义——
  `WorldContext::AddOrReplace` 现在就是这么干的，只是从单个实体变成一批。

---

## 流程

```
Merge<Match, Mapping, Ts...>(target, source)
│
├─ 建表   Match=Any  : for each T: for (E s : srcReg.storage<T>())
│                          if (未映射) map(s, 目标编号);
│         Match=All  : for (E s : srcReg.view<Ts...>()) map(s, 目标编号);
│
│         Mapping=NewEntity  : t = tgtReg.create(s);
│         Mapping=SameEntity : t = s，要求 tgtReg.valid(s)，否则跳过并 LOG_ERROR
│
├─ 搬运   对每个 T：
│           auto& src = srcReg.storage<T>();
│           auto& dst = tgtReg.storage<T>();          // 缺就建
│           for (E s : src)
│               if (E t = remap.Lookup(s); t != null)  // All 模式下挡掉缺别的组件的实体
│                   dst.emplace(t, [move|copy] src.get(s));
│
├─ 通知   target.OnExternalWrite<Ts...>(新实体)
│
└─ 销毁   Consume：source 随 UniquePtr 消失（元素已被掏空，正好）
```

搬运循环两种 Match 共用——`Lookup` 的判空在 `Any` 下永远成立，在 `All` 下正好起筛选作用。

---

## entt 的既成事实

循环为什么这么写，不是风格问题：

- **没有类型擦除的 storage 工厂。** `pools` 私有，唯一创建者是模板 `assure<Type>`；
  `sparse_set` 的 10 个 virtual（`get_at` / `swap_or_move` / `pop` / `pop_all` / `try_emplace` /
  `bind_any` / `reserve` / `capacity` / `shrink_to_fit` / 析构）里没有 clone。
  `registry.storage(id)`（`registry.hpp:434`）只找不建，会建的那个（`:455`）是模板。
  **这就是 `Ts` 必须由调用方列出的根本原因**——entt 自己的 `basic_continuous_loader`（它做的正是
  「把远端实体合进活 registry 并重映射」）也是这么选的：`template<typename Type> get(archive)`，
  然后 `reg->template storage<Type>(id)` 顺手建。它的 `remloc` / `map(entt)` 就是 remap 表。
- **`create(hint)`**：槽空闲时原样还原，被占时静默换号。所以要拿返回值。
- **不能迭代 `storage<E>()` 找存活实体**：实体 storage 是 swap_only，`begin()` 覆盖整个 packed，
  含空闲槽。要用 `view<E>()`。（本机制列出 `Ts` 之后已经不需要这个操作。）
- **空组件的判别**照抄 entt 自己在 `basic_continuous_loader` 里的写法：
  `std::tuple_size_v<decltype(storage.get_as_tuple({}))> == 0`，此时 `emplace(t)` 不带参数。
- **类型有了之后，`push(void*)` 那条路可以彻底不用**。类型擦除版本的两个坑随之消失：不可拷贝的
  组件会静默返回 `end()`（无声丢数据），tag 靠 `value()` 返回 nullptr 走默认构造分支。有类型的
  `emplace` 里，不可拷贝/不可移动的组件是**调用点编译错误**。

---

## 不属于这个机制的

- **全量合并的类型表。** 谁想要全量，谁自己维护类型表。
- **组件里的实体引用重映射。** 见下，是加法。
- **身份策略**（「目标里已经有这个东西了」，比如材质按资产 id 去重）。是加法。
- **自合并。**
- 文件、JSON、场景、材质——一个都不认识。

---

## 扩展点

四个，都是往上加，不改上面三个机制：

**a. 组件里的实体引用（Translate）。** 在建表和搬运之间插一个相位，在**源里**就地改引用。在源里做
天然只覆盖要进来的那批，不需要「我刚插了哪些」的记账。它需要两样基础版没有的东西：remap 表要能
**按实体类型 TypeId 被找到**（查哪张表由字段的类型决定，不由「在遍历哪个上下文」决定），以及表里
要存源编号来校验 version（悬空引用的 id 可能撞上但 version 已变，不校验会静默指向不相干的实体）。

「哪几个偏移是句柄」这件事必须有人告诉它。两条路：走反射（字段级标记），或者做成
`ComponentTraits<T>` 上的编译期成员指针列表——**后者让 Translate 也不依赖反射**，而且更快。列出
`Ts` 之后这条路才是通的。定的时候再选。

**b. 身份策略。** 「目标已经有它的对应物」是 merge 判断不了的——判据由上下文的所有者定义（材质用
资产 id）。这一条只替换建表的循环体，返回 `{目标编号, 搬不搬}`。基础版是它的默认实现。

**c. 显式实体列表。** 复制粘贴要的是「这几个选中的实体」，不是「所有带 Transform 的实体」。

```cpp
Merge<...>(target, source, eastl::span<const E> only);   // 缺省 = 源里全部
```

在 Match 筛完之后多一次过滤，零成本。建表那步的形状要留着它。

**d. 多上下文一起合并。** a 落地后才需要：跨类型引用要求「所有建表完成之前不能开始任何 Translate」，
于是相位要能分开调。基础版只有两步且无跨上下文依赖，一个函数就够。

---

## API

```cpp
enum class MergeMatch   { Any, All };
enum class MergeMapping { NewEntity, SameEntity };

template<typename E> using MergeContextT = typename ContextTraits<E>::ContextType;

// Consume
template<MergeMatch Match, MergeMapping Mapping, typename... Ts, typename E>
RemapTable<E> Merge(MergeContextT<E>& target, UniquePtr<MergeContextT<E>> source);

// Copy
template<MergeMatch Match, MergeMapping Mapping, typename... Ts, typename E>
RemapTable<E> Merge(MergeContextT<E>& target, const MergeContextT<E>& source);
```

`static_assert(sizeof...(Ts) > 0)`——空包在 `Any` 下什么都不做，在 `All` 下 `view<>()` 语义含糊。

常用组合可以再包短名字，但底层只有这一个。

落点：

```
Core/ECS/Merge/ContextAccess.h
Core/ECS/Merge/RemapTable.h
Core/ECS/Merge/ContextMerge.h
```

改动的现有文件：`BasicContext.h` / `WorldContext.h`（各一行 friend + `OnExternalWrite` 钩子）。

---

## 已定决策

- **搬的是组件，实体集合是推出来的。** 全量的类型表归使用者。
- **不依赖反射系统。** 因此 `Ts` 由调用方列出——这也正是 entt 自己的选择。
- **`SameEntity` 下目标实体不存在时：跳过并 `LOG_ERROR`，不补建。** 「严格匹配」就该是严格的；
  undo 删除实体那个用例本来就该用 `NewEntity`，`create(hint)` 已经能恢复原编号。
- **`Copy` 模式对不可拷贝组件：调用点编译不过，不设退路。** 比运行期静默丢数据好。真撞上再说。
- **写入语义由 Mapping 推出，不做成独立的轴。**
- **`Source` 用重载而不是枚举。**

---

## 落地顺序

| | 内容 | 测试 |
|---|---|---|
| 1 | `ContextAccess` + 两行 friend | 编译，行为零变化 |
| 2 | `RemapTable` + `Merge`（`Any`/`All` × `NewEntity` × `Consume`） | 目标空 / 目标非空且编号冲突 / tag 组件 / 目标缺 storage / `All` 挡掉缺组件的实体 |
| 3 | `OnExternalWrite` 钩子 | 探针 handler 计数，断言事件到达时组件已齐，且没给不带该组件的实体发 |
| 4 | `Copy` 重载 | 源在合并后仍然完整 |

挂 `Engine/Code/Test/Core/`，加 `Merge_TESTS` 选项。1–4 全部不依赖场景，可以独立验完。

第二步（要 undo 时再做）：`SameEntity` + 显式实体列表。改动集中在建表那十几行和通知钩子的
签名，搬运循环基本不动。
