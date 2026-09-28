# 资源绑定模型　重新梳理

起因：P4 讨论 compute 访问 View（`TODO_ScreenSpacePlan.md` D2）时，发现 view 与 pass 两档绑定和其余几档不协调。本文梳理
现状、指出问题、给出目标模型与待定项。

决策 D1~D7 已确认；实现前还有几项待定，见 §七。

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

另有 Scope 级的根常量块（`ScopeRootConstants`，128 字节）：`.Constant` / `.BindIndex` 的名字落在 shader 的根常量里时写进
这里，每个 Scope 各一份。

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
| 全局表 | scene、view、material、instance | 每帧上传，Scope 开头绑一次，shader 按下标取 |
| pass 参数 | 一个 pass 内各 Scope 一致的资源、采样器、常量 | space2 SRG，每 pass 一份 |
| Scope 参数 | 随 Scope 变的资源下标与小常量 | 根常量，每 Scope 一块 |
| 视图段 | 一个 `viewIndex` | 根常量里的一个字段，执行器在视图句柄处部分写；光栅照旧在此设 viewport |

view 从"一档绑定"变成"一张表 + 一个下标"，与 material / object 同构。pass 一档保留，只放各 Scope 一致的值；在它内部用
根常量引入 Scope 一档——数量小、变化多，正合根常量。

执行器里的嵌套关系（现在如此，本方案不变）：

```
Pass                            ← PSO 布局、space2（pass 参数）
└─ Scope（按流顺序）            ← BeginRenderPass、屏障、根常量整块（Scope 参数）
   └─ 视图段（提交表里的视图句柄） ← viewport、根常量里的 viewIndex
      └─ item（draw / dispatch）   ← instance 下标（经顶点流），material 下标从 instance 取
```

- 视图只在 Scope 内部切换，不跨 Scope 边界。每个 Scope 开头根常量整块重写，随后第一个视图句柄改 `viewIndex`。
- 只能是 Scope 包含视图：Scope 是屏障的单位，每个 Scope 只编译一组屏障，视图在外层会让同一个 Scope 执行多次。
- compute 在 Build 里选视图是一个 Scope 只有一个视图的退化情形：`.Constant("viewIndex")` 与执行器写的是同一个字段。
  按视图重放的 pass 由执行器负责这个字段，Scope 再 `.Constant("viewIndex")` 即断言。
- 表达不了"每个 (Scope, 视图) 各不相同"的参数（每个视图读写不同的资源），见 `TODO_MultiViewPlan.md`。

---

## 四、视图表

### 数据与上传

- `ViewData` 结构 = 现有 `ViewBindings` cbuffer 里随视图变的字段（`ViewBindings.hlsli`），shader 侧
  `ViewData GetView(uint index)`；frame time 等全组一致的字段放哪里待定（§七）。
- 上传用 `GlobalBuffer`（D3），同 `g_Materials` / `g_Instances`，Sources 为 `View`：`GlobalBuffer::Update` 每帧给带
  `View`、还没有槽位的视图实体分配 `SlotRef`，再经 process 回调写每个槽位对应的一行（取代 `ViewBindingSystem` 现在的
  逐视图写 SRG）。buffer 就绪前的帧没有槽位，这个视图被跳过，同现在"SRG 还没编译就跳过"。lowering、Build、shader 都用这个
  槽位作视图下标。
- 删除：每视图的 SRG、`ViewShaderBindings`、`ResolveViewShaderBindings`；lowering 里"视图绑定未就绪则跳过"改为"视图没有
  槽位则跳过"。
- `ViewBindingsReflect.hlsl` 保留：space1 组的 SRG 仍靠它反射布局，同 `SceneBindingsReflect.hlsl`，内容改为引用 `g_Views`。

### viewIndex 的传递

- shader 根常量结构体里的 `viewIndex` 字段。按视图重放的 pass：执行器在视图句柄处按偏移只写这 4 字节，替代现在的
  `BindShaderInputs(space1)`（D5）。Build 自己选视图的 compute：每个 Scope `.Constant("viewIndex", slot)`，不需要执行器
  参与。
- 每个读视图的 shader 都要在根常量里声明它，占 128 字节中的 4 字节；GBuffer 这类现在没有根常量的 shader 要新声明
  `g_Root`。

### 对 ScreenSpacePlan 的影响

compute 不再需要 `ForEachView`：`ComputePassBuilder` 只加 `Binds<>()`（绑视图表所在的组），Build 自己选视图、设
`viewIndex`、定 dispatch。每个视图不同的线程数、多 Scope × 多视图（资源不随视图变时）都能在 Build 里表达。
ScreenSpacePlan 的 D2 与 0a 待本文定稿后改写。

### 与 UE 的对应

UE 的常规路径是每视图一个 View uniform buffer，C++ 里按视图循环传给 pass；Nanite、VSM 等 GPU-driven 路径用打包的视图
数组加视图下标。本方案全程用数组形态。

---

## 五、pass 参数与 Scope 参数

### 规则

- **space2 只放一个 pass 内各 Scope 一致的值**：`.Bind` 的资源、`.Sampler`、未命中根常量的 `.Constant`。
- **随 Scope 变的值走根常量**：资源用 `.BindIndex`（bindless 已是硬依赖，见 D7），常量用 `.Constant` 并在 shader 的根常量
  结构体里声明。每个 Scope 各有一块（`ScopeRootConstants`），上限 128 字节，与 `viewIndex` 共用。

现状都满足：多 Scope 的只有 SceneDownsample、Bloom，随 Scope 变的下标与尺寸已全部走根常量（二十几字节），space2 里只有
各 Scope 相同的采样器。P4 规划的 HZB（每 mip 一个 Scope）、GTAO 的 PrefilterDepths（5 个 mip 下标）也在 128 字节内。

### 一致性断言

`CompileScopeBindings` 里，同一 pass 的后一个 Scope 对 space2 的同一输入写了不同的值即断言：图像比视图，采样器比
`SamplerState`，常量比字节。被挡住的两种写法要改走根常量：随 Scope 变的 `.Bind` 资源；`.BindIndex` 回退到 space2 的
cbuffer（名字没在根常量里声明）。`PassScopes.h` 里"必须一致"的注释改为指向这条断言。

### 以后的扩展

随 Scope 变的资源都能走 bindless，唯一的触发条件是**随 Scope 变的常量超过 128 字节**。届时让 space2 按 Scope 各持一块
内存，只影响超出的部分，是加法：

- 每个 pass 按 Scope 序号持有一组 SRG，跨帧复用；
- 或一个 SRG，每个 Scope 从每帧线性分配器里取一个版本（要改 DX12 `ShaderBindings` 每帧只存一份编译结果的做法）。

---

## 决策记录

### D1　视图表独立成组，留在 space1　✅ 已定

space 数量够用，不为省一个 space 并入 space0。space1 组的内容由每视图一个 SRG 改为全组一份，含 `g_Views` 表；frame time
这类不随视图变的值放哪里待定（§七，组里再加 cbuffer 会触发 CBV 数量断言）。

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
让 view 与 material / instance 完全同构，也给以后 GPU 上按视图下标跨帧保存的数据留好了身份。D2 选了并存，`g_Views` 没有
"一盏灯的各个面占连续的行"的约束，任意槽位都可用。

### D4　shader 侧显式访问：`GetView(g_Root.viewIndex).xxx`　✅ 已定

`ViewData view = GetView(g_Root.viewIndex);` 后读字段（`viewIndex` 是 shader 自己根常量结构体里的字段，见 D5），与 `GetMaterialData(index)` / `GetInstanceData(index)` 同一写法；读当前
视图与读任意视图也是同一写法。现有约 40 处 `g_*` 视图参数（12 个 shader）一次改完，改动机械。

不保留 `g_ViewProjection` 等名字经宏映射：下标藏在宏后面，读别的视图时又是另一种写法。

### D5　视图段写 `viewIndex`：按名字查偏移，部分写根常量　✅ 已定

一个 Scope 画多个视图时（阴影 pass、以后分屏的主视图 pass），执行器每切一个视图要改 `viewIndex`，同一块根常量里其余
字段（`.BindIndex` 的下标、`.Constant` 的参数）保持不变。

- **偏移**：`viewIndex` 是各 shader 根常量结构体里的普通字段，位置不限。lowering 从 pass 的根常量 `ConstantsLayout` 按名字
  查（`FindShaderInputIndex` → `GetInterval`），偏移缓存在 Scope 上，同 `.BindIndex`。查不到说明 shader 不读视图数据，
  执行器不写，只设 viewport（同现在"视图没有 space1 时照样用它的 viewport"）。
- **写入**：RHI 的 `SetRootConstants` 加字节偏移参数（DX12 `Set*Root32BitConstants` 的 DestOffset、Vulkan
  `vkCmdPushConstants` 的 offset），执行器在视图句柄处只写这 4 字节。

不给引擎保留块内的固定区域：`ConstantsLayout` 按名字就能给出完整的位置信息，在根常量里设一个值与在 `m_constants` 里设
一个值没有区别。不用 CBV：CBV 是指向显存的地址，同一 Scope 内每个不同的值都要一块独立内存（每块 256 字节，从每帧上传环
分配），根常量的值直接录在命令列表里，draw 之间随时可改。

### D6　space2 保持每 pass 一份，Scope 参数走根常量　✅ 已定

相当于在 pass 一档内部引入 Scope 一档：数量小、变化多，用根常量实现。space2 只放各 Scope 一致的值，写了不同的值即断言
（§五）。

不做每个 Scope 一份 SRG：现在和 P4 规划的 pass 都用不到，等随 Scope 变的常量超过 128 字节时再加。不做全 bindless（取消
space2，大常量走每帧上传环、采样器改静态采样器）：要先建上传环、根 CBV 绑定、静态采样器声明、buffer 的 `.BindIndex`、
漏写检查，光栅 pass 的每个访问还要补 `.Stage`，过于激进。

### D7　bindless 的 shader 写法：沿用 `ResourceDescriptorHeap`　✅ 已定

材质贴图、SceneDownsample、Bloom 已经在用。DXC 翻到 SPIR-V 时，每种资源类型各生成一个 runtime array，全部别名到同一个
binding 上，保住 DX12"一个堆、一套下标"的语义：`ImageView` 给出的是堆内绝对下标，读与读写是两个描述符。这要求该
binding 为 `VK_DESCRIPTOR_TYPE_MUTABLE_EXT`，**`VK_EXT_mutable_descriptor_type` 因此是 Vulkan 后端的必需扩展**：桌面三家
都有，移动端驱动覆盖不全。

不改用按类型分开的数组：每种类型各一套下标空间，与引擎现有的下标语义不符，且要改所有已用 bindless 的 shader。

---

## 六、步骤（草案）

| 步骤 | 内容 | 依赖 |
|---|---|---|
| 1 | 视图表：`ViewData`、上传、槽位、shader 访问；RHI 部分写根常量；执行器写 `viewIndex`；删每视图 SRG | D1~D5 |
| 2 | compute 的 `Binds<>()`；改写 ScreenSpacePlan D2 / 0a | 1 |
| 3 | space2 一致性断言；`PassScopes.h` 注释 | D6 |

1 与 3 互不依赖；1 解锁 P4。

**验证**（步骤 1）：画面不变（主视图各 pass、阴影、TAA 的上一帧矩阵）；GPU-based validation 无报错。

---

## 七、未决

- **space1 的组常量放哪里**。`CommandList::BindShaderInputsFor*` 断言 PSO 布局里该 space 的 CBV 数与 SRG 编译出的 CBV 数
  相等（描述符表缺了会跳过，CBV 不会）。space1 若是"`g_Views` + 组 cbuffer"，只读 `GetView()` 的 shader 被 DXC 删掉
  cbuffer，每次绑定都断言。所以 space1 只放 `g_Views`，frame time 另选：
  - 进 `ViewData` 每一行：每行多十几字节、各行相同，读法统一为 `GetView(i).frameNumber`；
  - 只用 space0 已有的 `g_SceneFrameNumber` 一份：要时间的 shader 须绑 space0，承担 `SpaceZeroKeepAlive` 的负担。
- **pass 怎么绑上 space1**。以前由执行器在视图句柄处绑，pass 不声明；改为全组一份后要在 Scope 开头绑，同 space0。
  - `RendersView` 隐含绑定，只有 Build 里自己选视图的 compute 写 `Binds<Tag>()`；
  - 所有读视图的 pass 都显式 `Binds<Tag>()`，`RendersView` 只管 viewport 与 `viewIndex`。

  两种都要给 space1 的 SRG 实体新加一个 tag（`MainViewTag` 是视图类型的 tag，不能用）。
- **D3 与 `GlobalBuffer` 注释的冲突**。`GlobalBuffer.h` 的注释写明稳定槽位只在"别处存了这个下标且要跨帧"时才值得，只按
  下标读的数组应该用 `StagedArrayBuffer` 稠密打包（同 `g_Lights`）；视图目前正是后一种。保持 D3 并放宽这段注释的适用范围，
  或回到每帧重新编号。
- 视图数据从 CBV 改为结构化 buffer 后的访问开销：下标全 wave 一致时在现代 GPU 上差别很小，步骤 1 做完量一次。
- 多视图下"每个视图一份资源"（金字塔 / 归约类 pass）不在本文范围，见 `TODO_MultiViewPlan.md`。

---

## 关联文档

- `TODO_ScreenSpacePlan.md` —— D2 / 0a 待本文定稿后改写
- `TODO_MultiViewPlan.md` —— 已定决策 4（per-view SRG 与 `g_ShadowViews` 并存）；未决项"`g_Views` 能否取代 per-view SRG"
- `TODO_StructureAlignPlan.md` —— 共享组的布局权威、`SpaceZeroKeepAlive`
- `TODO_RenderGraphItemPlan.md` —— Scope 模型、逐 Scope 的屏障与根常量
