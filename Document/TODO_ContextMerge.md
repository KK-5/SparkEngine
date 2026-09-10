# 上下文合并（Context Merge）

> 机制本体放 `SparkCore`，**只依赖 entt**。不依赖反射系统——今天的反射是 `entt::meta` 的封装，
> 以后换库时这个机制不该跟着动。
>
> 场景加载是它的第一个使用者，但它不是为场景设计的。见「用例」。

## 它是什么

一句话：**把暂存上下文里指定组件类型的数据，写进活的目标上下文。**

```cpp
Merge<MergeMatch::Any, MergeMapping::Remap,
      TransformComponent, MeshComponent>(world, eastl::move(staging));
```

搬的是**组件**。实体集合是从组件类型推出来的结果，不是另一个独立输入。要「整个实体都过去」的
效果，调用方把类型列全就行——**那份类型表是使用者的事，不是机制的事**，用反射、用手写清单、用别的
手段都可以。

源和目标是**两个不同的类型**：源是 `StagingContext<E>`（暂存上下文），目标是活的
`ContextTraits<E>::ContextType`。方向因此是编译期的，不是纪律。

## 为什么值得单独做成机制

同一个操作今天已经有手写的副本，以后还会更多：

- `SpawnModel` 今天是手写的一份：改成「解析进暂存 → merge」之后就是这个机制的用户。
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

**自合并按类型就不可能。** 活上下文不能出现在源的位置，`Merge(world, world)` 连编译都过不去
（插入会让正在遍历的 storage 重分配）。需要「原地复制」时走两趟：`Extract`（世界 → 暂存），
`Merge`（暂存 → 世界）。顺带把「剪贴板」这个东西白送了。

---

## 三个轴

| 轴 | 取值 | 决定什么 |
|---|---|---|
| **Match** | `Any` / `All` | 源里哪些实体参与：带**任一** `Ts` / 带**全部** `Ts` |
| **Mapping** | `Remap` / `Identity` | 源实体 → 目标实体 |
| **Source** | `Move`（移动，源死） / `Copy`（拷贝，源活） | 元素怎么过去 |

`Source` 用**重载**区分而不是第三个枚举：`StagingContext<E>&&` 是 Move，
`const StagingContext<E>&` 是 Copy，让类型系统去表达比多一个标志好。（不能写成传值 + `const&`，
那对右值实参是二义的。）

### 写入语义是推出来的，不是第四个轴

- `Remap` → 目标实体是新建的，组件必然不存在 → `emplace`
- `Identity` → 组件很可能已经存在，而且覆盖正是目的 → `emplace_or_replace`

名字就是映射本身：`Identity` 下映射是恒等函数，于是以后的引用重映射（见「扩展点」）在这个模式下是
空操作——**这正是对的**，undo 的数据本来就已经在目标的编号空间里。

---

## 用例

| 用例 | 操作 | Match | Mapping | Source |
|---|---|---|---|---|
| 场景加载 | Merge | Any | Remap | Move |
| 复制（世界 → 暂存） | **Extract** | Any | — | — |
| 粘贴（暂存 → 世界） | Merge | Any | Remap | Move |
| prefab 实例化 / `SpawnModel` | Merge | Any | Remap | Copy |
| Undo「删除实体」 | Merge | Any | Remap（`create(hint)` 恢复原编号） | Move |
| Undo「批量改组件」 | Merge | Any / All | **Identity** | Move |

「复制」的方向上源是活上下文、目标是暂存，所以它是单独的 `Extract`，不是 merge 的一个模式。
它内部复用同一个搬运循环，但 merge 的两件难事在这个方向上都是空操作——映射恒等，暂存上没有
任何 handler 要通知。

`SpawnModel` 和 prefab 是同一行：解析产出的暂存按 `ModelAsset` 缓存，spawn N 次就是 N 次
`Copy`，不重新解析。

**前置条件**：暂存上调不了 `IScene::SetParent`（它走 `WorldExecuteContext::Current()`，而
`StagingContext` 进不了那个栈），所以要先把层级链表操作从 `SceneManager` 里抽成不挑上下文类型的
自由函数，暂存和世界共用。不抽的话只剩「每个生产者手写四字段链表」和「merge 完再逐个
`SetParent`」，前者必错，后者让批量事件白做。

---

## 三个机制

### 1. `StagingContext<E>`：一批还没接上系统的实体和组件

**为什么非有不可。** 源如果也是活上下文，方向和「不许自合并」只能靠纪律维持，而且它会被误当成一个
能被系统看见的世界。独立类型把这两件事一起解决。

它拥有一个 `entt::basic_registry<E>`，装的东西和上下文同构，但：

- **不能作为 `Merge` 的目标**，只能是源。
- **不进 `ExecuteContext` 栈**，`WorldExecuteContext::Current()` 永远不会指向它。
- **没有系统在看着它**，往里写不会惊动任何 handler。
- **可写。** 生产者有四个：反序列化器、`Extract`、undo 记账、prefab 源。

于是场景加载只有**一个**集成点：反序列化按文件原样填（`Hierarchy` 本来就是自洽的），merge 重映射，
批量事件让 `SceneManager` 集成一次。

它**不保证** id / version / 空闲链表逐字还原——`create(hint)` 撞号就换。要逐字还原是另一件事。

**写入面**：`CreateEntity()` / `CreateEntity(hint)` / `Add<T>` —— 静态类型，手写解析
（`SpawnModel` / prefab 源）要的就这些。反序列化器的类型是运行期的，中间要一层反射生成的
per-type thunk 转成静态 `Add<T>`，那层归 `TODO_AssetSystemPlan`。

### 2. 上下文要公开的两个能力

merge 和 `Extract` 需要三件事，它们**都不是特权，是上下文本来就该有的能力**：

| 要做的 | 归属 |
|---|---|
| `CreateEntity(hint)` / `EntityAt(hint)` | `BasicContext` 的公开能力，见下 |
| 直取 `storage<T>` | 同上；`ContextReference` 上按 `CanWriteComponentV<T>` 门控，和 `GetView` 同一条路 |
| 插入时不立即派发事件 | **事件策略的一个取值**——和「立即派发 vs 压队列后期提交」是同一个轴 |

```cpp
Entity EntityAt(Entity hint) const;   // 该 id 位上当前的活实体，空槽返回 NullEntity
Entity CreateEntity(Entity hint);     // 契约：撞号静默换号，一律以返回值为准
```

`EntityAt` 必须跟着 `CreateEntity(hint)` 一起给，否则这个接口会被这样误用：

```cpp
if (!ctx.Valid(hint)) { e = ctx.CreateEntity(hint); }   // 错，e 不一定等于 hint
```

`Valid` 按**身份**判定（id 位 + version），而 `create(hint)` 的撞号按**槽位**判定（只看 id 位）。
槽 5 住着 `{5,3}`、hint 是 `{5,0}` 时 `Valid` 答不在，create 照样换号——错得没有声音。`EntityAt`
是槽位那个问题的正确问法。

`CreateEntity(hint)` 自己不带断言：反序列化进空暂存时撞号是 bug，merge 进活世界时撞号是常态。
谁要求精确谁自己断言，entt 也是这么分的（`generate(hint)` 里没有断言，`basic_snapshot_loader`
在自己那侧写了 `ENTT_ASSERT(entity == entt)`）。

第三件今天还不存在：派发逻辑硬编码在 `WorldContext` 的 `Add` / `AddOrReplace` / `Replace` 重载里
（泛型的 `BasicContext<E>::Add` 本来就是静默的）。它跟着事件策略轴一起做，**不为 merge 单开机制**。
在那之前 merge 直接吃上下文；之后 target 参数换成 `ContextReference<WorldContext, MergeTraits>`，
把「写全部组件 + 不立即派发」声明出来。

`Extract` 要面对 `const` 活上下文，而 const `registry.storage<T>()` 返回的是**可能为空的指针**
（`registry.hpp:466`，只找不建），循环里要判空。`Merge` 的源是 `StagingContext`，拿非 const 访问，
不吃这条。

### 3. 重映射：两个临时组件

**为什么非有不可。** `create(hint)` 在槽被占时**静默换号**（`storage.hpp:1151`），所以目标编号
不能假设恒等。

但映射本来就是**恒等 + 少数例外**——只有撞号才换。所以不建表，只把例外记在**造成例外的那个实体**
身上，两个组件都落在**目标**里：

```cpp
struct MergedFrom { Entity source; };   // 打在新建的实体上：我从源里的 source 来
struct MergedTo   { Entity entity; };   // 打在被撞号的老实体上：想要我这个号的人去了 entity
```

正向查找（源 → 目标）因此是 O(1)，不需要任何额外结构：

```cpp
Entity Forward(Entity s)
{
    Entity occupant = target.EntityAt(s);
    return target.Has<MergedTo>(occupant) ? target.Get<MergedTo>(occupant).entity : s;
}
```

必须走 `EntityAt`，不能直接 `Has<MergedTo>(s)`：撞号按槽位判定，组件查找按身份判定，
`sparse_set::contains`（`sparse_set.hpp:721`）是校验 version 的，两边粒度对不上就会静默退回恒等，
指向不相干的实体。

这个形态比表好在三处：

- **`Move` 和 `Copy` 一视同仁。** 什么都不往源里写，`Copy` 模式的 `const` 源不受影响。
- **handler 能看见。** 批量事件的契约是「批内引用当作真相，批边界才链入」，而 `Has<MergedFrom>` 就是
  「在不在这一批里」的 O(1) 答案。只给一个 span 的话这是 O(N²)。
- **只存例外。** 目标近乎空时（场景加载）一个组件都不写。

两个组件由 merge 在通知相位结束时清掉，作用域封死在一次 merge 内，两次合并不会串批。
`Identity` 模式映射恒等，不写。

`MergeResult` 因此只剩**本次新建的目标实体名单**——通知相位本来就要建，白送；调用方粘贴后要选中
它们，而那时组件已经清了。

### 4. 批量构造事件 + `OnExternalWrite<Ts...>` 钩子

**为什么非有不可。** 搬运直写 storage，绕过了目标的事件派发——**merge 破坏了目标的事件契约就
得修**。但「派发是什么」只有上下文类型知道：`BasicContext<E>` 根本没有事件，`WorldContext` 有
两条总线。所以 merge 只负责把名单交出去。

`OnExternalWrite` 是 merge 内部按目标类型分派的那个点，**不是上下文的公开成员**，它做的事就是
广播一批事件。

#### 补发的不是逐个 Construct

现有 handler 的 `OnComponentConstruct` 语义是**「一个新组件刚被加进来，把它增量链进已经自洽的
结构」**，而 merge 交出的是**「一整批彼此已经自洽的组件」**。这两件事不一样，逐个补发会当场毁数据：

源里 A 是父、B 是子，已经链好；搬完后 `A.firstChild == B`。补发 `Construct(B)` 时
`SceneManager::Valid` 走「prev/next 都空」那条分支，检查到 `A.firstChild != NullEntity`，判定
非法，于是**把 B 的 Hierarchy 删掉**。换个广播顺序一样死。

所以要补的是一个**批量构造事件**：一次事件带一批实体，handler 自己决定怎么消化这一批。

这个事件不是为 merge 造的，它本来就缺——`SceneManager::AddEntities` 走
`Add<Hierarchy>(first, last, Hierarchy{})`，而 `WorldContext` 的批量 `Add` 今天是逐个补发 N 次
Construct。那批组件恰好互不相干所以没炸。**merge 是它的第二个用户，不是第一个。**

同一份 handler 实现两边都对：默认 `Hierarchy{}` 全空 → 全是根 → 打 root tag；merge 来的已链好
→ 校验 + 打 root tag。

#### 形状

```cpp
// ComponentEvents 新增，带默认展开
virtual void OnComponentsConstruct(eastl::span<const Entity> entities)
{
    for (Entity entity : entities) { OnComponentConstruct(entity); }
}
// EntityEvent 同理新增 OnEntitiesCreate(span)，必须带默认体（那两个方法是 = 0）
```

```cpp
// BasicContext<E>：空模板
// WorldContext：
EntityEventBus::Broadcast(OnEntitiesCreate, entities);
(DispatchIf<Ts>(entities), ...);   // if constexpr 判 ComponentTraits<T>::componentEvents
```

全编译期，**零注册面**——不需要 `RegisterEventOnEntityRemove` 那样的运行期 TypeId 集合，因为
`Ts` 在调用点就是已知的。

四条约束：

- **一次事件 = 一个 `T` + 一批实体，不能跨组件类型。** `ComponentEventBus` 是 `ById` 寻址、地址
  就是 `TypeId`，跨类型的一次广播没有地址可发。所以搬完全部 `Ts` 之后，对每个 `T` 各发一次。
- **必须是全部搬完之后才开始发**。这条约束的是相位不是次数：逐组件边搬边发会让 handler 拿到半成品
  实体去查还没到的组件。
- **不需要新的 `ComponentEventMask` 位。** 有了默认展开，批量事件就是 `Create` 的另一种投递形状，
  复用 `ComponentEventMask::Create`。原来「每个 `T` 自己用 `pool.contains(t)` 过滤」那条约束也
  一并消失——过滤挪到了 span 的构造，`T` 的 span 里天然只有真带 `T` 的实体。
- **span 不能进事件队列。** `EntityEvent` 开了 `EnableEventQueue`，而 span 指向 merge 的局部
  vector，只在广播期间有效。声明处就要禁掉队列版，或者队列版拷一份。

#### 迁移面

`ComponentEvents` 的成员本来就是带空实现的虚函数，加默认展开对 `MeshSystem` / `SkyboxSystem` 是
零改动——它们的 Construct 是「给这个实体建 GPU 侧的东西」，逐个展开就是对的。必须重写批量版的只有
`SceneManager`，也就是当初唯一会坏的那个。

`SceneManager` 的批量 handler **不是纯对账**：粘贴到某个已有父节点下时，批的根的 `parent` 指向
批外的实体。职责精确地说是「**批内引用当作已成立的真相，批边界上的引用才做真正的链入**」。

`Identity` 模式下还要区分「原来没有 → Construct」和「原来有 → WillUpdate / Updated」，所以
搬运时得记一份「哪些是新构造的」，并且 `WillUpdate` / `Updated` 也要有批量版。这不是新语义——
`WorldContext::AddOrReplace` 现在就是这么干的，只是从单个实体变成一批。跟 `Identity` 一起做。

---

## 流程

```
Merge<Match, Mapping, Ts...>(target, source)
│
├─ 建实体  对每个 T： for (E s : T 的源 storage)
│              Any 全收，All 要 HasAll<Ts...>(s)；已处理过的跳过
│
│          Create(s)  Remap    : t = tgtReg.create(s);
│                                if (t != s) { Add<MergedTo>(tgt.EntityAt(s), t); }
│                                Add<MergedFrom>(t, s);
│                     Identity : t = s，要求 tgtReg.valid(s)，否则跳过并 LOG_ERROR
│
├─ 搬运    对每个 T： 同一个遍历，同一个匹配判定
│              dst.emplace(Forward(s), [move|copy] src.get(s));
│
├─ 通知    OnExternalWrite<Ts...>(target, 新实体)  // 每个 T 一次批量事件，全部搬完之后才开始
│          广播期间 target 必须是 current——handler 靠 WorldExecuteContext::Current() 找上下文
│
├─ 清理    清掉 MergedFrom / MergedTo
│
└─ 销毁    Move：source 随右值引用的所有权消失（元素已被掏空，正好）
```

两个相位遍历的东西完全一样：`T` 的源 storage，加一个 `MergeMatches` 判定（`Any` 恒真，`All` 是
`HasAll<Ts...>`）。各 `T` storage 里满足 `HasAll` 的并集就是 `view<Ts...>`，所以和用 view 等价，但
两个相位形状一致，`Ts` 也不用展开两遍。

`Forward` **只对参与了本次合并的源实体有意义**——它对没参与的实体会退回恒等，而不是报空。上面两种
遍历都保证了这一点，别在别处拿它去问任意实体。

**不会产生孤儿实体**：目标实体是「因为带了某个 `T`」才被建的，天然不可能一个组件都没有。
（entt 的 `snapshot_loader` 需要 `orphans()` 是因为它可以只导部分类型。）

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
- **`create(hint)` 的撞号按槽位判定，`contains` 按身份判定。** `generate(hint)`
  （`storage.hpp:1151`）先把 hint 的 version 丢掉、换成该槽当前的 version 再做占用检查——一个 id 位
  同时只可能住一个活实体，version 区分的是先后世代。而 `sparse_set::contains`（`sparse_set.hpp:721`）
  校验 version，因为它问的是身份（悬空引用必须答不在）。两者都对，但粒度不同，跨着用就会静默出错。
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
- **自合并。** 按类型就不可能。
- **id / version / 空闲链表的逐字还原。** `StagingContext` 不保证这个。
- 文件、JSON、场景、材质——一个都不认识。

---

## 扩展点

四个，都是往上加，不改上面三个机制：

**a. 组件里的实体引用（Translate）。** 搬完之后在**目标里**改引用，用的就是 `Forward`——字段里存的
是源编号，要的正是正向查找。放在目标侧的原因是 `Copy` 模式的源是 `const`，改不了。它需要一样基础版
没有的东西：`Forward` 要能**按实体类型 TypeId 找到对应的那一对组件**（查哪个上下文由字段的类型决定，
不由「在遍历哪个上下文」决定）。

「哪几个偏移是句柄」这件事必须有人告诉它。两条路：走反射（字段级标记），或者做成
`ComponentTraits<T>` 上的编译期成员指针列表——**后者让 Translate 也不依赖反射**，而且更快。列出
`Ts` 之后这条路才是通的。定的时候再选。

这份 traits 顺带兜住批量构造事件的默认展开：一旦 `T` 声明了实体引用字段，编译期就不给它默认展开，
逼 handler 自己写批量版。带引用的组件正是逐个补发会出事的那一类，两件事该共用同一份 traits。

**b. 身份策略。** 「目标已经有它的对应物」是 merge 判断不了的——判据由上下文的所有者定义（材质用
资产 id）。这一条只替换建实体那步的循环体，返回 `{目标编号, 搬不搬}`。它本质上是第三种映射，
落地时就是 `MergeMapping::Resolve`；基础版的两个值是它的退化情形。

**c. 显式实体列表。** 复制粘贴要的是「这几个选中的实体」，不是「所有带 Transform 的实体」。

```cpp
Merge<...>(target, source, eastl::span<const E> only);   // 缺省 = 源里全部
```

在 Match 筛完之后多一次过滤，零成本。建实体那步的形状要留着它。

**d. 多上下文一起合并。** a 落地后才需要：跨类型引用要求「所有实体都建完之前不能开始任何
Translate」，于是相位要能分开调。基础版只有两步且无跨上下文依赖，一个函数就够。

---

## API

形态未定稿，以下是当前这一版。

```cpp
enum class MergeMatch   { Any, All };
enum class MergeMapping { Remap, Identity };

template<typename E> using MergeContextT = typename ContextTraits<E>::ContextType;

// Move
template<MergeMatch Match, MergeMapping Mapping, typename... Ts, typename E>
MergeResult<E> Merge(MergeContextT<E>& target, StagingContext<E>&& source);

// Copy
template<MergeMatch Match, MergeMapping Mapping, typename... Ts, typename E>
MergeResult<E> Merge(MergeContextT<E>& target, const StagingContext<E>& source);

// 世界 → 暂存
template<MergeMatch Match, typename... Ts, typename Source>
auto Extract(const Source& source) -> StagingContext<typename Source::Entity>;
```

`Extract` 三个轴里只有 `Match`：映射恒等（暂存是空的，`create(hint)` 原样还原），源必然是拷贝
（世界要保持完整）。`All` 的第一个真实用户大概率在这里——「把既有 `Transform` 又有 `Mesh` 的实体
抽成一个 prefab」。

`Extract` 的源没有 `StagingContext<E>` 可推，所以它推导整个 `Source`，用 `static_assert` 限定成活
上下文。（为此 `BasicContext<Entity>` 特化补了主模板一直有的 `using Entity`。）

`Merge` 的 `E` 从**源**推出来，目标那个 `MergeContextT<E>&` 是非推导语境但已经无所谓了。所以
`Merge<Any, Remap, Transform, Mesh>(world, eastl::move(staging))` 这样是能推的，
两个上下文类也不必为此补统一的实体别名。

`static_assert(sizeof...(Ts) > 0)`——空包在 `Any` 下什么都不做，在 `All` 下 `view<>()` 语义含糊。

`MergeResult<E>` = 这次新建的目标实体名单。对应关系不在返回值里——它在通知相位内以
`MergedFrom` / `MergedTo` 的形式活着，相位结束就清掉。

常用组合可以再包短名字，但底层只有这一个。

落点：

```
Core/ECS/StagingContext.h
Core/ECS/Merge/MergeComponents.h      // MergedFrom / MergedTo
Core/ECS/Merge/ContextMerge.h
```

改动的现有文件：
`BasicContext.h` / `WorldContext.h`（公开 `CreateEntity(hint)` / `EntityAt(hint)` / `GetStorage<T>`）、
`ContextReference.h`（`GetStorage<T>` 的门控）、
`Bus/ComponentEventBus.h` / `Bus/EntityEventBus.h`（各一个带默认展开的批量事件）、
`SceneManager`（批量 `Hierarchy` 的处理）。

---

## 已定决策

- **搬的是组件，实体集合是推出来的。** 全量的类型表归使用者。
- **不依赖反射系统。** 因此 `Ts` 由调用方列出——这也正是 entt 自己的选择。
- **源是 `StagingContext<E>`，一个和活上下文不同的类型。** 不能当目标、不进 `ExecuteContext` 栈、
  可写。方向和「不许自合并」因此是编译期的。
- **merge 要的三件事都不是特权。** 建实体按编号、直取 storage 是上下文的公开能力（门控在
  `ContextReference` 的 Traits 上）；不立即派发事件是事件策略轴的一个取值。不为 merge 开旁路。
- **重映射用两个临时组件，不建表。** 映射本来就是恒等 + 少数例外，只记例外。两个组件都在目标侧，
  所以 `Copy` 的 `const` 源不受影响，handler 也看得见。merge 在通知相位结束时自己清掉。
- **世界 → 暂存是单独的 `Extract`**，不是 merge 的一个模式。
- **`Identity` 下目标实体不存在时：跳过并 `LOG_ERROR`，不补建。** 「严格匹配」就该是严格的；
  undo 删除实体那个用例本来就该用 `Remap`，`create(hint)` 已经能恢复原编号。
- **`Copy` 模式对不可拷贝组件：调用点编译不过，不设退路。** 比运行期静默丢数据好。真撞上再说。
- **写入语义由 Mapping 推出，不做成独立的轴。**
- **`Source` 用重载而不是枚举。**
- **补发的是批量构造事件，不是逐个 `Construct`。** 一次事件带一批实体，复用
  `ComponentEventMask::Create`，基类给默认展开所以现有 handler 零迁移。

---

## 落地顺序

四步，每步独立编译、独立单测。挂 `Engine/Code/Test/Core/`，加 `MERGE_TESTS` 选项，四步全部不依赖
场景。

### 1. 上下文能力 + `StagingContext`

`BasicContext.h` / `WorldContext.h` 各加三个（`WorldContext` 的 `CreateEntity(hint)` 照常发
`OnEntityCreate`，它是公开能力）：

```cpp
Entity CreateEntity(Entity hint) { return m_registry.create(hint); }

Entity EntityAt(Entity hint) const noexcept
{
    using Traits = entt::entt_traits<EntityType>;
    const EntityType candidate = Traits::construct(entt::to_entity(hint), m_registry.current(hint));
    return m_registry.valid(candidate) ? candidate : EntityType{entt::null};
}

template<typename T> decltype(auto) GetStorage()       { return m_registry.template storage<T>(); }
template<typename T> decltype(auto) GetStorage() const { return m_registry.template storage<T>(); }
```

用 `valid(candidate)` 兜底——槽从未分配时 `current()` 返回 tombstone 的 version，`valid` 自然答假。

`ContextReference.h`：`GetStorage<T>` 加 `CanWriteComponentV<T>` / `CanReadComponentV<T>` 门控。

`StagingContext.h`：独立类，不复用 `BasicContext`（后者对 `Entity` 已被特化成 `WorldContext`，复用
要先动现有结构）。只写它真需要的面：`CreateEntity` ×2 / `EntityAt` / `Valid` / `Add` / `Get` /
`Has` / `GetView` / `GetStorage` ×2 / `Clear`，可移动不可拷贝。

**测试**：`EntityAt` 在「槽空」「槽被同 version 占」「槽被不同 version 占」「槽从未分配」四种情形的
返回值；`CreateEntity(hint)` 撞号时返回值 != hint 且 `EntityAt(hint)` 指向原住户。

### 2. `Merge` 骨架（`Any`/`All` × `Remap` × `Move`）

`Merge/MergeComponents.h`：`MergedFrom<E>` / `MergedTo<E>`。**必须模板化**——merge 对 `E` 泛型。

`Merge/ContextMerge.h`：枚举、`MergeResult<E>`、实现。三段非平凡逻辑：

```cpp
// ① 正向查找
E Forward(const MergeContextT<E>& target, E s)
{
    const E occupant = target.EntityAt(s);
    return (occupant != NullEntityV<E> && target.Has<MergedTo<E>>(occupant))
         ? target.Get<MergedTo<E>>(occupant).entity : s;
}

// ② 这个源实体处理过了吗——Any 模式下一个实体会在多个 T 的 storage 里被访问到。
//    不需要额外记账，MergedFrom 本身就是那份记录。
bool AlreadyCreated(const MergeContextT<E>& target, E s)
{
    const E t = Forward(target, s);
    return target.Valid(t) && target.Has<MergedFrom<E>>(t)
        && target.Get<MergedFrom<E>>(t).source == s;
}

// ③ 建实体。走实体 storage 而不是 CreateEntity(hint)，后者在 WorldContext 上会逐个发
//    OnEntityCreate，而这一批要留到最后一次性宣布。entt 自己的 snapshot loader 也是这么建的。
E t = target.GetStorage<E>().generate(s);
if (t != s) { target.Add<MergedTo<E>>(target.EntityAt(s), MergedTo<E>{t}); }
target.Add<MergedFrom<E>>(t, MergedFrom<E>{s});
```

②里 `.source == s` 不能省：目标里占着 `s` 这个号的实体，可能本来就在，也可能带着**上一步刚为别的
源实体建的** `MergedFrom`。

③的 `EntityAt(s)` 只在撞号时调，且放在 `CreateEntity` 之后——撞号说明槽没变，拿到的仍是原住户。

`Move` / `Copy` 用 `bool Consume` 非类型模板参数传进同一个 impl，两个公开重载各自转发，搬运时
`if constexpr` 决定要不要 `eastl::move`。

**测试**：目标空（一个 `MergedTo` 都不该写）/ 撞号 / **源与目标 id 位相同但 version 不同** /
tag 组件 / 目标缺 storage / `All` 挡掉缺组件的实体 / 结束后两个组件清干净。

### 3. 批量构造事件 + `SceneManager` 批量版

`ComponentEventBus.h` / `EntityEventBus.h` 各加一个带默认展开的虚函数。`WorldContext` 的批量
`Add(first, last, value)` 改成发一次批量事件——`AddEntities` 是这个事件的第一个用户。

`OnExternalWrite` 按目标类型重载（非模板实参优先匹配，不会和泛型版歧义）：

```cpp
template<typename... Ts, typename E> void OnExternalWrite(BasicContext<E>&, eastl::span<const E>) {}
template<typename... Ts>             void OnExternalWrite(WorldContext&, eastl::span<const Entity>);
```

每个 `T` 的 span 要过滤成「真带 `T` 的那些」，用一个跨 `T` 复用的 scratch vector，拿
`dst.contains(t)` 筛。整段用 `ExecuteContextGuard<E>` 包住，保证广播期间 target 是 current。

`SceneManager::OnComponentsConstruct`：

```
for (Entity e : entities):
    parent == Null                -> 打 HierarchyRootTag
    Has<MergedFrom>(parent)       -> 批内，链表已自洽，跳过
    否则                          -> 批边界，走现有 AddEntityInternal 真链入
```

`AddEntities` 那条路没有 `MergedFrom`，于是每个实体都走第三支，而它们的 `Hierarchy{}` 全空，
`AddEntityInternal` 只会打 root tag——和今天行为一致。**这也是两个临时组件必须在通知相位结束后才清
的原因：handler 正在用。**

**测试**：探针 handler 计数；**一棵已链好的树 merge 之后 `Hierarchy` 一个不少**（逐个补发会在这里
删组件）；`AddEntities` 行为不变；目标上有 owning group 时 group 仍有效。

### 4. `Copy` 重载 + `Extract`

`Copy` 就是 `Consume = false`。`Extract` 单独一个函数：源 `const MergeContextT<E>&`，目标是新建的空
`StagingContext<E>`，映射恒等、不通知、必然拷贝，只复用搬运循环；const `GetStorage<T>()` 返回指针，
循环里判空。

**测试**：源在合并后仍然完整；`Extract` 面对缺 storage 的类型不炸。

### 这四步之外的两处依赖

- **层级链表要从 `SceneManager` 抽成不挑上下文类型的自由函数**——只有 `SpawnModel` 改走暂存时才
  需要，不挡前四步。
- **事件策略轴**——在它落地前，merge 的静默插入靠直接 `GetStorage<T>().emplace()`（泛型
  `BasicContext` 本来就静默，`WorldContext` 走 storage 也绕开了它的 `Add` 重载），不额外开机制。

### 再往后（要 undo 时再做）

`Identity` + 显式实体列表。改动集中在建实体那十几行和通知钩子的签名，搬运循环基本不动。
