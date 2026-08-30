# 资产系统补齐计划（路径身份 / 磁盘缓存 / 材质资产 / 场景保存）

> 标了「待细化」的地方还没定，不要当成已决方案实现。

## 背景

资产系统的骨架是齐的——`AssetId` → `AssetDataBase` → worker 线程 → 按 `AssetType` 走
`AssetBuildBus` 的 Load/Compile。欠的是三笔债：

1. **材质不是资产**——`StandardPBR` 只活在运行时的 `MaterialContext` 里，没名字、没文件、
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
不再跑 mip 与 BC 压缩，直接从 `Cache/` 读回。

**「Image 处理流程规整」已完成。** `Load` 只产 raw、`Compile` 只产成品，`.ktx2` 降为一种源格式，
KTX2 读写支持 cube，`ImageAssetData` 用 `m_isCubemap` 说明自己而不再靠「层数 == 6」推断。

**「子资产机制统一」已完成，cubemap 缓存随之生效。** 缓存条目从「一个文件」变成「一个构建单元」
（payload 若干 + `.unit` 清单），两个手写 publisher 删除，发布收敛成 `ProcessAsset` 里的两阶段提交。
shader 与 `.gltf` model 仍被「通用依赖机制」挡住（待办 A），`.glb` model 只差一个二进制格式（待办 D）。

**阶段 3 的资产层已落地**：`AssetType::Material` 与 `.smat` 扩展名、空的 `MaterialAssetDescriptor`、
`Resource/Material/` 下的 `StandardPBR` / `MaterialState` / 读侧（`MaterialAssetLoader` 产
`MaterialEncodedRawData`、compiler 解三个顶层键、builder 把贴图交给 `ctx.dependencies`）。
`shadingModel` 的校验就是拿它去 `Resolve` 参数组件的类型，未知名字判 Error 而不回落。

**阶段 3 的运行期已落地**：glTF 材质变子资产、`MaterialAssetRef` + 一个资产一个实例、材质 GC 删除、
覆盖、以及覆盖的编辑器入口。剩下的是编辑器的另外那半（材质窗口、Browser 刷新与 `.smat` 注册、
右键新建）。三处实现与原方案不同，已就地改写本文：组件名是 `MaterialAssetRef` 不是 `MaterialSource`；
覆盖用继承而非薄包装（约束不是 `ComponentOperation`，是 entt 的字段枚举不穿基类）；**材质缓冲没有
第二个 population**——覆盖在绑定时被合成成材质实体，`GlobalBuffer` 一行未改。

**阶段 3 定下了使用流程、数据模型与 `.smat` 的形态 / 产生方式**：八条编辑器用户流程与两个互不重叠的
编辑表面（材质窗口改材质、Component View 的材质槽改覆盖）；材质数据跨 World 与 MaterialContext 的
组件布局——覆盖挂世界实体、MaterialContext 只剩有主的材质、材质 GC 随之删除；`shadingModel` /
`state` / `properties` 三个顶层键；手写优先、导出即原地转换。剩下的是属性名定稿与实现细节。

**阶段 2 与阶段 4 的设计已定，尚未动工。** 阶段 2 的机制（`JsonOperation`、分派链位置、`null` 语义、
失败语义、数学类型走标记而非 `JsonOperation`）与阶段 4 的形态（storage-major、多上下文、entity 原值
当键、读写同构）都已落到文档里。落盘 key 已定为反射注册名（一名两用，见阶段 2）。

**阶段 2 的四件前置全部完成**：拼写校正、`JsonOperation`、默认值一律写出、名字校对（7 个字段改名，
组件 key 规则冻结为「类名去掉 `Component` 后缀」）。剩下的就是给 37 个字段打 `Serializable`，以及
给那六个还没有测试目标的组件找个地方做 round-trip。

另有两项已随 0.a 落地：

- 三个 descriptor 的 `Hash()` 用 `HashString("XxxDescriptor")` 做种子，避免跨类型撞哈希。
  `DescriptorHashTest` 守住这条。
- `ModelAssetDescriptor::type` 默认值改为 `GLTF`。

---

## 现状盘点

### 已经有的

- **磁盘缓存 ✅**。见阶段 1。条目是一个构建单元，Image（含 cubemap）已覆盖。
- **子资产机制 ✅**。声明归 `Compile`（`ctx.subAssets`），发布归 `ProcessAsset` 的两阶段提交，
  构建与命中两条路共用。见「子资产机制统一」一节。
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
| 内存驻留无淘汰 | `AssetDataBase.h` | map 持强 `Ptr<Asset>`，refcount 永不归零，`Asset::Shutdown`→`ReleaseAsset` 够不着 |
| 子资产提取未做 | 无 | 单独使用模型的一张贴图，正确做法是提取成独立资产。见「子被直接请求」一节 |
| ~~材质无运行期资产形态~~ ✅ | `Feature/Material/` | `MaterialAssetRef` + `Material::Resolve` 已落地，一个资产一个实例 |
| ~~两个平行的 CPU 材质结构~~ ✅ | `ModelAsset.h` | `Resource::Material` 降为 `ModelAssetBuilder` 的 TU 内中间物（`ResolvedMaterial`），`StandardPBRFromModel` 删除 |
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
10. **类型自带编解码 = 类型上一个反射 `Func`（`"JsonOperation"`），返回一张装两个裸函数指针的表；
    查表放分派链最前面，命中即终局。** 判据是「不能由字段遍历重建的类型」，不是「leaf 类型特判」。
    见阶段 2。
11. **场景文件是 storage-major、多上下文；键直接用 entt 的 entity 原值**，不做重映射下标、不引入额外
    id。落盘布局与内存布局同构，遍历 `storage()` 即写出。见阶段 4。
12. **落盘的 key 就是反射注册名，一名两用。** 组件 key 取 `.Type(...)`、字段 key 取 `.Data(...)`，
    也就是今天 Inspector 上显示的那个标签。不另设序列化专用名，也不由代码名推导——**名字必须显式
    指定**。规则与枚举名一致：**一经落盘即冻结**。见阶段 2。

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
> **「省略默认值」整条已在阶段 2 删除**，见「默认值一律写出」。0.c 当时的实现（`type.construct()`
> 造默认实例、序列化一遍、逐字段比 `JsonValue`，相等则 erase）连同它的那条坑（不能用
> `meta_any::operator==`，entt 只对 equality-comparable 的类型生成 compare）一起作废。

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

**两个方向都是显式的。** 四个键由 `AssetIdToJson` / `AssetIdFromJson` 自己写，不走字段遍历。
`AssetId` 不可变、哈希在构造时算，读侧没有逐字段写入的落点，只能读出四项后
`AssetId::Of(path, sub, type, desc)` 一次构造；写侧原本是走反射字段遍历的，阶段 2 给 `AssetId` 加了
`JsonOperation` 之后改成对称的显式写法，理由见下。

**只有 `AssetType` 的枚举名仍来自反射** —— `AssetIdToJson` 对它单独调一次 `SerializeToJson`，
所以 `"type":"Image"` 由序列化器的枚举分支产出，不写映射表。这是「把子部分交回分派器」的合法用法。

#### 写侧为什么不再走字段遍历（阶段 2 的修正）

原方案里 `type` / `path` / `sub` 是 `Reflect<AssetId>()` 的三个 by-value getter 字段，`AssetIdToJson`
把整个 id 交给 `SerializeToJson` 走通用遍历。阶段 2 给 `AssetId` 注册 `JsonOperation` 之后这条路
**成环**：分派器查到 operation → 调 `AssetIdToJsonField` → `AssetIdToJson` → 又交回分派器 → 栈溢出。

错的一方是 `AssetIdToJson`，不是分派器。分派器查 operation 就是重载决议，而一个 operation 声明了
「我完全负责这个类型的编码」之后又把自己的值交回去，等同于在 `operator<<(Foo)` 里写 `os << foo`。
**规则一条**：operation 可以把**子部分**交回分派器，不能把自己的值交回去。

于是三个 `.Data<nullptr, &Getter>` 与 `AssetIdField` 那三个 getter 一并删除——写侧不再需要它们，
读侧从来没用过，编辑器只用 display string、从不展开 `AssetId` 的字段。`Reflect<AssetId>()` 只剩
`.Type("AssetId")` 与 operation 注册。

曾经为这个环开过一个 `SerializeFieldsToJson`（只做字段遍历、跳过 operation 的公开入口），**已删除**：
它对任何带 operation 的类型误用都会静默产出错编码，正是 `desc` 丢失那个失败模式，而它偏偏是 public 的。

顺带一个好处：`sub` 的省略从「蹭 `WriteObject` 的默认省略」变成 `if (id.IsSubAsset())` 这个明确判断，
于是 `AssetId` 的编码**不再受任何全局策略影响**。它是身份，缓存 identity 与场景文件必须是同一串字节。

#### 一并落地的

- `AssetType` 反射（4 个值）。
- `desc` 全默认时 `DescriptorToJson` 编码为 `{}`，`AssetIdToJson` 判空后不写这个键。
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

**B 已完成，cubemap 缓存随之生效**；A、C、D 只有方向。

**A. 通用资产依赖机制（反向图）。** 一个文件变了，依赖它的所有资产失效重编。要点：

- 依赖边**必须持久化**——缓存命中的资产不会被编译，边不会在本次会话里被重新发现，
  「编译完顺手建图」在冷启动全命中时是空的。
- **不能挂在缓存条目里。** 依赖数据的消费者不止缓存（热重载、编辑器的「这张贴图被谁用了」、
  打包时的可达性分析），要做就做成通用的。
- **`.hlsli` 要列为资产**（必做项）。它今天不是——`GetSupportAssetType` 只认 `.hlsl`，
  「它变了」这件事没有主体。
- 配套的是**资产预加载**：启动时全量遍历，首次加载的编译并建图，已缓存的校验有效性。

**B. 子资产机制统一。** ✅ 已完成，见后面两节。cubemap 缓存已解锁；Model 缓存还欠 A。

**C. Shader 缓存。** 挂在 A 之后。收益是三类里最小的一档：实测全部重编也远不及一张图片的加载
时间。A 做完之后基本是把 blob 格式写出来的事。两个已知坑：

- `ShaderAssetData::m_resolvedPath` 与依赖列表存的都是**物理**路径，落盘换目录就错。前者今天没有
  任何消费者，不写；后者转虚拟路径。
- `m_stages` / `m_reflections` 是 `unordered_map`，按迭代顺序写盘则同一输入产出的字节不稳定。
  写前按 stage 枚举值排序。

**D. Model 缓存。** B 已经把「子资产存不下」这条最结构性的障碍解掉。剩两件：

- **`.gltf` 的外部 `.bin` 欠 A**：`EntryFor` 的键只戳一个文件，`.bin` 改了而 `.gltf` 没改就会命中
  过期条目。**但 `.glb` 不受影响**，可以先只对它开——附带条件是 loader 要记下「这个 glb 有没有外部
  buffer URI」（规范允许，少见），有就 `Serialize` 返回空拒写。
- **`ModelAssetData` 没有序列化格式**，纯工作量。**不要存 glb**：glb 是源格式不是产物格式，
  `Compile` 做的切线生成 + meshopt 三轮优化 + 交错打包在它里面表达不出来，读回来还得再烘一遍——
  等于把刚删掉的 `compiled` 标志请回来，还多背一个 glTF 规范。自定扁平容器（count 前缀 + memcpy），
  顶点/索引缓冲用 `meshopt_encodeVertexBuffer` / `encodeIndexBuffer` 压一遍（仓库里已经有），其余
  字段裸字节。编码格式跨 meshoptimizer 版本可能变，把它折进 `CacheFormat::version`，升级时 bump
  一次即可，不在容器里再管一层兼容。第一版可以先不上编码，跑通后再加，容器结构不变。
- 外部 URI 贴图**不是**阻碍：模型 payload 里只有它们的 `AssetId`，图片自己有键有戳、独立失效。
- 一个坑：`ModelAssetData::m_resolvedPath` 是**物理**路径（loader 要喂给 fastgltf），不能写进条目，
  与 C 里那条同样处理。

---

## 前置：Image 处理流程规整 ✅ 已完成

> 三步全部落地。「子资产机制统一」的前置——不先做，后者要为 Image 的几处特例一直开口子。

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

守卫两条：`Load` 里的 `IsDerivedUsage` 保留（派生子永不从磁盘读）；`Compile` 里那条改成
「rawData 不是 baked 才报错」。

**落地时的修正**：原本写的是「usage 决定 raw 的类型，所以不需要 RTTI 或类型 tag」，这条不成立——
表里第三行「像素或 ktx2 字节」同属 `Texture2D` usage，usage 分不开它们；而那条 baked 守卫本身
就是一次类型判断。实现改为给 image 自己的 raw 加一个共同基类 `ImageRawData` 与 `Kind`
（`Pixels` / `Encoded` / `Baked`）：**不动共享的 `AssetData`、不引入 RTTI**，分派由 raw 自述而不是
由调用方从路径二次推导。`ImageAssetCompiler::Compile` 因此成为唯一 compile 入口，三个 Kind 分支齐平。

### 一个改不掉的约束

**bake 挪不到 `Compile` 之后。** 三张图是同一个 GPU job 的产物，`BakeSky` 产出的活 GPU cube 直接
喂给两个卷积当 SRV 采样，不走 CPU 往返。能挪的只有**装配**——而那正好就是脏的那部分。

### 步骤（三步均已完成）

1. ✅ `ImageAssetData::m_mips` 改成按 subresource 索引（slice-major / mip-inner，与 `m_textureBytes`
   的实际布局一致）。索引公式直接用 `RHI::GetImageSubresourceIndex`，不另写一份。
   `AsyncUploadSystem` 不受影响——它自己重算每个 subresource 的紧凑 extent，从不读 offset 表。
2. ✅ KTX2 读写支持 cube；两份 format 映射表合并（`KtxFormatMap`，并补上写侧缺的
   `R16G16B16A16_SFLOAT = 97`——每个 bake 产物都是这个格式）。可独立验证：写进去、读回来。
   KTX2 与 libktx 本来就支持（`numFaces`、`SetImageFromMemory` 的 `face` 参数），我们两侧都是主动
   拒绝。但真正的缺口是 **`ImageAssetData` 说不出「我是 cube」**：它只有 `m_arrayLayers`，全引擎靠
   「层数 == 6」推断（`SkyboxSystem.cpp:39`），而写侧把它塞进了 `numLayers`——真写一个 cube 出来的
   会是 6 层 2D 数组。`Serialize` 拿不到 `AssetId`，问不到 descriptor，所以这个信息必须长在数据上：
   **加 `m_isCubemap` 标志，`m_arrayLayers` 继续表示总切片数**（cube = 6，cube array = 6N）——
   与 `RHI::ImageDescriptor` 的 `m_arraySize` + `m_isCubemap` 同一模型，也就是 DX12 与 Vulkan 的
   模型。`numFaces` / `numLayers` 分开是 KTX2 容器自己的字段划分，在序列化边界上除一次即可
   （`numFaces = isCubemap ? 6 : 1`），不渗进资产层。随后那条「层数 == 6」的约定与它的歧义一起消失。
3. ✅ 槽规则收紧：加两种 raw、`.ktx2` 走 Compile、`EntryFor` 第五条拒绝、删 `compiled` 标志、
   `AssembleCubemapData` 移进 compiler 的 usage 分派。行为不变。

现状表里的第 6 条随第 3 步顺手统一，不单列。

### 曾留给「子资产机制统一」的一道口子（已撤）

这三步做完后 sky cube 序列化已无障碍，但 `ImageAssetBuilder::Serialize` 一度仍对它返回空——理由不是
格式，是当时的缓存条目装不下一个完整构建单元：sky 入了缓存，命中时 bake 被跳过，两个 IBL 子资产就
凭空消失。下一节的第 4 步把条目扩成构建单元后，这条拒写已删除。

---

## 子资产机制统一 ✅ 已完成

> **cubemap 缓存已生效**，由 `BakeCubemap` 端到端校验（第二个 manager 不初始化 baker，
> 走到烘焙即失败）。阶段 3 的材质子资产可以直接用这套，不必再加第三个手写 publisher。

### 改造前的现状

两条产出子资产的路径各写各的，且**都不经过 `ProcessAsset`**：

| 来源 | 谁 | 父交出什么 | 重复时 | 失败时 |
|---|---|---|---|---|
| IBL | `ImageAssetBuilder::PublishSubAsset` | 成品（GPU bake 的结果） | 总是覆盖 | 全有或全无 |
| glTF 内嵌图 | `ModelAssetBuilder::DispatchImageSubAsset` | 源字节（在 glb 里） | 已存在就跳过 | `LOG_WARN` 继续 |

两个函数都已删除。

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

**3. 子资产不能被独立构建。** `ProcessAsset` 开头即拒绝 `IsSubAsset()` 的请求：它的源字节在父
文件里，只有父的 `Compile` 知道怎么取出来；不拦的话每个 builder 的 `Load` 都会把父文件当成它自己
的源读（`.glb` 被拿去解码成图片），报出来的错指向「文件损坏」而不是真实原因。要单独使用模型的一
张贴图，正确做法是**把它提取成独立资产**，见后面「子被直接请求」一节。

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
（全仓只有 `MakeChild` 写，没有任何地方读）。

**另加一个 `dependencies` 字段**（`eastl::vector<AssetId>`）：`.gltf` 的外部 URI 贴图有自己的
文件、自己的戳、自己的缓存键，是依赖不是子资产，塞进单元就错了。发布段之后逐个 `LoadAsset`——
它本来就是同步的，等价于删掉的 `DispatchImageSubAsset` 的内联行为，顺带让外部贴图走上完整的普通
资产语义（从此有自己的缓存条目）。

**命中路径不恢复 `dependencies`**：它只由 `Compile` 产出，而命中不跑 Compile。今天没有影响
（Model 还不可缓存），Model 缓存落地前必须解决——依赖边的持久化属于待办 A。

**落地时多改的一处**：`ImageAssetCompiler::Compile` 增加 `outSubAssets` 出参，环境烘焙整体从
builder 移进 compiler。原文写的「声明归 builder」只在「**发布**归 `ProcessAsset`、builder 不碰
`db`」这一半上成立；声明落在哪层是更小的取舍，而 bake 与声明是同一件事的两半，拆开要在两层之间倒手
三个 `BakedCubemap`。代价是 compiler 从此依赖 `SubAssetEntry` 这个构建管线类型。`ModelAssetBuilder`
保持在 builder 里声明——它的子 id 由材质槽的 usage 决定，那是在决定身份。

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

**产出与发布必须分开**，否则「任何一个子失败父就失败」做不到——发布一旦调用就收不回来。改造前的
`CompileEnvironmentCubemap` 正是「产出即发布」：先 publish 两个子、再装配 sky，sky 那步失败时两个
子已经 Ready 躺在 db 里而父是 Error，注释写的「全有或全无」并未做到。两阶段提交把它变成结构性保证，
顺带保住「子全部 Ready 之后父才 Ready」这条原先靠 publish 写在哪一行隐式维持的不变量。

**`CreateAsset` 属于第一阶段。** 它是发布链路上唯一会失败的一步，先做掉，第二阶段就只剩不可能失败
的操作。代价是一个 id 会短暂存在两个实例（新建的与库里既有的），由下面那条不变量兜住。

**子不递归进 `ProcessAsset`。** 父子真正共享的是 `AssetBuildBus`（按类型分发的 Load / Compile）；
`ProcessAsset` 在总线之上加的三样里，缓存对子是另一套，状态机无人观察，只剩事件。

`.unit` 里存的子 `AssetId` 顺带解决了命中路径的 identity：每个子 payload 的 `Deserialize` 要的
identity 就是它自己 AssetId 的 JSON（`AssetCache::IdentityFor`），算得出来，不用额外存。

### 连带修掉的两个不变量

**1. `Asset::Shutdown` 只能删掉库里确实是自己的那一条。** 原来按 id 无条件 `erase`。两阶段提交下
一个 id 可以短暂有两个实例——`InsertOrGet` 返回既有的那个，我们新建的副本析构时会把**已发布的那个**
从库里删掉。签名改成 `ReleaseAsset(id, self)` / `Remove(id, self)`，实例不匹配就不删。

**2. `AssetIdToJson` 序列化失败要报错。** 没注册 `Resource::Reflect` 时它静默返回 true、产出一个
缺 `type`/`path` 的对象。后果不只是清单读不回来：`EntryFor` 的 identity 会对所有资产退化成同一个
值，**缓存的键碰撞检测等于失效**。加了必填字段校验，失败模式从「静默错认」变成「缓存整个关掉」。
（沙盒样例都没注册它，`BakeCubemap` 已补上。）

### 子被直接请求：拒绝，并指向提取

**子资产不能被独立构建**，`ProcessAsset` 开头即拒绝。理由见「四条语义」第 3 条。

曾计划的「反向推出父」（从 `id.GetPath()` 推出父 id、加载整个单元、再取走子）**已否决**：

- **没有生产者。** 要知道 `Chair.glb` 里有个 `image/3`，必须先加载过它；材质里的子 id 来自
  `ModelAssetData::m_imageAssetIds`，IBL 的来自父 id 推导。全仓没有一处拿子 id 去 `LoadAsset`。
- **唯一成立的场景（引用持久化后父不在场）用它来解是错的**——为一张贴图把整个 glTF 的几何、全部
  材质、全部贴图都拖进来，代价对引用者还不可见。

正确答案是**子资产提取**：把那张贴图变成独立资产，有自己的文件、自己的戳、自己的缓存键，引用它的
人付它自己的代价（Unity 的 Extract Textures、UE 导入拆子对象都是这个做法）。这是个加法功能，不与
现有机制冲突，未开工。

### 父怎么引用子：不存，现算

`ImageAssetData` 的 `m_irradiance` / `m_prefiltered` 两个 `Ptr<ImageAsset>` **直接删掉**，没有改成
`AssetId`——一个 bake 产物的 id 是父 id 的纯函数（`MakeSubId(父 id, 固定 subLabel, 固定 usage)`），
存下来是冗余。存了反而在命中路径上多一个问题：那两个 id 不在 KTX2 里，`Deserialize` 恢复不出来，
得往 KV 段再塞两个键。

`ImageAsset::IrradianceId()` / `PrefilteredId()` 现算，`GetIrradianceAsset()` 拿它查库，签名不变，
**消费者零改动**。两个 subLabel 常量提到 `ImageAsset` 上——它们是子资产身份的一半，改一个字就作废
所有已缓存的单元。

### 外部 URI 贴图不属于这套

`.gltf` 的外部贴图 id 是顶层 `AssetId::Of`，有自己的文件、自己的戳、自己的缓存键。它是**依赖**，
不是子资产（对应 O3DE 的 job dependency），走 `ctx.dependencies`。「重复时怎么办」这个问题对它
自然消失：走普通资产语义。与改造前的差别是它从此有自己的缓存条目。

### 统一的步骤（全部完成）

0. ✅ 前置一节的三步。
1. ✅ `ImageAssetData` 的两个 `Ptr` 删掉，子 id 现算。**必须排在第 3 步之前**——声明阶段子资产的
   `Asset` 对象还不存在，sky 拿不到 `Ptr` 可赋。
2. ✅ `AssetCache` 读写构建单元；`CacheFormat` 的 image version `1 → 2`（条目布局变了，旧条目
   必须整体失效）。`ProcessAsset` 同步切到单元 API，子列表为空，行为等价。
3. ✅ `AssetBuildContext::subAssets` / `dependencies` + 两阶段发布段；删 `PublishSubAsset` 与
   `DispatchImageSubAsset`；删 `ctx.db` / `ctx.parentId`；删 `raw.m_rawImages.clear()`
   （`sourceData` 指进它，发布段在 Compile 返回之后才读）。
4. ✅ 命中路径接上同一个发布段；撤掉 `ImageAssetBuilder::Serialize` 的拒写。**cubemap 缓存到这一步
   生效。**
5. ❌ 反向推出父——已否决，见上。改为一条拒绝守卫。

`IsDerivedUsage` 那两个守卫保留。`Compile` 里那条仍可达（子资产若被声明成非 baked raw），
`Load` 里那条现在被 `ProcessAsset` 的守卫挡在前面，留作防御。

**本次不做：** Model 缓存（`CacheFormat` 里 Model 的 version 是 0，它还卡在 `.gltf` 的外部 `.bin`
上，属于待办 A）、运行期父子关系表（构建单元下淘汰是整单元的）、子资产提取。

### 覆盖缺口

- **「任一子失败 → 父失败且什么都不发布」没有测试。** 现有 fixture 里没有「内嵌图坏掉」的输入，
  要造得手搓一个 glb（JSON chunk + BIN chunk）。正路径由 `AnEmbeddedImageGoesReadyBeforeItsModel`
  （监听 `OnAssetReady` 顺序，断言图片先于模型）守着。
- **cubemap 缓存只有 `BakeCubemap` 覆盖**，需要 GPU，进不了 `SparkAssetTest`。

---

## 阶段 2：把序列化器铺到组件

> 序列化器本体在 0.c 已建好，这里是应用面。**机制已定，字段清单与命名待定。**

把六个 `Reflect.h` 的反射字段按类型清点一遍，通用遍历搞不定的只有 `AssetId`（`MeshComponent`、
`SkyboxComponent`、`StandardPBR` ×5）与 `MaterialHandle`（`MaterialComponent` ×1）。其余
标量 / 枚举 / `eastl::string` / 数学类型今天就能走通。

### `JsonOperation` = 类型上的一个反射 `Func`

判据不是「leaf 类型特判」，是：

> **不能由字段遍历重建的类型，才需要它。**

问一句「这个类型能不能靠逐个 `data.set()` 拼出来」，能就不用。`AssetId` 不能，两条**互相独立**的原因：

1. **它不可变，没有 setter。** 三个字段全是 `.Data<nullptr, &Getter>`（`m_hash` 是
   `f(path, sub, type, desc)` 的派生值，entt 对任何非 const 成员都会无条件装 setter，逐字段写会
   留下哈希过期的 id）。于是 `ReadObject` 的「取副本 → 填 → `set()` 回去」在它身上没有落点，
   **只能 `AssetId::Of(...)` 一次构造**。
2. **`desc` 的具体类型由 `type` 的值决定**，字段遍历表达不了这种依赖。

#### 形态

```cpp
// Core/Serialization/JsonSerializer.h
struct JsonOperation
{
    bool (*toJson)(const MetaAny& value, JsonValue& out);
    bool (*fromJson)(const JsonValue& in, MetaAny& target);
};

template<typename T, auto To, auto From>
void ReflectJsonOperation(ReflectContext& context);
```

注册器内部由一个 helper 生成两个类型擦除适配器（`T` 在那里是编译期已知的，藏进 detail 命名空间，
与 `ReflectDetail` 同样的做法），注册成**一个**名为 `"JsonOperation"` 的 `Func`，其返回值就是这张表：

```cpp
static bool ToJson(const MetaAny& value, JsonValue& out)
{
    const T* typed = value.try_cast<T>();
    return typed != nullptr && To(*typed, out);
}
```

调用点：

```cpp
Spark::ReflectJsonOperation<AssetId, &AssetIdToJsonField, &AssetIdFromJsonField>(context);
```

序列化器一次无参 `invoke` 取表，之后走裸函数指针：

```cpp
if (auto fn = type.func(kJsonOperationId))
{
    MetaAny got = fn.invoke({});
    if (const JsonOperation* op = got.try_cast<JsonOperation>())
    {
        return op->toJson(value, out);
    }
}
```

#### 一条规则：可以交出子部分，不能交出自己

`SerializeToJson` 是**分派器**，查 operation 就是重载决议。于是约束和 `operator<<` 完全同构：

> **operation 完全负责 T 的编码。它可以把子部分交回分派器，不能把自己的值交回去。**

`AssetId` 的 operation 对 `AssetType` 调 `SerializeToJson`（子部分，合法，枚举名因此仍来自反射），
但四个键自己写；把整个 id 交回去就等于 `os << *this`。

**不为这条加护栏**（递归检测、报错日志、编译期检查都考虑过并否掉）：它和「在拷贝构造里写
`Foo(other)`」「在 `operator==` 里比 `*this == other`」是同一类错误，语言本身天天有这个风险。为它
单独建一套机制，那个不对称本身就说明不该建。**更不提供任何「借用通用编码处理自己」的入口** ——
一度存在的 `SerializeFieldsToJson` 已删，见 0.d。

#### 为什么是「一个 `Func` 返回函数指针表」，不是「两个 `Func` 各自 `invoke`」

三条，第一条是决定性的：

- **出参会静默丢失。** [meta.hpp:1032](../Engine/3rdParty/entt/src/entt/meta/meta.hpp#L1032) 的
  `invoke(instance, Args&&...)` 把每个实参包成 `meta_any{*ctx, std::forward<Args>(args)}` ——
  **值构造**。写成 `fn.invoke({}, value, out)`，函数写在 `out` 的副本上，调用方的 `out` 一个字
  没变，**不报错、不返回 false**。必须记得 `entt::forward_as_meta(out)`。这与 0.c 已经踩过的坑
  （`DeserializeFromJson` 的 `target` 为什么必须取引用）是同一个。走函数指针则 `JsonValue&` 一路
  真引用传到底，坑不存在。
- **签名错了只有运行期知道。** 形参顺序写反照样编译通过，表现为「这个类型的 `JsonOperation` 好像
  没生效」。
  走 helper 则 `To(*typed, out)` 那行当场不编译。
- **每次读写都在编组。** 实参数组构造 + 逐个 `allow_cast` + 返回值裹 `meta_any`，而这条路在分派链
  最前面、每个字段每次读写都走。

**表里是裸函数指针，不是 `eastl::function`。** `meta_any` 内部持有 `entt::any`
（[meta.hpp:637](../Engine/3rdParty/entt/src/entt/meta/meta.hpp#L637)），而
`entt::any = basic_any<sizeof(double[2]), ...>`（[core/fwd.hpp:25-32](../Engine/3rdParty/entt/src/entt/core/fwd.hpp#L25-L32)）
—— **SBO 16 字节**。两个函数指针正好 16 字节、零分配；单个 `eastl::function` 的 SSO 缓冲就已经是
`2 * sizeof(void*)`（[function.h:24](../Engine/3rdParty/EASTL/include/EASTL/internal/function.h#L24)），
两个必然溢出，于是每次取表都变成一次堆分配。而这个注册形态天然无状态（`To` / `From` 是非类型模板
参数，没有能捕获的东西），`eastl::function` 的类型擦除买不到任何东西。

#### 名字与旁表

叫 `JsonOperation` 而不是 `JsonCodec`：它里面一行逻辑都没有，是入口表不是编解码器；而
[Utility.h:86-97](../Engine/Code/RunTime/Core/Reflection/Utility.h#L86-L97) 的 `ComponentOperation`
已经为「把一组自由函数注册成类型上的反射 `Func`、让调用方 context-free 地调」这件事定下了词。
这是第二次用同一个套路，就用同一个词。

**不走旁表。** 曾考虑按 `TypeId` 索引的独立注册表，否掉：`Func` 是开放命名空间（`Custom` 是单槽、
已被 `ComponentTraitsRuntime` / `UIElement` 占，类型级 `Traits` 是共享位掩码、`MetaTypeTraits` 在用），
而 `ComponentOperation` 已经是这个路子；旁表要多配一套生命周期。

> **原先「注册那一行必须落在 `.cpp` 里」的约束消失了。** 那条的根因是 `Func<&Fn>` 会对每个**形参**
> 类型实例化 `internal::resolve<T>`，其中 `std::is_default_constructible_v`（`node.hpp:282`）要求完整
> 类型，于是 `resolve<JsonValue>` 逼着注册行下沉。改成返回表之后，注册的那个函数**无参**、返回
> `JsonOperation`，只实例化 `resolve<JsonOperation>`；`JsonValue` 仅以函数指针类型出现，`json_fwd.hpp`
> 就够。对 `AssetId` 这条收益是零——`AssetIdToJsonField` 的定义本来就要 `json.hpp`、本来就在
> `AssetJsonSerializer.cpp`——但它对后来的类型不再是一条硬约束。

### 查表放在分派链最前面

不是原先写的「复合分支之前」。理由不是某个具体类型够不着，而是：**显式注册本身就是「我不走默认推断」
的声明，它没有理由排在五条按形状猜的分支后面。** 规则于是只剩一句「注册了 `JsonOperation` 的类型完全
接管」，不用记它跟内建分支的相对次序，将来加第六条内建分支也不用重想。

不违反 0.c 的「枚举判定先于任何 cast」：查表是 `type.func(...)`，只用已抓下的 `type`，不碰 value、
不做 cast。对没进过 `meta_factory` 的类型（`float` 等），`look_for`（`node.hpp:174`）第一句
`if(node.details)` 即返回，开销可忽略。

一条附带事实：entt 的 func 查找沿 `base` 上溯，所以反射了 `.Base<T>()` 的类型会继承 `T` 的
`JsonOperation`。今天没有这种反射继承关系，将来加时记得。

### `null` = 未指定

一条通用约定，不只对 `AssetId`：

> **`JsonOperation` 对「未指定」一律编码为 `null`；`null` 解码回该类型的未指定值。**

判断必须在 `JsonOperation` 内 —— 序列化器手里只有 `MetaAny`，问不出「这个值算不算空」。不在序列化器
里统一拦 `null`：那会把「未指定」等同于「默认构造」，还会让 `null` 落在 float 字段上时静默变成 0。
**没有 `JsonOperation` 的类型上出现 `null` 视为格式错误。**

这条**不用写代码**：`DeserializeFromJson` 的每个分支今天已经先验 JSON 类别——算术
`is_number()`（`JsonSerializer.cpp:65`）、字符串与枚举 `is_string()`、序列 `is_array()`、复合
`is_object()`，全部 `LOG_ERROR` + false。`null` 落在其中任何一条上都已经是错误。现状即所需。

`AssetId` 的两个包装：

```cpp
bool AssetIdToJsonField(const AssetId& id, JsonValue& out)
{ if (!id.IsValid()) { out = nullptr; return true; } return AssetIdToJson(id, out); }

bool AssetIdFromJsonField(const JsonValue& in, AssetId& target)
{ if (in.is_null()) { target = {}; return true; } target = AssetIdFromJson(in); return target.IsValid(); }
```

`AssetIdToJson` / `AssetIdFromJson` 本身**一个字不动** —— 缓存的 identity 要它对无效 id 严格报错。
读侧这层是必须的：`AssetIdFromJson` 今天在**失败时也返回默认 id**，包一层才能把「未指定」和
「这段 JSON 是坏的」分开。

组件里的空 `AssetId` 是常态（未赋值的模型槽、五个空贴图槽），所以这条不是边角料 —— 直接把
`AssetIdToJson` 接上去，存一个默认材质会刷五条 ERROR 且整个组件判失败。

### 命中即终局

失败 `LOG_ERROR` + false，**绝不回落到通用分派**。`null` 已经把「空」接走了，剩下的失败只有
「反射没注册」和「数据损坏」，都不是可降级的东西。回落正是要防的那个失败模式：`AssetId` 掉回复合
分支 → 缺 `desc` → 法线贴图静默变成 sRGB 颜色贴图。

### 默认值一律写出 ✅ 已完成

`WriteObject` 里那段「等于默认值就省略」**已删除**，不做成开关。反射注册且标了 `Serializable` 的
字段，一律写出。

去掉它的理由：

- **默认值会变成文件语义的一部分。** 把 `StandardPBR::m_roughness` 的默认从 0.5 改成 0.4，所有旧
  场景里「没写 roughness」的材质会**全部静默改变外观** —— 文件没变、代码改了一个数、画面变了。
  **descriptor 这条更重**：它是 `AssetId` 的一部分，而 `AssetId` 是身份，默认值一动，磁盘上所有旧
  引用和由它派生的所有缓存键会指向另一个编译产物。
- **文件不自解释。** 分不清「作者没设」与「作者设成了正好等于默认的值」。
- **每写一遍要序列化两遍**（造默认实例 + 完整递归序列化 + 逐字段比 `JsonValue`），嵌套每层都做。
- 不可默认构造的类型退化成全写，同一份格式里两种行为；去掉之后这个特例消失。

它买到的**只有文件体积**，实测一个普通贴图引用从 48 字节变 158 字节，整个场景文件量级是几十 KB
—— 在 JSON 这种格式里不值一提。版本化不靠它：那靠的是**读侧**的「缺键 = 保留现值」。

**不做成 `JsonDefaults{Omit, Write}` 开关**（一度是计划）：两套行为要各自测、策略参数要线程化到整条
递归、每个调用方都得想传哪个，而省下的那几十 KB 撑不起这些。

一并落地的：

- `AssetIdToJson` 里 `!descriptor.empty()` 那个条件删除 —— **`desc` 键从此总是写**。要保留「全默认
  就不写」得构造默认实例来比较，正是删掉的那个东西。`sub` 不受影响，它是 `IsSubAsset()` 判断。
- **现有缓存条目一次性全部失效**（identity 字符串变了，自愈重建）。
- 三个测试文件里断言 `dump()` 字面量的用例更新，`DefaultsAreOmitted` 改为 `DefaultsAreWritten`、
  `DefaultDescriptorsEncodeToEmptyObject` 改为 `DefaultDescriptorsAreWrittenInFull`。

### 数学类型标 `Serializable`，不给 `JsonOperation`

`Math::Vector3` / `Vector4` 的分量加 `Traits`。它们**能**逐字段重建，按上面的判据就不该走
`JsonOperation`。曾考虑让它产出 `[x,y,z]`，两条反对理由（项数不固定、往已有对象里读时逐分量合并）
**都是默认省略造成的**，而默认省略已经删掉，通用遍历现在也永远写三项。

原文列的 `Vector2` 与 `Quaternion` 一并推迟——全仓没有用到它们的反射字段（`TransformComponent`
的 `m_rotation` 是欧拉角 `Vector3`；`m_baseColor` / `m_emissive` 是 `Vector4`）。理由与下面删掉
`Matrix4X4` / `Entity` 的一样。

原文列的 `Matrix4X4` 与 `Entity` **删除** —— 全仓没有任何这两个类型的反射字段（`Hierarchy` 压根
没反射，`LocalTransformMatrix` / `WorldTransformMatrix` 是 `TransformComponent` 的派生值，
`CameraComponent` 里也没有矩阵）。等第一个用例出现再加，加法是一行注册。

（顺带查过一个可能的陷阱：`glm::vec3` 在本工程配置下是 `vec() = default`（`setup.hpp:855`），
entt 的 `type.construct()` 用 `plain_type()` 值初始化（`any.hpp:135`），defaulted 而非 user-provided
的默认构造会先零初始化，拿到的是确定的 `{0,0,0}` 而不是未初始化内存。）

### 不标的

`MeshComponent::m_vertexCount` / `m_triangleCount` 是从 model asset 算出的派生值——存了会在磁盘上
造出第二个真相来源，模型换了而场景文件没跟着变就是错的。`m_modelAsset`（`Ptr`）与
`MaterialTexture::m_image` 本来就没反射，不用管。

`MaterialComponent::m_material` 阶段 2 先不标，见阶段 3 的「`MaterialHandle` 怎么落盘」。漏标的后果
今天已经是响的（无枚举项的 `enum class` 走进枚举分支 → `WriteEnum` 找不到枚举项 → `LOG_WARN` +
`ok = false`），不用额外加守卫。

### 落盘的 key = 反射注册名

组件 key 取 `.Type(...)`、字段 key 取 `.Data(...)`，与今天 Inspector 上的标签是**同一个字符串**。
不加序列化专用名，也不从代码名推导标签——**名字必须显式指定**。

否掉「另设一个序列化名」的理由与 `JsonOperation` 那里否掉旁表的一样：字段级两个可用槽都被占了
（`Custom` 归 `UIElement`，类型级 `Traits` 是位掩码存不下字符串），要塞第三个字符串就得让 `Reflector`
自己维护一张 `(类型 id, 字段 id) → 名字` 的旁表，比那张还细一层。

代价明写在这里：**改一个 Inspector 标签就是改文件格式**，与枚举名同一条规则。随之固定的还有几处
名与实的错位——`"Near"` / `"Far"` 对应的是 `m_clipStart` / `m_clipEnd`，`m_innerConeDeg` 的单位不在
key 里。接受。

编辑器侧一个字不用改：`data.name()` 在全仓的唯一消费者就是 `ComponentView` 画标签，没有任何地方
按名字查字段（entt 的查找走 `hashed_string::value(name)` 算出的 id）。

#### 冻结前的名字校对 ✅ 已完成

从打标记那一刻起这批名字就是文件格式。校对结果：

**已改（7 个字段 + 1 个枚举值）**

| 原名 | 新名 | 为什么 |
|---|---|---|
| `Prespective` | `Perspective` | 拼错，C++ 枚举标识符本身也错 |
| `"Near"` / `"Far"` | `"Clip Start"` / `"Clip End"` | 与成员 `m_clipStart` / `m_clipEnd` 零对应，看文件推不回代码 |
| `"Inner Cone"` / `"Outer Cone"` | `"Inner Cone Degrees"` / `"Outer Cone Degrees"` | **单位丢了**。将来换弧度，文件里所有值的含义静默改变且无标记 |
| `"Shadow Normal Offset"` | `"Shadow Normal Offset Texels"` | 同上，丢的是 texels |
| `"Metallic-Roughness Map"` | `"Metallic Roughness Map"` | 全仓唯一用连字符分词的 key |
| `Name` 的 `"name"` | `"Value"` | 全仓唯一小写字段名，且落盘是 `{"Name":{"name":...}}` 叠字 |

前四条是同一类错误：**成员名里的限定词（单位、语义）在反射名里被当成「显示啰嗦」删掉了**。作为
Inspector 标签删得对，作为文件格式删错了。这是一名两用的固有张力，加字段时要记得往文件格式那边靠。

**保留**：`"FOV"`（公认缩写）、`"Rotation"`（欧拉角用度是行业惯例，`TransformSystem.cpp:33` 转弧度）、
`"Range"`（世界单位是长度字段的默认假设）、`ShadowFilterWidth` 的 `"3x3"/"5x5"/"7x7"`（滤波核尺寸的
表示法不会变）、`"Type"`（有组件名限定，不歧义）、`"Image Asset"/"Model Asset"`（类型就是 `AssetId`，
Id 后缀冗余）、数学分量 `x/y/z/w` 小写（GLSL/HLSL 惯例，与外层 Title Case 混排可以接受）。

#### 组件 key 的规则（已冻结）

> **类名去掉 `Component` 后缀。**

`Transform` / `Camera` / `Light` / `Skybox` / `Mesh` / `Material` / `Name`；`StandardPBR` 没有那个
后缀所以保持全名。

唯一遗留：`MaterialComponent` 的字段 `m_material` 也叫 `"Material"`，落盘会是
`{"Material":{"Material":4}}`。该字段阶段 2 不标（留给阶段 3），到时把字段名改成 `"Handle"` 即可，
不阻塞现在。

**已开的例外一个**：`StandardPBROverride` 的 key 是 `"StandardPBR Override"`，按规则本该是原样类名。
拆词是为了 Inspector 标题可读；不叫 `"Override Material"` 是因为那读起来像 UE 的 `Override Materials`
（替换整个材质槽数组），而它覆盖的是参数。

### 待定

- **`StandardPBR` 算阶段 2 还是阶段 3。** 它是唯一走 `Data<Set,Get>` 的字段，标了能让 setter
  那条路（编辑器 drag-drop 在用）被序列化测试覆盖。
- 完整字段清单、验收面（没有场景序列化器，阶段 2 的产物只能是单测里的组件 round-trip）、步骤拆分。

「这个组件整体是否持久化」是类型级问题，归 `ComponentTraits`，与 `editable` 并列。零字段的 tag
组件只有类型级能表达。阶段 4 才需要。

---

## 阶段 3：材质资产

> **使用流程、数据模型、`.smat` 的形态与产生方式已定**（下四节），其余仍是大致方向。

- 新增 `AssetType::Material`（**枚举末尾追加**——`AssetType` 折进 `AssetId::ComputeHash`），扩展名 `.smat`。✅
- `ModelAssetBuilder` 把 glTF 内嵌材质 publish 成子资产（`model.glb:material/0`），与内嵌图片
  子资产同一条路。✅ 一并落地的：raw 分 `Encoded` / `Decoded` 两种（`.smat` 是字节，glTF 材质是
  结构，编译器按 kind 分派）；`Resource::Material` 降为 builder 的 TU 内中间物 `ResolvedMaterial`；
  构建单元是**平的**——子资产的依赖并进根的，子资产再声明子资产直接断言。

原「待细化」的三条已定：

| 问题 | 结论 |
|---|---|
| authored 结构落哪个模块 | **`Resource/Material/`**——`ModelAssetBuilder` 与 `MaterialAssetBuilder` 都要构造它，两个都在资产层，`SparkAssetManager` 不能依赖 Feature 层 |
| `Ptr<ImageAsset> m_image` 怎么摘 | 并进 `MaterialTextureSystem::m_pool`（它本来就按 `AssetId` 索引）。`MaterialTexture` 这个 struct 消失，贴图槽变成 `array<AssetId, N>` |
| `Load` / `Compile` 怎么分 | `Load` 只把字节读进来（`Encoded` raw），`Compile` 解 JSON 出参数。与 `.ktx2` 走 `ImageEncodedRawData` 同构 |

第二条让 `StandardPBR` 成为**纯 authored 结构**——序列化时不再有「哪些字段是运行期的」这个
问题；顺带修掉 `MaterialTextureSystem::Update` 每帧在写 authored 数据这件事。

### 使用流程 ✅ 已定

> 以编辑器用户的视角写，只说「用户做什么、看到什么」。数据模型在下一节。

今天材质只有一个来源：拖一个模型进场景。glTF 的材质变成运行期的共享实例，关掉编辑器就没了。
下面八条是材质资产做完之后该成立的全部用户故事。

| # | 流程 | 用户看到 | 今天 |
|---|---|---|:--:|
| 1 | 调整模型带进来的材质 | 在材质窗口里改；**所有共用它的对象一起变**；存不回 glb，只能导出 | ⚠️ 今天在 Component View 内联改，入口要搬 |
| 2 | 导出成材质资产 | Browser 里出现 `.smat`；画面不变，共享关系不变 | ❌ |
| 3 | 把材质用到别的对象 | 从 Browser 拖 `.smat` 到材质槽 | ❌ |
| 4 | 编辑一个 `.smat` | 同一个材质窗口，可 Save | ❌ **必做** |
| 5 | 只改这一个对象 | 这个对象从此跟别人不一样，删掉覆盖就恢复 | ❌ |
| 6 | 从零新建材质 | Browser 右键新建 | ❌ 优先级最低 |
| 7 | 存场景、重开、一切还原 | 含**共享关系原样还原** | ❌ 依赖阶段 4 |
| 8 | `.smat` 被删掉 | 退回默认材质，不崩，有提示 | ❌ |

**面板名以代码为准**：`Inspector` 是**实体列表**（`EntityList` 表格），改参数的面板是
**Component View**，资产浏览器是 **Browser**（`BottomPanel`）。

#### 两个编辑表面，互不重叠

**改材质与改单个对象，在两个不同的窗口里。** 这是刻意的：两者混在同一个面板上，用户看不出自己正在
改的是所有人共用的那一份、还是只影响眼前这个对象——Unity 那个经典的「我只改了一个物体，怎么全变了」
就是这么来的。分开之后这个歧义**结构上不存在**，不必靠 UI 提示去缓解。

| 表面 | 改什么 | 影响 |
|---|---|---|
| **材质窗口**（新） | 材质本身 | 所有引用它的对象 |
| **Component View 的材质槽** | 这个对象的覆盖 | 只有这一个对象 |

Component View 上的材质槽因此**只有引用、没有参数**：

```
Material
  └ 材质        [project://Material/Wood.smat]   [打开]     ← 引用，拖放目标
  └ 本对象覆盖   (无)                             [创建]
```

有覆盖时下半部分展开成那份参数、可编辑。`MaterialRefElement` 的含义随之从「内联展开被引用材质的
字段」变成「显示材质身份 + 打开按钮 + 拖放目标」。

**材质窗口的主入口是材质槽上的「打开」按钮**，不是 Browser 的选中状态。原因是模型带进来的材质
（`Chair.glb:material/0`）是子资产、不是文件，**Browser 里选不中它**，而它恰恰是今天唯一的材质来源。
Browser 双击 `.smat` 是第二个入口。

这顺带绕开一个前置：流程 4 原本被「Component View 要支持选中资产」这个还不存在的编辑器能力挡住，
改成由显式动作打开窗口之后不再需要它。

**v1 不做预览**，就是一个参数面板。字段渲染那套机器现成——`ComponentView::RenderFields` 就是遍历
反射字段画 UI，材质窗口只是把目标换成材质实体的 `StandardPBR`。真正新增的是窗口自己的状态：
当前打开的是哪个材质、dirty 标记、Save / 导出按钮。

三条要点：

- **dirty 与 Save 归材质窗口**，不在对象面板上。glb 子资产背书的材质存不回去，它的窗口只有导出、
  没有 Save。
- **流程 2 在内嵌贴图的模型上第一版拒绝**，理由与替代路径见「`.smat` 怎么产生」。
- **流程 7 的「共享关系原样还原」是验收标准**：原来 50 个 primitive 共用 3 个材质，读回来仍是 3 个，
  不是 50 个。

**待答（都不影响格式）：** 材质窗口上要不要显示「被 N 个对象使用」（原本是用来警告「你正在改所有
人」的，两个表面分开之后不再必需，但仍有用）；`.smat` 被外部修改要不要热重载（倾向不要——该和贴图 /
模型一起整体做）；改完参数何时写盘（倾向显式 Save）。

### 数据模型 ✅ 已定

三个概念，跨两个上下文：

| 组件 | 住在 | 含义 |
|---|---|---|
| `MaterialComponent { MaterialHandle }` | World | 这个对象用哪个材质。**共享**，多个对象指同一个 |
| `MaterialAssetRef { AssetId }` | MaterialContext | 这个材质来自哪个资产 |
| `StandardPBR` | MaterialContext | 这个材质的参数 |
| `StandardPBROverride` | World | 这个对象对材质的覆盖 |

**解析规则一条：先看世界实体有没有覆盖，没有才取材质实体的。**

`StandardPBROverride` **继承** `Resource::StandardPBR`，是一个只为拿到独立类型身份的空派生类，
字段不重复。要独立类型是因为 `ComponentOperation` 一类型一绑定，同一个类型不能同时声明「我住
MaterialContext」和「我住 World」。

但**继承本身不够**：entt 的字段枚举 `data()` 只列类型自己那张表，不穿基类（只有按 id 查的
`data(id)` 才穿，见 `meta.hpp`），而序列化器和 inspector 用的都是枚举版。所以派生类型必须把 13 个
字段**注册在自己身上**——只 `.Base<StandardPBR>()` 会让它落盘成 `{}`、inspector 一片空白。字段表
因此写成 `ReflectStandardPBRFields<Params>` 模板，两个类型各实例化一次，源码不重复、注册表里两份。
`MaterialSerializeTest` 用「字段数与 `StandardPBR` 相等」钉住这条。

#### 一个资产一个实例

`Material::Resolve(mc, id)` 是唯一入口，同一个材质资产在一个 MaterialContext 里只有一个实例——
它扫 `MaterialAssetRef` 视图认人，没有就取资产、建实体。解析只发生在 spawn / 赋值时，不在帧内；
将来嫌线性扫描慢，换成表只动函数体，调用点不用改。

子资产 id 只能 `FindAsset`（父的构建已经发布过它，而 `ProcessAsset` 开头就拒绝单独构建子资产），
独立 `.smat` 才走 `LoadAsset`。

改这个材质，所有引用它的对象一起变——这正是流程 1、3、4 建立起来的预期。想让某个对象不一样，
走覆盖（流程 5），**不是再造一个实例**。

#### MaterialContext 里只剩两种东西

| 谁 | 谁持有 |
|---|---|
| 资产背书的材质，一个资产一个 | `MaterialAssetRef` 自己 |
| 默认材质（`DefaultMaterialTag`） | `MaterialSystem` |
| 覆盖合成出来的材质 | 那个世界实体的 `MaterialOverrideRef` |

**没有匿名创建的材质实体了**，每一个都有明确的所有者。glTF 材质也在其中——它的 `MaterialAssetRef`
指向子资产 `Chair.glb:material/0`，与 `.smat` 走同一条路，不需要任何特例。「glTF 材质可改不可存」
因此从一条人为规定变成机制的自然结果：改的是本地那份，存不回 glb，只能走流程 2。

第三类是实现覆盖时新增的，只活在 `MaterialBindingSystem` 里，不反射、不落盘，见下面「覆盖到了
GPU 就是材质」。

#### 由此消失的：材质 GC ✅ 已完成

`MaterialSystem::CollectGarbage`、`MaterialLiveMark`、`m_gcGeneration` 全部删除。

那套每帧两次全量遍历的 mark-sweep 存在的唯一原因，是材质实体被匿名创建而无人负责销毁——加 Material
组件造一个、`SpawnModel` 每个 glTF 材质造一个，销毁点零个，只能反过来每帧问「还有没有人引用你」。
所有者明确之后扫无可扫；覆盖挂在世界实体上，实体销毁时 entt 自动带走。

顺带消掉一个时序陷阱：原设计下「已从 `.smat` 建好、但还没有任何对象引用」的材质会被下一帧扫掉，
而场景加载期间这个窗口必然出现。

材质实例因此变得和资产一样长寿（加载过就一直在）。这不是新泄漏，与「内存驻留无淘汰」这条已知欠账
一致；将来做资产淘汰时材质实例跟着走，仍是显式销毁，不必把 mark-sweep 请回来。

`MaterialTextureSystem` 自己那个 GC 不受影响——它管的是 GPU 贴图常驻，是另一回事。

#### 覆盖是全量的

组件级 fallback 是有无之分，不是字段之分：实体一旦挂了覆盖就完全遮蔽材质，**此后材质资产再改也
不影响它**。

所以这不是 UE 的 MaterialInstance（继承 + 局部覆盖，父改了未覆盖项跟着变）——那需要每个字段带一位
「是否被覆盖」，纯 struct 表达不了，留给 `.smat` 的 `parent` 那一步一起做。

流程 5 因此简单到没有记账：**加覆盖组件 = 分歧，删覆盖组件 = 恢复。** `MaterialComponent` 全程不动，
「原来用的是哪个材质」这个信息从来没丢过。

#### 换 shading model 时，不匹配的覆盖被移除

把一个对象的材质换成另一个 shading model 的材质之后，它身上那个覆盖组件的类型就对不上了。
**规则：移除不匹配的覆盖并提示用户。**

今天只有一个 shading model，撞不到这条；但既然为多模型设计了，规则现在写下来，将来不用回头补。
零代码。

#### 覆盖到了 GPU 就是材质（原「代价：材质缓冲有两个来源」，已推翻）

原方案说：带覆盖的世界实体要自己一条 GPU 记录和一个槽位，于是 `MaterialBindingSystem` 管两个
population、`ResolveMaterialIndex` 先看实体自己的槽——并把这称作「本方案唯一实打实的新增复杂度」。

**没有两个 population。** 覆盖是上层的概念，到了 GPU 那一层就该被抹掉：对 shader 来说，一个覆盖
就是产生了一个新材质。所以 `MaterialBindingSystem` 在编码前把每个带 `StandardPBROverride` 的世界
实体**合成成一个材质实体**，之后一切照旧——`GlobalBuffer` 一行未改，`MaterialTextureSystem` 一行
未改，`ResolveMaterialIndex` 只是换个 handle 取。那笔"唯一的新增复杂度"没有发生。

合成实体与世界实体之间靠 `Render::MaterialOverrideRef` 连着。它**故意不反射**——inspector 和场景
序列化都只遍历反射类型，不注册就是两头都进不去，所以"上层不可见"是物理保证而不是约定。类型声明
也放在 `Feature/Render/Binding/Material/` 里，不出现在 SparkMaterial。

参数每帧整份重写，不做脏标记：一个覆盖 68 字节，换来"改覆盖"和"改被遮蔽的那个材质"都不需要任何
通知。

生命周期三条，都不是 GC：

| 情况 | 谁发现 |
|---|---|
| 覆盖组件被删（实体还在） | `GetView<MaterialOverrideRef>(Exclude<StandardPBROverride>)` |
| 世界实体被销毁 | `EntityEventBus::OnEntityDestory` —— 覆盖和链接跟着实体一起没了，世界侧再没有东西记得那个合成实体 |
| 组件新增 / 修改 | 不需要发现，每帧 find-or-create + 整份重写 |

销毁分两段夹住编码：**先打 `DeadTag`，编码后再真销毁**。因为 `GlobalBuffer` 回收槽的条件是实体同时
带着 slot ref 和 `DeadTag`，直接销毁会让那个槽永久泄漏。`OnEntityDestory` 里只读 world、只写
MaterialContext——它是在 `DestoryEntity` 遍历 registry 存储的过程中触发的，回头改 world 是重入。

**一帧延迟：** 合成实体在渲染期创建，`MaterialGPUTextures` 要等下一帧 `TICK_PRE_RENDER` 才写上，
所以带贴图的覆盖第一帧贴图为空。与「没有 RHI 资源保证本帧可用」这条既有约定一致。

「一千个对象共用一个材质只上传一份」不是这里买到的——GPU 记录本来就按材质实体算，共享本身已经
给了这条。

#### 场景落盘没有特例

material 上下文按阶段 4 的规则原样写出：storage-major、entity 原值当键、组件 key 与字段 key 都是
反射注册名。

```json
"material": {
  "StandardPBR":      { "12": { "Base Color": {"r":0.8,"g":0.8,"b":0.8,"a":1.0}, "Metallic": 0.0, ... } },
  "MaterialAssetRef": { "12": { "Asset": {"type":"Material","path":"project://Material/Wood.smat"} } }
}
```

- **「引用资产」还是「场景自有」由带没带 `MaterialAssetRef` 表达**，不需要额外的键。
- **「每种材质参数不一样」由带哪个参数组件表达。** 将来出现别的 shading model，材质带的是另一个
  参数组件，两种材质在同一上下文里共存——异构实体是 ECS 的原生能力，不用为它设计任何东西。老场景
  里的 `StandardPBR` 继续存在、继续读得回来，是加法不是替换。
- 有 `MaterialAssetRef` 时参数是**缓存**不是冗余：场景加载不必等资产读完才有画面；`.smat` 缺失时
  （流程 8）还能降级显示上次的样子。
- 覆盖合成出来的材质实体**不在这里**——它不反射，落盘的是世界实体上那个 `StandardPBR Override`，
  下次加载重新合成。

### `.smat` 的形态 ✅ 已定

JSON，三个顶层键：

```json
{
  "shadingModel": "StandardPBR",

  "state": {
    "Alpha Mode": "Opaque",
    "Alpha Cutoff": 0.5,
    "Double Sided": false
  },

  "properties": {
    "Base Color":         {"r": 0.8, "g": 0.8, "b": 0.8, "a": 1.0},
    "Metallic":           0.0,
    "Roughness":          0.5,
    "Specular":           0.5,
    "Emissive Color":     {"r": 0.0, "g": 0.0, "b": 0.0, "a": 1.0},
    "Emissive Strength":  1.0,
    "Normal Scale":       1.0,
    "Occlusion Strength": 1.0,

    "Base Color Map":         {"type":"Image","path":"project://Texture/Wood_BC.png"},
    "Metallic Roughness Map": {"type":"Image","path":"project://Texture/Wood_MR.png",
                               "desc":{"colorSpace":"Linear","usage":"NoColorTexture2D"}},
    "Normal Map":             null,
    "Occlusion Map":          null,
    "Emissive Map":           null
  }
}
```

**顶层三个键是手写的**（与 `AssetIdToJson` 同类，不走字段遍历），lowerCamel，与 0.c / 0.d 的
`path` / `sub` / `desc` / `colorSpace` 一致。`state` 与 `properties` **里面**的键是反射注册名，
也就是 Inspector 上的标签——阶段 2 的「一名两用」。

#### `shadingModel`：谁来解释下面这些值

今天只有 `"StandardPBR"` 一个值，**名字一经落盘即冻结**。

它与参数组件的反射注册名**是同一个字符串**（参数组件就叫 `StandardPBR`，见数据模型一节），于是读侧
可以拿这个值直接 `Resolve` 出参数组件的 `MetaType`，不必另建一个 `ShadingModel` 枚举再手写一张
枚举值→类型的映射。这是「一名两用」的又一次应用。要不要真这么做留到读侧落地时定。

它是「任意参数材质」的接缝：将来出现自定义 shader 材质时，这个键指向另一个模型，`properties` 装那个
模型声明的任意属性——**文件形状不变，变的只是解释器**，老文件继续走固定 struct 的快路径。加宽方式是
值从字符串变成 `AssetId` 复合对象，读侧接受两种形态：字符串 = 内置模型名，对象 = 资产引用。

**属性名属于 shading model，不属于某个 C++ struct。** 今天它恰好等于反射字段名，将来若 bag 化则
来自模型声明，文件不用改。

#### `state` 与 `properties` 分开

判据是消费者不同：

| | 谁消费 |
|---|---|
| `properties` | 材质的常量缓冲 + 贴图绑定 |
| `state` | PSO / 渲染路径的选择 |

分家是因为**把一个键从 `properties` 挪到 `state` 属于格式破坏**——现在分对就永远不用挪。

`alphaMode` / `alphaCutoff` 收进 `state`，原「这类被丢弃的字段要不要收进来」的待细化就此关闭。两者
今天都没有消费者，收它们是为了不丢 glTF 的信息——当时那条 `StandardPBRFromModel` 正在丢，现在
glTF 材质走子资产，`MaterialState` 一路带到材质实体上。

#### `properties` 是平坦对象，贴图和标量混在一起

贴图就是一个类型为 `AssetId` 的属性。

- 值是 0.d 的 `AssetId` 复合对象。usage / colorSpace 已经在 `desc` 里，不另外声明；`desc`
  全默认时省略（sRGB `Texture2D` 是默认，所以 base color 贴图通常没有 `desc`）。
- **颜色是 object，不是数组，键是 `r`/`g`/`b`/`a`。** 阶段 2 定了数学类型标 `Serializable` 而不给
  `JsonOperation`，所以颜色走通用字段遍历、落盘是一个对象；写成 `[r,g,b,a]` 的手写文件会在读侧的
  复合分支上直接判类别不符。键之所以是 rgba 而不是 xyzw，是因为 `Math::Color` 是一个**独立类型**
  而不再是 `Vector4` 的别名——反射按 C++ 类型索引，别名只能借用 `Vector4` 的字段名，颜色就会在每
  个文件里把自己拼成 x/y/z/w。
- **无贴图写 `null`，不是省略键。** 阶段 2 定了 `null` = 未指定、且默认值一律写出，所以属性永远
  全部在文件里——diff 稳定、文件自解释，也不靠「缺键」表达「没有贴图」这个明确语义。
- 不分组。Inspector 的分组是 UI 的事，需要时由模型声明携带，文件保持平坦。

#### 刻意不写的

| 不写 | 为什么 |
|---|---|
| `"type": "Material"` | 类型由扩展名与请求它的 `AssetId` 给出 |
| 版本号 | 0.c 的三条规则覆盖增删字段。真出现「同名字段语义变了」再在顶层加 `"v"` |
| 材质名 | 文件名就是名字 |
| 依赖列表 | 从 `properties` 的贴图引用现算。先例见「父怎么引用子：不存，现算」 |
| 编译产物 / GPU 布局 | `.smat` 是源，不是 cook 产物 |

#### 预留但现在不写

材质实例（父材质 + 覆盖）的形态是顶层加 `"parent": {AssetId}`。它一旦出现，`properties` 的含义从
「完整值」变成「相对父的覆盖」，「默认值一律写出」对实例不再适用。这是加法：没有 `parent` 就是独立
材质，规则不变。

#### 属性名定稿 ✅ 已定

`state` / `properties` 里的键 = 反射注册名 = 面板上的标签，**一经落盘即冻结**。大小写随仓库既有的
25 个已注册字段：**Title Case + 空格**（"Clip Start" / "Inner Cone Degrees" / "Cast Shadow"）。

| 位置 | 名字 | 来源 / 说明 |
|---|---|---|
| `state` | `Alpha Mode` | glTF；值与它同源，见下 |
| `state` | `Alpha Cutoff` | glTF `alphaCutoff` |
| `state` | `Double Sided` | glTF `doubleSided`。**不是 `Cull Mode`**：剔除哪一面是渲染器的约定，而作者表述的是「这个面有两面」；存成 `RHI::CullMode` 还会把 RHI 的枚举名钉进文件格式 |
| `properties` | `Base Color` | glTF / UE 同名 |
| `properties` | `Metallic` | glTF / UE / Unity 一致 |
| `properties` | `Roughness` | glTF / UE 一致 |
| `properties` | `Specular` | UE 的介电 F0 缩放（0.5 → F0 0.04） |
| `properties` | `Emissive Color` | **由 `Emissive` 改名** |
| `properties` | `Emissive Strength` | `KHR_materials_emissive_strength` |
| `properties` | `Normal Scale` | glTF `normalTexture.scale` |
| `properties` | `Occlusion Strength` | glTF `occlusionTexture.strength` |
| `properties` | `Base Color Map` | |
| `properties` | `Metallic Roughness Map` | |
| `properties` | `Normal Map` | |
| `properties` | `Occlusion Map` | |
| `properties` | `Emissive Map` | |

`state` 收的三个字段，恰好就是 glTF 材质在 PBR 参数之外 authored 的全部。判据不是「是不是 PSO 字段」
——按那个判据 `RasterState` 的九个字段全该进来——而是**这个值是材质作者写的，还是 pass 决定的**：
填充模式、深度偏移、深度测试、MSAA 都是 pass 的；混合与深度写入由 `Alpha Mode` 推出，不另外 authored。

**唯一的改名 `Emissive` → `Emissive Color`**：它是个颜色，而旁边就是 `Emissive Strength`；另一个颜色
因子 `Base Color` 把 Color 写出来了；裸的 `Emissive` 与 `Emissive Map` 并排时像是后者的开关。UE 同名。

**`Specular` 保留**——Metallic / Roughness / Specular 是一个自然三元组，只给第三个加限定词会破掉
节奏，而这个字段的语义就是 UE 的。老的 spec/gloss 工作流里这个词指一张高光颜色贴图（Blender 因此把
自己那个改成了 `Specular IOR Level`），歧义留在代码注释里说明。

**`Normal Scale` 与 `Occlusion Strength` 一个 Scale 一个 Strength**，看着不齐，但那是 glTF 的原词，
统一成 Strength 会同时偏离 glTF 和 Unity。

**贴图槽用 `… Map`**，不跟仓库里 `Model Asset` / `Image Asset` 那种 `… Asset`：那两个是「这个组件用的
那个资产」、只有一个；材质的贴图是众多输入之一，`Map` 是这个领域的通用词。

一起冻住的还有 `AlphaMode` 的三个枚举值——落盘写名字不写数值：

```
Opaque / Mask / Blend
```

glTF 规范里是全大写，这里跟仓库现有枚举反射的 PascalCase 一致。

#### 改名的代价与后路

冻结的精确对象是 `.Data<...>("这个字符串")` 里的字符串，**不是 C++ 成员名**——`m_roughness` 想怎么改
怎么改。加字段、删字段、调顺序、改滑条范围都安全，由 0.c 的三条规则覆盖。

**属性名不进任何持久化的哈希。** `AssetId::ComputeHash` 折的是 path / subLabel / type / descriptor，
缓存键折的是 id 的 JSON，两处都不碰文件内容；entt 内部会把注册名哈希成运行期 id，但那不落盘。所以
文件里的属性名是**明文**，改名后用脚本重写 JSON 键即可迁移，场景文件同理。

三条要记的：

- **改名不会报错，会静默丢字段。** 旧键未知被忽略、新键缺失取默认，两条规则各自都正确工作，合起来
  就是那个值悄悄回到默认。没有版本标记，所以「忘了迁移」不会被发现。
- **材质作为 glTF 子资产时，属性名在模型的缓存 payload 里**，而 identity 校验不覆盖它。改名时要
  **bump `GetCacheFormat(AssetType::Model).version`**，整批单元作废重建。
- **文件流出之后跑不到脚本。** 仓库里的 fixture 随时能改，用户工程里的 `.smat` 与别人保存的场景不行。

所以「冻结」的含义是**改名从一次编辑变成一次需要协调的迁移**，不是「从此不能动」。真到那天，比脚本
更便宜的是字段级别名（读时新旧两个键都认、写时只写新的），它是那三条规则的自然延伸，随时能加，
现在不做。

### `.smat` 怎么产生 ✅ 已定

三步，按这个顺序。

**1. 手写。** 第一批 `.smat` 手写，放 `test://`，直接当 `SparkAssetTest` 的输入。先建读侧、拿手写
文件当 fixture；写侧随后由 round-trip 验证——**判据是 JSON 相等**（读手写文件 → 写出 → 再读 →
断言两次 JSON 相等），与阶段 0.c 同一套，不用 `Hash()`。

**2. 从 glTF 材质导出。** Inspector 上一个按钮，把 glb / gltf 带进来的材质变成 `.smat`。

**语义是原地转换**：写文件 → 注册资产 → 把**这个材质实体**的来源从 `model.glb:material/0` 换成
`project://Foo.smat`。共享关系一个字节不动，画面不变。glTF 材质因此是「可改不可存」——改动改的是
运行期实例，只是存不回 glb。

连带两件事，都是有意的：

- **`AssetId → MaterialHandle` 表里旧键删、新键加。** 于是那个 glb 子资产从此没有共享实例了。
- **再拖一次同一个 glb，会新建一个实例，用的是 glb 里的原始参数，不是导出的 `.smat`。**
  导出是「派生出一个新资产」，不是「改写源模型」。用户可能预期相反，所以写在这里。

**内嵌贴图的槽第一版明确拒绝导出。** 内嵌贴图是子资产，而子资产不能被独立加载（`ProcessAsset` 开头
即拒，见「子被直接请求：拒绝，并指向提取」）。导出出来的 `.smat` 若引用 `model.glb:image/3`：

| 什么时候 | 结果 |
|---|---|
| 导出的那次会话 | ✅ 正常——glb 已加载、子资产已发布，`LoadAsset` 命中 Ready 直接返回 |
| 下次启动，只加载这个 `.smat` | ❌ 落到 `ProcessAsset` 被拒，贴图全丢，材质退化成纯 factor |

正确答案是**子资产提取**（把内嵌贴图落成独立文件），独立功能、单独排期。在它到位之前，导出遇到
子资产槽就明确报错并说明原因，**不能默默写进去**；提取做完后把这条拒绝换成调用即可。

外部贴图的 `.gltf` 不受影响——它的贴图本来就是顶层资产。

**3. 资产浏览器新建 `.smat`。** 把默认材质写出去，没有子资产问题。不急，排在导出之后。

**材质编辑器不在本阶段**：它要编辑的东西（一个 shading model 有哪些属性、怎么连线、怎么生成 shader）
依赖「模型声明从哪来」这个未决问题。

### `MaterialHandle` 怎么落盘

`MaterialComponent::m_material` 编码的是「material 上下文里的哪个实体」，不是那个 `uint32_t` 的运行期
值——材质是运行期创建的，下次启动创建顺序不同，同一个数字会指向另一个材质或悬空，与 entt 的 `Entity`
同一类问题。

**形态是字段级标记 + 加载后的重映射 pass**：`MetaFieldTraits` 加一位 `EntityRef`，值照常写成数字，
场景加载器在所有实体建完之后翻译一遍。不走 `JsonOperation`——重映射要的那张「文件键 → handle」表在
场景加载器手里，而 operation 的签名 `bool(const JsonValue&, T&)` 拿不到任何上下文。`Entity` 类型的
字段将来同理。

**写时按 handle 去重，不按内容哈希**，共享关系逐字节保住：一个 50 primitive / 3 材质的模型读回来
仍是 3 个材质（流程 7 的验收标准）。

机制上零新东西：`MaterialExecuteContext::Current()` 已由 `MaterialSystem::InitInternal` 推入。
「先加组件、再反序列化填 handle」也不会打架——阶段 3 落地后 `MaterialComponent` 不再有任何构造
回调，加上去就是 `NullMaterial`，没有东西会去覆盖反序列化填进来的值。

**场景保存不依赖阶段 3**：没有资产背书的材质就是 material 上下文里一个不带 `MaterialAssetRef` 的
实体，照样能存。阶段 3 落地后它多带一个 `MaterialAssetRef`，组件那一侧不用改。

**仍未定：** 常驻默认材质（带 `DefaultMaterialTag`）不该被场景文件复制一份出来——加载时怎么认出
「这条就是默认材质」还没定。

---

## 阶段 4：场景保存

> **文件形态、键、遍历方式已定**，其余待细化。

`SceneSerializer::Save/Load(path)`，由 MenuBar 调用，不进 AssetManager。

### 文件形态：storage-major，多上下文

场景存的是**两个 ECS 上下文**：`WorldContext`（实体 + 组件）与 `MaterialContext`（材质实例 +
`StandardPBR`）。跨引用一条规则：**(上下文, 键)**。

```json
{ "contexts": {
    "world": {
      "entities": [0, 65537, 65538],
      "parents":  {"65537": 0, "65538": 0},
      "components": {
        "TransformComponent": {"0":{...}, "65537":{...}, "65538":{...}},
        "MeshComponent":      {"65537":{...}, "65538":{...}},
        "MaterialComponent":  {"65537":{"Material":4}, "65538":{"Material":4}} } },
    "material": {
      "entities": [4],
      "components": {
        "StandardPBR": {"4":{...}} } } } }
```

**`entities` 清单是显式的，不从各段的键并集推。** 两个理由：一个不带任何组件的实体（纯层级节点）
推不出来；加载必须先把全部实体建完再挂组件（组件里有 entity 引用），有清单就是一趟扫描，没有就要
先并集一遍。

**`parents` 单独成段。** 它不是组件——`Hierarchy` 本身不序列化（见「仍待细化」）——而 storage-major
下没有「实体对象」这个容器可以挂它，所以它得有自己的位置。

#### 为什么翻掉了 entity-major

原先写的决定因素是「场景会被实例化，那个操作的单位是实体」。**这条站不住**：实例化的实质是 merge
——建 `旧键 → 新 Entity` 映射、翻译内部引用——而这两步在两种布局下**完全相同**。entity-major 不解决
merge，只在「枚举文件里有哪些实体」和「建实体 + 挂组件」两头稍顺一点，而前者已被显式 `entities`
清单抹平。

真正的账：

| | storage-major | entity-major |
|---|---|---|
| 与内存布局的关系 | **同构** —— storage 段即 storage | 打散再重组 |
| 写盘 | 遍历 `storage()` 直接写段 | 需要一次 `实体 → [类型]` 的中间聚合 |
| 文件体积 | 类型名每段一次 | 类型名每实体一次（默认值一律写出，差距更明显） |
| material 上下文 | 天然是表 | 给单组件、无层级、不被实例化的实体套两层空信封 |
| 未知组件类型 | 整段可跳过 | 散在各实体里 |
| prefab 实例 | 顶层另开一个结构 | 与普通实体并列在同一数组 |

最后一行是 entity-major 唯一没被抹掉的赢面，但两边都是「文件里有两种结构」，只差挨不挨着，
撑不起一个格式决策。

**曾列进理由、后来撤掉的两条**，记下来免得重提：

- **diff 局部性**（改一个实体的三个组件，entity-major 下 diff 集中在一处）。这是观感，而观感就是
  可读性，本节末尾已经排除。它与「数组下标当键」那条不是一类——后者是**正确性**问题（git 解完文本
  冲突得到一棵静默挂错的树），这条不是。
- **prefab override 与场景布局同构**。它依赖 prefab 是 live 的（见后面一节），而好处仅仅是少维护
  一套寻址逻辑。

分界线也不是「世界快照 vs 可实例化的创作产物」——那个二分是拿 Bevy `DynamicScene` 与 entt `snapshot`
的先例套上来的，但它们的差异来自各自的宿主框架，不来自这条分界。

可读性**不是**理由 —— 美术看到不对只能报 bug，引擎开发者有 debugger 和日志。

### 读写都按 storage-major，没有中间聚合

遍历 `registry.storage()`（`registry.hpp:420`），每个 storage 是稠密的、只装真正拥有该组件的实体，
**遍历即写出**。开销是**组件实例总数**，不是 `实体数 × 注册类型数`。以 1000 实体 / 平均 3 组件 /
20 注册类型算，3000 次对 20000 次，而且后者每次还要过一次反射 `HasComponent` 调用。entt 自己的
`registry::erase_if`（`registry.hpp:824`）就是这个模式。

（entity-major 时这里还要把结果收集成 `实体 → [类型]` 再按实体写出——用一个 map 把已经排好的数据
打散再重组。那一步现在不存在。）

**不用 entt 的 `snapshot`**：它的类型是模板参数（编译期），要用就得有一份静态的世界组件清单，而组件
是各模块 `Reflect.h` 各自注册的；它的 archive 是流式协议且喂进去的值是静态类型的，绕过我们的反射
序列化器（`AssetId` 的 `JsonOperation`、字段级 `Serializable` 全部失效）。它的价值在于示范了循环该
怎么转，不在于直接用。（原先还列了「它的布局是 storage-major」这条反对理由——现在我们也是，作废。）

#### 留着不做：组件值去重

大量同构实体（1000 棵树）的 `MeshComponent` 内容逐字相同，storage-major 下能顺手压掉：

```json
"MeshComponent": { "values": [ {...} ], "entities": {"0":0, "65537":0, "65538":0} }
```

一张值表 + 实体到值下标的映射。**第一版不做**，直接 `{"0":{...}, "65537":{...}}`；将来要压时读侧认
两种形态即可，写侧换个编码，纯加法。（entity-major 下做不了——组件散在各实体里，没有放值表的地方。）

一个接缝：`storage()` 给的 id 是 `type_hash<T>::value()`，而 `.Type("Transform")` 会把 meta 类型的
`id` 改成**名字的哈希**（`factory.hpp:176`），`Resolve(TypeId)` 那条路（按 id 线性扫描，
`resolve.hpp:60`）匹配不上。开场用 `GetAllTypes()` 建一张 `type.info().hash() → MetaType` 表
（meta context 的 map 键始终是 type_info 哈希，`.Type()` 只改 `elem.id` 字段），之后 O(1) 查。

`BasicContext` 要开一个受控的 `ForEachStorage`，不要把 `entt::registry` 整个漏出去。

`IsWorldComponent` 的角色随之改变：**不再是发现组件的手段**（storage 遍历直接给出实体真正拥有的
组件），只剩「该不该落盘」这个过滤职责，而那是类型级问题、归 `ComponentTraits`。

### 键：直接用 entt 的 entity 原值

**不做重映射下标，也不引入额外 id。** `Entity` 是 uint32（低 20 位 id + 高 12 位 version，
`Entity.h:23`），registry 内唯一，天然满足**「显式写出、永不复用、允许空洞」**这三条 —— 而这三条正是
稳定键的全部要求。`registry.create(hint)`（`registry.hpp:516`）在槽空闲时用给定值，所以能原值还原；
entt 的 `snapshot_loader` 就是这么干的。要加的只有 `BasicContext::CreateEntity(Entity hint)` 一个重载。

**为什么不能用数组下标（原文那条已推翻）。** 位置隐含的下标下，插入或删除一个实体会让后面所有实体的
下标平移，**所有指向它们的引用逐个改值**。500 实体的场景删掉中间一个，diff 覆盖半个文件 —— 单人开发
每天都会遇到，跟并发无关。合并时更糟：两个分支各自重写了同一批引用，git 解完文本冲突后得到一棵
**静默挂错的树**（正确答案「两次平移的叠加」在两边的文本里都不存在）。

用键之后这批改动**根本不产生**：`parents` 段里那个 `0` 永远指同一个实体，前面插多少东西都不动它。

**两条加载路径不冲突：**

| | 值怎么处理 | 是否写回原文件 |
|---|---|---|
| 打开场景编辑 | 加载进空世界，`create(hint)` 原值还原 | 会 —— 键不变，diff 稳定 |
| 实例化进已有世界 | 值可能被占；同一场景实例化两次必撞 → 重映射 | 不会 |

需要重映射的不写回，需要写回的不重映射。那张 `文件键 → Entity` 表仍然存在，只是在「打开场景」这条路
上是**恒等映射**。一套机制，一种情况下退化。

`MaterialHandle` 同样用原值当键 —— 它也是 entt 实体，同一条规则。

两个实现注意点：

- **值里带 version。** 删掉一个实体再建，值会变（version+1）—— 那本来就是另一个实体，键跟着变是对的。
- **还原必须在世界为空时做**，否则 `create(hint)` 会静默降级成「随便给一个」。加载路径要么保证空世界、
  要么显式走重映射分支，不能让两者混在一起靠运气。

**已知边界：并发编辑。** 两个分支各自 `create()` 会拿到同一个下一个值，合并后文件里出现重复键。但那是
**响的**失败（加载时重复键 `LOG_ERROR` 拒收），不是无声的错树。要根治只能换成随机分配的独立 id，
那时是加一个字段的事，不推翻结构。

### id 与协作：一条背景结论

讨论中差点把 id 归因错，记一笔：**稳定 id 不是为合并而生的，是为引用而生的** —— 跨文件引用、prefab
覆盖寻址、撤销重做、热重载配对、增量保存。合并只是副作用，而且是个不完整的副作用。

行业实践是**靠拆分解决协作，不靠合并**：Unity 专门做了懂格式的 `UnityYAMLMerge`，正因为普通 git merge
不好使，团队实践是拆多场景 + prefab；UE 的 `.umap` 是二进制、走独占签出，World Partition 的动机之一
就是让人不碰同一个文件；Godot 的 `.tscn` 可合并性被当作卖点，但推荐做法同样是场景实例化拆分。

而「各自编辑不同 prefab、引擎负责无损组合」这条路**需要局部 id 空间**，两者不是二选一而是上下层。
好消息是当前形态已经是那个骨架：**entity 原值就是文件局部的 id 空间**，实例化就是重映射进世界，两个
prefab 各带各的值、天然不冲突。将来做 prefab 要加的是**覆盖数据**（「这个实例改了哪些字段」）与嵌套
引用，不是重做键机制。

### prefab：flatten 还是 live（未定，不阻塞本阶段）

讨论中发现这个从没定过，记一笔：

- **flatten** —— prefab 是生成器，实例化即展开、断开链接，场景里存的就是普通实体。**今天
  `SpawnModel` 就是这个。** 相比现状增量很小：无非是能保存任意实体组合而不只是一个 glb。
- **live** —— 场景里存「原型引用 + 这个实例哪里不一样」，加载 = 原型内容 + 差异。改原型能传播到所有
  已有实例，这才是 prefab 的核心价值。Unity / UE（Blueprint 实例）/ Godot 都走这条。

live 的代价：三方语义（原型改了 + 实例也改了怎么合）、孤儿（原型删掉的部件正好被某个实例覆盖了）、
编辑器要表达 override 状态、嵌套 prefab 的覆盖穿透。

**不影响本阶段的格式选择** —— 两种布局都容纳得下 prefab 实例，storage-major 下顶层另开一个结构即可。
它决定的是阶段 4 之后要不要长出 override 数据，而 override 的寻址是 `(原型内实体键, 组件, 字段)`，
那个「原型内实体键」的骨架已经在了（见上一节）。

### 仍待细化

- **不序列化 `Hierarchy` 组件本身**（四个 entity handle）。只存 `parent`，兄弟次序由数组顺序表达，
  加载走 `IScene::SetParent` 重建。`firstChild` / `prevSibling` / `nextSibling` 是 `parent` + 顺序的
  派生值，存了是第二个真相来源。（键换成 entity 原值之后，原先「强顺序相关」那条顾虑弱了一半，但
  「派生值」这条不变，结论不变。）
- 加载后 `MeshComponent::m_modelAsset` 那个 `Ptr` 谁来填。今天只有 `SpawnModel` 直接赋值。两条路：
  场景加载时同步 `RequestAsset`，或复用已有的 `AssetResolveBus::ResolveAssetToComponent` 异步机制
  （编辑器侧的 `ComponentAssetResolver` 已经在跑）。
- `MaterialHandle` 的重映射 pass 与 `MetaFieldTraits::EntityRef` —— 见阶段 3 那一节。

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
                           ├──► 阶段 2（序列化器铺到组件）──► 阶段 4（场景保存）
                           └──► 阶段 3（材质资产）────────────► 阶段 4 的表项升级

（阶段 4 不依赖阶段 3：没有 asset 背书的材质就是 material 上下文里的一个内联实例，
  照样能存。阶段 3 落地后表项可以升级成 asset 引用，组件那一侧不用改。）

待办 A（通用依赖机制 + 预加载）── 只有方向 ──► shader 缓存

Image 处理流程规整 ✅ ──► 子资产机制统一 ✅ ──┬──► cubemap 缓存 ✅
                                               ├──► Model 缓存（还欠待办 A + 二进制格式）
                                               └──► 阶段 3（材质子资产，机制已就位）
```

## 下一步

阶段 0、阶段 1、Image 处理流程规整、子资产机制统一均已收尾。剩下的互不依赖：

- **待办 A：通用依赖机制 + 预加载**（只有方向）：shader 缓存的前置，也是 `.gltf` 缓存的前置。
- **阶段 2**：四件前置全部完成（拼写校正、`JsonOperation`、默认值一律写出、名字校对），打标记也已
  落地（六个 `Reflect.h` 共 36 个字段 + `Core/Reflect.h` 的数学分量）。剩下的是给那几个还没有测试
  目标的组件找个地方做 round-trip——今天只有 `MaterialSerializeTest` 一处。
- **阶段 3**：资产层与运行期都已落地（见「状态」）。剩下的是编辑器的另外那半：材质窗口、Browser
  刷新与 `.smat` 注册、右键新建 `.smat`、从 glTF 材质导出 `.smat`（流程 2，内嵌贴图的槽拒绝导出，
  等子资产提取）。材质槽的覆盖按钮已随覆盖一起做掉。
- **随阶段 3 记下的一笔债**：材质子资产没有 `Serialize`，而缓存单元里**任何一个 payload 为空就整个
  单元不落盘**（`AssetCache::WriteUnit`）。今天不发作，因为 `AssetType::Model` 本来就不可缓存；等
  Model 转可缓存那一步，得先给 `MaterialAssetBuilder` 补上 `Serialize` / `Deserialize`——独立 `.smat`
  该不该有缓存条目，和它在单元里能不能被序列化，是两件事。
- **阶段 4** 的文件形态、键与遍历方式已随阶段 2 的讨论定下（见该节），但它依赖阶段 2 与阶段 3。
