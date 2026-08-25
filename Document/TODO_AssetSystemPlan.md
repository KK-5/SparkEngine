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

**阶段 0.d（`AssetId` 复合形式）已完成。** 资产引用现在能写进文件、读回来变成一个可用的 `AssetId`——
背景里那三笔债共享的前置至此全部到位，阶段 0 收尾。

**阶段 1（磁盘缓存）已完成**，范围为顶层 Image 2D。同一张图第二次加载不再解码 PNG/JPEG、
不再跑 mip 与 BC 压缩，直接从 `Cache/` 读回。shader、cubemap、model 仍分别被「通用依赖机制」
与「子资产机制」挡住：前者只有方向（阶段 1 的待办 A），**后者已定方案**（见「子资产机制统一」
一节；它会把缓存条目从「一个文件」扩成「一个构建单元」，cubemap 随之解锁，model 仍欠依赖机制）。

另有两项已随 0.a 落地：

- 三个 descriptor 的 `Hash()` 用 `HashString("XxxDescriptor")` 做种子，避免跨类型撞哈希。
  `DescriptorHashTest` 守住这条。
- `ModelAssetDescriptor::type` 默认值改为 `GLTF`。

---

## 现状盘点

### 已经有的

- **磁盘缓存 ✅**。见阶段 1。
- **子资产机制（两套各写各的）**。`AssetId::OfSub` 是共用的，但发布路径有两条：
  `ImageAssetBuilder::PublishSubAsset`（总是覆盖）与 `ModelAssetBuilder::DispatchImageSubAsset`
  （已存在就跳过），且都不经过 `ProcessAsset`。统一方案见「子资产机制统一」一节。
- **反射系统在用**。组件、枚举（`Light/Reflect.h:15` 的 `LightType`）都已注册；
  `Reflection/Utility.h:86` 的 `ComponentOperation` 把 `HasComponent` / `GetComponent` /
  `AddOrReplaceComponent` / `IsWorldComponent` 注册成了 context-free 的反射函数，现有 7 个世界
  组件无需新增注册。

### 欠账

| 欠账 | 位置 | 说明 |
|---|---|---|
| ~~descriptor 不可序列化~~ ✅ | `Resource/AssetJsonSerializer.h` | 阶段 0.c 已完成 |
| ~~磁盘缓存~~ ✅ | `Resource/Cache/` | 阶段 1 已完成，覆盖顶层 Image 2D |
| 无资产依赖机制 | 无 | `.hlsli` / `.gltf` 的 `.bin` 变了没人知道；`.hlsli` 甚至不是资产。见阶段 1 待办 A |
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
| 0.c descriptor 序列化 ✅ | ✅ 键与戳都用 JSON | ✅ | ✅ |
| 0.d `AssetId` 复合形式 ✅ | ❌ | ✅ | ✅ |

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

阶段 1 已在同一接口上追加 `ReadFile` / `WriteFile` / `Exists` / `GetFileStamp`，并新增
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

`Resource/AssetJsonSerializer.h/.cpp`，`Spark::Resource` 的自由函数，不挂 `AssetManager`——场景加载与
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
5. ✅ `Resource/Reflect.h` + `Resource/AssetJsonSerializer.h/.cpp` + `Engine.cpp` 注册。
6. ✅ `SparkAssetTest` 加 `DescriptorSerializeTests.cpp` 7 例。**round-trip 判据是 JSON 相等**
   （`原始 → json1 → descriptor → json2`，断言 `json1 == json2`），不是 `Hash()` 相等——
   `ShaderDescriptor::Hash()` 只折 `backend`，`ImageAssetDescriptor::Hash()` 在非 cubemap usage 下
   跳过 `cubemapFaceSize`，用哈希当判据会让丢字段照样通过。另测：默认 descriptor 编码为 `{}`、
   缺键保留默认、产出不是共享单例、未知 `AssetType` 返回空。
   反射注册放 `Test/Resource/main.cpp`（`RegisterAll` 只能调一次，不能放 fixture）。

`SparkAssetTest` 不跑 `Engine.cpp`，测试须自行 `TypeRegistry::Register(Resource::Reflect)` +
`RegisterAll()`；`TypeRegistry` 是全局静态，只能调一次。

### 0.d　`AssetId` 复合形式 ✅ 已完成

```json
{"type":"Image","path":"project://Model/Furniture.glb","sub":"image/3",
 "desc":{"colorSpace":"Linear","usage":"NormalMap"}}
```

`type` 显式落盘：发行包里没有源文件，产物是 `.blob`，扩展名推断必然失效。

#### 落地形态

`Resource/AssetJsonSerializer.h/.cpp`（与 `Core/Serialization/JsonSerializer.h` 对称，格式与角色
都在名字里；由 0.c 的 `AssetDescriptorJson` 更名而来，id 层与 descriptor 层
同处一个文件，前者是后者唯一的调用者）：

```cpp
bool          AssetIdToJson(const AssetId& id, JsonValue& out);
AssetId       AssetIdFromJson(const JsonValue& in);
eastl::string AssetIdToDisplayString(const AssetId& id);   // 单向，log 与 Inspector 只读框
```

**写一半通用、一半显式。** `type` / `path` / `sub` 是 `Reflect<AssetId>()` 的反射字段，走通用遍历；
`desc` 由 `AssetIdToJson` 用 `DescriptorToJson(desc, id.GetAssetType(), ...)` 补上——它的具体类型
由 `type` 的**值**决定，字段遍历表达不了这种依赖，而调用方手里恰好有 `AssetType`。

**读整体是显式的。** `AssetId` 不可变、哈希在构造时算，没法逐字段填，只能读出四项后
`AssetId::Of(path, sub, type, desc)` 一次构造。这个不对称由类型本身决定。代价是键名在写侧来自反射、
在读侧是手写的，round-trip 测试守这条。

#### `Reflect<AssetId>()`

三个字段一律 **by-value getter，无 setter**（`.Data<nullptr, &Getter>`）。理由是 `m_hash` 是
`f(path, sub, type, desc)` 的派生值：entt 对任何非 const 成员都会无条件装上 setter
（`factory.hpp:349` 附近），逐字段写入会留下一个哈希过期的 id，而 `operator==` 首先比的就是哈希、
`AssetDataBase` 也按它做键。把成员改成 const 能关掉 setter，但那会删掉 `AssetId` 的拷贝赋值，
`ComponentView.cpp:396` 等处在用。

`m_descriptor` 与 `m_hash` 都不反射。

#### 一并落地的

- `AssetType` 反射（4 个值），于是 `"type":"Image"` 由序列化器的枚举分支产出，不写映射表。
- `sub` / `desc` 的省略**不是新规则**，是 `WriteObject` 已有的「等于默认值就省略」的自然结果：
  默认 `AssetId` 的 `sub` 为空串，顶层资产也为空串；`desc` 全默认时编码为 `{}`。
- `ComponentView.cpp` 的 `AssetElement` 与 `TextureElement` 两处只读框改用 display string，
  子资产贴图现在能看出是子资产。
- `AssetIdSerializeTests` 7 例。其中两条守着最容易错的地方：**`desc` 键缺失必须造默认 descriptor
  而不是留空**（空 descriptor 与默认 descriptor 哈希不同，留空会让 round-trip 后 `==` 失败）；
  **同一文件的 NormalMap 变体与 Texture2D 变体必须编码不同**（descriptor 是身份的一部分，
  丢掉 `desc` 会让两者静默坍缩成一个）。

#### 已定案（原「待细化」）

- **`sub` / `desc` 默认时省略键。** 与 descriptor 层同一条规则。
- **相对路径不做大小写归一化**，移出 0.d。统一小写会在大小写敏感的文件系统上找不到文件；保留原样
  则 Windows 上 `Foo.png` 与 `foo.png` 是两个 id，那属于引用方写错了，是 import / VFS 该管的事。
  0.d 只保证「写进去什么、读出来什么」。

---

## 阶段 1：磁盘 cook 缓存 ✅ 已完成

范围为**顶层 Image 2D**——输入等于自身路径那一个文件、且不产出子资产的资产。其余三类仍被挡住：

| 类型 | 挡在哪 |
|---|---|
| Shader | 通用依赖机制（待办 A，只有方向）。19 个 `.hlsl` 里 16 个有 `#include` |
| Image cubemap | 子资产机制（已定方案，见后文一节）。IBL 的 irradiance / prefiltered 是父的 bake 产物 |
| Model | 子资产机制（内嵌贴图）；`.gltf` 另欠依赖机制（外部 `.bin` 进了编译产物） |

### 落地形态

```
Engine/Code/RunTime/Resource/Cache/
    CacheFormat.h/.cpp   每类型的落盘形态：{version, extension}
    AssetCache.h/.cpp    CacheEntry、键、读写
```

`FileSystem` 追加 `ReadFile` / `WriteFile` / `Exists` / `GetFileStamp` 与 `FileStamp{mtime, size}`。
`cache://` 在 `Engine.cpp` 挂到仓库根 `Cache/`，紧邻 `engine://`——编辑器的 `project` / `editor`
两个挂载发生在 `SetUp()` 返回之后，那时 `AssetCache` 已经判断过挂载是否存在。

`.gitignore` 写 `/Cache/`。**必须锚定**：不带前导 `/` 会连 `Resource/Cache/` 这个源码目录一起吞掉。

### 键与身份

```
canonical = AssetIdToJson(id).dump()
key       = FNV1a64("SparkAssetCache" | $<CONFIG> | canonical | mtime | size | format.version)
```

同一份 `canonical` 兼作两用：哈希成 `CacheEntry::path`，原文存进 `CacheEntry::identity`。

descriptor 以序列化值入键，不用 `AssetDescriptor::Hash()`——后者是有损摘要且不覆盖全部字段，
而缓存没有身份层那样的实值兜底。

`identity` 写进载荷内部（图片是 KTX2 的 KV 段，键 `SparkAssetIdentity`），读回时在拷贝任何像素
之前整串比对。它防的是 64 位键碰撞——存实值不存摘要，因为精确比对的假阳性率是 0。

`$<CONFIG>` 由 CMake 以 `SPARK_CACHE_BUILD_TAG` 传入，Debug 与 Release 的条目互不可见。

mtime 与 version 都在键里，源文件一改键就变、旧条目不可达，**过期不需要判定**。代价是孤儿文件
累积，清理只能整体清空——今天没有调用方，`Clear()` 未实现。

### 落盘

```
Cache/
    3f/  3fa9c2b81d4e6075.ktx2
```

一级十六进制分片 = 256 个目录，文件名是键的十六进制。不建索引：存在性是一次 `Exists`。

`WriteFile` 自身是原子的——写唯一后缀的 tmp 再 rename。tmp 必须与目标同目录（跨卷 rename 会退化
成拷贝），后缀带线程 id + 计数（`ProcessAsset` 同时跑在 worker 与调用方线程）。rename 撞上已存在
目标当作「别人写好了」。

**`cache://` 对 `AssetRegistry` 不可见**：`.ktx2` 在 `GetSupportAssetType` 的图片扩展名表里，
遍历它会把每个缓存条目注册成一个资产。

### 接入点

`AssetBuildEvents` 加两个事件，**都不带 `AssetBuildContext`、也不带 `AssetId`**——格式不该够得着
数据库和文件系统，而类型已经是总线地址：

```cpp
virtual eastl::vector<uint8_t> Serialize(const AssetData& compiled, eastl::string_view identity);
virtual UniquePtr<AssetData>   Deserialize(const uint8_t* bytes, size_t size,
                                           eastl::string_view identity);
```

空 vector / null 即拒绝，也是默认实现——Shader 与 Model 的 builder 一个字未改。
`AssetBuildContext` 一个字段都没加。

`ProcessAsset` 里：

```
entry = m_cache->EntryFor(id)
命中 → Deserialize → SetDataReady，Load 与 Compile 全跳过
未命中 → Load → Compile → Serialize → Write
```

**不可缓存解析成一个空 `CacheEntry`，后面每个调用都自然拒绝**，所以未挂载缓存 / Shader / 子资产
走的路径与接入前逐字节相同，没有第二条分支。三条不写的口子：只有真跑过 Compile 才回写
（`compiled` 标志）、`EntryFor` 拒绝子资产与无戳源、builder 自己拒写。

`Deserialize` 拒收后不删条目，只 `LOG_WARN` 并重建——键是纯函数，重建写回同一个 path，覆盖即修复。

命中不新增 `AssetStatus`，走 `Loading → Ready`。

### Image 侧

- `SerializeToKtx2` 转公开并收 `identity`，在 `ktxTexture_WriteToMemory` 之前写 KV 段。
  `identity` 是 view，须先拷进 `eastl::string` 再取 `c_str()`——按 `size() + 1` 直接从 view 读会越界。
- `LoadKtx2` 改吃内存（`ktxTexture2_CreateFromMemory`），`expectedIdentity` 非空则校验。
  授权 `.ktx2`、缓存条目、内嵌图片因此各自只打开一次文件。
- **`ImageAssetLoader::Load` 的 `outIsCompiled` 出参删除**，拆成 `LoadSource` / `LoadCompiled`。
  那个 bool 只是把「结果放哪个槽」运回给调用方，而 `IsCompiledImagePath` 是路径的纯函数，
  builder 自己就能判。改后「调哪个函数」本身就是信号。
- `Compile` 里那次只为打日志而整算一遍 ktx2 blob 的白算删掉。
- cube 拒写：`GetArrayLayers() != 1`。`SerializeToKtx2` 写死 `numFaces = 1`，且烘焙产物的 `m_mips`
  只描述 base mip。**此条无测试覆盖**——构造 cube payload 需要 GPU baker，`SparkAssetTest` 里没有
  RHI device。

### 测试

每个 `ImageAssetTestFixture` 用例一个独立临时缓存目录，TearDown 删干净；其余三个 Resource fixture
不挂 `cache://`，顺带守住「未挂载时行为不变」。

重建走 `Restart()`（销毁 manager 再建新的），**不能同时存在两个 `SparkAssetManager`**：
`Service<AssetManager>::Register` 是先到先得，第二个静默不注册，于是它的资产析构时会经由
`Asset::Shutdown` 打到第一个的数据库上，`AssetDataBase::Remove` 持锁 erase 触发同锁重入而死锁。

用例：写出一个条目、第二次运行逐字节还原且不重写、条目截断后拒收并重建、KV 段身份被改一个字节后
拒收、授权 `.ktx2` 不回写。另有 `CacheTests.cpp` 11 例覆盖 `EntryFor` 的四条不可缓存路径、
usage 变体分属不同条目、改源文件后 `path` 变而 `identity` 不变。

### 不做

内存驻留淘汰、自动过期清理、后台异步写盘、`Clear()`。

### 待办：解锁其余三类

三条，都是独立机制。**B 已定方案，见下一节**；A、C 只有方向。

**A. 通用资产依赖机制（反向图）。** 一个文件变了，依赖它的所有资产失效重编。要点：

- 依赖边**必须持久化**——缓存命中的资产不会被编译，边不会在本次会话里被重新发现，
  「编译完顺手建图」在冷启动全命中时是空的。
- **不能挂在缓存条目里。** 依赖数据的消费者不止缓存（热重载、编辑器的「这张贴图被谁用了」、
  打包时的可达性分析），要做就做成通用的。
- **`.hlsli` 要列为资产**（必做项）。它今天不是——`GetSupportAssetType` 只认 `.hlsl`，
  「它变了」这件事没有主体。
- 配套的是**资产预加载**：启动时全量遍历，首次加载的编译并建图，已缓存的校验有效性。

**B. 子资产机制统一。** 已定方案，见后面两节（前置是「Image 处理流程规整」）。**在它落地之前
不允许再加第三个手写 publisher**——阶段 3 的 glTF 材质子资产就会是那第三个。

**C. Shader 缓存。** 挂在 A 之后。收益是三类里最小的一档：实测全部重编也远不及一张图片的加载
时间。A 做完之后基本是把 blob 格式写出来的事。两个已知坑：

- `ShaderAssetData::m_resolvedPath` 与依赖列表存的都是**物理**路径，落盘换目录就错。前者今天没有
  任何消费者，不写；后者转虚拟路径。
- `m_stages` / `m_reflections` 是 `unordered_map`，按迭代顺序写盘则同一输入产出的字节不稳定。
  写前按 stage 枚举值排序。

---

## 前置：Image 处理流程规整

> 已定方案，未开工。「子资产机制统一」的前置——不先做，后者要为 Image 的几处特例一直开口子。

Image 是两条产出子资产的路径的共同类型（IBL 的 bake 产物、glTF 的内嵌图）。它今天有两处「凭空产出
成品」和一处「数据结构描述不了自己」，子资产机制若先落地，就得逐条为它们开特例。

### 现状的乱

| # | 乱在哪 |
|---|---|
| 1 | `Load` 能写 `compiledData`（`.ktx2` 路径），于是 `ProcessAsset` 需要 `compiled` 标志 |
| 2 | `AssembleCubemapData` 在 builder 里被调三次，装配逻辑不在 compiler |
| 3 | `ImageAssetData::m_mips` 只按 mip 索引、无 layer 维度，描述不了 face-major 的 cube buffer |
| 4 | `SerializeToKtx2` / `LoadKtx2` 都不支持 cube |
| 5 | `MapVkFormatToRHI` 与 `MapToVkFormat` 是两份手写镜像（loader 的注释已注明该合并） |
| 6 | `LoadSource` 走物理路径 + stbi 直读，`LoadCompiled` 走 `fileSystem.ReadFile` |

**第 3 条是 cubemap 不能缓存的根因**——`Serialize` 里 `arrayLayers != 1` 的早退与 `LoadKtx2` 的
cube 拒绝都是它的下游，`AssembleCubemapData` 今天只能塞一条 base-mip 占位。

### 总规则

**`Load` 只产 raw，`Compile` 只产成品，装配逻辑全在 compiler。**

`AssetBuildContext` 的槽从此无例外：

```
rawData       ← Load 的唯一输出
compiledData  ← Compile 的唯一输出
subAssets     ← Compile 的第二个输出
```

`ProcessAsset` 里那个 `compiled` 标志随之删除。

### Image 的三种 raw

| raw 类型 | 谁产 | Compile 对它做什么 |
|---|---|---|
| `ImageAssetRawData`（已有：解码后的像素） | `LoadSource` / `DecodeFromMemory` | mip 链 + BCn |
| `ImageBakedRawData`（包一个 `BakedCubemap`） | 父的 bake | 装配 cube |
| `ImageEncodedRawData`（包 ktx2 字节） | `.ktx2` 的 Load | `LoadKtx2` 解析 |

`BakedCubemap` 不直接继承 `AssetData`——后者删了拷贝且没声明移动，`BakedEnvironment` 按值持有三个
就传不出来。用薄包装。

### `.ktx2` 是一种源格式，不是成品

`.png` 的 Compile 是「解码 + mip + BC」，`.ktx2` 的 Compile 是「解析」，两者齐平。这是删掉
`Load → compiledData` 那条路的理由。

它现在会走到写缓存那一步，用 **`EntryFor` 的第五条拒绝**挡掉：源文件本身就是本类型的缓存格式时
不缓存。纯策略，放 `AssetCache` 里，不放 `ProcessAsset` 的控制流里。

### `Compile` 按 usage 分派

| usage | 收什么 raw | 做什么 |
|---|---|---|
| `EnvironmentCubemap` | equirect 像素 | bake → 装配 sky → 声明两个子（带 baked raw） |
| `Irradiance` / `Prefiltered` | baked faces | 装配 |
| 其余 | 像素或 ktx2 字节 | compiler 的常规路径 |

**usage 决定 raw 的类型**，所以不需要 RTTI 或类型 tag。守卫两条：`Load` 里的 `IsDerivedUsage`
保留（派生子永不从磁盘读）；`Compile` 里那条改成「rawData 不是 baked 才报错」。

### 一个改不掉的约束

**bake 挪不到 `Compile` 之后。** 三张图是同一个 GPU job 的产物，`BakeSky` 产出的活 GPU cube 直接
喂给两个卷积当 SRV 采样，不走 CPU 往返。能挪的只有**装配**——而那正好就是脏的那部分。

### 步骤

1. `ImageAssetData::m_mips` 改成按 subresource 索引（face-major / mip-inner，与 `m_textureBytes`
   的实际布局一致）。`AsyncUploadSystem` 不受影响——它自己重算每个 subresource 的紧凑 extent，
   从不读 offset 表。
2. KTX2 读写支持 cube；两份 format 映射表合并。可独立验证：写进去、读回来。
   KTX2 与 libktx 本来就支持（`numFaces`、`SetImageFromMemory` 的 `face` 参数），我们两侧都是主动
   拒绝。但真正的缺口是 **`ImageAssetData` 说不出「我是 cube」**：它只有 `m_arrayLayers`，全引擎靠
   「层数 == 6」推断（`SkyboxSystem.cpp:39`），而写侧把它塞进了 `numLayers`——真写一个 cube 出来的
   会是 6 层 2D 数组。`Serialize` 拿不到 `AssetId`，问不到 descriptor，所以这个信息必须长在数据上：
   **加 `m_faceCount`（1 或 6）与 `m_arrayLayers` 并列**（对上 KTX2 / Vulkan / DX12 的模型，`bool`
   表达不了 cube array），随后那条「层数 == 6」的约定与它的歧义一起消失。
3. 槽规则收紧：加两种 raw、`.ktx2` 走 Compile、`EntryFor` 第五条拒绝、删 `compiled` 标志、
   `AssembleCubemapData` 移进 compiler 的 usage 分派。行为不变。

现状表里的第 6 条随第 3 步顺手统一，不单列。

---

## 子资产机制统一

> 已定方案，未开工。**前置是「Image 处理流程规整」**。解锁：cubemap 缓存、阶段 3 材质子资产。

### 现状

两条产出子资产的路径各写各的，且**都不经过 `ProcessAsset`**：

| 来源 | 谁 | 父交出什么 | 重复时 | 失败时 |
|---|---|---|---|---|
| IBL | `ImageAssetBuilder::PublishSubAsset` | 成品（GPU bake 的结果） | 总是覆盖 | 全有或全无 |
| glTF 内嵌图 | `ModelAssetBuilder::DispatchImageSubAsset` | 源字节（在 glb 里） | 已存在就跳过 | `LOG_WARN` 继续 |

### 核心：一次构建的全部产物是一个「构建单元」

> **子资产保留独立可寻址的 `AssetId`，但没有独立的存储生命。**

一次 `ProcessAsset` 的产出——父 + 它声明的全部子——是缓存、失效、加载的原子。不存在「父命中了、
子不见了」这个中间态。

定这条的理由：**子资产的源字节在父文件里**。它没有自己的时间戳、没有自己的输入、不可能在父之外
被生产出来，缓存键只能是父文件的戳。于是父一变、所有子的键全变、全部重建——**拆开存储本该买到的
「细粒度失效」在这里恒等于零**，只剩原子性、清单、父子状态一致性这些管理成本。

身份与存储是两件事：子必须能被单独指名（材质、场景文件都要引用它），但「能被单独指名」不蕴含
「能被单独存放」。这是 UE 的分法——子对象有自己的路径，但住在同一个包里。O3DE 那条（每个产物独立
成文件 + sqlite 资产库 + 三类依赖）买到的是加载粒度与发行体积，代价是一整套常驻管线，不适用。

### 四条语义

**1. 声明归 builder，发布归 `ProcessAsset`。** builder 知道自己产出了什么，但不决定什么时候让它
对外可见——「一批子加一个父要么全成、要么全不成」这个事务边界，只有持有父的状态与缓存的那一层能划。
**builder 不碰 `db`。**

**2. 数据从哪来，由它落进哪个槽表达，不由类型表达。** 「父烘出来的成品」与「父文件里的一段源字节」
不是两种机制，是同一个机制的两种输入。

**3. 加载一个子，就是加载它所属的整个单元。** 请求 `model.glb:image/0` 即请求把这个模型建出来，
再从中取走那张图。

**4. 任何一个子失败，父就失败。** 没有可选的子，也就没有 `required` 这类开关。单元是原子，
「一半成功」不是一个可表达的状态——缓存本来就必须整体不写（少一个 payload 下次命中就会给出残缺
单元），运行期再放行「父 Ready 但少一张贴图」只会让同一份资产在冷热两次启动里长得不一样。这一条
收紧了 `DispatchImageSubAsset` 今天「`LOG_WARN` 继续」的行为。

### `AssetBuildContext` 的改动

Compile 除自己的 `compiledData` 外，再交一份子资产声明：

```cpp
//! Compile 声明的子资产。数据来源由填了哪个槽表达：
//!   rawData    → 已在手的 raw（父的 bake 产物）：跳过 Load，仍走 Compile
//!   sourceData → 源字节在父文件里：先 Load 再 Compile
struct SubAssetEntry
{
    AssetId              id;
    UniquePtr<AssetData> rawData;
    const uint8_t*       sourceData = nullptr;
    size_t               sourceSize = 0;
};

eastl::vector<SubAssetEntry> subAssets;
```

两个槽都是 **raw**，没有成品槽——「每个资产的最终形态只有 `Compile` 产出」这条规则由前置一节定下，
子资产不开例外。于是发布段没有特例分支：`rawData` 空就 Load，然后一律 Compile。

声明是只装数据的结构，不是 `AssetBuildContext`——后者九个字段里声明只用得上四个，且多层嵌套要靠
断言去挡。`SubAssetEntry` 里没有 `subAssets` 字段，**一层到底是类型保证**，与 `MakeSubId`
里已有的「父不能是子资产」断言对齐。

同一次改动里 `AssetBuildContext` 减两个字段：`db`（两个 publisher 一走就没有使用者）与 `parentId`
（全仓只有 `MakeChild` 写，没有任何地方读）。净减一个。

### 缓存条目从「一个文件」变成「一个单元」

同一个键、不同后缀：

```
cache://3f/3fa9c2b8.unit      目录：父 identity + 子 id 列表（复用 AssetId 的 JSON）
cache://3f/3fa9c2b8.ktx2      父的 payload
cache://3f/3fa9c2b8.0.ktx2    子 0 的 payload
cache://3f/3fa9c2b8.1.ktx2    子 1 的 payload
```

- **子不参与 `EntryFor`。** 它由「父的键 + 序号」定位，`AssetCache` 里那条拒绝子资产的判断原样保留。
- **原子性靠写入顺序。** payload 全部写完才写 `.unit`；读时 `.unit` 在即完整，不在即整体 miss。
  不需要逐个 `Exists`。
- **`Serialize` / `Deserialize` 签名不变。** 它们本来就是「一段字节」的接口，只是现在被调用多次。
  builder 依然只管格式，看不见路径与键。
- 容器是**逻辑单元**：一份目录 + 整体有效性。物理上仍是一个子一个文件，粒度没有被焊死。

### 两个产出者，一个发布段

数据从哪来有两条路，**变成 db 里一个 Ready 的资产只有一条**：

```
产出（两条路，都归 AssetBuildBus）
    命中 → Deserialize(payload, identity)   → AssetData
    构建 → [Load] → Compile                 → AssetData

发布（一处，归 ProcessAsset，两阶段）
    1. 把全部子的 AssetData 收进临时列表
    2. 有一个失败 → 全部丢弃，父 Error，缓存不写
    3. 全成功 → 逐个 Publish，最后父 SetDataReady

    Publish(id, data):
        asset = db->InsertOrGet(id, CreateAsset(id))
        asset->SetDataReady(move(data))
        AssetBus::Event(type, OnAssetReady, *asset)
```

**产出与发布必须分开**，否则「任何一个子失败父就失败」做不到——发布一旦调用就收不回来。今天
`CompileEnvironmentCubemap` 正是「产出即发布」：先 publish 两个子、再 `AssembleCubemapData(sky)`，
sky 那步失败时两个子已经 Ready 躺在 db 里而父是 Error，注释写的「全有或全无」并未做到。两阶段提交
把它变成结构性保证，顺带保住「子全部 Ready 之后父才 Ready」这条今天靠 publish 位置隐式维持的不变量。

今天的 `PublishSubAsset` 做的事是对的，位置错了——它在 builder 里，只有构建路径够得着。挪进
`ProcessAsset` 之后两条产出路径共用。

**子不递归进 `ProcessAsset`。** 父子真正共享的是 `AssetBuildBus`（按类型分发的 Load / Compile）；
`ProcessAsset` 在总线之上加的三样里，缓存对子是另一套，状态机无人观察，只剩事件。

`.unit` 里存的子 `AssetId` 顺带解决了命中路径的 identity：每个子 payload 的 `Deserialize` 要的
identity 就是它自己 AssetId 的 JSON，算得出来，不用额外存。

### 子被直接请求：反向推出父

`LoadAsset(model.glb:image/0)` 而 db 里没有时：

```
parentId = MakeAssetIdForType(id.GetPath(), GetSupportAssetType(path))
LoadAsset(parentId)        整个单元被建或被命中
return FindAsset(id)       子已在 db 中
```

`MakeAssetIdForType` 是路径的纯函数，`.glb → Model`、`.hdr → EnvironmentCubemap` 两条都给出父的
精确 id。**前提是「一个文件对应一个规范顶层 id」**——今天成立；待决里的「逐资产导入设置」会打破它，
届时需要别的答案，现在不为它留口子。

这条让阶段 3/4「场景文件直接引用 `model.glb:image/0`」从特殊情况变成普通情况。

### 父怎么引用子：统一成 `AssetId`

`ImageAssetData` 那两个 `Ptr<ImageAsset>` 改成 `AssetId`，访问器查 DB，与 Model 的
`m_imageAssetIds` 一致。构建单元保证「父在子必在」，`Ptr` 那点强引用价值随之消失。

`ImageAsset::GetIrradianceAsset()` / `GetPrefilteredAsset()` 仍返回 `Ptr<ImageAsset>`，DB 查询放在
访问器内部，**消费者零改动**（`SkyboxSystem.cpp:190-201`、`BakeCubemap.cpp:210-211`）。

### 外部 URI 贴图不属于这套

`.gltf` 的外部贴图 id 是顶层 `AssetId::Of`，有自己的文件、自己的戳、自己的缓存键。它是**依赖**，
不是子资产（对应 O3DE 的 job dependency）。`DispatchImageSubAsset` 今天把两者混在一条路上处理，
拆开之后「重复时怎么办」这个问题对它自然消失：走普通资产语义。本次保持行为不变，归属留给待办 A。

### 统一的步骤

0. 前置一节的三步先做完。
1. `AssetCache` 会读写构建单元。可独立验证：写进去、读回来、缺一个 payload 要整体 miss。
2. `AssetBuildContext::subAssets` + 发布段（含两阶段提交）；删 `PublishSubAsset` 与
   `DispatchImageSubAsset`；删 `ctx.db` / `ctx.parentId`。此时行为与今天等价，发布集中到一处。
3. 命中路径接上同一个发布段。**cubemap 缓存到这一步才真正生效**——读写两侧由前置备好。
4. `ImageAssetData` 的两个 `Ptr` 改 `AssetId`。
5. 反向推出父的路由。

`IsDerivedUsage` 那两个拒绝守卫保留——它们防的是「有人直接 `RequestAsset` 一个派生子资产」，
仍然有效。

**本次不做：** Model 缓存（`CacheFormat` 里 Model 的 version 是 0，它还卡在 `.gltf` 的外部 `.bin`
上，属于待办 A）、运行期父子关系表（构建单元下淘汰是整单元的）。

一个实现注意点：**`ModelAssetBuilder` 末尾的 `raw.m_rawImages.clear()` 要删掉。** `sourceData` 是
非拥有指针，今天有效是因为 publish 发生在 Compile 内部、raw 还活着。发布推迟到 Compile 返回之后，
`ctx.rawData` 仍活到 `ProcessAsset` 结束（没被 move 走），指针照样有效——但那句提前 clear 会让它
悬空。删掉即可，内存随 ctx 一起释放。

---

## 阶段 2：把序列化器铺到组件

> 大致方向。序列化器本体在 0.c 已建好，这里是应用面。

- 给该落盘的组件字段标 `MetaFieldTraits::Serializable`（0.c 已建）。`Mesh/Reflect.h:23-26` 的
  `m_vertexCount` / `m_triangleCount` 是从 model asset 算出的派生值，不标——存了会在磁盘上造出
  第二个真相来源，模型换了而场景文件没跟着变就是错的。
- **先补「类型自带编解码」的钩子**：序列化器在复合分支之前，先看这个 `MetaType` 上有没有注册一对
  编解码函数，有就调。`Resource/Reflect.h` 为 `AssetId` 注册 `AssetIdToJson` / `AssetIdFromJson`，
  `SparkCore` 里不出现 `Resource`。没有这个钩子，标了 `Serializable` 的 `AssetId` 字段会被通用遍历
  走进去，产出缺 `desc` 的三项对象——语法完好、字段齐全，但法线贴图会静默变成 sRGB 颜色贴图。
- 补齐 leaf 类型特判：`Math::Vector3/4`、`Matrix4X4`、`Entity`、
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

缓存不受这条影响：键走 descriptor 的 JSON，`stages` 天然进键。这条仍是身份层的债。

**逐资产导入设置**（给某张贴图单独指定 mip 数 / 压缩格式）未排期。0.c 的字段级序列化能表达它，
但配置存哪、谁编辑、和 usage 什么关系是独立的设计问题。

---

## 依赖关系

```
阶段 0.a（VFS 挂载点 / 虚拟路径）✅
   └──► 阶段 0.b（AssetId 携带 AssetType）✅
           └──► 阶段 0.c（反射序列化器 + descriptor 反射）✅
                   ├──► 阶段 1（磁盘缓存 / 顶层 Image 2D）✅
                   └──► 阶段 0.d（AssetId 复合形式）✅
                           ├──► 阶段 2（序列化器铺到组件）
                           └──► 阶段 3（材质资产）
                                   └──► 阶段 4（场景保存）

待办 A（通用依赖机制 + 预加载）── 只有方向 ──► shader 缓存

Image 处理流程规整 ── 已定方案 ──► 子资产机制统一 ──┬──► cubemap 缓存（model 还欠待办 A）
                                                     └──► 阶段 3（材质子资产）
                                                          ↑ 不统一就会多出第三个手写 publisher
```

## 下一步

阶段 0 与阶段 1 已收尾。剩下的三条互不依赖：

- **Image 处理流程规整 → 子资产机制统一**（都已定方案）：合起来是 cubemap 缓存与阶段 3 的前置；
  model 缓存还需要待办 A。前置那三步不碰构建流程，可以先单独做掉。
- **待办 A：通用依赖机制 + 预加载**（只有方向）：shader 缓存的前置。
- **阶段 2**：第一件事是「类型自带编解码」的钩子——组件里的 `AssetId` 字段若被标上
  `Serializable`，通用遍历会走进去、产出一个缺 `desc` 的三项对象。今天没有任何组件字段标了
  `Serializable`，这条路还够不着。
