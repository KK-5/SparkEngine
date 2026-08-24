# 资产系统补齐计划（路径身份 / 磁盘缓存 / 材质资产 / 场景保存）

> 标了「待细化」的地方还没定，不要当成已决方案实现。

## 背景

资产系统的骨架是齐的——`AssetId` → `AssetDataBase` → worker 线程 → 按 `AssetType` 走
`AssetBuildBus` 的 Load/Compile。欠的是三笔债：

1. **材质不是资产**——`MaterialParams` 只活在运行时的 `MaterialContext` 里，没名字、没文件、
   不能跨场景共享，Inspector 里改完退出就没了。
2. **没有磁盘缓存**——每次启动重解 PNG、重跑 BC 压缩、重跑 DXC、重解 glTF + meshopt、重烘 HDRI。
3. **场景存不了**——`MenuBar.cpp:24-32` 三个菜单项都是 `LOG_INFO` 空壳。

三件事共享同一个前置：**资产引用得能被写进文件、再读回来变成一个可用的 `AssetId`**。

---

## 状态

**阶段 0.a 已完成。** `AssetId::m_path` 现在恒为虚拟路径，`searchPaths` 管道整条拆除，
glTF 外部 URI 改为词法解析。

**阶段 0.b（`AssetId` 携带类型）已完成。** 类型现为身份的一部分，`Asset::m_type` 与
`AssetBuildContext::type` 两个副本已删除。

**阶段 0.c（descriptor 反射序列化）已完成。** JSON 库、EASTL 容器 traits、`MetaFieldTraits`、
反射序列化器（13 例测试）、descriptor 反射与 JSON 两向函数（7 例测试）全部落地。

**阶段 1（磁盘缓存）已定方案，未开工**，是当前的下一步；0.d 亦已解锁。

另有两项已随 0.a 落地：

- 三个 descriptor 的 `Hash()` 用 `HashString("XxxDescriptor")` 做种子，避免跨类型撞哈希。
  `DescriptorHashTest` 守住这条。
- `ModelAssetDescriptor::type` 默认值改为 `GLTF`。

---

## 现状盘点

### 已经有的

- **Load/Compile 之间的缓存缝**。`AssetManager.cpp:281`：Load 若直接给回 `ctx.compiledData`，
  Compile 整段跳过。今天服务 `.ktx2`，将来服务 cache hit。
- **图片的编解码两头**。`ImageAssetCompiler.cpp:411` 已经算出 ktx2 blob（只用来打了条 log 就扔了）；
  `ImageAssetLoader.cpp:241` 已经能读 ktx2。
- **子资产机制**。`AssetId::OfSub` + `ImageAssetBuilder::PublishSubAsset`，glTF 内嵌图片和 IBL
  烘焙产物都走这条路。
- **反射系统在用**。组件、枚举（`Light/Reflect.h:15` 的 `LightType`）都已注册；
  `Reflection/Utility.h:86` 的 `ComponentOperation` 把 `HasComponent` / `GetComponent` /
  `AddOrReplaceComponent` / `IsWorldComponent` 注册成了 context-free 的反射函数，现有 7 个世界
  组件无需新增注册。

### 欠账

| 欠账 | 位置 | 说明 |
|---|---|---|
| ~~descriptor 不可序列化~~ ✅ | `Resource/AssetDescriptorJson.h` | 阶段 0.c 已完成 |
| 磁盘缓存 | 无 | 见阶段 1 |
| 内存驻留无淘汰 | `AssetDataBase.h` | map 持强 `Ptr<Asset>`，refcount 永不归零，`Asset.cpp:17` 的 `Shutdown`→`ReleaseAsset` 够不着 |
| 材质无资产形态 | `Feature/Material/` | 唯一创建点是 `SpawnModel.cpp:111` |
| 三个平行的 CPU 材质结构 | `ModelAsset.h` / `Material/Components.h` | `Resource::Material` 与 `MaterialParams`，靠 `MaterialParamsFromModel` 搭桥 |
| ~~无 JSON 库~~ ✅ | `Core/Serialization/Json.h` | nlohmann 3.11.3 已 vendor（simdjson 虽在树内但只读，当不了写盘端） |
| 场景序列化 | 无 | 见阶段 4 |

---

## 已定决策

1. **路径即身份，不上 GUID。** 存储形式为复合对象，预留 `guid` 字段位：将来若加，`guid` 为主键、
   `path` 为 fallback。
2. **虚拟路径 + 命名挂载点**，语法 `mount://relative`。见阶段 0.a。
3. **`AssetId` 的持久化形式是 JSON 复合对象。** 单一字符串仅作单向显示形式（log、Inspector 只读框），
   不用于解析。第一版不提供「全默认值退化成裸字符串」的短形式。
4. **资产类型是身份的一部分，由 `AssetId` 携带。** 见阶段 0.b。
5. **`AssetId::operator==` 比实值，`AssetHash` 保持 32 位。** 哈希只做桶索引与快速否定；
   descriptor 仍由哈希覆盖。`operator<` 与 `AssetDescriptor::Equals` 删除。
6. **descriptor 按字段序列化，走反射。** 见阶段 0.c。
7. **材质走子资产路线**（阶段 3）——glTF 内嵌材质由 `ModelAssetBuilder` publish 成
   `model.glb:material/0`，不给 `.smat` 另起结构。
8. **JSON 使用 vendor 的 `nlohmann/json.hpp` 单头文件。**
9. **挂载表独立成 `Core/VFS/` 模块**，namespace `Spark`。接口 `FileSystem`，实现 `MountTable`，
   系统 `VFSSystem`。

---

## 阶段 0：路径身份

| 子问题 | 阶段 1 缓存 | 阶段 3 材质 | 阶段 4 场景 |
|---|:--:|:--:|:--:|
| 0.a 虚拟路径 + 挂载点 ✅ | ✅ | ✅ | ✅ |
| 0.b `AssetId` 携带类型 ✅ | ✅ catalog 需要 | ✅ | ✅ |
| 0.c descriptor 序列化 ✅ | ❌ 只需 `desc->Hash()`，已有 | ✅ | ✅ |
| 0.d `AssetId` 复合形式 | ❌ | ✅ | ✅ |

### 0.a　VFS 挂载点 ✅ 已完成

核心不变量：

> **`AssetId::m_path` 永远是虚拟路径 `mount://relative`，不存在第二种形态。**
> 物理路径只在真正读文件的那一刻出现，且只经过 `FileSystem` 一处。

#### 落地形态

```
Engine/Code/RunTime/Core/VFS/
    FileSystem.h        class FileSystem  —— 接口，Service 的键
    MountTable.h/.cpp   class MountTable : public FileSystem  —— 实现
    VFSSystem.h/.cpp    class VFSSystem : public ISystem,
                                          public Service<FileSystem>::Handler
```

`FileSystem` 接口：`Mount` / `Unmount` / `ToVirtual` / `ToPhysical` / `GetMountNames` /
`GetPhysicalDirs` / `IterateDirectory`。`ToPhysical` 是查表不是搜索；`Mount` 拒绝物理目录相互
包含的挂载点，因此至多一个挂载点能命中一个物理路径。

阶段 1 会在同一接口上追加 `ReadFile` / `WriteFile` / `Exists` / `FileStamp`，并新增
可写的 `cache://` 挂载。

#### 挂载点

| 挂载 | 物理目录 | 内容 |
|---|---|---|
| `engine://` | `Engine/Asset/` | Shaders、BRDFLut、Shaderball |
| `project://` | `Project/Asset/` | 用户内容；将来的 `.smat` / `.scene` |
| `editor://` | `Engine/Code/Editor/Asset/` | 编辑器 UI 图标（`BottomPanel.cpp:203-209` 在用） |
| `test://` | `Engine/Code/Test/Resource/` | 测试资产 |
| `sandbox://` | `SandBox/Asset/` | SandBox 程序 |

#### 获取方式

| 谁 | 怎么拿 |
|---|---|
| `SparkAssetManager` | `Service<FileSystem>::Get()`，`InitInternal` 取一次存下并 ASSERT |
| Loader / `ShaderAssetCompiler` | 按参数传入 `const FileSystem&`，三个 loader 均无状态 |
| 编辑器资产浏览器 | `Service<FileSystem>::Get()` |
| 单测 | 局部 `MountTable`，按 `const FileSystem&` 传入 |

#### 一并落地的

- `AssetId` 构造时 `ASSERT` 路径含 `://`。
- glTF 外部 URI 用 `ResolveSiblingVirtualPath` 对父模型的虚拟目录做词法解析；
  `extraSearchPaths` 与 `AssetBuildContext::searchPaths` 删除。
- DXC 的 source name 用**物理**路径（它拿这个去拼引号 include）。
- 全仓库唯一保留搜索语义的地方是 `ShaderAssetCompiler` 的 include handler，走 `GetPhysicalDirs()`。
- `AddSearchPath` / `RemoveSearchPath` / `GetSearchPathes` / `ResolveAssetPath` /
  `SetSearchPaths` 全部删除。

### 0.b　`AssetId` 携带 `AssetType` ✅ 已完成

核心不变量：

> **`AssetId` 的任何构造路径都必须显式给出类型**，形式可以是编译期的 `Of<T>`，
> 也可以是运行期的 `AssetType` 实参。不存在「不给类型也能造出 id」的口子。

类型是身份的属性，不是实例的属性——引用可以在没有实例的情况下大量存在（组件字段、场景文件、
缓存 catalog、发行包）。

#### 落地形态

`AssetId` 的公开构造入口只剩五个，前四个由 `T::GetAssetTypeStatic()` 供型：

```cpp
Of<T>(path)                                    OfSub<T>(parentPath, subLabel)
Of<T>(path, desc)                              OfSub<T>(parentPath, subLabel, desc)
Of(path, subLabel, AssetType, Ptr<AssetDescriptor>)   // 反序列化 / 已备好的 descriptor Ptr
```

`WithDescriptor` 沿用原 id 的类型。descriptor 自身不带类型标签，因此换 descriptor 时的
类型一致性不可校验——由「同一资产类型的 usage 变体」这一用法约束保证。

#### 一并落地的

- `AssetType` 折进 `ComputeHash`；`IsValid()` 要求类型非 `Unknown`。
- `ValidateAssetId(path, type)` 取代 `ValidateAssetPath`，多断言一条「路径非空则类型非 Unknown」。
- `Asset::m_type` 删除，`GetAssetType()` 委托 `m_id`；构造函数收成 `explicit Asset(AssetId)`。
- `LoadAsset` / `RequestAsset` / `CreateAsset` 去掉类型参数；`AssetBuildContext::type` 与
  `MakeChild` 的类型参数删除，builder 的 `ASSERT` 改看 `ctx.id.GetAssetType()`。
- `LoadAsset<T>` / `RequestAsset<T>` 现在校验 id 的类型与 `T` 一致（`ValidateAssetType`，
  与 `ValidateAssetId` 同样离线定义以免把日志头文件拖进 `AssetTypes.h`）。
- `GetSupportAssetType`（扩展名嗅探）保留，但仅用于 import / 注册时回答「这个文件是什么」。
  此后类型一路显式携带，不再有第二次推断。
- `AssetIdTypeTests` 守住：`Of<T>` 供型、子资产类型与其路径扩展名不一致、类型区分同路径 id、
  `WithDescriptor` 保型、默认 id 无类型。

### 0.c　descriptor 反射序列化 ✅ 已完成

descriptor 按字段序列化，能表达任意字段组合。序列化形态，省略等于默认值的字段：

```json
"desc": { "usage": "NormalMap", "colorSpace": "Linear" }
```

descriptor 自身不写类型标签——类型由 `AssetId` 的 `type` 字段决定（见 0.d）。
**枚举存名字不存数值。**

#### JSON 落位 ✅ 已完成

`Engine/3rdParty/nlohmann/include/nlohmann/` 放 `json.hpp` + `json_fwd.hpp`（v3.11.3），
INTERFACE target `nlohmann_json`，`SparkCore` PUBLIC 链接。

> **公共头只 include `json_fwd.hpp`，`json.hpp` 只出现在 `.cpp`。**
> `nlohmann::json` 是 `basic_json<...>` 的别名，无法手写前置声明，`json_fwd.hpp` 就是为此存在的。

`Core/Serialization/Json.h` 定 `using JsonValue = nlohmann::ordered_json`。用 ordered 版：字段按
插入顺序落盘，文件 diff 稳定；`map` 排序版会把 `usage` 排到 `colorSpace` 之后。

代价：`json_fwd.hpp` 自身 include 了 `<map>` `<memory>` `<string>` `<vector>`，公共头无法完全躲开 STL。

#### 字段级 traits

**落盘字段是选择性加入的**：只有显式标记的字段才序列化。反射在本仓库是先为 Inspector 建的，
opt-out 会让每个为面板加的只读字段自动变成文件格式的一部分。

```cpp
// Core/Serialization/MetaFieldTraits.h
enum class MetaFieldTraits : uint8_t
{
    None         = 0,
    Serializable = 1 << 0,
};
```

用法 `.Data<&ImageAssetDescriptor::usage>("usage").Traits(MetaFieldTraits::Serializable)`。

- **不加进 `MetaTypeTraits`**——那个类型正在被 `ComponentTraits` 取代，现有三处用法全是类型级。
- **不加进 `ComponentTraits`**——它以类型为键，且继承 `entt::component_traits`（ECS storage 配置），
  descriptor 不是组件。「组件整体是否持久化」这个类型级问题才归它，阶段 4 再加。
- **字段级只能走 `Traits` 不能走 `Custom`**——entt 的 `custom` 是单槽赋值，字段的那个槽已被
  `UIElement` 占用；`traits` 是 `|=` 位掩码，可累加。

#### 序列化器 ✅ 已完成

`Core/Serialization/JsonSerializer.h/.cpp`，命名空间平铺 `Spark`（与 `MetaTypeTraits.h` /
`UIElement.h` 一致）。两个函数，没有第三个：

```cpp
//! 失败 = 有标了 Serializable 的字段没能序列化。out 保留已写入的部分，不回滚。
bool SerializeToJson(const MetaAny& value, JsonValue& out);

//! target 取引用而非传值：传值时若调用方给的是持有型 MetaAny，写入会落到临时对象上而静默丢失。
//! 缺键 = 保留 target 现值。尽力而为：单个字段失败不中断遍历，只反映在返回值上。
bool DeserializeFromJson(const JsonValue& in, MetaAny& target);
```

分派顺序（`eastl::string` 必须排在复合之前——它是没反射过字段的 class，落到复合分支会安静产出 `{}`）：

| 顺序 | 类别 | 判据 | 形态 |
|:--:|---|---|---|
| 1 | 枚举 | `is_enum()` | 字符串（名字） |
| 2 | 算术 | 基础类型表 `try_cast` | number / bool |
| 3 | 字符串 | `try_cast<eastl::string>` | string |
| 4 | 序列容器 | `is_sequence_container()` | array，逐元素递归 |
| 5 | 复合 | `is_class()` | object，遍历 `data()` 递归 |

未标 `Serializable` 的字段静默跳过，不是错误、不 warn。标了却落不到上表任一类，才 `LOG_WARN` + false。

五个必须踩准的点：

- **枚举判定先于任何 cast**，且先把 `MetaType` 存下来——`ComponentView.cpp:409` 记录的 entt 行为。
- **枚举按值比对取名**，遍历 `t.data()` 逐个 `d.get({})` 与当前值比整数，不假设「声明顺序 == 数值」。
  比对走 `allow_cast<int64_t>`（entt 对 enum 也登记了 `conversion_helper`）；`meta_any` 没有公开的
  裸指针访问，memcmp 那条路走不通。
- **算术表列基础类型，不列定宽别名。** `int8_t` 只是 `signed char` 的别名；真正的坑是 MSVC 上 `long`
  是独立于 `int` 和 `long long` 的第三个 32 位类型，任何定宽别名都覆盖不到。表列 `bool` / `char` /
  `signed char` / `unsigned char` / `short` / `unsigned short` / `int` / `unsigned` / `long` /
  `unsigned long` / `long long` / `unsigned long long` / `float` / `double`，`try_cast` 精确匹配。
- **写回一律 `data.set()`，不原地改。** entt 的 data 默认 policy 是 `as_is`（`factory.hpp:319`），
  `data.get(instance)` 对成员字段返回副本。反序列化是「取副本 → 递归填 → set 回去」，容器同理。
  这与仓库现有的反射写约定（`set()` + `ReplaceComponent`）一致。
- **「省略默认值」比较序列化结果，不用 `meta_any::operator==`。** entt 只对 equality-comparable 的
  类型生成 compare，`ShaderStageEntry` 与 `eastl::vector` 都没有 `operator==`，一律判成「不等于默认」
  于是永不省略。改为 `type.construct()` 造默认实例、序列化一遍、逐字段比 `JsonValue`，相等则 erase。

省略默认值是类类型序列化的固有行为，**逐层生效**，不做成开关：嵌套结构全默认时塌成 `{}`，父层再比
一次又整个省掉，规则只有一条。不可默认构造的类型退化成全字段写出。

**字段顺序是确定的**：entt 用 `std::vector<meta_data_node>` 存字段，迭代即注册顺序；配上
`ordered_json`，落盘顺序 == 反射注册顺序。测试可直接断言 `dump()` 字面量。

#### EASTL 容器 traits

entt 3.16 只特化了 `std::*` 的容器 traits。新建 `Core/Reflection/EASTLMeta.h`，为 `eastl::vector`
继承 `entt::basic_meta_sequence_container_traits`——EASTL 的
`size/clear/reserve/resize/begin/end/insert/erase` 与 `value_type` / `const_reference` /
`const_iterator` 全部对得上。

> **这个头由 `ReflectContext.h` 直接 include**，不能靠各处自己记得。特化点晚于
> `resolve<eastl::vector<X>>` 的实例化即 ODR 违规，症状是运行期 `is_sequence_container()`
> 静默返回 false。

**`eastl::array` 不做。** entt 判定容器定长与否靠 `std::tuple_size<Type>` 是否完整，而 EASTL 特化的是
`eastl::tuple_size`；`eastl::array` 会被误判成动态容器，进而实例化它没有的 `clear()` / `resize()`。
目前无字段使用，等第一个用例出现时连同 extent 问题一起解。

#### 反射 ✅ 已完成

`Resource/Reflect.h` 反射三个 descriptor、`ShaderStageEntry`，以及 `TextureCompression` /
`ImageColorSpace` / `ImageUsage` / `ModelAssetType` / `ShaderBackend` / `RHI::ShaderStage`。
枚举反射沿用 `Light/Reflect.h:15` 的写法。**枚举名即落盘格式，一经反射即冻结。**

字段全部要落盘，11 处 `MetaFieldTraits::Serializable`（descriptor 8 + `ShaderStageEntry` 3），无例外。

`RHI::ShaderStage` 只反射真实 stage，**不反射 `Count` 与 `GraphicsCount`**——后者与 `Compute` 同值，
反射了会让 Compute 以错误的名字落盘。

`ShaderDescriptor::stages` 照常序列化。它与 `Hash()` 的不一致（stages 不在身份里，却能被写进文件）
是独立的一条待决，不在 0.c 内解决。

#### descriptor ↔ JSON ✅ 已完成

`Resource/AssetDescriptorJson.h/.cpp`，`Spark::Resource` 的自由函数，不挂 `AssetManager`——场景加载与
材质加载都要用，却都不该经过一个系统单例，且两个方向都不查 DB、不依赖资产是否加载过：

```cpp
bool                 DescriptorToJson(const AssetDescriptor&, AssetType, JsonValue&);
Ptr<AssetDescriptor> DescriptorFromJson(AssetType, const JsonValue&);
```

**各一个 switch，`static_cast` 写在 case 里。** 不建类型表、不写 `ToJson<T>` 模板、不给
`AssetDescriptor` 加「自报反射类型」的虚函数——那些都是为绕开一个三分支 switch 而做的类型擦除，
而**类型在两个方向上都是已知的**：写时来自 `AssetId::GetAssetType()`，读时来自文件里先解出的
`type` 字段。case 内部类型是编译期确定的，`static_cast` 由分支本身保证正确。

`MakeDefaultDescriptor` 不单独存在：`DescriptorFromJson(type, JsonValue::object())` 即「全默认」，
空对象走一遍缺键回落。

**产出必须是新实例，不能是 `T::DefaultDescriptor()`**——三个 `DefaultDescriptor()` 返回的都是
`static Ptr<...> instance` 共享单例，往里填等于改写所有现有 `AssetId` 共享的那一份。`Ptr<>::get()`
交出的是基类指针，所以 `new` 出来的具体指针要留住喂给 `from_void`。

`MakeAssetIdForType`、`GetSupportAssetType` 都不动——前者要的是单例（身份共享）而非新实例，与这里
需求相反；后者是扩展名嗅探，与 descriptor、与 JSON 无关。

`ImageAsset::DescriptorForUsage` 那 6 个单例不动。

#### 版本化

不写版本号，靠三条规则：

| 规则 | 覆盖的变更 |
|---|---|
| 缺字段 = 默认值 | 加字段 |
| 未知字段 = 忽略 | 删字段 |
| 枚举名冻结，不改名、不复用 | 枚举增值 |

覆盖不到的只有「同名字段语义变了」，到时在 0.d 的复合对象顶层加 `"v"` 键，现在留白。

容错一律不崩：未知枚举名 `LOG_ERROR` 并保留默认；未知 `AssetType` 返回空 `Ptr`，由调用方降级成无效 id。

#### 实施步骤

1. ✅ vendor nlohmann + `Core/Serialization/Json.h`。
2. ✅ `Core/Reflection/EASTLMeta.h`，`ReflectContext.h` include 它。
3. ✅ `Core/Serialization/MetaFieldTraits.h`。
4. ✅ `JsonSerializer.h/.cpp` + `Test/Core/JsonSerializerTest.cpp` 13 例（本地 struct，不依赖
   Resource）：标量精度（`uint64` 最大值 / `int64` 最小值 / `long` / `unsigned char`）、枚举名、
   非连续枚举值、嵌套、容器（含空 vector 与元素全默认）、默认省略、字段顺序断言、缺字段、
   未知字段、未知枚举名、JSON 类别不符、未标 `Serializable` 的字段被跳过、
   `eastl::vector` 被识别为序列容器。
5. ✅ `Resource/Reflect.h` + `Resource/AssetDescriptorJson.h/.cpp` + `Engine.cpp` 注册。
6. ✅ `SparkAssetTest` 加 `DescriptorSerializeTests.cpp` 7 例。**round-trip 判据是 JSON 相等**
   （`原始 → json1 → descriptor → json2`，断言 `json1 == json2`），不是 `Hash()` 相等——
   `ShaderDescriptor::Hash()` 只折 `backend`，`ImageAssetDescriptor::Hash()` 在非 cubemap usage 下
   跳过 `cubemapFaceSize`，用哈希当判据会让丢字段照样通过。另测：默认 descriptor 编码为 `{}`、
   缺键保留默认、产出不是共享单例、未知 `AssetType` 返回空。
   反射注册放 `Test/Resource/main.cpp`（`RegisterAll` 只能调一次，不能放 fixture）。

`SparkAssetTest` 不跑 `Engine.cpp`，测试须自行 `TypeRegistry::Register(Resource::Reflect)` +
`RegisterAll()`；`TypeRegistry` 是全局静态，只能调一次。

### 0.d　`AssetId` 复合形式

```json
"m_modelAssetId": {
    "type": "Image",
    "path": "project://Model/Furniture.glb",
    "sub":  "image/3",
    "desc": { "usage": "NormalMap" }
}
```

`type` 显式落盘：发行包里没有源文件，产物是 `.blob`，扩展名推断必然失效。

外加单向的 `AssetIdToDisplayString`（log、Inspector 只读框）。

**待细化：** 相对路径归一化规则——当前行为是 relative 段保留作者书写的大小写，因此在 Windows 上
`engine://Foo.png` 与 `engine://foo.png` 是两个 id；`sub` / `desc` 为默认时是否省略键。

---

## 阶段 1：磁盘 cook 缓存

> 已定方案，未开工。依赖 0.a、0.b。

### 接入点

`AssetBuildBus` 加 `Serialize` / `Deserialize` 两个事件，缓存流程归 `AssetManager::ProcessAsset`：

```
命中 → Deserialize(ctx) 填 compiledData → Load 与 Compile 全跳过
未命中 → Load → Compile → Serialize(ctx) → 写盘
```

缓存**策略**（键怎么算、什么时候回写）在一处；builder 只负责自己的**格式**。

### 缓存键

64 位，独立于 `AssetHash`——身份比较有实值兜底，缓存键没有。

```
key = H64(path, subLabel, type, descHash, sourceStamp, builderVersion)
```

`sourceStamp` = 源文件 mtime + size。shader 额外折进每个 include 依赖的 mtime
（`ShaderAssetData::m_dependencies` 编译时已在记录）。

`builderVersion` 是每类一个手写常量，改编译器时手动 bump，并折进构建配置——Debug 与 Release
的引擎若产出不同 blob 不会串味。

mtime 与 builderVersion 都是键的输入，所以源文件一改键就变、旧文件查不到：**过期不需要判定**。
代价是孤儿文件累积，靠手动 `PurgeUnreferenced()` 清。

### 落盘

```
Cache/
    3f/  3fa9c2b81d4e6075.ktx2     图片
    a7/  a72b0f4c19e6d385.blob     shader / model
```

仓库根 `Cache/`，进 `.gitignore`，挂成 `cache://`——缓存读写走 `FileSystem`，不另开物理路径通道。
`AssetRegistry` 会遍历到它，`.blob` / `.ktx2` 之外的判定由 `GetSupportAssetType` 兜住。

一级十六进制分片 = 256 个目录；文件名是键的十六进制。不建索引：存在性是一次 `Exists`，
正确性由文件自身证明，要列举时扫目录。

**载荷格式按类型定，不统一套壳：**

| 类型 | 文件 | 说明 |
|---|---|---|
| Image | `.ktx2` | `SerializeToKtx2` / `LoadKtx2` 现成，且能直接用贴图查看器打开 |
| Shader | `.blob` | backend + 每 stage 的 entryPoint/bytecode + `ShaderStageReflection` |
| Model | `.blob` | mesh / node / material / bounds |

文件内只存**身份四项**（path / subLabel / type / descHash）防键碰撞：图片放 libktx 的 KV 段
（`ktxHashList_AddKVPair` / `ktxHashList_FindValue`），blob 放自定义头。一个 `CacheStamp`
结构，两个编码器。

写盘先写 `<key>.tmp` 再 rename，两类文件都要——否则写到一半崩溃会留下能骗过校验的截断文件。

### 子资产

环境立方图的 sky / irradiance / prefiltered 各有自己的 `AssetId`，因而各有自己的键、各是一个
独立缓存项。父资产命中时 `Deserialize` 按子资产各自的键查到并 publish；**缺任何一个就整体当
未命中重新烘**。

### 实现步骤

1. **骨架 + Image 2D。** `FileSystem` 加 `ReadFile` / `WriteFile` / `Exists` /
   `FileStamp`；`cache://` 挂载；`Resource/Cache/AssetCache`（键、读写、原子写、`CacheStamp`）；
   两个 bus 事件与 `ProcessAsset` 接入；`ImageAssetBuilder` 的 2D 路径直接接现成的 ktx2 两头；
   `AssetTest` fixture 挂独立临时目录到 `cache://`。
2. **Shader。** blob 格式 + include 依赖折进键。
3. **Image cubemap。** `m_mips` 改成能描述 face-major 布局（现为 base-mip 占位）；
   `SerializeToKtx2` 支持 `numFaces=6` 并遍历 face/layer，`LoadKtx2` 对应读回。
4. **Model。** blob 格式；图片引用不写 `AssetId`，写重建所需的最小信息（外部 URI 字符串或
   内嵌下标 + `ImageUsage`），加载时用 `MakeSubId` / `Of<ImageAsset>` 重建——因此 Model 缓存
   不依赖 0.c。

### 不做

内存驻留淘汰、自动过期清理、后台异步写盘（worker 线程同步写）。

**测试用独立缓存目录**：共用会让测试之间产生顺序依赖，陈旧缓存能掩盖真实的编译 bug。

---

## 阶段 2：把序列化器铺到组件

> 大致方向。序列化器本体在 0.c 已建好，这里是应用面。

- 给该落盘的组件字段标 `MetaFieldTraits::Serializable`（0.c 已建）。`Mesh/Reflect.h:23-26` 的
  `m_vertexCount` / `m_triangleCount` 是从 model asset 算出的派生值，不标——存了会在磁盘上造出
  第二个真相来源，模型换了而场景文件没跟着变就是错的。
- 补齐 leaf 类型特判：`Math::Vector3/4`、`Matrix4X4`、`Resource::AssetId`（走 0.d）、`Entity`、
  `MaterialHandle`。
- 「这个组件整体是否持久化」是类型级问题，归 `ComponentTraits`，与 `editable` 并列。零字段的 tag
  组件只有类型级能表达。阶段 4 做组件发现时才需要。

---

## 阶段 3：材质资产

> 大致方向。

- 新增 `AssetType::Material`，扩展名 `.smat`。Loader 直接给回 `compiledData`，不设 Compile 阶段。
- `ModelAssetBuilder` 把 glTF 内嵌材质 publish 成子资产（`model.glb:material/0`），与内嵌图片
  子资产同一条路。`Resource::Material` 的中间态与 `MaterialParamsFromModel` 随之拆除。
- glTF 材质只读，在 Inspector 里改它走 **Extract to `.smat`**（Save As）。
- `MaterialSystem` 加一张 `AssetId → MaterialHandle` 表，同一材质资产被多个实体引用时共享一个 handle。
- 材质实体加 `MaterialSourceAsset { AssetId }` 组件标记来源。
- 编辑器：Inspector 的 Save / Save As、资产浏览器新建 `.smat`、`.smat` 拖到 `MaterialComponent`。

**待细化：** authored 材质结构最终落在哪个模块，以及 `MaterialParams` 里那个运行时的
`Ptr<ImageAsset> m_image` 怎么摘干净；`alphaMode` / `alphaCutoff` 这类今天被丢弃的字段要不要收进来。

---

## 阶段 4：场景保存

> 大致方向。

`SceneSerializer::Save/Load(path)`，由 MenuBar 调用，不进 AssetManager。

- **实体 id 存重映射后的下标**（0..N-1），不是 entt 原始值。加载时先建 index → Entity 表，
  再回填 parent 和所有 Entity 类型的字段。
- **不序列化 `Hierarchy` 组件本身**（四个 entity handle，强顺序相关）。只存 `parent`，用数组顺序
  表达兄弟次序，加载时走 `IScene::SetParent` 重建。
- 组件发现：遍历 `ReflectContext::GetAllTypes()`，用反射函数 `IsWorldComponent` 过滤，调
  `HasComponent` / `GetComponent`。
- 顶层放一张 `"materials": [...]` 表，实体按下标引用。阶段 3 做完后表项基本都能退化成纯 asset 引用。

**待细化：** 加载后 `MeshComponent::m_modelAsset` 那个 `Ptr` 谁来填。今天只有 `SpawnModel` 直接
赋值。两条路：场景加载时同步 `RequestAsset`，或复用已有的 `AssetResolveBus::ResolveAssetToComponent`
异步机制（编辑器侧的 `ComponentAssetResolver` 已经在跑）。

---

## 待决

**`DescriptorForUsage` 交出可变的共享单例。** 收紧办法是返回 `ConstPtr<AssetDescriptor>`，
波及 `AssetId::Of` 的签名，单独一步做。

**`ShaderDescriptor::stages` 不在 `Hash()` 里。** 于是两个只有 stages 不同的 id 是同一个资产，
但 0.c 之后它能被写进文件再读回来——同一 id 可以带两种 stages。要么折进 `Hash()`（波及所有已存在的
shader id），要么承认 stages 不是配置而是编译期发现的产物（`[shader()]` 属性 / pragma），从 descriptor
上摘掉。取决于「stage 的 authoring 来源」这个至今未定的问题。

**逐资产导入设置**（给某张贴图单独指定 mip 数 / 压缩格式）未排期。0.c 的字段级序列化能表达它，
但配置存哪、谁编辑、和 usage 什么关系是独立的设计问题。

---

## 依赖关系

```
阶段 0.a（VFS 挂载点 / 虚拟路径）✅
   └──► 阶段 0.b（AssetId 携带 AssetType）✅
           ├──► 阶段 1（磁盘缓存）          ← 键需要类型，已定方案
           └──► 阶段 0.c（反射序列化器 + descriptor 反射）✅
                   └──► 阶段 0.d（AssetId 复合形式）      ← 已解锁
                           ├──► 阶段 2（序列化器铺到组件）
                           └──► 阶段 3（材质资产）
                                   └──► 阶段 4（场景保存）
```

## 下一步

0.c 完成后有两条路，互不依赖：

- **阶段 1 的实现步骤 1**：缓存骨架 + Image 2D（已定方案）。
- **阶段 0.d**：`AssetId` 复合形式。descriptor 两向已经就绪，剩下的是 id 外层那四个键，
  外加两条待细化（相对路径大小写归一化、`sub` / `desc` 默认时是否省略键）。
