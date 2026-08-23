# 资产系统补齐计划（路径身份 / 磁盘缓存 / 材质资产 / 场景保存）

> **阶段 0 已细化到可实施；阶段 1~4 仍是大致方向**，标了「待细化」的地方还没定，
> 不要当成已决方案实现。

## 背景

资产系统的骨架是齐的——`AssetId` → `AssetDataBase` → worker 线程 → 按 `AssetType` 走
`AssetBuildBus` 的 Load/Compile。欠的是三笔债：

1. **材质不是资产**——`MaterialParams` 只活在运行时的 `MaterialContext` 里，没名字、没文件、
   不能跨场景共享，Inspector 里改完退出就没了。
2. **没有磁盘缓存**——每次启动重解 PNG、重跑 BC 压缩、重跑 DXC、重解 glTF + meshopt、重烘 HDRI。
3. **场景存不了**——`MenuBar.cpp:24-32` 三个菜单项都是 `LOG_INFO` 空壳。

三件事共享同一个前置：**资产引用得能被写进文件、再读回来变成一个可用的 `AssetId`**。

---

## 现状盘点

### 已经有的

- **Load/Compile 之间的缓存缝**。`AssetManager.cpp:296-307`：Load 若直接给回 `ctx.compiledData`，
  Compile 整段跳过。今天服务 `.ktx2`，将来服务 cache hit。
- **图片的编解码两头**。`ImageAssetCompiler.cpp:411` 已经算出 ktx2 blob（只用来打了条 log 就扔了）；
  `ImageAssetLoader.cpp:244` 已经能读 ktx2。
- **子资产机制**。`AssetId::OfSub` + `ImageAssetBuilder::PublishSubAsset`，glTF 内嵌图片和 IBL
  烘焙产物都走这条路。
- **场景序列化所需的反射面**。`Reflection/Utility.h:86` 的 `ComponentOperation` 把 `HasComponent` /
  `GetComponent` / `AddOrReplaceComponent` / `IsWorldComponent` 注册成了 context-free 的反射函数。
  现有 7 个世界组件（Name / Transform / Mesh / Material / Camera / Light / Skybox）无需新增注册。

### 核心欠账：`AssetId::m_path` 同时存着三种不同的东西

| 来源 | 存进 `AssetId` 的路径 | 例子 |
|---|---|---|
| 编辑器（`Editor.cpp:38`、`Engine.cpp:81` 传 CWD 相对目录） | CWD 相对 | `Engine/Asset/foo.png` |
| 测试 / SandBox（CMake 宏是 `${ENGINE_ROOT_DIR}/Asset`） | 机器绝对路径 | `D:/SparkEngine/Engine/Asset/foo.png` |
| glTF 外部贴图（`ModelAssetBuilder.cpp:203`） | 未解析的 glTF 相对 URI | `textures/wood.png` |

第三种的可解析性依赖调用时的带外上下文——靠 `DispatchImageSubAsset` 把模型所在目录临时插进
child 的搜索路径头部（`ModelAssetBuilder.cpp:69-72`）才成立，离开那个 context 即是死引用。

同一个 `Engine/Asset/BRDFLut.ktx2`，编辑器与测试算出的 `AssetId` 不同。

### 其余欠账

| 欠账 | 位置 | 说明 |
|---|---|---|
| descriptor 不可重建 | `Resource/AssetTypes.h` | `AssetDescriptor` 只暴露 `Hash()` |
| 磁盘缓存 | 无 | 见阶段 1 |
| 内存驻留无淘汰 | `AssetDataBase.h` | map 持强 `Ptr<Asset>`，refcount 永不归零，`Asset.cpp:17` 的 `Shutdown`→`ReleaseAsset` 够不着 |
| 材质无资产形态 | `Feature/Material/` | 唯一创建点是 `SpawnModel.cpp:111` |
| 三个平行的 CPU 材质结构 | `ModelAsset.h:46` / `Material/Components.h:45` | `Resource::Material` 与 `MaterialParams`，靠 `MaterialParamsFromModel` 搭桥 |
| 无 JSON 库 | — | simdjson 作为 fastgltf 依赖存在，但只读 |
| 场景序列化 | 无 | 见阶段 4 |

---

## 已定决策

1. **路径即身份，不上 GUID。** 存储形式为复合对象，预留 `guid` 字段位：将来若加，`guid` 为主键、
   `path` 为 fallback。
2. **虚拟路径 + 命名挂载点**，语法 `mount://relative`。见阶段 0.a。
3. **`AssetId` 的持久化形式是 JSON 复合对象。** 单一字符串仅作单向显示形式（log、Inspector 只读框），
   不用于解析。第一版不提供「全默认值退化成裸字符串」的短形式。
4. **材质走子资产路线**（阶段 3）——glTF 内嵌材质由 `ModelAssetBuilder` publish 成
   `model.glb:material/0`，不给 `.smat` 另起结构。
5. **JSON 使用 vendor 的 `nlohmann/json.hpp` 单头文件。**
6. **挂载表独立成 `Core/VFS/` 模块**，namespace `Spark`（与 `Core/SceneManager/` 的
   `IScene` / `SceneManager` 同构，不设子命名空间）。接口 `FileSystem`，实现 `MountTable`，
   系统 `VFSSystem`。

---

## 阶段 0：路径身份

拆成三个子问题，依赖面不同：

| 子问题 | 阶段 1 缓存 | 阶段 3 材质 | 阶段 4 场景 |
|---|:--:|:--:|:--:|
| 0.a 虚拟路径 + 挂载点 | ✅ | ✅ | ✅ |
| 0.b descriptor 可重建 | ❌ 只需 `desc->Hash()`，已有 | ✅ | ✅ |
| 0.c 复合存储形式 | ❌ | ✅ | ✅ |

阶段 1 只依赖 0.a，可在 0.a 完成后立刻开工。

### 0.a　VFS 挂载点

核心不变量：

> **`AssetId::m_path` 永远是虚拟路径 `mount://relative`，不存在第二种形态。**
> 物理路径只在真正读文件的那一刻出现，且只经过 `FileSystem` 一处。

#### 模块结构

```
Engine/Code/RunTime/Core/VFS/
    FileSystem.h        class FileSystem  —— 接口，Service 的键
    MountTable.h/.cpp   class MountTable : public FileSystem  —— 实现
    VFSSystem.h/.cpp    class VFSSystem : public ISystem,
                                          public Service<FileSystem>::Handler
```

三者分工：`FileSystem` 是能干什么，`MountTable` 是怎么实现的，`VFSSystem` 是谁持有它、
什么时候起来。`MountTable` 是普通可构造对象，自身不注册 Service。

#### 接口

```cpp
namespace Spark
{
    class FileSystem
    {
    public:
        virtual ~FileSystem() = default;

        virtual void Mount(eastl::string_view name, eastl::string_view physicalDir) = 0;
        virtual void Unmount(eastl::string_view name) = 0;

        //! 物理/宽松路径 → 虚拟路径。最长前缀匹配所有挂载点。
        //! 匹配不到：返回空串 + LOG_ERROR（报出物理路径与当前挂载表）。
        virtual eastl::string ToVirtual(eastl::string_view physicalPath) const = 0;

        //! 虚拟路径 → 物理路径。查表，不搜索。
        virtual eastl::string ToPhysical(eastl::string_view virtualPath) const = 0;

        //! 编辑器资产浏览器 / AssetRegistry —— 取到名字后全程走虚拟路径。
        virtual eastl::vector<eastl::string> GetMountNames() const = 0;

        //! 仅 DXC include。按挂载注册顺序返回，保留「先命中先赢」语义。
        virtual eastl::vector<eastl::string> GetPhysicalDirs() const = 0;

        //! 遍历一个挂载点，回调收到的是虚拟路径（相对部分直接拼到挂载名后，不走 ToVirtual）。
        virtual void IterateDirectory(eastl::string_view virtualDir,
                                      eastl::function<void(eastl::string_view)> visit) const = 0;

        // 阶段 1 追加：ReadFile / WriteFile / Exists
    };
}
```

- `ToPhysical` 是查表，不遍历搜索。
- 两个挂载点指向重叠的物理目录时，`Mount` 检测并报错。
- `ToVirtual` 匹配失败时上游 `MakeAssetId` 返回无效 `AssetId`，与它今天找不到文件时的行为一致
  （`AssetManager.cpp:356-360`）。

#### 获取方式

| 谁 | 怎么拿 |
|---|---|
| `SparkAssetManager`（`MakeAssetId` / `AssetRegistry`） | `Service<FileSystem>::Get()`，`InitInternal` 取一次存下并 ASSERT |
| Builder / Loader / `ShaderAssetCompiler` | `AssetBuildContext::fileSystem`（`const FileSystem*`，与已有的 `db` 同构） |
| 编辑器资产浏览器 | `Service<FileSystem>::Get()` |
| 阶段 1 缓存 / 阶段 4 场景保存 | `Service<FileSystem>::Get()` |
| 单测 | 局部 `MountTable`，按 `const FileSystem*` 传入 |

`VFSSystem` 在 `Engine.cpp` 里紧跟 `SpdLogSystem` 之后创建，挂载点注册随之从 AssetManager 移走：

```cpp
m_vfs = CreateSystem<VFSSystem>();
m_vfs->Init();
m_vfs->Mount("engine", "Engine/Asset");     // 取代 m_assetManager->AddSearchPath(...)
```

`AssetManagerInterface.h` 上的 `AddSearchPath` / `RemoveSearchPath` / `GetSearchPathes` 三个删除。

#### 挂载点划分

| 挂载 | 物理目录 | 内容 | 谁挂 |
|---|---|---|---|
| `engine://` | `Engine/Asset/` | Shaders、BRDFLut、Shaderball —— 引擎自带 | `Engine.cpp` |
| `project://` | `Project/Asset/`（新建，仓库根，与 `Engine/`、`SandBox/` 平级） | 用户内容；将来的 `.smat` / `.scene` | `Editor.cpp` |
| `editor://` | `Engine/Code/Editor/Asset/` | 编辑器 UI 图标（7 个 svg） | `Editor.cpp` |
| `test://` | `Engine/Code/Test/Resource/Asset/` | 测试资产 | 各测试 |
| `sandbox://` | `SandBox/Asset/` | SandBox 程序 | SandBox |

要动的文件：

- `Engine/Asset/*.glb`（除 `Shaderball.glb`）+ 4 张未跟踪 HDR → `Project/Asset/`
- `Engine/Code/Editor/Asset/DECWood-redoak.cgfind.cn.glb` → `Project/Asset/`；该目录余下的 7 个
  svg 是编辑器自己的 UI 资源（`BottomPanel.cpp:203-209` 在用），留在原地作为 `editor://`
- `Project/Asset/` 加 `.gitignore`，这些大文件不进 git
- 编辑器改挂 `project` + `editor`，不再挂测试资产目录（`Editor.cpp:38`）
- CMake 宏（`ENGINE_ASSET_DIR` 等）保持绝对路径不变

#### glTF 外部 URI 改为纯词法解析

父模型的虚拟路径已知（`ctx.id.GetPath()`），子贴图 URI 相对于它：

```
engine://Models/chair.gltf  +  textures/wood.png
        → engine://Models/textures/wood.png
```

纯字符串拼接，不碰文件系统。`DispatchImageSubAsset` 的 `extraSearchPaths` 参数、
`child.searchPaths.insert(...)`、以及 `MakeChild` 传 searchPaths 的必要性全部删除。

#### `searchPaths` 管道拆除

| 现在 | 之后 |
|---|---|
| `AssetBuildContext::searchPaths` | 换成 `const FileSystem* fileSystem`（与已有的 `db` 指针同构） |
| `ResolveAssetPath(path, searchPaths)` | 删除，由 `FileSystem::ToPhysical` 取代 |
| `ImageAssetLoader` / `ModelAssetLoader` / `BinaryAssetLoader::SetSearchPaths` | 三个全删 |
| `AddSearchPath` / `RemoveSearchPath` / `GetSearchPathes`（`AssetManagerInterface.h:32-34`） | 三个删除，挂载点注册移到 `VFSSystem` |
| Loader 内部的 `ResolveAssetPath(id.GetPath(), m_searchPaths)` | → `ToPhysical(id.GetPath())`。`std::ifstream` 与第三方调用不动——`ktxTexture2_CreateFromNamedFile`、`stbi_load`、`fastgltf::GltfDataBuffer::FromPath` 及其 `baseDir` 都取物理路径 |
| `AssetRegistry()` 的 `fs::recursive_directory_iterator`（`AssetManager.cpp:369-396`） | → `GetMountNames` + `IterateDirectory(mount://)`，直接产出虚拟路径 |
| 编辑器资产浏览器（`BottomPanel.cpp:511-519`） | 树根名 = 挂载名，`fullPath` = 虚拟路径，枚举走 `IterateDirectory`；获取方式从 `Service<AssetManager>` 改为 `Service<FileSystem>` |
| `SnapshotSearchPaths()`（`AssetManager.h:64`） | 删除 |

**保留的例外**：`ShaderAssetCompiler` 给 DXC 的 include 目录列表（`ShaderAssetCompiler.cpp:74-77`、
`:266`）走 `FileSystem::GetPhysicalDirs()`，保留遍历搜索语义。

#### 实施顺序

1. `Core/VFS/`（`FileSystem` + `MountTable` + `VFSSystem`）+ 单测。不动 `Resource/`。
2. `Project/Asset/` 建目录、挪文件、加 `.gitignore`，`Editor.cpp` 改挂载目录。不碰 VFS。
3. VFSSystem 接入；`AssetBuildContext::searchPaths` 换成 `const FileSystem*`；loader 翻到
   `ToPhysical`；约 33 个字面量路径调用点改虚拟路径；修 `ImageAssetTests` 的挂载重叠。
4. glTF 外部 URI 改词法解析，删 `extraSearchPaths`。

第 3 步不能再拆成「先改注册点、再翻 loader」：注册一旦移到 `Mount`，AssetManager 就不再有
`searchPaths` 喂给 `AssetBuildContext`，而 loader 还在读它——编得过但跑不起来。

第 3 步的 33 个调用点分布：编辑器图标 7（`OpenIcon("folder.svg")` 这类裸文件名同样依赖搜索
语义）、引擎 shader 8、`EnvironmentBaker` 4、SandBox 12，另有约 15 处测试注册。

### 0.b　descriptor 可重建

descriptor 是身份的一部分，必须能 round-trip：同一张 `Wood.png` 拖到 Base Color 是
`Texture2D`(sRGB)、拖到 Normal 是 `NormalMap`(Linear)、拖到 Occlusion 是
`NoColorTexture2D`(Linear)——三个不同 `AssetId`、三份不同编译产物（`ComponentView.cpp:375`）。
只存路径会让加载时退回默认的 `Texture2D`，法线贴图按 sRGB 解出来且不报错。

方案：**规范 descriptor 表 + 短 key**。

- `AssetDescriptor` 加虚函数 `DescriptorKey()` 返回短字符串。
- 建 `(AssetType, key) → Ptr<AssetDescriptor>` 的规范表。Image 注册 6 个 usage
  （`ImageAsset::DescriptorForUsage` 今天已经是这些单例）；Model / Shader 各注册一个 `default`。
- 非规范 descriptor 的 round-trip 失败时报错。真需要时按决策 3 加子键升级。

### 0.c　复合存储形式

```json
"m_modelAssetId": {
    "path": "project://Model/Furniture.glb",
    "sub":  "image/3",
    "desc": { "key": "NormalMap" }
}
```

`AssetManager` 提供 `AssetIdToJson` / `AssetIdFromJson`，外加单向的 `AssetIdToDisplayString`。

**待细化：** 相对路径归一化规则（大小写、分隔符）；`sub` 为空、`desc` 为规范默认时是否省略键。

---

## 阶段 1：磁盘 cook 缓存

> 大致方向。只依赖 0.a。

在 `AssetBuildContext` 上加两个字段（与已有的 `db` 同构）：预先算好的 `cacheKey` 和一个
`AssetCache*`。Builder 在自己的 `Load` 开头试读缓存，命中就填 `ctx.compiledData`，走
`AssetManager.cpp:296-307` 已有的契约，状态机不动。

- **key** = hash(`AssetId::GetHash()` + 源文件 mtime/size + per-type builder 版本号)。
  0.a 完成后 `GetHash()` 已经稳定，不需要文本形式。
- **落盘**：`Cache/<hh>/<key>.blob`，blob 头部复写 key 的输入做自校验。
- **实现顺序**：Image → Shader → Model。

**⚠️ 硬约束：** 跳过 Compile 会跳掉它的副作用。环境立方图在 Compile 里 publish irradiance /
prefiltered 两个子资产（`AssetManager.cpp:296`、`ImageAssetCompiler.cpp:452`）。environment 的
缓存项必须是三个 blob 的 bundle，命中时由 `ImageAssetBuilder` 整体重新 publish。同时
`AssembleCubemapData` 的 `m_mips` 现在只是 base-mip 占位（描述不了 face-major 布局），
做 cubemap 缓存时一并修。

**不做：** 内存驻留淘汰。只加一个手动 `PurgeUnreferenced()`。

**待细化：** 缓存目录是否做成 `cache://` 挂载；失效/清理入口；Model blob 格式。

---

## 阶段 2：反射驱动的序列化器

> 大致方向。写一次给 `.smat`、`.scene`、以后的 prefab 共用。

- vendor `nlohmann/json.hpp`。
- `Core/Serialization/` 加 `SerializeValue(MetaAny, Writer&)` / `DeserializeValue`：遍历
  `MetaType::data()` 递归，对少数 leaf 类型特判——`Math::Vector3/4`、`Matrix4X4`、
  `eastl::string`、`Resource::AssetId`（走 0.c）、`Entity`、`MaterialHandle`。
- 只序列化反射过的字段。`Serialization/MetaTypeTraits.h` 加一位 `Transient`（现在只有 `Editable`），
  并给 `Mesh/Reflect.h:23-26` 的 `m_vertexCount` / `m_triangleCount` 打上。

**待细化：** 容器类型（`eastl::vector` / `eastl::array`）怎么走 entt meta container；
版本化与字段增删的兼容策略。

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

**`AssetHash` 是 32 位的。** `AssetHash = ObjectName::Hash = entt::hashed_string::hash_type =
uint32_t`；`AssetId::ComputeHash` 内部用 64 位 `hash_combine_raw` 之后 `static_cast` 截回 32 位。
`AssetTypes.h:85` 的 `operator==` 是纯哈希比较，不比路径——两个资产哈希相撞即被静默视为同一个。
持久化 id 并用它做磁盘缓存的键之后，撞一次的后果从「这次运行画错」变成「缓存里永久存着错数据」。

两个选项，阶段 0 动这块时一并处理：

- `AssetHash` 提到 64 位；
- 或 `operator==` 退回比 `(path, subLabel, descHash)` 实值，哈希只做桶索引（需确认有无性能敏感的比较点）。

---

## 依赖关系

```
阶段 0.a（VFS 挂载点 / 虚拟路径）
   ├──► 阶段 1（磁盘缓存）          ← 只依赖 0.a，可并行开工
   └──► 阶段 0.b（descriptor 可重建）
           └──► 阶段 0.c（复合存储形式）
                   └──► 阶段 2（序列化器 + JSON）
                           └──► 阶段 3（材质资产）
                                   └──► 阶段 4（场景保存）
```

## 状态

**全部未开始。** 下一步：`Core/VFS/`（`FileSystem` + `MountTable` + `VFSSystem`）+ 单测
（阶段 0.a 第 1 步）。
