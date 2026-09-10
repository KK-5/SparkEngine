# 欢迎页与资产预加载

> 预加载机制放 `SparkAssetManager`（运行时的事，打包后的游戏一样要预加载），
> 欢迎页放 `SparkEditor`（只是那个机制的一个观察者）。
>
> 这一期只把两件事做到底：**预加载**和**页面布局**。项目、最近列表、背景图是占位。

## 现状与问题

今天资产的加载时机是这样的：

| 时机 | 做了什么 |
|---|---|
| `SparkEngine::SetUp` | 挂 `engine://` `cache://`，`AssetRegistry()` 走一遍，只**注册**不加载 |
| `SparkEditor::Init` | 挂 `project://` `editor://`，再 `AssetRegistry()` 一遍 |
| 面板第一次绘制 | `IconManager::OpenIcon` → `LoadAsset`（**主线程同步**，卡帧） |
| 拖进场景 | `AssetHandler::OnModelAssetDragToScene` → `RequestAsset` → 挂起等 `OnAssetReady` |
| 拖进组件字段 | `QueueComponentBind` → 同上，挂 `m_pendingBinds` |

于是有两套本不该存在的东西：

1. **`AssetHandler` 的两条等待轨**（`m_loadingAssets` / `m_pendingBinds`）。它自己的注释已经写了
   ——「once asset preloading guarantees a dropped asset is already Ready」，这份文档就是那个 once。
2. **主线程同步加载**。`OpenIcon` 走 `LoadAsset`，而 `LoadAsset` 在调用线程上直接跑 `ProcessAsset`：
   解码、编译、甚至 HDR 的 GPU bake 全在主线程上。图标是小图看不出来，换成大图就是卡死。

还有一个今天没人踩到、但已经在的坑：主线程 `LoadAsset(X)` 时 X 正好在 worker 的队列里（`Queued`），
`LoadAsset` 会因为 `IsLoading()` 为真而**直接返回一个没准备好的 asset**。`OpenIcon` 紧接着检查
`IsReady()` 失败，打一条 error 就没图标了。今天没有并发的加载源，所以撞不上——加了预加载就会撞上。

## 契约

**注册表里的资产，要么已经 Ready，要么正在路上。** 拖拽、绑定、打开材质都不再需要「先请求，
再等事件」——它们只需要读。

启动时的批量填充由预加载完成；之后由文件监视器维持（`OnFileAdded` 注册完立刻请求）。

唯一的例外是**用途变体**：同一个文件在不同 usage 下是不同的 `AssetId`，注册表只登记其中一种。
这一期的做法是「未知用途按颜色图加载，用到时发现不对就以正确用途重新加载」，见第五节。

---

## 一、批量加载机制

### 机制是「一批资产的加载与进度」，预加载只是它的第一个场景

剥开只有三件事：批量请求、对这一批的进度、这一批的完成判定。「启动时预加载」是调用者，不是定义
——后面还会有打开场景加载引用闭包、切换项目、以后的 cook。所以**类型按机制命名，变量按场景命名**：
`AssetLoadBatch` 是类型，`m_preloadBatch` 是编辑器里那个实例。

**不注册 `Service`。** 批次天然是多实例的（启动一批、之后打开场景又一批，可以并存），而
`Service<T>` 是单例注册，注册它等于宣称「同时只能有一批」。不做 Service，也就不需要
「接口 + 实现」两个类型——那个约束只在 `Service<T>::Handler`（定义成 `class Handler : public T`，
自继承不成立）下才存在。

**不是 `ISystem`**，不上 `TickBus`：进度是被 UI 每帧轮询的，它自己没有要 tick 的东西。

### 位置与形状

```
Engine/Code/RunTime/Resource/
    AssetLoadBatch.h / .cpp
```

```cpp
namespace Spark::Resource
{
    struct AssetLoadProgress
    {
        //! 一个 AssetType 在这一批里的计数。
        struct Entry
        {
            AssetType type   = AssetType::Unknown;
            uint32_t  total  = 0;
            uint32_t  ready  = 0;
            uint32_t  failed = 0;
        };

        eastl::vector<Entry> entries;          ///< 每个 AssetType 一条，顺序固定
        uint32_t             total    = 0;
        uint32_t             ready    = 0;
        uint32_t             failed   = 0;
        eastl::string        current;          ///< 第一个仍在加载中的资产路径
        bool                 complete = false;
    };

    //! 一批资产的加载与进度。谁发起谁持有。
    //!
    //! 内容在构造时定死：中途加东西会让 total 变动、进度往回跳。要加就开新的一批。
    //!
    //! 进度是**读出来的，不是被通知的**——不订阅 AssetBus，理由见「为什么不订阅总线」。
    //! 于是这个类没有基类、没有锁、没有原子量，生命周期是普通对象的生命周期。
    class AssetLoadBatch final
    {
    public:
        explicit AssetLoadBatch(eastl::vector<AssetId> ids);

        //! AssetManager::RequestAsset 的批量形式。
        void              RequestAll();

        AssetLoadProgress GetProgress() const;
        bool              IsComplete() const;

    private:
        struct Item
        {
            AssetId    id;
            Ptr<Asset> asset;   ///< RequestAll 的返回值；null = 该类型没有 builder，计入 failed
        };

        eastl::vector<Item> m_items;
    };
}
```

用词都跟着仓里既有的：完成态叫 **Ready**（`AssetStatus::Ready` / `IsReady` / `OnAssetReady`），
不叫 done；发起异步加载叫 **Request**（`RequestAsset`），不叫 Start；每类一行叫 **Entry**
（`CacheEntry` / `SubAssetEntry` / `AssetEntry`）。`Loader` / `Builder` / `Compiler` /
`Context` / `Unit` 在这个命名空间里都已经有专属含义，一个都不能借。

### 为什么不订阅总线

第一版让批次继承 `AssetBus::MultiHandler`，然后发现要给它挂一条「**必须活到 `IsComplete()`**」的
约束——而这条约束只有注释在守。查了 EBus 的实现，强度是这样的：

`AssetBusTraits` 的 `LocklessDispatch = true` 且 `MutexType = NullMutex`，于是 `EBus.h:152` 把
`ContextMutexType` 定成 `std::shared_mutex`，但 `DispatchLockGuard` 走 `NullLockGuard`
——**派发不加锁，断连加独占锁**，两者可以真正重叠。`EBus.h:361` 的
`assert(!LocklessDispatch || !IsInDispatch(...))` 让 Debug 会炸，Release 则是静默的
use-after-free。

**所以不给这条约束加护栏，而是让它不存在。** 批次订阅总线只为知道「谁变 Ready 了」，可这件事
不需要推送：`Asset::m_status` 本来就是 `eastl::atomic<AssetStatus>`，任何线程随时可读。
`RequestAll` 把 `RequestAsset` 的返回值（正是该盯着的那个实例）存进 `Item::asset`，
`GetProgress()` 就是 N 次原子读。没有 handler 可断连，生命周期约束随之消失。

顺带解决两件事：

- **一个推送模型下的真 bug。** `RequestAsset` 对已经 Ready 的资产直接 return，**不发事件**
  （`AssetManager.cpp` 里 `RequestAsset` 的第一个分支）。启动时已 Ready 的不少——
  `SetUpDefaultPipeline` 编过的 shader、欢迎页自己刚加载的 logo。走总线的话它们永远不触发
  `OnAssetReady`，`ready` 到不了 `total`，**批次永远完不成**。要补就得在 `RequestAll` 里额外
  查状态，还要防「查完的同时事件也来了」的重复计数。轮询没有这个问题。
- **`current` 能是真的「正在做什么」。** 推送只能告诉你最后一个完成的；轮询可以扫出第一个处于
  `Loading` / `Compiling` 的项，那才是设计稿上那行字的本意。

代价是 `GetProgress()` 每帧 O(n)——167 次原子读，而且只在欢迎页开着时调。

### 谁持有、什么时候开始

`SparkEditor` 持有。开始的位置只有一个正确答案——**编辑器挂完 `project://` / `editor://`
并调完 `AssetRegistry()` 之后**，因为在那之前数据库里只有 `engine://` 的东西。

```cpp
// Editor::Init，AssetRegistry() 之后
m_preloadBatch = MakeUnique<Resource::AssetLoadBatch>(assetManager->GetRegisteredAssetIds());
m_welcomeScreen->LoadImages();       // ← 必须在 RequestAll() 之前，见下
m_editorUI->SetPreloadBatch(m_preloadBatch.get());
m_preloadBatch->RequestAll();
```

**欢迎页自己的图（logo、背景）必须在 `RequestAll()` 之前同步加载完**，否则就撞上面那个
`Queued` → `LoadAsset` 返回未就绪的坑——欢迎页会画不出自己的 logo。

`WelcomeScreen` 只拿一个 `const AssetLoadBatch*`，不持有所有权：批次的生命周期由
`SparkEditor` 管，而它必须活到 `IsComplete()`。

### 全量，不做引用筛选

批次的内容就是 `GetRegisteredAssetIds()` 的全部，不区分「谁引用了谁」。

想过一个更聪明的版本：只预加载能发起引用的根（Shader / Model / Material），散落的图片交给引用
它们的模型和材质带进来。**不做，理由是它注定会退化成一个优化项：**

- 它解不了散图的加载时机。缩略图自己也要走一次加载，图片终归要被读一遍。
- 它的前提是「图片的用途只能靠引用它的人来解释」。而正解是**让图片自解释**——用途信息跟着图片
  本身走，不需要别人来说明。那个机制落地之后，加载一张图从一开始就是对的，不会浪费，
  引用筛选就从「机制」变成「可选的优化」。
- 现在为它建机制，是给一个注定变成可选的东西花预算。

所以这一期的取舍是明确的：**接受浪费，换取路径唯一。** 未知用途一律按颜色图（`Texture2D`）
加载，用到时发现不对，再以正确的用途加载一份。今天确实有重复解码，但后面接缩略图和图片用途
信息都是**无损**的——有了用途信息之后，加载时可以直接把缩略图一起生成出来，而且保证不会出错。

至于「项目里有 100 张没人引用的图」，那是往项目里无脑塞文件的结果，是项目卫生问题，不是引擎
的问题。软件设计有边界，不该为它把机制拧弯，何况后面有正经机制来解决。

### 计数

构造函数把 ids 存进 `m_items`，`Entry::total` 就是按类型分组的条数——构造完就定死。

`RequestAll` 逐个 `RequestAsset`，把返回的 `Ptr<Asset>` 收进 `Item::asset`。返回 null 说明该
`AssetType` 没有注册 builder，这一项直接算 failed，不必再看。

`GetProgress()` 走一遍 `m_items`，每项读一次 `asset->GetStatus()`：

| 状态 | 计入 |
|---|---|
| `Ready` | `ready` |
| `Error` | `failed` |
| `NotLoaded` / `Queued` / `Loading` / `Compiling` | 都不计；第一个 `Loading` / `Compiling` 的填 `current` |

**不需要过滤别人。** 只看自己 `m_items` 里的实例，`glb` 发布的子资产、材质的贴图依赖、别的批次
请求的东西，天然都不在里面——推送模型下那条「不在这一批里的 id 一律忽略」的规则随订阅一起没了。

### 「完成」怎么判定

```
IsComplete()  ==  ready + failed == total
```

就这一条，不需要问「引擎彻底闲下来了没」。理由在 `ProcessAsset` 的顺序里：`Publish(子资产)` 和
`LoadAsset(依赖)` 都排在 `SetDataReady` + `OnAssetReady` **之前**。所以一个 root 报 Ready 的时候：

- **子资产**一定 Ready——同步构建、同步发布。
- **依赖**要么已经同步处理完，要么它本身就在这一批里、会被单独跟踪。

两种情况都不会留下「批次全绿但还有活没干」。

一度打算给 `AssetManager` 加一个 `HasPendingWork()` 来问全局队列，**不加**：批次是多实例的，
全局队列非空可能只是**别的批次**还没完，拿它判定自己这一批本来就是错的。语义收紧到「我请求的
这些」之后，这个方法就没有存在的理由了。

**轮询出来的快照只会漏报，不会误报。** 资产状态是单向的：走到 `Ready` / `Error` 就不再回头
（不支持热重载——`AssetManager` 的 `OnFileModified` 明确不实现，`LoadAsset` 遇到 Ready 也直接
返回）。所以逐项原子读拼出来的快照即使跨了几次状态变更，读到 `Ready` 就一定是真的 Ready，
`IsComplete()` 最坏晚一帧为真，绝不会提前为真。方向是安全的那一侧。

### 需要在 `AssetManager` 上加的一个方法

```cpp
//! 注册表里可独立加载的资产 id。**不含子资产**。
virtual eastl::vector<AssetId> GetRegisteredAssetIds() const = 0;
```

`m_db` 是 private，外面拿不到注册表内容。`AssetDataBase::Snapshot()` 已经写好了，这里在它上面
取 id 即可——批次只要 id，返回 `Ptr<Asset>` 会让调用者平白拿到一堆强引用。

**必须跳过 `IsSubAsset()`。** `ProcessAsset` 对子资产是显式拒绝的（「is a sub-asset and cannot
be built on its own」→ Error），它们由各自的 root 发布，不能单独请求。首次启动时数据库里还没有
子资产，所以这个坑看不出来；但「Rescan」是在预加载**完成之后**重建批次，那时每个 glb 都已经发布
了几十个子资产——不滤掉就会把它们全部请求一遍、全部标成 failed，欢迎页显示几百个失败。

---

## 二、门

`EditorUI::DrawUI` 开头分叉，就这一处：

```cpp
if (!m_welcomeScreen->IsDismissed())
{
    m_welcomeScreen->Draw();
    return;
}
// ……原来的 dockspace 和所有面板
```

后面的面板一个都不画。引擎照常 tick、照常渲染（一个空场景），欢迎页是一个铺满 viewport、
背景不透明的窗口，把它盖住。

几个连带的确认，都不需要改代码：

- `EditorUI::WantCaptureMouse` 会先问 `m_sceneView->IsHovered()`，SceneView 这几帧没画，
  `m_hovered` 保持 false，于是落到 `io.WantCaptureMouse`——欢迎页窗口上它是真，鼠标被吃掉。对。
- `GetFrameBufferSize` 找不到 "Scene View" 窗口时回落到 1024×576，渲染照常有个尺寸。对。
- 键盘不会被捕获（`ConfigNavCaptureKeyboard = false`），所以欢迎页期间按 WASD 会让背后那台看不见的
  相机动。无害，先不管。
- `BottomPanel::LoadIcons` 在第一次 `DrawAssets` 里跑，那时预加载已经结束，
  `LoadAsset` 命中 Ready 直接返回——今天的首帧卡顿顺带没了。

---

## 三、欢迎页布局

```
Engine/Code/Editor/UI/Private/WelcomeScreen.h / .cpp
```

尺寸全部走 `Theme::Px()`（设计稿像素 → 屏幕像素），和 `MaterialWindow` / `SaveAssetDialog` 一致。
配色 `EditorTheme.h` 里基本齐了（`kAccent` 就是设计稿的 `#7FD6C2`），要补两个更深的底色：

```cpp
inline constexpr ImU32 kWelcomeBg    = IM_COL32(0x0D, 0x0E, 0x10, 0xFF);  // 页面底
inline constexpr ImU32 kWelcomePanel = IM_COL32(0x10, 0x12, 0x16, 0xFF);  // 左栏
```

### 左栏（设计稿 596px 宽，右侧 1px `#1E2126` 分隔线）

| 区块 | 真 / 占位 | 说明 |
|---|---|---|
| logo + `Spark`/`Engine` 字标 | **真** | `editor://APP-Icon.svg` 已经在仓里了，走 `IconManager::OpenIcon`（SVG 由 nanosvg 光栅化，已支持）。**前几帧拿不到**，见下 |
| 版本行 | 半真 | `0.4.x-dev · dx12 · win64`。设计稿写的 vulkan，本引擎是 DX12，照实写。版本号目前全仓没有，加一个常量 |
| 当前项目 | 半真 | 没有项目概念。显示 `project://` 挂载点的物理目录和目录名 |
| 最近 | **占位** | 空状态一行字，不伪造三条假记录 |
| 分类进度网格 | **真** | 见下 |
| 总计 `done / total` | **真** | Mono 字体；设计稿的 `tabular-nums` 在这里等价于用 figure space 左侧补齐，和它的 `pad()` 一个意思 |
| spinner + 当前项 | **真** | `AssetLoadProgress::current`，是真正还在 `Loading` / `Compiling` 的那一个 |
| 进入编辑器 | **真** | 未完成时 `kButton` 底 + `kTextFaint` 字（不可点），完成后 `kAccent` 底 + `kOnAccent` 字 |
| 打开其他项目… | **占位** | 画出来，点了什么都不做 |
| Rescan | **真** | 重跑 `AssetRegistry()`，**换一个新的 `AssetLoadBatch`** 再 `RequestAll()`——批次内容构造时定死，重来就是重建一个。不叫 Reload：`RequestAsset` 对已 Ready 的直接返回，它只能捡到新文件，不会重新编译任何东西 |

**分类只画四行，不画六行。** 设计稿有「音频」「脚本」，`AssetType` 里没有这两样，编不出真数字。
这个页面的全部意义就是数字是真的，所以宁可四行。四行正好排成 2×2，网格结构不变。

| 行 | AssetType | 现在的量 |
|---|---|---|
| Shaders | `Shader` | 19（`.hlsl`，全在 `engine://`；`.hlsli` 不登记） |
| Meshes | `Model` | 21（`.glb`） |
| Textures | `Image` | ~122（png / jpg / svg / hdr / ktx2，三个挂载点合计） |
| Materials | `Material` | 5（`.smat`） |

### 右栏

`image-slot` 那块是一张引擎截图。仓里还没有，所以：有 `editor://Welcome/Background.png` 就画，
没有就画纯色 + 渐变遮罩 + 右下角两行说明文字。渐变和遮罩本来就是叠在图上的，缺图也不难看。

### 文案语言

**统一用英文。** imgui 的字体图集只烘了默认的拉丁字形（`SparkImGui.cpp:51` 的
`AddFontFromFileTTF` 是两参数版），中文一个字都画不出来；而且整个编辑器现在全是英文，单独一个
中文欢迎页反而更怪。设计稿上的中文文案照下面这样落：

| 设计稿 | 用 |
|---|---|
| 当前项目 / 最近 | Current Project / Recent |
| 正在预加载资产 / 预加载完成 | Preloading Assets / Preload Complete |
| 着色器变体 / 网格 / 纹理 / 材质 | Shaders / Meshes / Textures / Materials |
| 资产表已就绪 | Asset registry ready |
| 进入编辑器 / 打开其他项目… / 重新加载 | Enter Editor / Open Another Project… / Rescan |

要中文是另一件事，而且是全局的：换一个带 CJK 的字体文件 + 传
`GetGlyphRangesChineseSimplifiedCommon()`，图集会涨到几 MB。想做就单独开一条，别混在这里。

---

## 四、改动清单

| 文件 | 改什么 |
|---|---|
| `Resource/AssetManagerInterface.h` | + `GetRegisteredAssetIds()` |
| `Resource/AssetManager.h/.cpp` | 实现它（`m_db->Snapshot()` 取 id）。`ProcessThread` 不动 |
| `Resource/AssetLoadBatch.h/.cpp` | 新增 |
| `Resource/CMakeLists.txt` | + `AssetLoadBatch.cpp`（显式源文件列表，不是 glob） |
| `Editor/UI/Private/EditorTheme.h` | + 两个欢迎页底色 |
| `Editor/UI/Private/WelcomeScreen.h/.cpp` | 新增 |
| `Editor/UI/EditorUI.h/.cpp` | 持有 `WelcomeScreen`，`DrawUI` 开头分叉 |
| `Editor/Editor.cpp` | `AssetRegistry()` 之后建 `m_preloadBatch`、加载欢迎页的图、`SetPreloadBatch`、`RequestAll()` |
| `Editor/CMakeLists.txt` | + `WelcomeScreen.cpp` |

`AssetHandler` 这一期**不动**。

---

## 五、已知的坑

### 用途靠猜，用到时重来

`AssetId` 的身份里含描述符（`AssetTypes.h:156` 把 `desc->Hash()` 也 combine 进去了），而
`ImageAssetDescriptor` 里有 `usage` / `colorSpace`。所以同一个文件在不同用途下是不同的资产：

```
project://…/T.png + usage=Texture2D   → 哈希 A   ← AssetRegistry 登记的那份
project://…/T.png + usage=NormalMap   → 哈希 B   ← 拖到法线槽时 FieldWidgets 现造的那份
```

这个设计是对的：sRGB 解和 Linear 解出来的字节不一样，法线图以后要 BC5、颜色图要 BC3，产物本来
就该是两份。

**这一期的做法就是接受它**：注册表按 `Texture2D` 登记并预加载，拖到法线槽时
`FieldWidgets.cpp:527` 现造一个 NormalMap 身份的 id，`FindAsset` 返回 null，走
`RequestAsset` + 等事件。也就是说 **`AssetHandler::m_pendingBinds` 不能删**，第六节第 2 期
能删掉的只有 `m_loadingAssets`。

正解是让图片自解释用途（第六节第 4 期），不是改 `AssetId` 的身份模型。

### 冷启动很慢，最贵的是 hdr

6 个 `.hdr` 都会被预加载，每个都要 BakeSky / Irradiance / Prefilter 三趟 GPU bake，
在 worker 线程上跑自己的队列并**阻塞等 GPU**，再读回 CPU。这是全仓最贵的单项，18 趟串行。
加上 21 个 glb（每个再带出几十张内嵌贴图）和 19 个 DXC 编译，第一次跑可能要几分钟。

有 `cache://` 之后就是读文件了——欢迎页存在的理由正是这段时间得有东西看。

### 图标不是马上就能画

`OpenIcon` 返回之后，`RequestIconId` 还会返回 `ImTextureID_Invalid` 好几帧：`IconGPUComponent`
的 `m_iconId` 由 `UIProcessFeature` 在渲染帧里填，而且要等 `PendingSync` 的 fence 完成。

所以欢迎页的 logo 前几帧是空的，必须容忍——照 `BottomPanel` 的
`if (icon != ImTextureID_Invalid)` 写法，不要 assert。这也是为什么右栏的背景图缺席时要有一套
纯色 + 渐变的画法：那不只是「图还没做」，也是「图还没上传完」。

### `.hlsl` 全量编译

`DetectShaderStages` 是拿子串在源码里找 `VSMain` / `PSMain` / `CSMain`。注册表里 19 个 `.hlsl`
如果有纯 include 用途的（没有任何 entry），`stages` 为空，`Compile` 产出一个空的
`ShaderAssetData`——是成功还是失败要跑一遍才知道。预加载会第一次把这 19 个全过一遍，大概率会
照出几条以前没人看见的错。这是好事，但要预期到。

### 依赖排在队列后面是正常的

worker 只有一条线程、FIFO。材质 `ProcessAsset` 末尾 `LoadAsset(依赖)` 时如果依赖已经 `Queued`，
`LoadAsset` 会原样返回未就绪的对象，材质照样 Ready。不会死锁，依赖后面会被 pop 出来处理。

对批次没有影响：这种情况下那个依赖必然也在这一批里（`Queued` 说明有人请求过它），它自己那条
计数会等到它 Ready，`IsComplete()` 不会提前为真。

---

## 六、分期

1. **本期**：预加载机制 + 门 + 欢迎页（四类真进度，项目 / 最近 / 背景图占位）。
2. 文件监视接入预加载（`OnFileAdded` 之后立刻请求），删掉 `AssetHandler::m_loadingAssets`
   和 `AssetType::Model` 那个 `AssetBus` 订阅——拖模型进场景这条路到那时才真正不用等。
   `m_pendingBinds` 留着，原因见第五节。
3. 项目概念：`.sproj`、当前项目、最近列表、打开其他项目。左栏那三块占位这时候才变真。
4. **图片自解释用途信息。** 用途跟着图片本身走，不靠引用它的资产来解释。落地后加载一张图从一开始
   就是对的，第五节那条重复加载消失，`m_pendingBinds` 才能真的删掉。
5. **缩略图。** 依赖第 4 期：有了用途信息，加载时可以直接把缩略图一并生成，而且保证不会出错。
   浏览器格子、hdr 预览、贴图槽的小图都吃这一份产物。
6. 引用闭包 / 引用根筛选，**作为优化项**而不是机制——第 4 期之后它才有意义，而且到那时它只是
   「少读几个文件」，不再是「猜对用途的唯一手段」。
7. GPU 常驻：把 Ready 的含义从「CPU 数据就绪」推到「GPU 可用」。要等
   `TODO_ResourceResidencyPipeline.md` 那条线。
