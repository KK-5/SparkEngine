# P3 后处理主干　实现方案

路线图 P3 的落地计划。总览与阶段依赖见 `TODO_RenderPipelineRoadmap.md`。

目标是把后处理从"一个 Reinhard + gamma 的 Tonemap"变成一条完整的链路：场景亮度自己测出来 → 曝光自动跟随 →
Bloom → filmic 曲线 + 分级 → 显示编码。同时这是引擎第一次在渲染图里跑 compute（I3），以及第一次给 View 挂
后处理参数（I6）。

---

## 状态

| 步骤 | 内容 | 依赖 | 状态 |
|---|---|---|---|
| 1 | I6：`PostProcessSettings` 挂 View，ViewBindings 加字段 | — | 未开始 |
| 2 | I3：SceneDownsample 链（第一个 compute pass） | 1 | 未开始 |
| 3 | Histogram + EyeAdaptation（compute，持久 1×1） | 2 | 未开始 |
| 4 | Bloom（dual-filter，复用降采样链） | 2 | 未开始 |
| 5 | Tonemap 换 AgX + Look，合入 Bloom 与自动曝光 | 3、4 | 未开始 |
| 6 | PreExposure 接自动曝光（含 TAA 校正） | 5、**D7** | 未开始 |

执行顺序即表序。**步骤 6 刻意排在最后**：前五步做完就是一条能用的后处理链，`PreExposure` 仍固定为 1；
第 6 步是纯加法，且它是唯一一个成本估不准的（见 D7）。这样前五步不被它阻塞。

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

### D2　自动曝光：直方图　✅ 已定

| | 做法 | 弱点 |
|---|---|---|
| 平均亮度 | log 亮度降采样到 1×1 | 一盏亮灯或一个暗门洞就把全图曝光拽走 |
| **直方图** | 64 bin，取 `LowPercent`~`HighPercent` 百分位 | 要 atomic + 两趟 dispatch |

百分位裁剪是选它的唯一理由，也是够硬的理由。对应 UE 的 Auto Exposure Histogram（它的默认档）。

### D3　自动曝光驱动 PreExposure　✅ 已定：驱动

`PreExposure` 不再固定为 1，由上一帧的 EyeAdaptation 结果驱动。这是 P2 铺这条管子的本来目的：场景亮度跨
几个数量级时，SceneColor 存原始值会在暗部丢 FP16 精度。

**已知代价，明确接受**：TAA 的 history 是上一帧曝光下的 SceneColor。`PreExposure` 一变，history 与当前帧
不同量纲，TAA 里要除以 `PreExposureCorrection = PreExposure(N) / PreExposure(N-1)` 补回来。也就是说 P3 会
回头动 P1 的 TAA。

注意区分两个曝光，它们**不是同一个数**：

| | 性质 | 何时用 |
|---|---|---|
| `g_PreExposure` | 编码缩放，纯数值卫生 | 每个写 SceneColor 的 shader 乘，Tonemap 除回 |
| EyeAdaptation | 艺术/感知，决定画面明暗 | 只在 Tonemap 里乘，在色调曲线之前 |

两者都来自同一个测量值，但 PreExposure 有延迟、可以取整到 2 的幂、可以钳制范围——它错了只影响精度不影响
画面；EyeAdaptation 错了画面就错。

### D4　Bloom：dual-filter，不用 UE4 高斯　✅ 已定

路线图原写"UE 高斯 Bloom 起步"，此处推翻。

- UE4 高斯：降采样链 + 每级可分离模糊 + 合并。pass 多，taps 多。
- **Jimenez dual-filter**（COD:AW 那套）：降采样 13-tap，上采样 9-tap tent 逐级累加。pass 少、taps 少，
  **而且直接复用 D2 要建的降采样链**。

效果不输，成本明显低。与 D1 同一个道理：结构对齐要的是"Bloom 这个 pass 在正确的位置、吃正确的输入"，
不是滤波核长什么样。

### D5　颜色分级：内联，不建 CombineLUTs　✅ 已定（由 D1 解掉）

AgX 的 Look 层本身就是分级层，形式是 **ASC CDL**——逐通道 `(in * slope + offset)^power`，加一个饱和度。
这是行业标准的分级原语，不是某个引擎专属的参数面。

3D LUT（UE 的 CombineLUTs）存在的理由是把一长串分级运算烘进一次三线性采样。我们现在只有 CDL 四个参数，
烘不出收益。等分级链长到值得烘的时候再建，那时 Look 的输入输出契约不变。

### D6　FXAA：不做　✅ 已定

它的价值是 TAA 关闭时的低配路径，而我们没有关 TAA 的场景。真需要时是纯加法。

### D7　`g_PreExposure` 的值怎么从 GPU 到达 shader　⬜ 未定

**这是 P3 唯一一个成本估不准的点，也是步骤 6 排在最后的原因。**

问题：EyeAdaptation 在 GPU 上算出来（一个 1×1 的值），而 `g_PreExposure` 是 `ViewBindings` cbuffer 里的一个
常量，由 CPU 侧的 `ViewBindingSystem` 填。这两者之间缺一段。

| | 做法 | 延迟 | 代价 |
|---|---|---|---|
| **A1** | GPU→CPU 回读。1×1 值拷到 host-visible buffer，N 帧后 `ViewBindingSystem` 读出来写进 cbuffer | 2~3 帧 | 要建回读机制：host-visible buffer 池 + fence 跟踪。`HostMemoryAccess::Read` 与 `MemoryView::Map` 已存在，但**没有任何池化/围栏的上层封装**，规模待评估。这条路在渲染图之外 |
| **A2** | 值留在 GPU。`g_PreExposure` 从 cbuffer 常量改成 space1 的一个 1×1 SRV，每个写 SceneColor 的 shader 读它 | 1 帧 | 不要新 RHI 机制。每个消费者多一次 buffer load（全 wave 一致，实际接近免费）。但**移植 UE shader 时 `View.PreExposure` 要改成函数调用**，而且 TAA 校正要同时拿当前帧和上一帧两个值 |

UE 走 A1。倾向 A2，理由是它不需要在渲染图之外新建一套带围栏的回读路径——那是一块有自己的正确性陷阱、
规模未知的工作；而 A2 的代价是可量化的、分散的、很小的。

反对 A2 的点是它确实碰了数据契约：移植来的 UE shader 写 `View.PreExposure` 时假设那是个常量。

**开工步骤 6 之前定。前五步不受影响。**

---

## 一、曝光链路

一帧之内的顺序：

```
SceneColor (× PreExposure)
   │
   ├─► SceneDownsample 链（compute，每级独立纹理）────┬─► Histogram（compute，atomic → 64 bin buffer）
   │                                                  │        │
   │                                                  │        ▼
   │                                                  │   EyeAdaptation（compute，1×1 持久）
   │                                                  │        │
   │                                                  └─► Bloom（dual-filter，沿链上采样累加）
   │                                                           │
   └──────────────────────────────────────────────────────────►┴─► Tonemap
                                                                     ÷ PreExposure
                                                                     × EyeAdaptation
                                                                     + Bloom
                                                                     AgX → Look → OETF
```

**Tonemap 读的是本帧的 EyeAdaptation，没有延迟**——它在 GPU 上，同一帧算完就能用。有延迟的只有
`PreExposure`（D7）。这一点常被混淆：自动曝光本身不欠一帧，欠帧的是编码缩放。

EyeAdaptation 的 1×1 结果用现成的 `ReadPreviousImageAttachment` 机制持有（I0，TAA history 用的同一套），
不需要新机制：本帧写，下一帧读回来做 speed-up / speed-down 平滑。

直方图的输入取降采样链的某一级而不是全分辨率——全分辨率打 atomic 是纯浪费，采样密度远超需要。

---

## 二、降采样链与 Bloom

**每级一张独立纹理，不用 mip 链。** mip 链要逐 mip 屏障，那是 I4，排在 P4。独立纹理不依赖它，代价是多几个
资源名，而瞬态资源本来就是池化的。

链的两个消费者：直方图取中间某一级，Bloom 沿整条链上采样累加。所以链只建一次。

Bloom 的阈值（`BloomThreshold`）在降采样的第一级施加，作用在**已乘 EyeAdaptation 的亮度**上——否则阈值的
含义会随场景亮度漂移。这要求第一级降采样能拿到上一帧的 EyeAdaptation，用同一个持久 1×1。

---

## 三、Tonemap

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

Look 的参数进 `PostProcessSettings`（§四）。默认值要给一组"把 AgX 从灰拉回来"的基准，不能留单位值——
留单位值会让人以为 AgX 本身不能看。

---

## 四、PostProcessSettings（I6）

挂在 View 上，形状参照 UE 的 `FPostProcessSettings` 但**只收本阶段真的有消费者的字段**。没有消费者的字段
不进结构——那是在给未来的自己留一堆不知道对不对的默认值。

| 组 | 字段 |
|---|---|
| 自动曝光 | `m_method`（Manual / Histogram）、`m_minBrightness`、`m_maxBrightness`、`m_lowPercent`、`m_highPercent`、`m_speedUp`、`m_speedDown`、`m_exposureCompensation` |
| Bloom | `m_bloomIntensity`、`m_bloomThreshold` |
| Look（ASC CDL） | `m_slope`、`m_offset`、`m_power`、`m_saturation` |

`View::m_exposure` 这个裸 float 被 `m_method = Manual` + `m_exposureCompensation` 取代。

哪些进 `ViewBindings` cbuffer、哪些进各 Pass 自己的 space2，按消费者数量分：被多个 Pass 读的（曝光相关）
进 View 组，只有 Tonemap 读的（Look、Bloom 强度）进 Tonemap 的 space2。

**注意 space0 的布局缺陷**（P2 遗留）：给 `ViewBindings` 加字段是安全的（cbuffer 是单个描述符，不受表内
偏移错位影响），但如果 P3 要给 space1 加 **SRV**（D7 的 A2 就要加一个），那会撞上同一个机制——届时先修
`BuildPipelineLayoutFromShaders`，别再加绕过。

---

## 五、改动清单

| 步骤 | 文件 |
|---|---|
| 1 | 新增 `View/PostProcessSettings.h`；`View.h` 去掉 `m_exposure`；`ViewComponents.h` 加组件；`CameraViewSystem.cpp` 填充；`ViewBindings.hlsli` / `ViewBindingsReflect.hlsl` / `ViewBindingSystem.cpp` 加字段 |
| 2 | 新增 `Feature/PostProcess/SceneDownsamplePass.{h,cpp}` + `Shaders/PostProcess/SceneDownsample.hlsl`（CS） |
| 3 | 新增 `Feature/PostProcess/HistogramPass`、`EyeAdaptationPass` + 对应 CS |
| 4 | 新增 `Feature/PostProcess/BloomPass` + `Bloom.hlsl`（CS，下采样与上采样两个入口） |
| 5 | `Tonemap.hlsl` 换 `ToneCurve`、加 Look 与 Bloom 合成；`TonemapPass.cpp` 加输入 |
| 6 | 按 D7 的结论定 |

---

## 六、验证

- **步骤 2**：第一个 compute pass。重点不是画面而是机制——RenderDoc 下确认 dispatch 发出、UAV 写入正确、
  compute→graphics 屏障存在。降采样结果肉眼可查（就是模糊的 SceneColor）。
- **步骤 3**：明暗场景之间移动相机，曝光平滑跟随，不过冲不振荡。把一盏很亮的小灯拉进画面，**曝光不应该
  被它拽走**——这是选直方图而非平均亮度的唯一理由，必须验证到。
- **步骤 4**：Bloom 只作用于高亮；阈值以下的区域完全不变。
- **步骤 5**：AgX + Look 的观感。**这一步要留时间调 Look 的默认值**，不是接上就算完。同时验证亮饱和色
  （彩色自发光 / 强色光）不偏色相——这是选 AgX 的理由。
- **步骤 6**：`PreExposure` 生效后画面与它固定为 1 时**一致**（它是编码缩放，不该改变画面）。暗部在明亮
  场景下的精度提升用 RenderDoc 看 SceneColor 的原始值确认。TAA 在曝光剧变时不拖尾、不闪。

全程 DX12 validation 零警告。

---

## 七、未决

- **D7**：`g_PreExposure` 的传递路径，见上。步骤 6 开工前定。
- **AgX Look 的默认值**：只能调出来，不能算出来。步骤 5 留时间。
- **降采样链的级数与直方图取哪一级**：级数由 Bloom 的最大扩散半径决定，直方图那一级由采样密度决定，
  两者不必相同。实现时按分辨率定，先写成常量。
- **`ExposureCompensation` 的曲线**：UE 支持按亮度查曲线做补偿（`ExposureCompensationCurve`）。先做常量，
  曲线等有 curve 资产类型时再说。
- **P2 遗留的共享组布局缺陷**：若 D7 选 A2，P3 里就要先修它，见 §四末。

---

## 关联文档

- `TODO_RenderPipelineRoadmap.md` —— 总览，P3 在 §四
- `TODO_StructureAlignPlan.md` —— P2，PreExposure 的来历与共享组布局缺陷
- `TODO_TemporalPlan.md` —— P1，TAA；步骤 6 要回头改它
