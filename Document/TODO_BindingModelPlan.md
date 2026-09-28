# 资源绑定模型　重新梳理

起因：P4 讨论 compute 访问 View（`TODO_ScreenSpacePlan.md` D2）时，发现 view 与 pass 两档绑定和其余几档不协调。本文梳理
现状、指出问题、给出目标模型与待定项。

决策 D1~D11 已确认；§七 余下的两项不阻塞实现。步骤 1~3 已完成，§一、§二 记的是改造前的状态。

---

## 一、现状

五个 space 按数据粒度命名，按实际绑定方式看：

| space | 名义 | 载体 | 实际怎么绑 | 绑定频率 |
|---|---|---|---|---|
| 0 | scene | `SceneBindingSystem`，一个 SRG（`MainSceneTag`）：`g_Lights`、`g_ShadowViews` 两张表、环境贴图、`SceneConstants` | `.Binds<MainSceneTag>()` | 每 Scope 一次 |
| 1 | view | 每个视图实体一个 SRG（`ViewFactory` 建，`ViewShaderBindings` 指向），`ViewBindingSystem` 每帧写 cbuffer | 执行器在提交表的视图句柄处绑（`SubmitScopeRange`） | **每视图段一次** |
| 2 | pass | 每个 pass 一个 SRG（`CreatePassBindings`，Finalize 时按反射建）；Scope 的 `.Bind` / `.Sampler` / `.Constant` 写进它 | `ScopeState` | 每 Scope 一次，但所有 Scope 共用同一个对象 |
| 3 | material | 全局表 `g_Materials`，下标在 `InstanceData.MaterialIndex` | `.Binds<>()` | 每 Scope 一次 |
| 4 | object | 全局表 `g_Instances`，下标来自逐实例顶点流（`StartInstanceLocation`） | `.Binds<>()` | 每 Scope 一次 |

另有 Scope 级的根常量块（`ScopeRootConstants`，128 字节，shader 侧在 space5）：`.Constant` / `.BindIndex` 的名字落在
shader 的根常量里时写进这里，每个 Scope 各一份。

material 与 object 名义上频率最高，绑定上却和 scene 一样：**全局表 + 按下标取**。按绑定频率只有三档：全局表（0 / 3 /
4）、视图段（1）、pass（2）。

---

## 二、问题

### 1. view 是唯一"切换绑定"而不是"按下标取"的逐实例数据

一个 pass 引用的是一类视图的全部实例，在 pass 内逐个切换。它的形态和 material / object 一样是"一类实体各一份"，
绑定方式却不同，由此带来：

- **视图数据只能经执行器的视图句柄进 shader**。compute 想在 Build 里自己选视图、自己定 dispatch，就拿不到视图参数，
  只能另开 `ForEachView` 让执行器去绑（ScreenSpacePlan D2）。经 `.Constant` 传又受限于 space2 每 pass 一份、根常量
  128 字节。
- **一次 draw 要读多个视图时只能另建表**：`g_ShadowViews` 就是这样来的，LightingPass 一次读 N 个阴影视图。MultiViewPlan
  已定决策 4 "per-view SRG 与 `g_ShadowViews` 并存"，当时的理由之一是"不依赖 root constant"，根常量现在已经实现。

### 2. Scope 往 pass 级的存储里写，却没有检查

`.Bind` / `.Sampler` / `.Constant` 声明在 Scope 上，落到的 space2 却是每 pass 一份：

- 同一 pass 的多个 Scope 对 space2 的同一输入写不同的值，后写者对所有 Scope 生效，没有断言（`RenderGraphCompiler.cpp` 的
  `CompileScopeBindings`）。"必须一致"只写在 `PassScopes.h` 的注释里。
- space2 的载体是 SRG：DX12 的 `ShaderBindings` 按帧轮换描述符表与常量内存（`FrameCountMax` 份），**同一帧内只能有一个
  值**。要让它随 Scope 变，每个 Scope 都得有自己的一块内存。

---

## 三、目标模型

| 档 | 内容 | 载体 |
|---|---|---|
| 全局表 | scene、view、material、instance | 每帧上传，pass 以 `.Binds<>()` 声明、Scope 开头绑一次，shader 按下标取 |
| pass 参数 | 一个 pass 内各 Scope 一致的资源、采样器、常量 | space2 SRG，每 pass 一份 |
| Scope 参数 | 随 Scope 变的资源下标与小常量 | space5（D11），目前的载体是根常量，每 Scope 一块 |
| 视图段 | 一个 `viewIndex` | space5 里的一个字段，执行器在视图句柄处部分写；光栅照旧在此设 viewport |

view 从"一档绑定"变成"一张表 + 一个下标"，与 material / object 同构。pass 一档保留，只放各 Scope 一致的值；在它内部
引入 Scope 一档（space5）——数量小、变化多，正合根常量。

执行器里的嵌套关系（现在如此，本方案不变）：

```
Pass                            ← PSO 布局、space2（pass 参数）
└─ Scope（按流顺序）            ← BeginRenderPass、屏障、space5 整块（Scope 参数）
   └─ 视图段（提交表里的视图句柄） ← viewport、space5 里的 viewIndex
      └─ item（draw / dispatch）   ← instance 下标（经顶点流），material 下标从 instance 取
```

- 视图只在 Scope 内部切换，不跨 Scope 边界。每个 Scope 开头 space5 整块重写，随后第一个视图句柄改 `viewIndex`。
- 只能是 Scope 包含视图：Scope 是屏障的单位，每个 Scope 只编译一组屏障，视图在外层会让同一个 Scope 执行多次。
- compute 在 Build 里选视图是一个 Scope 只有一个视图的退化情形：`.Constant("viewIndex")` 与执行器写的是同一个字段。
  按视图重放的 pass 由执行器负责这个字段，Scope 再 `.Constant("viewIndex")` 即断言。
- 表达不了"每个 (Scope, 视图) 各不相同"的参数（每个视图读写不同的资源），见 `TODO_MultiViewPlan.md`。

---

## 四、视图表

### 数据与上传

- `ViewData` 结构 = 现有 `ViewBindings` cbuffer 里随视图变的字段（`ViewBindings.hlsli`），shader 侧
  `ViewData GetView(uint index)`；frame time 也进每一行（D8）。
- 上传用 `GlobalBuffer`（D3），同 `g_Materials` / `g_Instances`，Sources 为 `View`：`GlobalBuffer::Update` 每帧给带
  `View`、还没有槽位的视图实体分配 `SlotRef`，再经 process 回调写每个槽位对应的一行（取代 `ViewBindingSystem` 现在的
  逐视图写 SRG）。buffer 就绪前的帧没有槽位，这个视图被跳过，同现在"SRG 还没编译就跳过"。lowering、Build、shader 都用这个
  槽位作视图下标。
- 删除：每视图的 SRG、`ViewShaderBindings`、`ResolveViewShaderBindings`；lowering 里"视图绑定未就绪则跳过"改为"shader 读
  视图（根常量里有 `viewIndex`）而视图没有槽位则跳过"；只要 viewport 的 pass 不受槽位影响。
- `ViewBindingsReflect.hlsl` 保留：space1 组的 SRG 仍靠它反射布局，同 `SceneBindingsReflect.hlsl`，内容改为引用 `g_Views`。
- 绑定：space1 的 SRG 实体打 `ViewBindingTag`，读视图的 pass 都声明 `.Binds<ViewBindingTag>()`，在 Scope 开头绑一次
  （D9）；执行器在视图句柄处不再绑任何 SRG。

### viewIndex 的传递

- shader 的 `ScopeParameters` 里的 `viewIndex` 字段（D11）。按视图重放的 pass：执行器在视图句柄处按偏移只写这 4 字节，
  替代现在的 `BindShaderInputs(space1)`（D5）。Build 自己选视图的 compute：每个 Scope `.Constant("viewIndex", slot)`，
  不需要执行器参与。
- 每个读视图的 shader 都要在 `ScopeParameters` 里声明它，占 128 字节中的 4 字节；GBuffer 这类现在没有 Scope 参数的
  shader 新声明 `struct ScopeParameters { uint viewIndex; };` 并 include `ScopeBindings.hlsli`。

### 对 ScreenSpacePlan 的影响

compute 不再需要 `ForEachView`：`ComputePassBuilder` 只加 `Binds<>()`，pass 声明 `.Binds<ViewBindingTag>()`，Build 自己
选视图、设 `viewIndex`、定 dispatch。每个视图不同的线程数、多 Scope × 多视图（资源不随视图变时）都能在 Build 里表达。
ScreenSpacePlan 的 D2 与 0a 待本文定稿后改写。

### 与 UE 的对应

UE 的常规路径是每视图一个 View uniform buffer，C++ 里按视图循环传给 pass；Nanite、VSM 等 GPU-driven 路径用打包的视图
数组加视图下标。本方案全程用数组形态。

---

## 五、pass 参数与 Scope 参数

### 规则

- **space2 只放一个 pass 内各 Scope 一致的值**：`.Bind` 的资源、`.Sampler`、未命中 space5 的 `.Constant`。
- **随 Scope 变的值走 space5**：资源用 `.BindIndex`（bindless 已是硬依赖，见 D7），常量用 `.Constant` 并在 shader 的
  `ScopeParameters` 里声明。每个 Scope 各有一块（`ScopeRootConstants`），上限 128 字节，与 `viewIndex` 共用。

现状都满足：多 Scope 的只有 SceneDownsample、Bloom，随 Scope 变的下标与尺寸已全部走根常量（二十几字节），space2 里只有
各 Scope 相同的采样器。P4 规划的 HZB（每 mip 一个 Scope）、GTAO 的 PrefilterDepths（5 个 mip 下标）也在 128 字节内。

### 一致性断言

`CompileScopeBindings` 里，同一 pass 的后一个 Scope 对 space2 的同一输入写了不同的值即断言：图像比视图，采样器比
`SamplerState`，常量比字节。被挡住的两种写法要改走 space5：随 Scope 变的 `.Bind` 资源；`.BindIndex` 回退到 space2 的
cbuffer（名字没在 `ScopeParameters` 里声明）。`PassScopes.h` 里"必须一致"的注释改为指向这条断言。

### 以后的扩展

随 Scope 变的资源都能走 bindless，唯一的触发条件是**随 Scope 变的常量超过 128 字节**。届时在 space5 加每 Scope 一份的
存储，只影响超出的部分，是加法；space2 始终只放各 Scope 一致的值：

- 每个 pass 按 Scope 序号持有一组 space5 SRG，跨帧复用；
- 或一个 SRG，每个 Scope 从每帧线性分配器里取一个版本（要改 DX12 `ShaderBindings` 每帧只存一份编译结果的做法）。

Vulkan 上 space5 届时成为第 6 个 descriptor set，要求 `maxBoundDescriptorSets ≥ 6`：规范保证的最低值是 4，部分移动端
正是 4。现在只用 push constant，不占 set。

---

## 决策记录

### D1　视图表独立成组，留在 space1　✅ 已定

space 数量够用，不为省一个 space 并入 space0。space1 组的内容由每视图一个 SRG 改为全组一份，只含 `g_Views`；frame time
这类不随视图变的值也进 `ViewData`（D8）。

组里只有 `g_Views` 一个 SRV，所以不会出现 space0 那种"pass 只引用部分成员时表内偏移错位"的问题（`SpaceZeroKeepAlive`，
见 `TODO_StructureAlignPlan.md`）。以后往这个组加第二个成员，就要先解决组布局的归属。

### D2　与 `g_ShadowViews` 并存　✅ 已定

`g_ShadowViews` 保持现状（space0，下标为 atlas tile 槽位），`ViewData` 不加阴影字段。阴影视图有两套下标：渲染用
`g_Views` 的行，采样用 `ShadowViewIndex`。

不合并：合并要把视图推导的三项（世界 → buffer UV、rect 的 UV 范围、每单位 w 的像素世界尺寸）做成 `ViewData` 的通用字段，
三个 bias 挪进 `LightData` 的 padding，还要让一盏灯的各个面在 `g_Views` 里占连续的行、保住"没有 tile 的面读作无阴影"，
引入的机制太多。

定稿时改写 MultiViewPlan 已定决策 4 的表述："per-view SRG"一半改为视图表，"并存"不变。

### D3　视图在表里的下标：稳定槽位　✅ 已定

与 `g_Materials` / `g_Instances` 同一套机制：`GlobalBuffer` 加每个视图实体上的 `SlotRef`。`CreateViewEntity` 不再建 SRG
实体；槽位由 `GlobalBuffer::Update` 分配给带 `View` 的实体，实体销毁时最后一份 `SlotRef` 归还槽位，不论销毁走哪条路径。

不用每帧重新编号：现在虽然没有东西跨帧依赖这个下标（上一帧矩阵在 `ViewHistory` 上，每帧重写进当帧的行），但稳定槽位
让 view 与 material / instance 完全同构，也给以后 GPU 上按视图下标跨帧保存的数据留好了身份（D10）。D2 选了并存，
`g_Views` 没有"一盏灯的各个面占连续的行"的约束，任意槽位都可用。

### D4　shader 侧显式访问：`GetView(g_Scope.viewIndex).xxx`　✅ 已定

`ViewData view = GetView(g_Scope.viewIndex);` 后读字段（`viewIndex` 是 shader 自己 `ScopeParameters` 里的字段，见 D5、
D11），与 `GetMaterialData(index)` / `GetInstanceData(index)` 同一写法；读当前视图与读任意视图也是同一写法。现有约 40 处
`g_*` 视图参数（引擎 10 个、SandBox 1 个 shader）一次改完，改动机械。

不保留 `g_ViewProjection` 等名字经宏映射：下标藏在宏后面，读别的视图时又是另一种写法。

### D5　视图段写 `viewIndex`：按名字查偏移，部分写根常量　✅ 已定

一个 Scope 画多个视图时（阴影 pass、以后分屏的主视图 pass），执行器每切一个视图要改 `viewIndex`，同一块 Scope 参数里
其余字段（`.BindIndex` 的下标、`.Constant` 的参数）保持不变。

- **偏移**：`viewIndex` 是各 shader `ScopeParameters` 里的普通字段，位置不限。lowering 从 pass 的根常量 `ConstantsLayout`
  按名字查（`FindShaderInputIndex` → `GetInterval`），偏移缓存在 Scope 上，同 `.BindIndex`。查不到说明 shader 不读视图
  数据，执行器不写，只设 viewport（同现在"视图没有 space1 时照样用它的 viewport"）。
- **写入**：RHI 的 `SetRootConstants` 加字节偏移参数（DX12 `Set*Root32BitConstants` 的 DestOffset、Vulkan
  `vkCmdPushConstants` 的 offset），执行器在视图句柄处只写这 4 字节。

不给引擎保留块内的固定区域：`ConstantsLayout` 按名字就能给出完整的位置信息，在根常量里设一个值与在 `m_constants` 里设
一个值没有区别。不用 CBV：CBV 是指向显存的地址，同一 Scope 内每个不同的值都要一块独立内存（每块 256 字节，从每帧上传环
分配），根常量的值直接录在命令列表里，draw 之间随时可改。

### D6　space2 保持每 pass 一份，Scope 参数走 space5　✅ 已定

相当于在 pass 一档内部引入 Scope 一档（D11）：数量小、变化多，用根常量实现。space2 只放各 Scope 一致的值，写了不同的
值即断言（§五）。

不做每个 Scope 一份的 SRG：现在和 P4 规划的 pass 都用不到，等随 Scope 变的常量超过 128 字节时在 space5 加（§五）。
不做全 bindless（取消 space2，大常量走每帧上传环、采样器改静态采样器）：要先建上传环、根 CBV 绑定、静态采样器声明、
buffer 的 `.BindIndex`、漏写检查，光栅 pass 的每个访问还要补 `.Stage`，过于激进。

### D7　bindless 的 shader 写法：沿用 `ResourceDescriptorHeap`　✅ 已定

材质贴图、SceneDownsample、Bloom 已经在用。DXC 翻到 SPIR-V 时，每种资源类型各生成一个 runtime array，全部别名到同一个
binding 上，保住 DX12"一个堆、一套下标"的语义：`ImageView` 给出的是堆内绝对下标，读与读写是两个描述符。这要求该
binding 为 `VK_DESCRIPTOR_TYPE_MUTABLE_EXT`，**`VK_EXT_mutable_descriptor_type` 因此是 Vulkan 后端的必需扩展**：桌面三家
都有，移动端驱动覆盖不全。

不改用按类型分开的数组：每种类型各一套下标空间，与引擎现有的下标语义不符，且要改所有已用 bindless 的 shader。

### D8　frame time 进 `ViewData`　✅ 已定

space1 只放 `g_Views`，不加组 cbuffer：`CommandList::BindShaderInputsFor*` 断言 PSO 布局里该 space 的 CBV 数与 SRG 编译出
的 CBV 数相等（描述符表缺了会跳过，CBV 不会），只读 `GetView()` 的 shader 被 DXC 删掉 cbuffer 后每次绑定都断言。

`FrameNumber` / `GameTime` / `PrevGameTime` / `DeltaTime` 放进 `ViewData` 每一行，读法 `GetView(i).gameTime`。每行多 16
字节，视图只有几十个。各视图的时间也不必相同（暂停的编辑器视口、SceneCapture），UE 的 View uniform buffer 同样带这些字段。
现在没有 shader 读这四个字段。

space0 `SceneConstants` 里的 `g_Scene*` 镜像保留不动。

### D9　pass 显式 `.Binds<ViewBindingTag>()`　✅ 已定

space1 全组一份后在 Scope 开头绑一次，同 space0 / 3 / 4。space1 的 SRG 实体打 `ViewBindingTag`，与 `MaterialBindingTag` /
`InstanceBindingTag` 同名式；`GlobalBuffer` 的数组 tag 叫 `Views`，同 `Materials` / `Instances`。`MainViewTag` 等是视图
实体上的视图类型 tag，不能用。读视图的 pass 都写 `.Binds<ViewBindingTag, ...>()`，光栅与 compute 同一写法。
`RendersView` 只管 viewport 与 `viewIndex`，不再绑任何 SRG。

与其余共享组行为一致：声明了而 shader 没读，该 space 不在 PSO 布局里，`BindShaderInputsFor*` 断言；shader 读了而没声明，
不检查。space1 只有一个成员，不会出现 space0 的 CBV 数不符与表内偏移错位。

不由 `RendersView` 隐含绑定：`RendersView` 与"shader 读 `g_Views`"不等价（只要 viewport 的光栅 pass，如 SandBox 的
Triangle / MSAA；Build 里选视图的 compute），且 `Binds<>()` 只存一个函数指针，隐含的与显式的要能叠加。

以后：由 pass 的反射决定绑哪些共享组（布局里有该 space 就绑），"读了而没声明"随之消失。对所有共享组一起做，单独立项。

### D10　保持 D3，放宽 `GlobalBuffer` 注释的判据　✅ 已定

`GlobalBuffer.h` 的注释只把"别处存了这个下标且要跨帧"算作用稳定槽位的理由，视图目前不满足。改为满足其一即可：

- 别处存了下标且要跨帧（`g_Instances` 的 `StartInstanceLocation`、`InstanceData::m_materialIndex`）；
- 下标是 GPU 上跨帧持久数据的身份：以后按视图保存的 HZB、曝光历史、VSM 类的页缓存，都要求同一视图跨帧拿到同一个下标
  （UE 的 VSM 用持久 ID 而不是逐帧编号也是为此）。

只在一帧内遍历的数组（`g_Lights`）仍稠密打包。视图数量小，空洞的代价可忽略。

### D11　space5 定为 Scope 档　✅ 已定

space5 已经专用于根常量（`ShaderAsset.h` 的 `RootConstantsSpaceId`）。把它定为与 space0~4 并列的一档：space5 = per-Scope，
根常量是它目前的载体。

- **shader 侧**：共享头 `ScopeBindings.hlsli` 声明
  `[[vk::push_constant]] ConstantBuffer<ScopeParameters> g_Scope : register(b0, space5);`。shader 先定义自己的
  `struct ScopeParameters`，再 include 它。只读视图的 shader 写 `struct ScopeParameters { uint viewIndex; };`。
  SceneDownsample、Bloom 的 `g_Root` 改为 `g_Scope`。各 `*Bindings.hlsli` 的 space 约定注释补上"space5 per-Scope"。
- **分层**：Scope 是 render 层的概念，RHI 与 Resource 层保持"根常量"的叫法（`RootConstantsSpaceId`、`SetRootConstants`）；
  render 层加 `kPerScopeSpaceId = RootConstantsSpaceId`，同 `kPerPassSpaceId`。Resource 层的测试资源（`RootConstantsTest.hlsl`
  等）测的是机制，保持 `g_Root`。
- 超出 128 字节时的扩展落在 space5（§五），以后由 shader 反射驱动绑定时，space5 作为一档一起纳入。

不让头文件自动声明默认的 `ScopeParameters`：与自带 Scope 参数的 shader 冲突，要靠宏协调，读 shader 时也看不出 `g_Scope`
的来源。

---

## 六、步骤

| 步骤 | 内容 | 依赖 |
|---|---|---|
| 1 | Scope 档与 RHI 部分写 ✅ | D5、D11 |
| 2 | 视图表并行上线，不接 shader ✅ | D1、D3、D8 |
| 3 | 切换到视图表 ✅ | 1、2 |
| 4 | compute 访问视图 | 3 |
| 5 | space2 一致性断言 | D6 |

1 与 2 互不依赖；5 与其余都不依赖。3 做完视图数据就走表了，4 解锁 P4。

### 1　Scope 档与 RHI 部分写

- `ScopeBindings.hlsli`；SceneDownsample、Bloom 改为 `ScopeParameters` / `g_Scope`；`kPerScopeSpaceId`；各
  `*Bindings.hlsli` 的约定注释。
- RHI `SetRootConstants(data, byteCount, byteOffset)`：DX12 用 `DestOffsetIn32BitValues = byteOffset / 4`，断言偏移 4 字节
  对齐、偏移加长度不超过布局大小。现有的唯一调用方传 0。

**验证**：画面不变（SceneDownsample、Bloom）。

### 2　视图表并行上线，不接 shader

- `ViewData`：C++ `Binding/View/ViewData.h`，HLSL `ViewData.hlsli`（同 `LightData.hlsli`）。字段 = 现 cbuffer 字段 +
  frame time；16 字节对齐，C++ 侧 static_assert 大小，矩阵约定同 `InstanceData`。
- `ViewBindingSystem`：建 space1 SRG 实体（`ViewBindingTag`），`GlobalBuffer<Views, ViewData, View>` 上传；process 回调里写
  行并更新 `ViewHistory`。旧的逐视图 SRG 照写。
- 新 SRG 的布局来自临时反射宿主 `ViewsReflect.hlsl`（只声明 `g_Views`），步骤 3 删除。这一步没有 pass 绑新 SRG，与旧的
  逐视图 SRG 不冲突。

**验证**：画面不变；PIX 里 `g_Views` 各行与各视图旧 cbuffer 的值一致；增删相机、开关阴影灯时槽位分配与归还正确，没有
溢出日志。

### 3　切换到视图表

shader 与 C++ 必须一次切换：

- **shader**：`ViewBindings.hlsli` 改为 `g_Views`（t0, space1）+ `GetView()`，`ConvertFromDeviceZ` 改为接收 `ViewData`。
  引擎 10 个 shader（DepthOnly、GBuffer、Lights、Reflections、IndirectDiffuse、Skybox、ShadowProjection、VelocityResolve、
  TemporalAA、Tonemap）与 SandBox 的 `CubeTextured.hlsl` 改用 `GetView(g_Scope.viewIndex)`，声明 `ScopeParameters`。
  `ViewBindingsReflect.hlsl` 改为引用 `g_Views`，删 `ViewsReflect.hlsl`。
- **lowering**：`OpenScope` 对渲视图的 pass 按名字查 `viewIndex` 的偏移，存进 `ScopeRootConstants`，并在
  `m_writtenDwords` 里预先标记（Scope 再 `.Constant("viewIndex")` 即断言）；视图没有槽位则跳过，取代
  `ResolveViewShaderBindings`。
- **执行器**：视图句柄处按偏移写槽位的 4 字节；删在视图句柄处绑 space1。
- **pass**：11 个引擎 pass（DepthPre、Shadow、GBuffer、Lights、Reflections、IndirectDiffuse、Skybox、ShadowProjection、
  VelocityResolve、TemporalAA、Tonemap）与 SandBox 的 DrawCube、MultiView 声明 `.Binds<ViewBindingTag, ...>()`。
  Triangle、MSAA 不读视图，不改。
- **删除**：`ViewShaderBindings`、`CreateViewShaderBindings`、`ResolveViewShaderBindings`、写旧 cbuffer 的代码、
  `DestroyViewEntity` 里的 SRG；CameraView / ShadowView 的 Shutdown 改为遍历 `<Tag, View>`。
- **注释与文档**：`ScopeState` 等处的注释；`GlobalBuffer.h` 的注释（D10）；MultiViewPlan 已定决策 4（D2）。

**验证**：画面不变（主视图各 pass、阴影、TAA 的上一帧矩阵）；GPU-based validation 无报错；对比切换前后的 GPU 时间
（§七）。

### 4　compute 访问视图

- `ComputePassBuilder` 加 `Binds<>()`。
- Build 用的"视图句柄 → 槽位"查询（没有槽位时返回无效），供 `.Constant("viewIndex", slot)`。
- lowering 检查：pass 的 `ScopeParameters` 里有 `viewIndex`、而这个 Scope 既不按视图重放也没写它，即断言（否则读到清零
  块里的槽位 0）。
- 改写 ScreenSpacePlan 的 D2 与 0a。

### 5　space2 一致性断言

- `CompileScopeBindings` 的断言（§五）；`PassScopes.h` 注释改为指向它。

---

## 七、未决

- 视图数据从 CBV 改为结构化 buffer 后的访问开销：下标全 wave 一致时在现代 GPU 上差别很小，步骤 3 做完量一次。
- 多视图下"每个视图一份资源"（金字塔 / 归约类 pass）不在本文范围，见 `TODO_MultiViewPlan.md`。

---

## 关联文档

- `TODO_ScreenSpacePlan.md` —— D2 / 0a 待本文定稿后改写
- `TODO_MultiViewPlan.md` —— 已定决策 4（per-view SRG 与 `g_ShadowViews` 并存）；未决项"`g_Views` 能否取代 per-view SRG"
- `TODO_StructureAlignPlan.md` —— 共享组的布局权威、`SpaceZeroKeepAlive`
- `TODO_RenderGraphItemPlan.md` —— Scope 模型、逐 Scope 的屏障与根常量
