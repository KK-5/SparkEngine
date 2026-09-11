# 场景存读实施计划

> 设计见 `TODO_AssetSystemPlan.md`「阶段 4：场景保存」与 `TODO_ContextMerge.md`。**本文只管落地顺序、
> 每步的判据与踩坑点**，不重复设计论证。
>
> 有两处推翻了阶段 4 的原设计，见「对阶段 4 的两处修正」——那两节应以本文为准。

## 前置盘点

| 前置 | 状态 |
|---|---|
| 阶段 0 / 1 / 3、子资产统一、Image 规整 | ✅ |
| 阶段 2 的字段标记 | ✅ 9 个 `Reflect.h` 共 69 处 `Serializable` |
| 上下文合并（含实体引用重写、批量事件补发、`Extract`） | ✅ `Core/ECS/Merge/`、`MergeTest` |
| 键原值还原 | ✅ `ContextStorage.h:64` 的 `CreateEntity(hint)` 与 `EntityAt` |
| 资产预加载 | ✅ `AssetLoadBatch` + 欢迎页；`GetRegisteredAssetIds`（`AssetManager.cpp:121`）滤掉子资产、父 Ready 即子可解析 |
| 资源回收（`DeadTag` 职责收窄） | ❌ **推迟，见文末待办**。阶段 4 的清空先接受漏 slot |

阶段 4 自身要写的（都是加法）：类型级 flags、`GetStorages` / `GetEntities`、`Hierarchy` 反射、实体句柄编解码、
场景模块、清空世界、`MeshComponent` 去 `Ptr`。

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
`EditorInputSystem` 在建实体时各打一个。

**文件确定性是显式要求**，由 `SameSceneSameText` 锁住：同一个场景以相反顺序构建，文本逐字相同。空段
不写出——段存在 ⇔ 有实体带这个组件。

**零字段组件写 `{}`**：storage 的 `value()` 对它返回 `nullptr`，没有实例可遍历，类型本身就是数据。

写时验证仍不做：写侧现在确实攥着容器，但照实写的理由（见 Step 2「三处推翻」）不因此改变。

### 留给 Step 4 的

- **`DeadTag` 时序**：`Inspector` 删实体只打 `DeadTag`，`EntityReaper` 到 `TICK_LAST` 才销毁，而链接
  修补挂在销毁事件上。删除后、收割前保存，那个实体仍被当成正常实体写进文件。不能靠跳过 `DeadTag`
  实体解决——父与兄弟的链接还指着它，加载时被丢弃，兄弟链断掉；也不能就地收割——保存发生在 UI pass
  里，而 UI pass 是 render graph 执行的一部分。随 Step 4 的两帧命令一起解决。
- **`FindOrCreateEditorCamera` 仍会抢错人**：查找分支取任意一个带 Camera + Transform 的实体，只有新建
  的那个打了 `SystemOwnedTag`。改成按 tag 找，属于 Step 4 的所有权收尾。

### 顺带发现：`StagingContext` 意外是个聚合

`StagingContext() = default;` 在首次声明处默认化，不算用户提供的构造函数；C++17 又允许聚合带公开基类。
于是 `StagingContext<E>{}` 走聚合初始化，要在调用点直接构造 `protected` 的 `ContextStorage` 基类 →
C2512；`StagingContext<E> x;` 不受影响。测试里避开了这个写法。根治是把默认构造改成用户提供的
（`StagingContext() {}`）；C++20 起用户声明的构造函数即不再是聚合。**未改，待定。**

## Step 4　读侧 + 清空世界

1. `Load(path)`：JSON → `StagingContext<Entity>` / `StagingContext<MaterialHandle>` → `Merge`。
   段名 → 建哪种 staging、合进哪个活上下文，这两个分支就是模块划线里「需要具体类型的那一点」。
2. 先按 `entities` 清单 `CreateEntity(hint)` 建全，再挂组件；`HierarchyRootTag` 在 Notify 之前补。
3. 清空 = 除 `SystemOwnedTag` 外全打 `DeadTag`，交 `EntityReaper`。Open Scene 因此是编辑器侧的两帧命令
   （帧 N 标记、帧 N+1 加载），`LoadScene` 本身仍是一趟直线返回 bool。
4. 删掉 `LightSystem.cpp:59` 的默认平行光；`FindOrCreateEditorCamera` 改成按 `SystemOwnedTag` 找
   （tag 本身已在 Step 3 打上）。

`MergeMapping::Identity` 目前是 "Not implemented yet"，**不需要**：空世界下 `Remap` 的
`generate(source)` 本来就还原原值。

## Step 5　`MeshComponent` 去 `Ptr`

预加载之后这条干净了：删 `m_modelAsset`（`Feature/Mesh/Components.h:16`），`OnComponentConstruct`
（`MeshSystem.cpp:41/46/53`）改 `FindAsset(m_modelAssetId)` + 断言 Ready，**阶段 4 里那段
`AssetBus::MultiHandler` 重试直接不写**——它是为无预加载时期设计的过渡。`SpawnModel.cpp:131` 少一行。

`PendingBufferUpload` 持的是借指针（`Component.h:131-136`），源数据是 `ModelAssetData` 的顶点/索引数组，
租期跨帧：让 pending 这一侧带一个 owning 引用，清掉 pending 组件时一起析构。

## Step 6　（可选）路径选择器泛化

`SaveAssetDialogBus` 今天是资产形状的：收 `Ptr<Asset>`、自己调 `SaveAsset`、经 `AssetBus::OnAssetSaved`
回话。场景不是资产。场景正是「选一个 `project://` 下的路径」的第二个用例，拆成「选路径」+「保存资产」
两半是划算的——但排在最后，等格式和读写都跑通再动。

---

## 两个未定

**编辑器相机与恒等映射冲突。** 阶段 4 说「还原必须在世界为空时做」，又说编辑器相机常驻不清。相机大概率
占着低位 id，场景文件里的同一个 id 一撞就走 remap：引用会被正确翻译，但**写回时键漂移**，正是选原值当键
要避免的 diff 抖动。

倾向的解法：Open Scene 的标记帧里由 `EditorInputSystem` 自己收回相机，加载完成后 `FindOrCreate` 重建。
所有权说法不破（所有者决定何时收回何时重建），恒等映射保住，也不用引入被否掉的 `OnWorldReset` 总线——
两边都在编辑器侧，直接调用即可。**Step 4 动工前定。**

**Persistent 忘了挂载的失败模式。** 特化里写了 `Persistent` 但 `Reflect.h` 忘了链首那一句 → 静默不落盘。
可选的堵法：场景保存第一趟校验「有 `AddComponent` 却 `flags == None`」并 `LOG_WARN`。等落盘跑通再定。

## 记下的待办

**资源回收要通用化，不只服务 `InstanceBindingSystem`。** `GlobalBuffer.h:99` 今天靠
`GetView<Slot, DeadTag>` 恰好看见一次来归还 slot id，而 `DeadTag` 只是可见性过滤器（论证见
`TODO_AssetSystemPlan.md`「资源回收」）。原计划的方案 A（挂组件销毁事件）只对世界侧可用——泛型
`BasicContext<E>` 不派发 `ComponentEventBus`，material 侧够不着，所以它是个局部解。

要的是一套两个上下文通用的机制。**单独设计，不进本计划**；在那之前，Step 4 的清空会把今天已有的泄漏
（`Inspector` 每删一个实体漏一个 slot id）放大到一次一千。
