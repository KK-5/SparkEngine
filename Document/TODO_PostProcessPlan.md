# P3 后处理主干　实现方案

路线图 P3 的落地计划。总览与阶段依赖见 `TODO_RenderPipelineRoadmap.md`。

P3 做两件事：**Bloom** 和**换掉色调曲线**。同时这是引擎第一次在渲染图里跑 compute（I3），以及第一次给 View
挂 Bloom 与分级参数。

**曝光整条分支（直方图、EyeAdaptation、PreExposure）不在 P3**，见下方「推迟的部分」。曝光沿用手调常量。

---

## 状态

| 步骤 | 内容 | 依赖 | 状态 |
|---|---|---|---|
| 1 | 参数组件 `BloomComponent`（照 AntiAliasing 的模式） | — | 完成 |
| 2 | I3：SceneDownsample 链（第一个多 Scope 的 compute pass） | 1 | 代码完成，待 RenderDoc 验证（§五） |
| 3 | Bloom 上采样（第二个 compute pass，沿降采样链逐级累加） | 2 | 代码完成，待验证 |
| 4a | Tonemap 合入 Bloom | 3 | 代码完成，待验证 |
| 4b | Tonemap 换 AgX + Look，`ColorGradingComponent` | 4a | 未开始 |

执行顺序即表序。

**前置已就绪**：`TODO_RenderGraphItemPlan.md` 的 A~C 段（Scope、compute Scope 的 `Dispatch`、root constant、
`.BindIndex`、按 Scope 编译屏障）已完成。步骤 2、3 的 pass 按那份文档的 Scope API 写，不再需要新的渲染图机制。

---

## 推迟的部分：曝光

**推迟的是整条分支**：直方图、EyeAdaptation、`PreExposure`、以及随 `PreExposure` 而来的 TAA 校正。
`View` 的手调曝光常量照旧。

**触发条件：与物理灯光单位一起做。** 理由是自动曝光的收益全都要物理单位才兑现得出来，而且两者放一起才验得动：

- 在一个手调好的静态场景里，自动曝光的收益**是零**——`m_exposure` 已经是对的值了。它要有回报，得有亮度差异
  大的区域、相机移动、或者物理灯光单位。前两者我们的测试场景里没有。
- 真正的收益其实是**创作侧**的：现在灯光的 intensity 不是物理量，是美术对着 `exposure = 1.0` 的 tonemapper
  凑出来的数，跟色调曲线绑死——换曲线要重调所有灯，同一套灯搬去别的场景也不对。自动曝光把这两件事解耦，
  intensity 才能变成真实单位。**所以顺序应该是先有物理单位，自动曝光才有意义，反过来做没有验证对象。**
- `PreExposure` 更是如此：它**不改变任何一个像素**，是纯数值卫生。它防的是 FP16 的**指数溢出**——太阳盘面
  在 10⁹ cd/m² 量级，直接存就是 inf。而我们现在没有会溢出的内容，它一天也用不上。

  （更正本文档早先的说法：不是"暗部丢精度"。FP16 是浮点，相对精度在整个正常范围内恒定在 ~0.05%，
  缩放不会给你更多尾数位。失效发生在指数两端：超过 65504 变 inf，低于 ~6.1e-5 进次正规。）

推迟不作废已定的决策。D2（用直方图不用平均亮度）、D3（自动曝光驱动 `PreExposure`）的结论继续有效，
到时直接用。D7（`g_PreExposure` 怎么从 GPU 到达 shader）仍未定，届时再谈。

**到时 TAA 还要一起改的**：它在 `c / (1 + luma)` 的压缩空间里滤波与混合（`ToPerceptual`），拐点在亮度 1，
隐含"场景值 1 ≈ 屏幕白"。曝光不再是 1 后，拐点要跟着曝光走，改成 `c / (1 + luma × exposure)`：否则暗场景
（值远小于 1）几乎不压缩、萤火虫漏过，亮场景（值远大于 1）所有边缘都被过度压缩、线性能量损失放大。

---

## 决策记录

### D1　色调曲线　✅ 已定：AgX + Look

不用 ACES 拟合，也不用 UE 的 `FilmToneMap`。

"ACES" 在游戏里至少指三种不同的东西——Narkowicz 的一行有理式拟合、Hill 的矩阵夹拟合、完整的
IDT→RRT→ODT 链——三者画面能差出肉眼可辨的程度，主要在明亮高饱和区域。而 UE 的 `FilmToneMap` 是一条
参数化 filmic 曲线（slope / toe / shoulder / black clip / white clip，默认值调得接近 ACES RRT+ODT），
也不是其中任何一个。

选 AgX 的理由是**它解掉 ACES 系最明显的缺陷**："notorious six"——亮的饱和蓝偏紫、亮的饱和红偏橙黄，
色相被曲线拽走。AgX 在对数空间做映射，让饱和色随亮度朝白收敛而不是朝邻近色相滑，这也是真实胶片的行为。

代价是 AgX 开箱观感偏灰、中间调平、黑位被长 toe 抬起。所以 **Look 不是可选项**，是这条方案的一半。

**结构上零代价**：色调曲线在链条最末端，输出只进交换链，下游没有任何东西读它。对齐 UE 的硬目标是帧结构、
数据契约、插件点——曲线不在其中任何一项里。参数是为效果服务的，不是反过来。

以上对 ACES 与 AgX 的描述是定性的，没有实测。

### D2　自动曝光：直方图　✅ 已定，随曝光分支推迟

| | 做法 | 弱点 |
|---|---|---|
| 平均亮度 | log 亮度降采样到 1×1 | 一盏亮灯或一个暗门洞就把全图曝光拽走 |
| **直方图** | 64 bin，取 `LowPercent`~`HighPercent` 百分位 | 要 atomic + 两趟 dispatch |

百分位裁剪是选它的唯一理由，也是够硬的理由。对应 UE 的 Auto Exposure Histogram（它的默认档）。

### D3　自动曝光驱动 PreExposure　✅ 已定，随曝光分支推迟

做的时候 `PreExposure` 由上一帧的 EyeAdaptation 结果驱动，并接受随之而来的代价：TAA 的 history 是上一帧
曝光下的 SceneColor，`PreExposure` 一变就要除以 `PreExposureCorrection = PreExposure(N) / PreExposure(N-1)`。

注意区分两个曝光，它们**不是同一个数**：

| | 性质 | 何时用 |
|---|---|---|
| `g_PreExposure` | 编码缩放，纯数值卫生，不改画面 | 每个写 SceneColor 的 shader 乘，Tonemap 除回 |
| EyeAdaptation | 艺术/感知，决定画面明暗 | 只在 Tonemap 里乘，在色调曲线之前 |

还有一点常被混淆：**自动曝光本身不欠帧**。EyeAdaptation 在 GPU 上算完，同一帧 Tonemap 就能读。
欠帧的只有 `PreExposure`，因为它是 CPU 填的 cbuffer 常量。

### D4　Bloom：dual-filter，不用 UE4 高斯　✅ 已定

路线图原写"UE 高斯 Bloom 起步"，此处推翻。

- UE4 高斯：降采样链 + 每级可分离模糊 + 合并。pass 多，taps 多。
- **Jimenez dual-filter**（COD:AW 那套）：降采样 13-tap，上采样 9-tap tent 逐级累加。pass 少、taps 少。

效果不输，成本明显低。与 D1 同一个道理：结构对齐要的是"Bloom 这个 pass 在正确的位置、吃正确的输入"，
不是滤波核长什么样。

### D5　颜色分级：内联，不建 CombineLUTs　✅ 已定（由 D1 解掉）

AgX 的 Look 层本身就是分级层，形式是 **ASC CDL**——逐通道 `(in * slope + offset)^power`，加一个饱和度。
这是行业标准的分级原语，不是某个引擎专属的参数面。

3D LUT（UE 的 CombineLUTs）存在的理由是把一长串分级运算烘进一次三线性采样。我们现在只有 CDL 四个参数，
烘不出收益。等分级链长到值得烘的时候再建，那时 Look 的输入输出契约不变。

### D6　FXAA：不做　✅ 已定

它的价值是 TAA 关闭时的低配路径，而我们没有关 TAA 的场景。真需要时是纯加法。

### D7　`g_PreExposure` 的值怎么从 GPU 到达 shader　⬜ 未定，不在 P3

EyeAdaptation 在 GPU 上算出一个 1×1 的值，而 `g_PreExposure` 是 cbuffer 常量、由 CPU 侧的
`ViewBindingSystem` 填。这两者之间缺一段。

| | 做法 | 延迟 | 代价 |
|---|---|---|---|
| **A1** | GPU→CPU 回读，N 帧后写进 cbuffer | 2~3 帧 | 要建回读机制：host-visible buffer 池 + fence 跟踪。`HostMemoryAccess::Read` 与 `MemoryView::Map` 已存在，但**没有任何池化/围栏的上层封装**，规模待评估。这条路在渲染图之外 |
| **A2** | 值留在 GPU，`g_PreExposure` 从 cbuffer 常量改成 space1 的 1×1 SRV | 1 帧 | 不要新 RHI 机制。每个消费者多一次全 wave 一致的 load。但**移植 UE shader 时 `View.PreExposure` 要改成函数调用**，且 TAA 校正要同时拿两帧的值 |

UE 走 A1。倾向 A2，但它确实碰了数据契约，留到开工前定。

⚠️ **若选 A2，要先修 P2 遗留的共享组布局缺陷**：给 space1 加 cbuffer 字段是安全的（cbuffer 是单个描述符），
但加 **SRV** 会撞上同一个错位机制。

### D8　降采样链是共享的，阈值不加在链上　✅ 已定

原方案有矛盾：§一 要求链"建成可被别人消费的样子"（曝光分支的直方图要读它），又把 Bloom 阈值加在链的第一级。
加了阈值的链只剩高亮部分，直方图不能用。

| | 做法 | 代价 |
|---|---|---|
| **A** | 链不加阈值，由 SceneDownsample 独立拥有；Bloom 只做上采样。阈值默认不开；真要开，另建一个 BloomSetup pass 从链上取一级加阈值，再分出一条 Bloom 私有的链 | 默认的 Bloom 作用于全部亮度，观感靠低强度控制，而不是靠阈值截断 |
| B | 链归 Bloom 私有，第一级加阈值；曝光分支来时直方图另开一条链 | 以后多一条链；"第一个 compute 用例顺带给直方图铺路"这层收益没了 |

选 A。UE5 也是这个结构：`BloomThreshold` 默认 -1，即关闭；大于 -1 时才插入 BloomSetup。Jimenez 的
dual-filter 原本也不用阈值。A 还有一个附带的好处：链全程留在 PreExposure 域里，是线性运算，在 Tonemap 里和
SceneColor 一起除回即可。降采样 shader 因此不需要任何曝光相关的常量（见 D9）。

不加阈值也成立，是因为 HDR 自己起到了阈值的作用。核的尾巴只剩百分之一时，亮度 1000 的高光留下的仍然看得见，
亮度 1 的表面留下的看不见。阈值是 LDR 的做法：那时最亮只有 1，只能人为挑出亮部。

随之定下的：

- **`m_threshold` 不进 P3**，BloomSetup 等有需求时再加。
- **合成用插值**：`lerp(scene, bloom, m_intensity)`，能量守恒，`m_intensity` 的意思是"多少光被散射出去"。
  用加法会把整张画面按强度调亮，在没有阈值时相当于偷偷改了曝光。
- **各级等权，按级数归一化**。等权相加得到的核在距离 r 处约为 1/r²，与实测镜头 PSF 的尾部一致；归一化让
  窗口尺寸改变、级数随之跳变时，Bloom 亮度不跟着跳。各级权重不对外开放。
- **不加 Karis average**：它会压低高对比区域的平均亮度，放在共享链上会影响以后的直方图。关掉 TAA 后若看到
  Bloom 闪烁，再决定加在链上还是只加在 Bloom 那一侧。
  - 原理：权重 `1 / (1 + luma)`，即在 Reinhard 压缩过的空间里取平均，单个极亮样本的贡献被封顶在 1 左右。
    Jimenez 只用在第一级、按 13-tap 的 5 个盒子加权。TAA 的 `ToPerceptual` 是同一个技巧，所以 TAA 开着时
    萤火虫进 Bloom 前大多已被抹平。
  - 需要时的顺序：先从源头做高光抗锯齿（按法线变化放大粗糙度，Toksvig 或 Kaplanyan 的方法，属材质系统）；
    其次是 Bloom 私有的 Karis 第一级（意味着私有一整条链，降采样开销翻倍）；加在共享链第一级只作为曝光分支
    到来前的临时手段。
- **第一级降采样把非有限值清零**：否则一个 NaN / Inf 像素会被链扩散成一整块。

### D9　compute pass 拿不到视图参数，由 Build 从 View 读出、经 `.Constant` 传入　✅ 已定

有多个 Scope 的 pass 不能 `RendersView`（`RenderGraphBuilder::EndPass` 断言），`ComputePassBuilder` 也没有
`RendersView` / `Binds<>`，所以 compute shader 看不到 space1：`g_Exposure`、`g_OneOverPreExposure`、`g_ViewRectMin`
等都读不到。需要时由 Build 回调从 MainView 的 `View` 读出，以 root constant 或 space2 常量交给 shader。

- **曝光**：在 D8 的 A 下，链用不到曝光。只有加阈值的 BloomSetup 需要它：`View::m_exposure` 乘上 PreExposure 的
  倒数（今天是常量 1，见 `ViewBindingSystem`）。D7 若选 A2，曝光就留在 GPU 上，这条路径到那时要改。
- **视图区域**：今天 MainView 的 `m_rect` 恒为整个缓冲区，所以链直接覆盖整张 SceneColor。视图不占满缓冲区时
  （分屏、动态分辨率），第 0 级的读取区域要用常量传入。在此之前写成断言，不预先实现。
- 多个 MainView 各有一条链，要等 RenderGraphItemPlan「未决」里"多视图下 Scope 与视图的展开顺序"定下来。
  在此之前只处理第一个 MainView，即 `FindMainViewComponent<T>` 的语义。

### D10　链的各级用 `R16G16B16A16_FLOAT`　✅ 已定

各级都要作为 storage image 被写入。Vulkan 保证支持 storage 的格式里有 `R16G16B16A16_SFLOAT`；
`B10G11R11_UFLOAT_PACK32` 做 storage 要靠可选特性 `shaderStorageImageExtendedFormats`，不保证可用。DX12 两种都能用。
按"跟随更严格的后端"，选 RGBA16F。多出来的带宽发生在降采样后的小分辨率上，量很小。

### D11　风格参数放在场景的后处理 Volume 上，画质参数不放　✅ 已定

起因：编辑器相机不进场景，挂在它上面的 Bloom 无从编辑；把它放进场景又会随换场景被清掉。根子在于把"从哪里看"
（相机）和"场景看起来什么样"（风格）绑在了一起。

按参数回答的问题分两类：

| | 风格：Bloom、调色、曝光、运动模糊 | 重建质量：TAA、渲染分辨率、阴影精度 |
|---|---|---|
| 回答 | 场景**应该**长什么样 | 把它**还原得多准**、花多少代价 |
| 有无正确答案 | 没有，只有作者意图 | 有：无限超采样的那张理想图 |
| 换一台机器打开 | 不该变 | 可以变（低配关 TAA、降分辨率） |
| 归属 | 场景，随场景保存 | 渲染器 / 视口 |

- **风格参数挂在 Volume 实体上**（见 §三「后处理 Volume」），随场景保存、在层级里编辑。编辑器相机不挂任何风格
  组件，自然显示场景的风格。
- **TAA 暂时留在不进场景的编辑器相机上，不可调。** 画质设置的归宿另议：引擎倾向用组件而不是配置文件表达数据，
  画质设置用哪种实体承载，等出现第二个画质参数时再定。风格参数不回到相机上：相机上的风格组件只作为对 Volume
  的覆盖。
- 新建场景不自动放 Volume：没有 Volume 就没有 Bloom，调色退回默认 Look。

---

## 一、降采样链与 Bloom

```
SceneColor（或 TemporalAA）
   │
   ├─► SceneDownsamplePass（compute，N 个 Scope，每级独立纹理，不加阈值）
   │        │
   │        └─► BloomPass（compute，沿链逐级上采样累加）
   │                 │
   └─────────────────┴─► Tonemap
                           × g_OneOverPreExposure（SceneColor 与 Bloom 都乘）
                           × g_Exposure（手调）
                           + Bloom
                           AgX → Look → OETF
```

**每级一张独立纹理，不用 mip 链。** mip 链要逐 mip 屏障，那是 I4，排在 P4。独立纹理不依赖它：层级由 Scope
静态选定，代价只是多几个资源名，而瞬态资源本来就是池化的。

**两个 pass，两个 shader 文件。** 降采样与上采样是两种算法，按 RenderGraphItemPlan 第四条应是两个 pass。
shader 侧也只能这样拆：`ShaderAssetBuilder` 靠找 `CSMain` 这个名字认出 compute stage，一个文件只有一个 compute
入口，也没有宏变体；`ComputePassBuilder` 一个 pass 也只收一个 shader。两个文件用的滤波核不同（13-tap 与
tent），没有需要共享的代码。

**pass 之间交接的资源**写在 `Feature/PostProcess/PostProcessResources.h`（namespace `PostProcess`）：名字与形状
各定义一次，每个 pass 只 include 它，不 include 别的 pass。pass 头文件只暴露 `SetUp`，只有自己用的（`BloomUp2..7`、
链是否要建）留在自己的 .cpp 里。"主视图上的某个设置组件"用 `View/MainView.h` 的 `FindMainViewComponent<T>`。

- `SceneColorName`：TemporalAA 开着读它的输出，否则读 SceneColor。降采样第 0 级与 Tonemap 都用它。
- `SceneDownsampleChain::LevelCount(renderSize) = clamp(floor(log2(min(w, h))) - 4, 2, 8)`。720p 得 5，1080p 得 6，
  4K 得 7；最小一级约 16~32 像素，扩散范围相对屏幕基本不随分辨率变。下限 2 保证 Bloom 至少有一个 Scope。
- `SceneDownsampleChain::LevelSize(renderSize, j)`：逐级 `max(1, (n + 1) / 2)`。各级按归一化 UV 采样，奇数尺寸
  的错位不到一个纹素。
- `SceneDownsampleChain::LevelName(j)`：静态表 `SceneDownsample1..8`，不每帧构造 `ObjectName`。
- `BloomName` 与 `BloomScale`：Bloom 是 N 级之和，`BloomScale = 1 / N` 把它缩回一份能量，Tonemap 不必知道链的形状。

**SceneDownsamplePass**：第 j 级（j = 1..N）一个 Scope，读第 j-1 级（第 0 级即场景色），写第 j 级。

- `Read(上一级).BindIndex("inputIndex")`、`Write(本级).BindIndex("outputIndex")`，常量 `inputInvSize`、
  `outputSize` 都是 root constant；sampler（线性、clamp）在 space2，各 Scope 相同。`Dispatch(本级像素数)`。
- 滤波：Jimenez 的 13-tap。13 次双线性采样，组成 4 个角上的 4×4 盒子（各权重 0.125）加中心 4×4 盒子（0.5）。
  覆盖 6×6 个输入纹素，比 2×2 平均更接近理想低通，亮点移动一个像素时下一级的值连续变化，不闪。
- 结果中的非有限值清零（D8）。每级都做，只多一次比较，省掉"是否第一级"的分支。

**BloomPass**：j = N-1 → 1，每级一个 Scope，`BloomUp(j) = Down(j) + Tent(低一级)`。低一级在 j = N-1 时就是
`Down(N)`，之后是 `BloomUp(j+1)`，不需要额外拷贝。

- 不原地改共享链：链的版本号会被推高，后续读者（以后的直方图）读到的就成了被 Bloom 改过的版本。多出的显存约为
  半分辨率 RGBA16F 的 1/3。
- `BloomUp(1)` 对外叫 `Bloom`（`PostProcess::BloomName`），是 N 级之和；Tonemap 用 `BloomScale` 缩回，并进权重。
- 放大用 9-tap tent（`[1 2 1; 2 4 2; 1 2 1] / 16`，偏移一个低分辨率纹素）。直接双线性放大会出现菱形块，tent 让
  各级衔接连续。tent 半径 P3 不开放。

**Tonemap 合成**：`hdr = scene · (1 - i) + Bloom · (i / N)`，然后照旧 × `g_OneOverPreExposure` × `g_Exposure`。
没有 `ViewBloom` 时权重为 (1, 0)，槽里没有绑定，shader 用一个全 draw 一致的分支跳过读取：Vulkan 读空描述符要靠可选的 `nullDescriptor`（robustness2），不能依赖"读出来是 0"。Bloom 的 UV 由输入像素坐标换算，space1 为此加
`g_InputBufferSizeAndInvSize`（cbuffer 字段，碰不到 P2 的布局缺陷）。

链是共享的，阈值不在链上，见 D8。曝光分支到来后直方图会取这条链的某一级作为输入，**所以链要建成可被别人
消费的样子**：各级是有独立名字的 attachment，不是 Bloom 内部的私有中间量。

**为什么用降采样链当第一个 compute 用例**：它只写 image UAV，是 compute 里最简单的形状。直方图那种
atomic + buffer UAV 的形状留给第二个用例——第一个用例同时踩三件事，出问题不好定位哪一环。

---

## 二、Tonemap

现有 [Tonemap.hlsl](../Engine/Asset/Shaders/Tonemap/Tonemap.hlsl) 已经把 exposure / tone curve / OETF 分成三个
独立函数，换曲线只换 `ToneCurve` 的函数体，接缝不用新建。

AgX 的形状：

```
linear sRGB
  → inset 3×3 矩阵
  → log2 编码（固定动态范围，约 -12.47 ~ +4.026 EV）
  → sigmoid（多项式拟合）
  → Look：ASC CDL（offset / slope / power 逐通道 + saturation）
  → outset 3×3 矩阵
  → OETF
```

参考实现是 OCIO 配置带 3D LUT，但 shader 移植版成熟（three.js 带一个）。总量 50~60 行加两个矩阵常量。

Look 的默认值要给一组"把 AgX 从灰拉回来"的基准，**不能留单位值**——留单位值会让人以为 AgX 本身不能看。

---

## 三、参数组件（I6）

**不做一个 `PostProcessSettings` 大结构体，一个功能一组组件。** 代码库里已有完整先例——`Feature/AntiAliasing/`
的 TAA 参数——照着做即可：

```
Feature/<Name>/Components.h    <Name>Component   世界侧，作者编辑，SPARK_COMPONENT_TRAITS
Feature/<Name>/Reflect.h       编辑器反射
Render/View/ViewComponents.h   View<Name>        渲染侧，校验过的副本
CameraViewSystem.cpp           解析出设置→AddOrReplace，没有→Remove（风格参数从 Volume 解析，见下）
<Name>Pass.cpp                 FindMainViewComponent<View<Name>> 取不到就 Build 里 return
```

**组件在不在，就是功能开不开。** 大结构体要靠 `m_bloomEnabled` 这类 bool 表达的东西，在 ECS 里是免费的；
反过来，一个 Pass 读一个大结构体就等于声明依赖全部后处理参数，加一个字段所有 Pass 重编译。UE 的
`FPostProcessSettings` 是两百来个字段配一排 `bOverride_` 的单体，那正是不照抄的东西——它长成那样是为了
后处理体积之间的混合，而混合逐组件做同样成立。

**I6 其实已经完成了**：路线图把它列成待建的基础设施，但 P1 做 TAA 时这套机制就建好了。P3 不建机制，只用。

### 后处理 Volume（D11）

**一个实体就是一个 Volume**，由 `PostProcess::PostProcessVolumeComponent { m_priority }` 标明身份（世界侧模块
`Feature/PostProcess/`）。用组件而不是 tag：它现在就要优先级，以后还有范围、混合半径、混合权重。

**同一实体上的风格组件就是这个 Volume 覆盖的参数组**，组件在不在即覆盖不覆盖，对应 UE 一个字段一个 `bOverride_`。
Volume 是普通的场景实体：在层级里、随场景保存、换场景时清掉；由作者手动添加（任意实体上经 Component View 加
`PostProcessVolumeComponent`）。

**逐组解析**，每个视图、每组参数：

1. 相机自己的组件（覆盖，给游戏相机特殊处理用）；
2. 否则，带这组组件、优先级最高的 Volume；
3. 否则没有：Bloom 关，调色退回默认 Look。

- **"关"是强度为 0，不是开关字段**。高优先级 Volume 把强度设为 0 即覆盖掉低优先级的 Bloom；解析结果为 0 时不生成
  `ViewBloom`，pass 全部跳过。强度能插值，以后混合时可以平滑淡出，开关只能跳变。
- **同优先级冲突**：谁胜出取决于 entt 的遍历顺序，重新加载后不稳定。打一次警告（进入冲突状态时，而不是每帧），
  要求作者给出不同的优先级。
- **暂时全是 Unbound**：没有形状，解析与相机位置无关，每帧对全体 Volume 做一次（`CameraViewSystem` 的
  `FindVolumeSettings<T>`），再叠上相机自己的覆盖。

**以后的扩展**，结构不变，只改 `CameraViewSystem` 的解析：

- **局部 Volume**：`TransformComponent` + `VolumeBoxComponent { halfExtents }`；没有形状组件即全局，不设 Unbound
  字段。判断的是相机位置：变换到盒子的局部空间与半尺寸比较。Volume 数量是个位到几十，逐个判断即可。
- **混合**：`PostProcessVolumeComponent` 加 `m_blendRadius`（盒外到表面的距离内权重从 1 线性降到 0）与
  `m_blendWeight`（整体权重，动态效果如受伤暗角就改它）。按优先级升序叠加：
  `settings = lerp(settings, volume.settings, 距离权重 × blendWeight)`，相机自己的设置最后叠。每组风格组件为此
  提供逐字段插值。
- 用途：全局基调（最常用）、按区域切换（山洞、水下、剧情区域）、动态效果。

### P3 要加的两组

| 组件 | 字段 | 门控 |
|---|---|---|
| `Bloom::BloomComponent` → `ViewBloom` | `m_intensity`（没有阈值，见 D8） | **是**。解析不出设置、或强度为 0，就没有 Bloom pass，Tonemap 也不加 Bloom。降采样链此时也不建：P3 里它只有 Bloom 一个消费者。曝光分支到来后，要改成"任一消费者存在就建"。默认零开销 |
| `ColorGrading::ColorGradingComponent` → `ViewColorGrading` | ASC CDL：`m_slope`、`m_offset`、`m_power`、`m_saturation` | **否**，见下 |

**Look 不能靠"组件缺席"关掉。** D1 说过 AgX 开箱偏灰，Look 是这条方案的一半——缺席时退回单位值就是把
"AgX 不能看"的那一面直接端上来。所以：`ViewColorGrading` 的**成员默认值就是那组基准 Look**，TonemapPass
取不到组件时用一个默认构造的副本。组件的默认值与之相同，所以挂上组件在调之前什么也不改变。这样"组件在不在"仍然是有意义的（有没有 Volume 或相机设了自定义分级），而画面永远不会掉进未分级的 AgX。

**曝光不动**：`View::m_exposure` 保持现状。它跟上面两组不同——每个 View 都必须有一个曝光值，presence 门控
对它没有意义；而它要长出来的那些字段（method、min/max、百分位、speed up/down）属于推迟掉的曝光分支。
到那一轮再决定它是留在 `View` 上还是变成 `ExposureComponent`。

---

## 四、改动清单

| 步骤 | 文件 |
|---|---|
| 1 | 新增 `Feature/Bloom/{Components,Reflect}.h` 与 CMake（`SparkBloom`，同 `SparkAntiAliasing`）；`Engine.cpp` 注册反射；`ViewComponents.h` 加 `ViewBloom`；`CameraViewSystem.cpp` 加校验与 AddOrReplace/Remove。随后按 D11 移到 Volume：新增 `Feature/PostProcess/{Components,Reflect}.h`（`SparkPostProcess`），`CameraViewSystem.cpp` 按 Volume 解析；编辑器相机不挂 Bloom。`ColorGrading` 的同一套挪到步骤 4，它与 Look 的默认值一起定。`View::m_exposure` 不动 |
| 2 | 新增 `Render/Feature/SceneDownsample/SceneDownsamplePass.{h,cpp}`；`Render/Feature/PostProcess/PostProcessResources.{h,cpp}`（链的级数、名字、尺寸，场景色与 Bloom 的名字）；`Render/View/MainView.h`；`Shaders/SceneDownsample/SceneDownsample.hlsl`（CS）；`RenderSystem.cpp` 在 TemporalAA 与 Tonemap 之间注册 |
| 3 | 新增 `Render/Feature/Bloom/BloomPass.{h,cpp}` + `Shaders/Bloom/BloomUpsample.hlsl`（CS）；`RenderSystem.cpp` 注册在 SceneDownsample 之后 |
| 4 | `Tonemap.hlsl` 换 `ToneCurve`、加 Look 与 Bloom 合成；`TonemapPass.cpp` 加 Bloom 输入与 space2 常量。没有 Bloom 时权重为 0，shader 按权重跳过对 `g_Bloom` 的读取 |

路径沿用现有布局：pass 在 `Feature/Render/Feature/<Name>/`，shader 在 `Engine/Asset/Shaders/<Name>/`；
世界侧组件在 `Feature/<Name>/`（同 `Feature/AntiAliasing/`）。

---

## 五、验证

- **步骤 2**：第一个多 Scope 的 compute pass。重点不是画面而是机制。下面几条路径代码都已写好，但 ComputePass
  示例（单 Scope、只用 root constant、compute 写 → graphics 读）没有跑到过，要在 RenderDoc 下逐条确认：
  - 同一 pass 相邻 Scope 之间，对同一级的 UAV → SRV 转换屏障；
  - 渲染目标（SceneColor / TemporalAA）→ compute SRV 的转换；
  - compute pass 经 space2 绑定 sampler（`SetComputeRootDescriptorTable`）；
  - 各 Scope 的 root constant 不相互继承。

  降采样结果肉眼可查（就是模糊的 SceneColor）。队列用 Graphics：放到 async compute 会走跨队列路径，而那条路径
  没有运行时覆盖（RenderGraphItemPlan「未验证」）。
- **步骤 3**：Bloom 的扩散范围随级数变化，窗口尺寸改变时级数跟着变、不残留上一帧的级。阈值相关的验证取决于 D8。
- **Volume（D11）**：场景里没有 Volume 时没有 Bloom，降采样与 Bloom pass 都不出现；加一个带 Bloom 的 Volume 后
  出现，调它的强度实时生效；再加一个优先级更高、强度为 0 的 Volume，Bloom 消失；两者优先级相同时打一次警告；
  存盘、换场景再切回，Volume 与参数都在，编辑器相机始终不出现在层级里。
- **步骤 4**：AgX + Look 的观感。**这一步要留时间调 Look 的默认值**，不是接上就算完。同时验证亮饱和色
  （彩色自发光 / 强色光）不偏色相——这是选 AgX 的理由，不验证就等于没选。

全程 DX12 validation 零警告。

---

## 六、未决

- **AgX Look 的默认值**：只能调出来，不能算出来。步骤 4 留时间。
- **降采样链的级数**：按 `GetRenderSize()` 算（见 §一）；具体公式随 Bloom 细节一起定。曝光分支到来后直方图
  取哪一级是另一个数，两者不必相同。
- **D7**：见上，不在 P3。
- **P2 遗留的共享组布局缺陷**：P3 本身碰不到它（只加 cbuffer 字段和 space2 资源），但曝光分支若选 D7 的
  A2 就要先修。

---

## 关联文档

- `TODO_RenderPipelineRoadmap.md` —— 总览，P3 在 §四
- `TODO_StructureAlignPlan.md` —— P2，PreExposure 的来历与共享组布局缺陷
- `TODO_TemporalPlan.md` —— P1，TAA；曝光分支到来时要回头改它
