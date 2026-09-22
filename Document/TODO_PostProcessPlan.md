# P3 后处理主干　实现方案

路线图 P3 的落地计划。总览与阶段依赖见 `TODO_RenderPipelineRoadmap.md`。

P3 做两件事：**Bloom** 和**换掉色调曲线**。同时这是引擎第一次在渲染图里跑 compute（I3），以及第一次给 View
挂后处理参数（I6）。

**曝光整条分支（直方图、EyeAdaptation、PreExposure）不在 P3**，见下方「推迟的部分」。曝光沿用手调常量。

---

## 状态

| 步骤 | 内容 | 依赖 | 状态 |
|---|---|---|---|
| 1 | I6：`PostProcessSettings` 挂 View（Bloom + Look + 手调曝光） | — | 未开始 |
| 2 | I3：SceneDownsample 链（第一个 compute pass） | 1 | 未开始 |
| 3 | Bloom（dual-filter，沿降采样链上采样累加） | 2 | 未开始 |
| 4 | Tonemap 换 AgX + Look，合入 Bloom | 1、3 | 未开始 |

执行顺序即表序。

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

---

## 一、降采样链与 Bloom

```
SceneColor
   │
   ├─► SceneDownsample 链（compute，每级独立纹理）──► Bloom 沿链上采样累加
   │                                                        │
   └────────────────────────────────────────────────────────┴─► Tonemap
                                                                  × g_Exposure（手调）
                                                                  + Bloom
                                                                  AgX → Look → OETF
```

**每级一张独立纹理，不用 mip 链。** mip 链要逐 mip 屏障，那是 I4，排在 P4。独立纹理不依赖它，代价是多几个
资源名，而瞬态资源本来就是池化的。

链的级数由 Bloom 的最大扩散半径决定。曝光分支到来后直方图会取这条链的某一级作为输入——**所以链要建成
可被别人消费的样子**（独立的 attachment 名，不是 Bloom 内部的私有中间量）。

Bloom 的阈值 `m_bloomThreshold` 在降采样第一级施加，作用在**已乘 `g_Exposure` 的亮度**上。曝光是手调常量时
这只是一次乘法；将来换成 EyeAdaptation，阈值的含义自动跟着走，不用改公式。

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

## 三、PostProcessSettings（I6）

挂在 View 上，形状参照 UE 的 `FPostProcessSettings` 但**只收本阶段真的有消费者的字段**。没有消费者的字段
不进结构——那是在给未来的自己留一堆不知道对不对的默认值。

| 组 | 字段 |
|---|---|
| 曝光 | `m_exposure`（手调，从 `View::m_exposure` 搬过来） |
| Bloom | `m_bloomIntensity`、`m_bloomThreshold` |
| Look（ASC CDL） | `m_slope`、`m_offset`、`m_power`、`m_saturation` |

曝光组到自动曝光那一轮再扩（`m_method`、min/max 亮度、百分位、speed up/down、曝光补偿）。

哪些进 `ViewBindings` cbuffer、哪些进各 Pass 自己的 space2，按消费者数量分：`m_exposure` 已经在 View 组里
（`g_Exposure`），保持不动；Look 与 Bloom 强度只有 Tonemap 读，进 Tonemap 的 space2。

---

## 四、改动清单

| 步骤 | 文件 |
|---|---|
| 1 | 新增 `View/PostProcessSettings.h`；`View.h` 的 `m_exposure` 搬进去；`ViewComponents.h` 加组件；`CameraViewSystem.cpp` 填充 |
| 2 | 新增 `Feature/PostProcess/SceneDownsamplePass.{h,cpp}` + `Shaders/PostProcess/SceneDownsample.hlsl`（CS） |
| 3 | 新增 `Feature/PostProcess/BloomPass.{h,cpp}` + `Shaders/PostProcess/Bloom.hlsl`（CS，下采样与上采样两个入口） |
| 4 | `Tonemap.hlsl` 换 `ToneCurve`、加 Look 与 Bloom 合成；`TonemapPass.cpp` 加 Bloom 输入与 space2 常量 |

---

## 五、验证

- **步骤 2**：第一个 compute pass。重点不是画面而是机制——RenderDoc 下确认 dispatch 发出、UAV 写入正确、
  compute→graphics 屏障存在。降采样结果肉眼可查（就是模糊的 SceneColor）。
- **步骤 3**：Bloom 只作用于高亮；阈值以下的区域完全不变。改 `m_bloomThreshold` 时起作用的范围跟着变。
- **步骤 4**：AgX + Look 的观感。**这一步要留时间调 Look 的默认值**，不是接上就算完。同时验证亮饱和色
  （彩色自发光 / 强色光）不偏色相——这是选 AgX 的理由，不验证就等于没选。

全程 DX12 validation 零警告。

---

## 六、未决

- **AgX Look 的默认值**：只能调出来，不能算出来。步骤 4 留时间。
- **降采样链的级数**：由 Bloom 的最大扩散半径决定，按分辨率定，先写成常量。曝光分支到来后直方图取哪一级
  是另一个数，两者不必相同。
- **D7**：见上，不在 P3。
- **P2 遗留的共享组布局缺陷**：P3 本身碰不到它（只加 cbuffer 字段和 space2 资源），但曝光分支若选 D7 的
  A2 就要先修。

---

## 关联文档

- `TODO_RenderPipelineRoadmap.md` —— 总览，P3 在 §四
- `TODO_StructureAlignPlan.md` —— P2，PreExposure 的来历与共享组布局缺陷
- `TODO_TemporalPlan.md` —— P1，TAA；曝光分支到来时要回头改它
