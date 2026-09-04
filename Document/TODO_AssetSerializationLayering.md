# 资产序列化的分层（Serialize 解绑 Cache / Save 上总线）

## 状态

**方案已定，未动工。** 与材质资产（`TODO_AssetSystemPlan.md` 阶段 3）解耦，可单独排期。

材质保存这条线按**当前实现**继续走（见「过渡形态」一节），不等这个重构。

---

## 背景与问题

`AssetBuildBus` 上今天有五个虚函数：

```cpp
virtual Ptr<Asset> CreateAsset(const AssetId& id) = 0;
virtual void       Load(AssetBuildContext& ctx) = 0;
virtual void       Compile(AssetBuildContext& ctx) = 0;
virtual eastl::vector<uint8_t> Serialize(const AssetData& compiled, eastl::string_view identity);
virtual UniquePtr<AssetData>   Deserialize(const uint8_t* bytes, size_t size, eastl::string_view identity);
```

后两个的注释写着「The cook cache's two format halves」——**序列化被钉死在缓存上**。三个后果：

1. **材质只好另造一套。** 材质要把 `MaterialAssetData` 写成 `.smat` 字节，这就是序列化，
   但那条路被缓存占着（`identity` 参数是缓存的，`.smat` 不能带它），于是有了平行的
   `WriteMaterialAsset` 自由函数。同一件事，两个概念。

2. **「能不能序列化」和「要不要缓存」混成了一个问题。** 材质不缓存的理由是收益问题——
   `.smat` 是 JSON，编译形态几乎就是它自己的一次 parse，缓存一条等于花一次文件读省一次
   文件读（见 `Cache/CacheFormat.cpp` 的注释）。那是**用途的取舍**，不是「材质没有序列化
   能力」。现在的接口让这两件事分不开。

3. **带副作用的操作从 per-type 头漏出去。** 材质保存最后落成 `Resource/Material/` 下一个
   自由函数，编辑器直接 include 它。已有的 per-type 暴露（`AssetIdToDisplayString`、
   `StandardPBR`、`DescriptorForUsage`）都是**类型和纯函数**，而这个是**会写文件、会往资产库
   塞条目的操作**。每种资产都这么来一下，`AssetManager` 作为「唯一的门」就名存实亡了。

---

## 核心划分

**序列化是每个资产的基础能力；缓存和写源文件是它的两个用途，各自多一步。**

```
Serialize(AssetData) → bytes          ← 基础能力，每个类型都有
Deserialize(bytes)   → AssetData

缓存     = Serialize + identity + 键 + cache:// 落位
写源文件  = Serialize + 合法性判断 + 路径 + 注册
```

多出来的那一步**都可能因资产类型而异**，但异的方式不同：

| | 缓存多的那一步 | 写源文件多的那一步 |
|---|---|---|
| 是什么 | identity、键、落位 | 合法性判断（材质：内嵌贴图不能写；将来：自动提取） |
| per-type 吗 | **否**，纯策略 | **是**，要看具体数据 |
| 已有的表达 | `GetCacheFormat(type)` 声明扩展名 + 版本 | 无 |
| 结论 | 留在 `AssetCache`，不上总线 | 上总线 |

---

## 证据：identity 是漏进来的，不是必需的

两处代码说明当前绑定是历史造成的：

**一、`identity` 在格式里已经是可选的。** `ImageAssetCompiler::SerializeToKtx2` 是
`if (!identity.empty())` 才写那个 KV 对。传空串进去，出来就是一个干净的 `.ktx2`。
「不带缓存包袱的纯序列化」今天就能跑，只是签名上还挂着那个参数，没人这么用过。

**二、identity 在 manifest 里已经独立存了一份。** `AssetCache::ReadUnit` 读 manifest 时
就做了同样的比对，注释说它「covers the manifest itself, which has no format of its own to
hide an identity in」。

而**真正的威胁是键碰撞**：两个不同资产哈希到同一个键。A 写了 payload + manifest，B 用同一个
键查过来，读到 A 的 manifest，identity 不符，miss——**在 manifest 那一层就挡住了**。
payload 里那一份能多挡的只有「manifest 和 payload 对不上」，而 payload 先写、manifest 最后写
（manifest 的存在即完整性标记），那个状态到不了。

**所以 payload 级的 identity 是冗余的，可以整个拿掉。** 实施时需要再确认一遍这个推理。

---

## 目标形状

```
AssetBuildBus   ── 资产模块内部的 per-type 行为表，外部不可见
  CreateAsset / Load / Compile        构建
  Serialize / Deserialize             格式（不带 identity）
  <写源文件的钩子>                     合法性判断，将来是提取

AssetManager    ── 唯一对外的门
  LoadAsset / RequestAsset / FindAsset / ...
  WriteAssetFile(path, bytes) → AssetId       字节 → 已注册的资产（已实现）
  SaveAsset(type, data, path) → AssetId       组合好的动作，编辑器只调这个

AssetCache      ── 内部的一个用途，策略集中在这里
  = Serialize + identity + 键 + cache:// 落位
```

**编辑器从此不 include 任何 per-type 的函数**，也不知道 `.smat` 是 JSON。类型还是要 include
（得能造出 `MaterialAssetData` 才有东西可存），但那是数据，不是操作。

这比「per-type 头只许暴露类型和纯函数」那条**规则**强：规则要靠人每次记得执行，这个是结构。

---

## 三件事

### A. `Serialize` / `Deserialize` 去掉 `identity`

- `AssetBuildBus` 改签名。
- `ImageAssetBuilder` / `ImageAssetCompiler::SerializeToKtx2` / `ImageAssetLoader::LoadKtx2`
  拔掉 identity 管线。
- `AssetCache` 只靠 manifest 那一份验身。
- `GetCacheFormat(Image)` 版本号 `2 → 3`，旧缓存条目自然作废重建。
- **`MaterialAssetBuilder` 实现 `Serialize` / `Deserialize`**。`WriteMaterialAsset` 从「材质
  专有的平行概念」变成「材质这个类型的格式实现」——和 `ImageAssetCompiler::SerializeToKtx2`
  同一个位置、同一个角色。

**`GetCacheFormat` 仍然是「要不要缓存」的唯一决定者。** 材质实现了 `Serialize` 不等于材质会被
缓存，它继续返回 `{}`。能力和用途的取舍就在这里分开。

顺带可考虑（不强求）：按对称性，`WriteMaterialAsset` 更该是 `MaterialAssetCompiler` 上的一个
方法而不是自由函数。原设计选自由函数的理由见 `TODO_AssetSystemPlan.md`「写侧的形态」。

### B. 写源文件上总线 + `AssetManager::SaveAsset`

- 总线加一个每类型可覆写的钩子。**名字待定**：`CanSave` 太弱——将来提取要在这一步**写出
  额外的贴图文件并注册**，是有副作用的，名字和签名要容得下那个未来（`PrepareSourceWrite`
  之类，并且要能拿到 `AssetManager` 来写兄弟文件）。
- 多数类型接受默认「可以写」，材质覆写一个检查。
- `AssetManager::SaveAsset(type, data, path)` = 钩子 → `Serialize` → `WriteAssetFile`。
- `Serialize` 随之**收成内部**：只有 `AssetCache` 和 `SaveAsset` 调它，都在模块内。

### C. 缓存维持现状

`AssetCache` 的类注释是这条的依据：

> Cache policy in one place -- what the key is made of, where an entry lands.
> **Builders own only their own format and never see a path or a key.**

缓存的 per-type 部分已经被 `Serialize` + `GetCacheFormat` 表达完了。把 `Cache` 做成总线事件
等于让每个 builder 各自实现落位和键，正好把上面这条推翻。

所以 `Create → Load → Compile → Cache/Save` 这个序列读起来顺，但 **Cache 那一格里没有
per-type 的东西可填**，不成立。

---

## 与已有决策的关系

`TODO_AssetSystemPlan.md`「`.smat` 怎么产生」里否掉了 `AssetBuildBus::WriteSource`，理由是：

> 缓存是通用流程……而写回源文件不是任何通用流程的一环，`ProcessAsset` 永远不会调它。
> 给总线加一个核心流程从不调用、且只有一个实现者的虚函数，就是为一个用例造机制。
> **而且第二个用例不存在**。

**这一条本方案推翻了一半，要说清楚哪半。**

- **仍然成立**：`ProcessAsset` 确实永远不调它；第二个实现者今天确实不存在（图片被提取写出去时
  没有什么可判的，它会接受默认）。
- **不再成立的是那个结论**。原判断把总线看作**流水线**，于是「不在流水线上」就等于「不该在
  总线上」。但总线实际上是**per-type 行为的分派表**——`Serialize`/`Deserialize` 也不在
  `ProcessAsset` 的直接调用链上（它们经 `AssetCache`），照样在表上。
- 而且这次买到的东西不一样：不是「通用性」，是**对外表面收口**。往一张已经存在的行为表上加
  一格，和「为一个用例新造一套机制」不是一回事。多数类型接受默认、一个类型覆写，是分派表的
  正常形状。

---

## 未决

**具体的失败原因怎么传到 UI。** 现在 `MaterialSaveResult::EmbeddedTexture` 能让编辑器说出
「这个材质用了内嵌贴图」，而 `TODO_AssetSystemPlan.md` 要求「明确报错并说明原因」。检查藏到
总线后面之后，`SaveAsset` 只能返回通用的 `{Ok, Rejected, Failed}`，具体原因在日志里。

两条路，做的时候定：接受「无法保存，详见日志」（和「错误信息统一走日志、怎么显示是编辑器的
事」这条原则一致），或者在总线契约上带一个小的原因码。

---

## 过渡形态（当前实现）

材质保存已经按下面这个形状落地，**本重构到来前不改**：

| 现在 | 重构后 |
|---|---|
| `Resource/Material/MaterialAssetSaver.h` 的自由函数 `SaveMaterialAsset` | 消失；拆成总线钩子 + `AssetManager::SaveAsset` |
| `MaterialSaveResult` 三值枚举 | 变成通用的 `{Ok, Rejected, Failed}`（或带原因码） |
| `WriteMaterialAsset` 自由函数，`SaveMaterialAsset` 调 | 变成 `MaterialAssetBuilder::Serialize` 的实现 |
| 内嵌贴图检查在 `MaterialAssetSaver.cpp` 的静态函数里 | 移到总线钩子的材质实现里 |
| `AssetManager::WriteAssetFile` | **不变**，两个世界里都是那个通用原语 |

编辑器侧的调用点只有材质窗口的 Save / Save As / New 三处，届时一起改。

---

## 影响面

动的是**已经在跑、有测试覆盖**的缓存子系统（`SparkAssetTest` 的 CacheTests）。改完要跑：

- `ctest --test-dir build -C Debug`（四个套件）
- 重点看 CacheTests 与 `MaterialAssetTests`（round-trip 判字节相等、每字段非默认值比对）
- 手动验一次冷/热缓存：删掉 `cache://` 后首次启动重建，第二次启动命中
