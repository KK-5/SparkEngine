# 阴影优化

前置：`TODO_MultiViewPlan.md` §五（阴影的落地实现，第 1~5 步已完成）。本文只写优化，实现细节不重复。

## 基线

| 项 | 值 |
|---|---|
| atlas | 4096²，D32_FLOAT，持久 imported |
| tile | 4×4 = 16，每块 1024²，border 1 texel，可用 1022 |
| 方向光 | 正交盒 halfExtent 12，pullback 200，盒原点按纹素量化 |
| 聚光灯 | 透视 fov = 2×outerCone，near 0.05 |
| 光栅 | slope-scale bias 2.0，cull back |
| 滤波 | 9 抽头 Vogel 圆盘，半径正交 2.0 / 透视 1.0 texel，抽头 clamp 进 tile |
| 采样端 bias | `m_shadowBias` 5e-4（NDC）、`m_shadowNormalOffset` 0.02（世界单位）|

## 一、纹素密度是唯一的杠杆

边缘锯齿的一级台阶就是一个 texel。PCF 抽头数、bias、滤波方式都不改变台阶大小。

```
方向光：单位/texel = 2 · halfExtent / usableTexels
聚光灯：单位/texel = 2 · d · tan(outerHalf) / usableTexels     (d = 距光源距离，最糟在 d = range)
```

两类光由完全不同的量决定，`usableTexels` 是唯一的公共项。

实测（usableTexels = 1022）：

| 光源 | 参数 | 单位/texel |
|---|---|---|
| 方向光 | halfExtent 12 | 0.023 |
| 方向光 | halfExtent 30 | 0.059 |
| 聚光灯 | 30° / range 10 | 0.011 |
| 聚光灯 | 30° / range 50 | 0.057 |
| 聚光灯 | 60° / range 20 | 0.068 |

tile 尺寸不是瓶颈。方向光差在盒子——60 个世界单位摊在一块 tile 上；聚光灯差在锥角和 range 把纹素铺到照不亮的远处。

## 二、方向光已撞到单 tile 的上界

`halfExtent = 12` 换来 0.023 单位/texel，代价是**离相机 12 单位之外没有阴影**——采样落在 tile rect 之外，返回受光。这是一条硬边界，不是锯齿。

距离与密度在一块 tile 上不可兼得，要同时拿到必须有多个不同尺度的 tile，即 clipmap。**方向光的下一个台阶是结构性的，没有中间形态的调参。**

## 三、bias 的量纲（推迟，并入 §五 第 6 步）

`m_shadowBias` 的单位是 NDC 深度，世界含义随投影变化：

| | 固定 5e-4 实际是 | 1 texel 应为 | 倍数 |
|---|---|---|---|
| 方向光 | 0.106 世界单位 | 0.023 | 4.5× |
| 聚光灯 d=1 | 0.010 | 0.0011 | 8.8× |
| 聚光灯 d=10 | 0.995 | 0.011 | 88× |

过度偏置 ∝ d，同一盏灯内部跨 10 倍，任何单一取值都必然在一端错。

推迟的依据：透视光的 `1/d²` 衰减与 range 窗函数让误差最大处恰好是能量最小处，远端阴影本就不可见；方向光那 4.5 倍是普通的「bias 调大了」，滑块可解。

**失效条件**，满足任一条就要做：

- tile 分辨率不再统一（第 6 步的分辨率阶梯）：`texelWorldSize` 逐 tile 不同，一个 NDC 数值连两盏方向光都伺候不了
- 高强度长射程聚光灯：作者拉高 intensity 补偿衰减后，远端不再暗

### 方案

`clip.w` 是两种投影的全部差别——正交恒为 1，透视为光源视空间深度（`TileRemap` / `ClipToUV` 只动 x/y，w 透传）。于是 `texelWorld = texelWorldSize · clip.w` 对两者同时成立，深度导数统一为 `depthSlope / w²`（正交 w=1 时 `/w²` 是空操作）。常量部分在 CPU 折成两个系数，`ShadowViewData` 字段数与 96B 布局不变：

```
m_depthBiasCoeff    = biasTexels   · texelWorldSize · depthSlope   → shader: / clip.w
m_normalOffsetCoeff = offsetTexels · texelWorldSize                → shader: * clip.w
```

| 侧 | 改动 |
|---|---|
| `LightComponent` / `LightRenderData` / `LightSystem` / `Reflect.h` | `m_shadowBias` → `m_shadowBiasTexels = 1.0`、`m_shadowNormalOffset` → `m_shadowNormalOffsetTexels = 2.0`；滑块范围 0..4、步进 0.1 |
| `ShadowViewSystem` | view 实体加 `ShadowViewBias { texelWorldSize, depthSlope }`；正交 `depthSlope = 1/(f-n)`，透视 `f·n/(f-n)`。投影参数只有这里知道，不去反推 `m_viewToClip` |
| `SceneBindingSystem` | `PackShadowViews` 写每 texel 的系数，光源循环乘作者的 texel 数——两阶段各读各的来源 |
| `Lib/Lights.hlsli` | 法线偏移先于投影，w 需提前取：`dot(sv.worldToShadowUV[3], float4(worldPos, 1))`。HLSL 的 `M[3]` 是逻辑第 4 行，与存储布局无关 |

第 6 步会算出 `texelWorldSize`，届时这一步只剩改名和两次乘法。

可选的一行止血：默认值 5e-4 → 1e-4。三种情形全部改善，且无一处转为欠偏置（欠偏置长痘，比脱节难看）。

## 四、明确不做

| 不做 | 理由 |
|---|---|
| 方向光视锥拟合（外接球） | 被 clipmap 取代 |
| 级联 | 被 clipmap 取代 |
| 四叉树 tile 分配器 | 被 VSM 的 page 分配取代；且判据未定前不该先写分配器 |

分配器的判据是**屏幕占比而非光源类型**：一盏近处的广角聚光灯完全可能比远处的方向光更该拿大 tile。

## 五、clipmap / VSM 之后的存活情况

| 手段 | 之后 |
|---|---|
| texel snapping | 升级成必需——page 缓存靠它成立，不 snap 则每帧全部失效 |
| 按屏幕占比定分辨率 | 升级成核心——即 VSM 的 page 等级选择 |
| 抽头 clamp 进 rect | 升级成必需——page 在物理内存里散布，虚拟空间相邻的两页物理上不相邻，固定 border 原理上失效；每抽头独立解析地址是它的一般形式 |
| 聚光灯 near plane 收紧 | 原样有效（管深度精度，与横向密度无关，两者别混）|
| slope-scaled bias | 原样有效，光栅状态 |
| PCF | 有效，边界问题从 tile 变成 page |
| 视锥拟合 / 级联 | 被取代 |

`worldToShadowUV` 预乘 tile 变换、shader 完全不知道布局的做法直接迁移：VSM 下变成预乘进虚拟地址空间，shader 多一次 page table 查找，调用方不动。

## 六、顺序

1. `TODO_MultiViewPlan.md` §五 第 6 步（投影判据 + 分辨率阶梯 + gate），bias 量纲并入
2. 同 §五 第 7 步（点光源 6 面）
3. clipmap —— 方向光唯一的出路
