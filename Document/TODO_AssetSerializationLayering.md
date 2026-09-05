# 资产序列化的分层（Serialize 解绑 Cache / Save 上总线）

## 状态

**B 已落地**，随 `TODO_AssetSystemPlan.md` 阶段 3 的材质保存一起做——保存对话框必须按类型分派，这
正是那个钩子存在的理由（见「与已有决策的关系」）。原先「与阶段 3 解耦、单独排期」的判断不成立：不是
阶段 3 等这个重构，是它的 B 部分成了阶段 3 的前提。

- **A**：只做了 `MaterialAssetBuilder::Serialize`，而且它**在收到 identity 时拒绝**——缺 `Deserialize`
  的那一半，写出去的缓存单元没人能恢复。去掉 `identity` 那一串（Image 管线、`AssetCache` 验身、
  `GetCacheFormat(Image)` 版本号）**未动**。
- **B ✅**：`PrepareToSave` 上总线（材质覆写内嵌贴图检查）、`AssetManager::SaveAsset`、
  `WriteAssetFile` 转私有、`MaterialAssetSaver` 删除、`AssetBus::OnAssetSaved`。
- **C**：未动。

「过渡形态」一节作废：`MaterialAssetSaver` 不经过渡直接删掉了。

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

## 三层数据流（保存的形状由它决定）

```
文件  ──①──►  Asset 对象  ──②──►  运行期组件
```

- **① 文件 → Asset**：资产系统的事（加载；将来的热重载让它自动发生）。
- **② Asset → 组件**：`Resolve` 那一步的**拷贝**——材质实体拿到值的副本，此后两份各走各的。
- **绝大多数运行期逻辑改的是组件**，材质窗口也是（渲染读的就是组件）。

**Asset 是权威副本，组件是它的运行期投影。** 所以保存永远是「先把组件的值放回资产，再把资产写成
文件」，不是「把组件直接写成文件」：

| 保存形态 | 资产从哪来 |
|---|---|
| 原地保存（`Save`） | 资产就在手边，把组件的值写回它 |
| 另存 / 新建（`Save As` / `New`） | 现造一个**临时资产**装组件的值 |

两条路交出来的都是一个 `Asset`，于是**保存对话框只需要认识 `Asset`**——不需要认识材质、场景或
任何具体类型，也不需要为「要存的东西」发明一个通用载荷。那个载荷是这套设计绕开的东西：它今天
只有一个样本（材质实体句柄），怎么定都是在为一个用例造抽象，而第二个类型来的时候多半不合身。

组件→资产这一步确实是 per-type 的（要认识 `StandardPBR` / `MaterialState`），但它发生在**进入
通用流程之前**，留在发起方那一侧，不需要任何抽象。`Asset::SetDataReady` 就是它的落点，
`MaterialAssetData(StandardPBR, MaterialState)` 那个公开构造本来就是为它加的。

两处后果，实施时记住：

- **原地保存里，组件写回资产发生在写盘之前**。写盘失败时 DB 里那份比文件新——那是正确状态
  （一个未落盘的资产），不是 bug。顺带把 `TODO_AssetSystemPlan.md` 记的那笔「覆盖保存后 DB 里
  那份 `MaterialAssetData` 会变旧」的债还掉了：原地保存之后它是新的。
- **另存之后 DB 里会有两份**：`WriteAssetFile` 内部的 `RegisterFile` 给新路径建了一个
  `NotLoaded` 的资产对象，而临时资产才装着正确的值。第一版丢掉临时的，下次 `Resolve` 从文件读
  （另存后活的材质实体本来就在，没人会立刻去读它）。真正的「文件写完即同步到 Asset 对象」是
  ① 那一格的事，落地时它的位置就在 `SaveAsset` 里——把数据直接发布给刚注册的那个实例，省掉一次
  磁盘往返。

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
  PrepareToSave                       合法性判断，将来是提取

AssetManager    ── 唯一对外的门
  LoadAsset / RequestAsset / FindAsset / ...
  SaveAsset(const Asset&, path) → AssetId     保存的唯一入口
  （WriteAssetFile 私有：绕过它就绕过了钩子和格式）

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

- 钩子叫 **`PrepareToSave(AssetData&, virtualPath) → bool`**。不叫 `CanSave`：提取将来要在这一步
  **写出贴图文件并改写 id**，是有副作用的，所以数据是可变的。写兄弟文件要的入口还不存在
  （`WriteAssetFile` 是私有的），到时候作为参数加在这里。
- **钩子只做校验/准备，不产出字节**，字节仍归 `Serialize`。分开是因为将来提取要改的是前者，格式要
  改的是后者。
- 多数类型接受默认「可以写」，材质覆写一个检查（内嵌贴图）。
- `AssetManager::SaveAsset(const Asset&, path)` = 扩展名定类型 → 钩子 → `Serialize` → 写文件。
  **它是唯一入口**：`WriteAssetFile` 转成私有，因为直接写字节会绕过前两步，那样材质那条拒绝就一文
  不值。`Serialize` 也随之收成内部——只有 `AssetCache` 和 `SaveAsset` 调它。

**保存成功广播一条 `AssetBus::OnAssetSaved(AssetId)`**（按类型寻址）。执行者是对话框，它不认识发起
方，所以发起方只能靠通知知道「存成了」，而它确实有事要做：材质窗口要改 `MaterialAssetRef`、清
`Modified`、刷 `Revert` 快照，`New` 那条则是把新文件 `Resolve` 出来并切过去。

三点定死：

- **载体是 `WriteAssetFile` 那个咽喉，不是 `FileEventBus`。** 文件监视报告不了失败（拒绝、写盘失败
  都不产生文件事件）、延迟 200ms（而原地转换要同帧生效，这正是 `WriteAssetFile` 把写和注册合成一次
  调用要绕开的东西）、说的是「文件变了」而不是「我发起的那次成了」（外部编辑器改同一个文件长得一样，
  而那是热重载的题目），并且没开监视的挂载点上无声失效。
- **只报成功。** 失败时发起方什么都不做，不需要被通知；用户要看的原因在日志里。
- **取消不产生任何事件**，理由同上——什么都没发生。

**签名为什么是 `(const Asset&, path)` 而不是原先写的 `(type, data, path)`**：

- `type` 不用传——**扩展名决定文件类型**，`GetSupportAssetType(path)` 就是答案，顺带挡住
  「把材质存成 `.png`」。临时资产在选定路径前 `AssetId` 还无效、`GetAssetType()` 是 `Unknown`，
  按资产自己的类型分派反而断在这里。
- 传 `Asset` 而不是 `AssetData`，是因为保存对话框要**跨帧持有**它（从打开到用户确认好几帧）。
  `Ptr<Asset>` 是引用计数的；`AssetData` 显式删了拷贝构造，连隐式移动也一并没了，既不能存也
  不能传。

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

## 未决（已定）

**具体的失败原因怎么传到 UI**——**取第一条：只打日志**，不给总线契约加原因码。

`SaveAsset` 失败时发起方什么都不做（身份不变、`Modified` 继续亮着、目标不换），所以它不需要
知道为什么；需要知道的是用户，而用户看的是 Console。原因写在 `LOG_ERROR` 里（内嵌贴图那条记下
是哪个槽、哪个 id）。

代价说清楚：内嵌贴图那种情况，界面上不会有一句话解释，`TODO_AssetSystemPlan.md` 要求的「明确
报错并说明原因」这一版只做到了「不默默写进去」的那一半。这是已知的临时状态——等错误显示在
对话框里长出来（它就在按下 Save 的地方），或者子资产提取落地把这条拒绝整个换掉。

---

## 材质保存的最终形态

| 谁 | 做什么 |
|---|---|
| `MaterialAssetBuilder::Serialize` | `MaterialAssetData` → `.smat` 字节（转调 `WriteMaterialAsset`）；带 identity 时拒绝 |
| `MaterialAssetBuilder::PrepareToSave` | 内嵌贴图即拒绝，原因进日志 |
| `AssetManager::SaveAsset` | 扩展名定类型 → 钩子 → `Serialize` → 写文件 → `OnAssetSaved` |
| 材质窗口 | 两头的 per-type 活：存之前把组件读进资产（原地）或临时资产（另存/新建），存之后改 `MaterialAssetRef`、清 `Modified`、刷快照，或把新建的那个打开 |

`MaterialAssetSaver` 与 `MaterialSaveResult` 已删除；失败＝`SaveAsset` 返回无效 `AssetId`。

---

## 影响面

**剩下的 A** 动的是已经在跑、有测试覆盖的缓存子系统（`SparkAssetTest` 的 CacheTests）。改完要跑：

- `ctest --test-dir build -C Debug`（四个套件）
- 重点看 CacheTests 与 `MaterialAssetTests`（round-trip 判字节相等、每字段非默认值比对、
  `MaterialSaveTestFixture` 的七条）
- 手动验一次冷/热缓存：删掉 `cache://` 后首次启动重建，第二次启动命中
