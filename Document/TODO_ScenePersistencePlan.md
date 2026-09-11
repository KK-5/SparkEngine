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

阶段 4 自身要写的（都是加法）：类型级 flags、`ForEachStorage`、`Hierarchy` 反射、实体句柄编解码、
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

## Step 3　`Hierarchy` 反射 + 写侧

1. `Hierarchy` 四个字段全反射、全标 `Serializable`，类型标 `Persistent`，字段 key 为
   `"Parent"` / `"First Child"` / `"Prev Sibling"` / `"Next Sibling"`，**不注册** `ComponentOperation`
   （否则 Inspector 露出四个可改的裸句柄）。`entityRefs` 已经列着这四个成员指针，不用动。
2. `ContextStorage` 开受控的 `ForEachStorage(fn(TypeId, common_type&))`，不把 `entt::registry` 漏出去。
3. 开场用 `GetAllTypes()` 建 `type.info().hash() → MetaType` 表——`.Type("X")` 改掉的是 `elem.id`，
   `Resolve(TypeId)` 那条线性扫描匹配不上 storage 给的 `type_hash`。
4. 新模块 `Engine/Code/RunTime/Feature/SceneIO/`，target `SparkSceneIO`，链 `SparkCore + SparkMaterial`
   （CMake 照 `Feature/Spawn/CMakeLists.txt`）。**刻意不叫 `SparkScene`**：Core 已有 `SceneManager` /
   `IScene`，那是层级管理，撞名会一直误导。
5. `SceneSerializer::Save(path)`：遍历两个上下文写 storage-major；per-entity 两个例外——默认材质实体、
   带编辑态 tag 的实体。
6. MenuBar 的 `Save Scene` 接上，**第一版固定路径** `project://Scenes/Scene.scene`。

**待决：保存与收割的时序。** `Inspector` 删实体只打 `DeadTag`，`EntityReaper` 到 `TICK_LAST` 才
销毁，而链接修补挂在销毁事件上。在这之间保存（MenuBar 与 Inspector 画在同一个 UI pass 里，够得着），
那个实体还活着、还挂在树上，会被当成正常实体写进文件。`Valid()` 遮不掉它——它那时确实有效。解法在
时序：保存命令排在收割之后，或保存前先跑一次收割。动工时定。

**若那时仍想让文件干净**：写侧自己压一个带 `isValid` 的作用域守卫，名字来自它正在遍历的那个容器
（不是环境）。那是纯加法，而且只有到这一步才有正确的容器可用——见 Step 2 的「三处推翻」。

判据：新建 `Test/Scene` 目标，建实体 + 层级 + 材质 → `Save` → 比对 JSON 文本。**格式在这一步冻结。**

## Step 4　读侧 + 清空世界

1. `Load(path)`：JSON → `StagingContext<Entity>` / `StagingContext<MaterialHandle>` → `Merge`。
   段名 → 建哪种 staging、合进哪个活上下文，这两个分支就是模块划线里「需要具体类型的那一点」。
2. 先按 `entities` 清单 `CreateEntity(hint)` 建全，再挂组件；`HierarchyRootTag` 在 Notify 之前补。
3. 清空 = 除编辑态 tag 外全打 `DeadTag`，交 `EntityReaper`。Open Scene 因此是编辑器侧的两帧命令
   （帧 N 标记、帧 N+1 加载），`LoadScene` 本身仍是一趟直线返回 bool。
4. `EditorInputSystem` 的相机打编辑态 tag；删掉 `LightSystem.cpp:59` 的默认平行光。

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
