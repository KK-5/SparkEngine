# 场景存读实施计划

> 设计见 `TODO_AssetSystemPlan.md`「阶段 4：场景保存」与 `TODO_ContextMerge.md`。**本文只管落地顺序、
> 每步的判据与踩坑点**，不重复设计论证。
>
> 有两处推翻了阶段 4 的原设计，见「对阶段 4 的两处修正」——那两节应以本文为准。落地过程中又推翻了本文
> 自己的两处：Step 3 的 `SystemOwnedTag`（见「对 Step 3 的修正」）与 Step 4 的「staging → Merge 由调用方
> 列类型」（见 Step 4）。已完成的步骤都按实际落地重写过。

## 前置盘点

| 前置 | 状态 |
|---|---|
| 阶段 0 / 1 / 3、子资产统一、Image 规整 | ✅ |
| 阶段 2 的字段标记 | ✅ 9 个 `Reflect.h` 共 69 处 `Serializable` |
| 上下文合并（含实体引用重写、批量事件补发、`Extract`） | ✅ `Core/ECS/Merge/`、`MergeTest` |
| 键原值还原 | ✅ `ContextStorage.h:64` 的 `CreateEntity(hint)` 与 `EntityAt` |
| 资产预加载 | ✅ `AssetLoadBatch` + 欢迎页；`GetRegisteredAssetIds`（`AssetManager.cpp:121`）滤掉子资产、父 Ready 即子可解析 |
| 资源回收（`DeadTag` 职责收窄） | ✅ 事后补上：引用计数句柄，见 `TODO_AssetSystemPlan.md`「资源回收」 |

阶段 4 自身要写的：类型级 flags、`GetStorages`、`Hierarchy` 反射、实体句柄编解码、场景模块、运行期数据
入口（`ComponentRuntime` + 运行期 `Merge`）、清空世界、`MeshComponent` 去 `Ptr`。

## 对阶段 4 的两处修正

### 一、类型级元数据只有一处：`ComponentTraits`

阶段 4「连带：类型级元数据的归属定死」把标志按消费者分家：编译期的进 `ComponentTraits<T>`，反射消费的
进 `.Traits()`。**结论改为：手写处只有 `ComponentTraits<T>`，反射侧是它的机械镜像。**

- 理由不是对称，是「同一个组件的类型级事实只该被人手写一次」。`componentEvents` / `entityRefs` 已经在
  那里，`editable` / `persistent` 是同一类断言，分到 `Reflect.h` 去写，"这个组件什么性质"就要查两个文件。
- 反射侧那份是**派生**的，不是第二个真相来源——文档反对的是两个手写点。
- 今天真正的乱是**一个**标志住了两处：`Name` 走 `.Traits(MetaTypeTraits::Editable)`，其余 8 个走
  `.Custom<ComponentTraitsRuntime>`，于是 `ComponentView.cpp:110-114` 得把两边都查一遍。

**挂载机制仍是 `.Traits()`，不是 `.Custom`。** 理由换成实打实的那条：类型级 custom 槽只有一个
（`shared_ptr<void>` 一次分配 + 一次跳转），拿它装几个 bool，等以后要挂真正的运行期对象（默认值工厂、
图标、分类字符串）就得塞进同一个结构里，那才是第二次重构。位掩码正是为 flag 准备的。
（`.Custom` 的类型安全不是问题：`entt.hpp:64643` 的 `operator Type*()` 会比对类型哈希，不匹配返回 null。）

于是 `MetaTypeTraits` 删除，枚举改名进 `ComponentTraits.h`；`ComponentTraitsRuntime` 连同
`SPARK_COMPONENT_TRAITS` 里那个转换运算符一起删。

### 二、实体句柄的编解码走 `JsonOperation`，不走字段级 trait

阶段 4「`EntityRef` 的编解码」要求落在 `WriteObject` / `ReadObject` 的字段循环里，理由是
「只有那里看得见 `MetaData`」。**那是循环论证**：因为先选了字段级标记，才需要 `MetaData`。
编码只看类型，不需要知道值来自哪个字段。

区分依据是字段的**类型**：`Entity`（Core）与 `MaterialHandle`（`Feature/Material/MaterialHandle.h:12`）
都是专用 `enum class`，除了实体 id 不做别的。给这两个类型各注册一个 `JsonOperation` 即可：

- 分派链第一站就是 `JsonOperation` 查表（`JsonSerializer.cpp:307`），天然排在 `is_enum()` 之前，
  原文担心的「走进 `WriteEnum` 会 `LOG_WARN` + false」自动避开；
- `Entity` 的注册放 Core，`MaterialHandle` 的放 `SparkMaterial`——正落在模块划线那条
  「material 的身份策略是从 `SparkMaterial` 注册进来的，不是序列化器写死的」上；
- **不新增 `MetaFieldTraits::EntityRef`**，字段那边只留 `Serializable`。

连带的好处：`ComponentTraits::entityRefs` 回到唯一手写点，它只服务 merge 的重映射（编译期、类型化成员
指针），序列化侧一个字都不说。原方案里「标了 trait 却忘了列成员指针 → merge 不重映射 → 引用指向活世界
里不相干的实体」这条静默分叉不存在了。

---

## Step 1　类型级 flags ✅ 已完成

1. `ComponentTraits.h` 加 `enum class ComponentFlags : uint8_t { None, Editable = 1<<0, Persistent = 1<<1 }`，
   `ComponentTraitsBase` 加 `static constexpr ComponentFlags flags = ComponentFlags::None;`。
2. **掩码直接手写，不从 bool 折算。** 派生会逼着宏在每个特化里重新生成折算代码——基类算出来的
   `flags` 读的是基类的 `editable`，特化改了 bool 不会更新（constexpr 成员没有虚派发）。今天
   `operator ComponentTraitsRuntime()` 必须写进宏，就是这个原因。手写掩码后继承覆盖天然成立，宏里
   什么都不用生成。
3. 8 个现成特化改一行 `editable = true` → `flags = ComponentFlags::Editable`（按阶段 4 的清单叠
   `| Persistent`）；material 侧三个类型（`Resource::StandardPBR` / `Resource::MaterialState` /
   `Material::MaterialAssetRef`）新增特化，**放 `Feature/Material/Components.h`**——「它是个组件」这件事
   今天就住在 Feature/Material，`Resource/` 不该知道 ECS。
4. 各 `Reflect.h` 链首挂载：`context.Reflect<T>().Type("X").Traits(ComponentTraits<T>::flags)`。
5. 删 `MetaTypeTraits.h`、删 `ComponentTraitsRuntime` 与宏里的转换运算符；`ComponentView.cpp:110-114`
   塌成单路读 `type.traits<ComponentFlags>()`。

**两个 entt 事实**：

- `traits()` 有 `static_assert(std::is_enum_v<Value>)`（`entt.hpp:67401`），值必须是枚举；而**转换运算符
  不参与模板实参推导**，所以 `.Traits(ComponentTraits<T>{})` 会把 `Value` 推成结构体、撞上断言。传
  `ComponentTraits<T>::flags`（枚举成员）推导才成立。
- `.Traits()` 作用于「最近创建的 meta 对象」，排在某个 `.Data()` 之后会**静默**挂到那个字段上。规矩是
  紧跟 `.Type()`。（新建的 factory 构造时 `bucket{parent}`，`entt.hpp:67040`，所以链首永远是类型级。）

判据：Inspector 的可添加组件列表与改动前完全一致。四个测试目标全绿。

### 落地与计划的两处出入

**`Name` / `Position` / `AllUIElement` 原本没有 `ComponentTraits` 特化**——它们是直接手写
`.Traits(MetaTypeTraits::Editable)` 的那一路，所以要连特化一起补。`Name` 的那份放在
`Core/CoreComponents/Name.h`，为此该头文件开始包含 `ECS/ComponentTraits.h`（无环：
`ComponentTraits.h` 只依赖 `Entity.h` / `EntityRefs.h`）。

**「挂载独立成句」确实有一个正当场景**，之前判它是过度设计时漏了：`Resource::StandardPBR` 与
`Resource::MaterialState` 的 `.Type()` 在 `Resource/Material/Reflect.h`，而那一层不该知道 ECS。
它们的 flags 因此在 `Feature/Material/Reflect.h` 里单独挂：

```cpp
context.Reflect<Resource::StandardPBR>().Traits(ComponentTraits<Resource::StandardPBR>::flags);
```

成立的依据仍是 `bucket{parent}`：新建的 factory 从类型级开始。**规则因此是「紧跟 `.Type()`，
或自成一句」，不是「只能紧跟 `.Type()`」**——类型的反射链在下层模块时，后者是唯一的写法。

## Step 2　实体句柄的 `JsonOperation` ✅ 已完成

1. `Core/Serialization/EntityJson.h` / `.cpp`：编码本体是一份，`EntityToJsonField<E>` /
   `EntityFromJsonField<E>` 是模板，两个上下文的句柄类型共用，**不可能分叉成两种落盘形式**。
   函数体落在 `.cpp` 里：`Json.h` 只前置声明 `JsonValue`，头文件碰不得它的成员，所以模板那一层
   只做 `static_cast` 和空值判断，真正接触 JSON 的是 `SerializeDetail::HandleToJson` /
   `HandleFromJson`。
2. Core 注册 `Entity` 的一对，`SparkMaterial` 注册 `MaterialHandle` 的一对 —— 后者正落在
   「material 的身份策略从 `SparkMaterial` 注册进来」那条划线上。两个类型都补了反射名
   （`.Type("Entity")` / `.Type("MaterialHandle")`），否则诊断日志里是 `<unnamed>`；名字只改
   `elem.id`，不影响 Step 3 那张按 `type.info().hash()` 建的表。
3. `MaterialComponent::m_material` 加 `Serializable`。

读侧三分，与 `AssetIdFromJsonField` 同一套判据：`null` = 未指定 → 空句柄 + true；无符号数 → 解码；
其它类型 = 坏文件 → `LOG_WARN` + false（`JsonOperation` 失败不回落到通用分派）。

判据：`JsonSerializerTest` 六例 + `MaterialSerializeTest` 两例，全绿。

### 三处推翻了原计划

**一、不做 `!Valid(handle)`，只编码哨兵值。** 阶段 4 原文要求「空与悬空一律写 `null`，判据是
`!Valid(handle)` 而不是 `== Null`」，依据是消费端把两者一视同仁
（`InstanceBindingSystem.cpp:55`）。**那是渲染期的兜底**（句柄坏了这一帧也得画出来），不能反推
文件该这么写。

这个仓库里实体引用都有明确的维护者——`Hierarchy` 的四个字段由 `SceneManager::OnComponentDestory`
在销毁时补链（`SceneManager.cpp:696`），材质实体在阶段 3 删掉 GC 之后不会被销毁。**所以活世界里
出现悬空引用 = 维护者有 bug，序列化器把它静默改写成 `null` 是在销毁证据**：存盘看着正常，重载
之后行为又变一次。序列化器照实写，加载时 `MergeTranslate` 自会丢弃并 `LOG_ERROR`。

顺带作废了中间那版方案——「用 `WorldExecuteContext::Current()` 取上下文来验」：`Current()` 给的是
环境里恰好压在栈顶的世界，不是正在被写的容器。序列化暂存上下文时（prefab、复制粘贴）里面的编号
在活世界里全都无效，**每个引用都会被静默写成 `null`**；而且 `s_current` 是 `static inline`、不是
`thread_local`（`ExecuteContext.h:50`——`CLAUDE.md` 里「thread-local context stack」的说法与代码
不符）。

**二、不新增 `MetaFieldTraits::EntityRef`**（已在本文前面记过），区分依据是字段类型。

**三、`MaterialComponent::m_material` 的字段名不改。** 阶段 4 要求改成 `"Handle"` 以避开
`{"Material":{"Material":4}}` 的叠字。但 `DrawMaterialSlot` 拿 `data.name()` 当材质槽那一行的
标签（`MaterialSlot.cpp:167`），改完 Inspector 里会显示 "Handle" ——说的是存储形式，不是这一行的
含义。**按阶段 4 自己那条判据**（「可读性不是理由——引擎开发者有 debugger 和日志」），一名两用的
两头冲突时让步的该是不被人读的那一头。叠字没有歧义成本：两个 `Material` 在不同层级上。

## Step 3　`Hierarchy` 反射 + 写侧 ✅ 已完成

1. `Hierarchy` 四个字段全反射、全标 `Serializable`，字段 key 为 `"Parent"` / `"First Child"` /
   `"Prev Sibling"` / `"Next Sibling"`。**不注册** `ComponentOperation`——否则 Inspector 露出四个可改的
   裸句柄；没有 `GetComponent`，`ComponentView` 连列都不列它。`entityRefs` 早已列着这四个成员指针。
2. `ContextStorage` 开两个**快照**：`GetStorages()`（类型擦除，不含实体存储）与 `GetEntities()`
   （走 `each()`——实体存储把已销毁的编号留在 packed 数组里待复用，直接迭代会吐出来）。只给 const 版。
3. 开场用 `GetAllTypes()` 建 `type.info().hash() → MetaType` 表，只收 `Persistent`——`.Type("X")`
   改掉的是 `elem.id`，`Resolve(TypeId)` 匹配不上 storage 给的 `type_hash`。
4. 新模块 `Feature/Scene/`，target `SparkScene`，链 `SparkCore + SparkMaterial`，由 `SparkRuntime`
   PUBLIC 链入。名字是先把 Core 的 `SceneManager` / `IScene` 改名成 `HierarchyManager` / `IHierarchy`
   腾出来的（`3686c45`）——`IScene` 的每个方法都是 `Hierarchy` 操作。
5. 接口两半：`WriteScene(world, materials, JsonValue&)` 是纯函数，收 `const ContextStorage<E>&`，
   **暂存上下文照样能写**；`SaveScene(path)` 是边缘，全仓唯一取环境上下文的地方，VFS 原子写盘，
   `dump(2)` 与 `.smat` 一致。**有组件编码失败就整个不写**：一个看着完整、实际丢了东西的文件比没存更糟。
6. MenuBar 的 `Save Scene` 接上，固定路径 `project://Scenes/Scene.scene`。

判据：`SparkSceneTest` 三例（形态、已销毁实体不列出、同一场景同一文本），五个测试目标全绿。

### 与计划的出入

**枚举给快照，不给回调。** 原计划是 `ForEachStorage(fn)`。硬理由是**确定性**：`registry::storage()`
迭代的是 `pools`，顺序 = storage 的创建顺序，即运行期历史；组件存储是 swap-and-pop，删一个实体就会让
packed 序抖动。文件要稳定，序列化器本来就必须物化再排序（段按类型名、实体按值），回调什么也不省。快照
还顺带关掉了回调的风险——遍历期间新建一个组件类型会让 `pools` 扩容。业务代码要某个组件仍走
`GetStorage<T>()`，显式声明类型是那条正路。

**两个 per-entity 例外收成一个 `SystemOwnedTag`**（`Core/CoreComponents/Tags.h`）。原计划按
`DefaultMaterialTag` 与 `EditorOnlyTag` 分别跳过：前者另有含义（`GetDefaultMaterial` 靠它发现），拿来
判落盘是让一个标记干第二份工作；后者太窄，用在默认材质上明显不对。两者的共同点是**生命周期归某个
系统、不归场景**——「资源回收」一节已给编辑器相机定过这个性质（所有权声明）。按事实命名，写侧跳过与
清空跳过都从同一条推出；按后果命名（`NoSaveTag`），清空那一侧就得另找依据。`MaterialSystem` 与
`EditorInputSystem` 在建实体时各打一个。**这一条已被下面的「对 Step 3 的修正」推翻。**

**文件确定性是显式要求**，由 `SameSceneSameText` 锁住：同一个场景以相反顺序构建，文本逐字相同。空段
不写出——段存在 ⇔ 有实体带这个组件。

**零字段组件写 `{}`**：storage 的 `value()` 对它返回 `nullptr`，没有实例可遍历，类型本身就是数据。

写时验证仍不做：写侧现在确实攥着容器，但照实写的理由（见 Step 2「三处推翻」）不因此改变。

### 留给 Step 4 的

- ~~**`DeadTag` 时序**~~：已解决，但不是靠两帧命令。这个引擎一帧只有一次 `TickBus::Broadcast`，渲染发生
  在 `RenderSystem::OnTick`（`TICK_DEFAULT`）里，而 `EntityReaper` 在 `TICK_LAST`——**同一帧的更晚位置**。
  保存排到 `TICK_LAST + 1` 执行即可：那时本帧的删除已被收割、链接已被修补。
- ~~**`FindOrCreateEditorCamera` 仍会抢错人**~~：已随下节的判据改动一并解决，查找分支改成
  `Exclude<Hierarchy>`。

### 顺带发现：`StagingContext` 意外是个聚合

`StagingContext() = default;` 在首次声明处默认化，不算用户提供的构造函数；C++17 又允许聚合带公开基类。
于是 `StagingContext<E>{}` 走聚合初始化，要在调用点直接构造 `protected` 的 `ContextStorage` 基类 →
C2512；`StagingContext<E> x;` 不受影响。测试里避开了这个写法。根治是把默认构造改成用户提供的
（`StagingContext() {}`）；C++20 起用户声明的构造函数即不再是聚合。**未改，待定。**

## 对 Step 3 的修正：成员资格是实体自己带的事实 ✅ 已完成

`SystemOwnedTag` 删除。判据换成实体自带的事实：

- 世界：**进场景 ⇔ 有 `Hierarchy`**（在场景图里）
- 材质：**进场景 ⇔ 有 `MaterialAssetRef`**（有资产身份）

推翻的理由不是命名口味，是两条硬的：

**一、要打标的那一侧是个开放集合。** 落地当天就漏了一个：`IconManager` 每个图标建一个世界实体（只挂
`IconComponent`，自己在 Shutdown 里收走），没人记得给它打标。第一份存盘因此有 25 个实体、段只覆盖 16
个，剩下 9 个是裸 id 的幽灵。系统会越来越多，而「场景内容」的入口是封闭的少数几处——声明该放在封闭的
那一侧。

**二、跳过法会写出指向集合外的链接。** 打标跳过是「从全体里减去几个」，被减掉的实体仍可能被写出去的
`Hierarchy` 链接指着（把物体挂到编辑器相机下即是）。而「带 `Hierarchy` 才写」写出去的正是场景图的闭包，
`HierarchyManager` 维护的不变量保证链接不出集合——**判据与引用完整性成了同一件事**，不用两套论证。这
也是 Godot 的模型（节点有 `owner` 才进 `PackedScene`）。

材质侧同理：`Resolve` 是唯一产生「被世界引用得到的材质」的路径，而它一定挂 `MaterialAssetRef`
（`MaterialUtils.cpp:59`）。没有 AssetRef 的恰好是两类系统持有物——默认材质，以及
`MaterialBindingSystem` 每帧从 `StandardPBROverride` 合成、用完即弃的那些（`MaterialOverrideRef` 不
persistent，覆盖数据本身存在世界侧，加载后照样重建）。所以「材质全部进」不成立，而默认材质不进也不欠
什么：没有任何 `MaterialComponent` 指向它，它只是渲染期兜底。

落地：

1. `WriteContext<Membership, E>` 收成员存储当实体集合；成员存储以**擦除基类**持有——typed storage 迭代
   的是值，基类迭代的才是实体。
2. `ContextStorage::GetEntities()` 删除（唯一调用者没了）。清空走 `GetView<Hierarchy>`，读侧从 JSON 建。
3. 编辑器相机不再 `AddEntity` 进层级，`FindOrCreateEditorCamera` 的查找分支改成 `Exclude<Hierarchy>`
   ——「不在场景图里的那个相机」就是它。`TransformSystem` 对无 `Hierarchy` 实体走 local=world
   （`TransformSystem.cpp:43-47`），世界矩阵不受影响。**可见变化：相机不再出现在大纲里**（Unity/Unreal
   的视口相机同样不在）。
4. `IconManager` 一个字不用改。

**由此多出一条约定**：代码建的实体想被保存，必须进层级（`IHierarchy::AddEntity` / `SetParent`）。这条
是自描述的——把东西放进场景图，本来就是在声明它属于这个场景。

`LightSystem.cpp:59` 的默认平行光在新判据下**仍会进文件**（它调了 `AddEntity`，确实是场景内容），按原
计划由 Step 4 删除。

## Step 4　读侧 + 清空世界 ✅ 已完成

原计划的第 1 条（「JSON → StagingContext → Merge」）在落地时**走不通**：`Merge<Match, Mapping, Ts...>`
的组件类型是模板参数，而读侧的类型来自文件；`SparkScene` 也不该认识 Transform/Mesh/Light。修正后的形状
分两层。

### 一、ECS：运行期数据入口

一个类型擦成 TypeId 之后，**唯一要不回来的是它的存储**——entt 用元素类型构造 `basic_storage`，
`registry.storage(id)` 只查不建（`entt.hpp:40074`）。所以每类型注册**一个**函数，其余全部擦除：

- `ComponentRuntime<T>(context)`（`Reflection/Utility.h` 旁的 `ECS/ComponentRuntime.h`）注册
  `"ComponentStorage"` 一个 meta func，E 由 `ComponentTraits<T>::entity_type` 推出；顺带
  `static_assert` 挡住不可拷贝的组件（entt 的 `push` 对它们是静默不插入），并核对
  `entityRefs` 与反射字段的镜像。
- `StagingContext<E>::Add(entity, const MetaAny&)`：经 `RuntimeComponentStorage` 调那个 meta func 拿到
  存储 → `push(entity, value.base().data())`。
  **只开在暂存容器上**——运行期来的数据只能先落进没人观察的地方，再由 Merge 按协议送进活世界。
- `Merge(target, StagingContext<E>&&)` 运行期重载：合什么由**源里有什么**决定。建实体、`MergedFrom`/
  `MergedTo` 记账、批量通知、清账的顺序与模板版一字不差；差别只有三条，都是擦除逼出来的——只支持
  `MergeMatch::Any`、拷贝而非移动、要求类型已反射并注册。

**引用重写按字段类型**（`data.type().info() == GetTypeInfo<E>()`），与 Step 2 编码侧「区分依据是字段的
类型」同源，不需要第二处标记。代价说清楚：它看得见的是**反射过的**字段，而 `entityRefs` 声明的是组件的
全部引用成员——两者今天重合，注册时核对一次，不合就 `LOG_WARN`。编译期那条 `Merge<Ts...>` 不动，仍走
成员指针，也不依赖反射是否注册。

### 二、`MergeResult` 删除

`MergedFrom`/`MergedTo` 存在的意义就是「记账即上下文里的组件」，再返回一份 `created` / `renumbered`
拷贝是第二种表示。改为 `MergeRecords::{Clear, Keep}`：默认照旧清账，需要继续提问的调用方自己留着，用
`target.GetView<MergedFrom<E>>()` 问「建了哪些」、`MergedEntity(target, source)` 问「它落到哪」，
用完 `ClearMergeRecords`。下一次合并前若记账还在，`DropStaleRecords` 报错并丢弃——两批不能读成一批。

### 三、Scene：读侧

`StageScene`（文件 → 两个 staging）与 `MergeScene`（staging → 活上下文）分开，于是：

- **两半都立完才开始合**，坏文件时两个活上下文一个字节没动——写侧「有一个编码失败就整个不写」的对称面。
- **材质先合，并 `MergeRecords::Keep`**；世界 staging 里类型为 `MaterialHandle` 的字段按材质的记账翻译，
  然后才合世界。世界那趟的翻译只认 `Entity` 类型的字段，跨上下文这一步只能在这里做，而且在 staging 里做
  ——事件还没发出去，世界侧从头看到的就是对的值。**这是原计划完全没有的一条**：材质上下文同样会改号
  （默认材质常驻、占着低位 id）。
- 段名 → `Resolve(HashString::value(name))` 直查（`.Type("X")` 改的就是 meta id）。未知段名 `LOG_WARN`
  跳过；id 解析失败、组件解码失败、组件挂到清单外的实体上，都是坏文件 → 整个不合。
- `HierarchyRootTag` 不用补：批量 `OnComponentsConstruct` 里 `HierarchyManager` 自己按 `parent == Null`
  打上。

### 四、清空与编辑器命令

**清空直接销毁，不打 `DeadTag`。** `DeadTag` 是过滤器，不是工作队列；靠它触发回收动作，等于每加一条逻辑
都要先想「会不会影响回收」。安全性来自 `DestoryEntity(first, last)` 的实现——它先把整个范围的销毁事件发
完再统一销毁，所以链接修补时被指向的实体都还活着，一整棵树按任意顺序清都不会踩空。

**菜单只记意图。** 菜单回调跑在 `RenderUI::Render(commandList)` 里，也就是渲染通道正在录制命令时；在那里
销毁或新建上千个实体是在渲染中途抽数据。`SceneCommandSystem`（tick 序 `TICK_LAST + 1`）在**同一帧**执行：
那时 `EntityReaper` 已经收割完本帧的删除、`HierarchyManager` 已经修补完链接，所以保存写出去的场景读回来
就是它自己——原计划记着的「`DeadTag` 时序」问题由此消失，而且不需要跨帧命令。

`OpenScene` 先立后清：文件坏掉时你手上的场景不动。

`MergeMapping::Identity` 仍然不需要：空位下 `generate(source)` 本来就还原原值。

## Step 5　`MeshComponent` 去 `Ptr` ✅ 已完成

删 `m_modelAsset`，`OnComponentConstruct` 改 `FindAsset<ModelAsset>(m_modelAssetId)`（`AssetManagerInterface`
补了一个与 `LoadAsset<T>` 同形的类型化 `FindAsset<T>`），`SpawnModel.cpp:131` 少一行。`AssetBus` 重试没写。

**这条是被实机问题逼出来的，值得记**：场景能打开、天空盒正常，但网格不显示。原因正是组件里那个借指针——
它没反射也没落盘，于是从文件读回来的 `MeshComponent` 有正确的资产 id 却没有指针，`OnComponentConstruct`
一句 `if (!m_modelAsset) return;` **完全静默**地退出。一个组件带着有效 id 却建不出资源必须出声，现在两条
失败路径各自 `LOG_ERROR` 带资产路径与实体编号。

`PendingBufferUpload` 的借指针**不用加 owning 引用**：查清楚了，源数据的所有权在资产数据库
（`AssetDataBase::m_assets` 持强引用，注释明说 never evicts），真有驱逐/卸载时再做，那时理由才是实的。
依赖写在发起上传的那一行。

## Step 6　（可选）路径选择器泛化

`SaveAssetDialogBus` 今天是资产形状的：收 `Ptr<Asset>`、自己调 `SaveAsset`、经 `AssetBus::OnAssetSaved`
回话。场景不是资产。场景正是「选一个 `project://` 下的路径」的第二个用例，拆成「选路径」+「保存资产」
两半是划算的——但排在最后，等格式和读写都跑通再动。

---

## 两个未定 —— 都已定

**编辑器相机与恒等映射冲突：接受 remap。** 倾向过的解法（Open Scene 时收回相机再重建）解决不了问题：
主要占位者不是相机，而是图标实体，而且它们的数量随会话变化（`FieldWidgets`、`SaveAssetDialog` 会按需再
建）。所以键漂移接受，改号时引用由 `MergedTo` 正确翻译，材质侧同理。

**`Persistent` 忘了挂载：加载入口校验。** `CheckPersistentTypesCanBeRead()` 在 `ReadScene` 开头扫一遍
「有 `Persistent` 却没有 `ComponentStorage` 函数」并 `LOG_ERROR`。写读侧测试时它当场抓到一个真货
（`SceneTest::Marker`），成本是一次 O(类型数) 的遍历。

## 记下的待办

**资源回收已通用化。** 原文写的是「`GlobalBuffer.h:99` 靠 `GetView<Slot, DeadTag>` 恰好看见一次来
归还 slot id，清空改成直销之后这条路彻底不触发」——属实，每次 Open/New Scene 漏掉与场景实体数相同的
slot id。

已解决，但**不是**按当时设想的「挂组件销毁事件」。普查发现同一个形状有四处、分属三个上下文，而事件
只有 `BasicContext<Entity>` 派发。落地的是引用计数句柄（`Handle/HandlePool.h` + `SlotPool` +
`ShadowTilePool` / `ShadowRowPool`）：最后一个 `SharedHandle` 析构即归还，与谁销毁、在哪个 tick 销毁
无关，三个上下文一套代码。完整记录在 `TODO_AssetSystemPlan.md`「资源回收」一节末。

**`StagingContext` 意外是个聚合**（见 Step 3 的「顺带发现」）：`StagingContext<E>{}` 会走聚合初始化并撞上
`protected` 基类 → C2512。根治是把默认构造改成用户提供的（`StagingContext() {}`）。**未改，待定。**

**路径写死。** Save / Open 都是 `project://Scenes/Scene.scene`，等 Step 6 的路径选择器。
