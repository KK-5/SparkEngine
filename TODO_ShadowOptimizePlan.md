# 阴影优化

前置：`TODO_MultiViewPlan.md` §五（阴影的落地实现，第 1~5 步已完成）。本文只写优化，实现细节不重复。

## 基线

写这份基线时是固定 4×4 网格。6b 落地后 tile 尺寸可变，那一行已经过时——保留原值是为了让下面几节的实测数字有参照系，现状见 §七。

| 项 | 值 |
|---|---|
| atlas | 4096²，D32_FLOAT，持久 imported |
| tile | ~~4×4 = 16，每块 1024²~~ → 见 §七，2048/1024/512 三档，border 1 texel |
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

tile 尺寸不是瓶颈。方向光差在盒子——60 个世界单位摊在一块 tile 上；聚光灯差在锥角。

**上表里 range 那一列会骗人，读的时候注意。** `range` 不进入横向密度的算式——透视投影的 x/y 缩放是 `1/tan(fovY/2)`，只跟角度有关，near/far 一概不影响。表里 range 之所以出现，是因为它决定了**最坏点的位置** `d = range`。对一块固定位置的几何，改 range 对它头顶的纹素大小毫无作用（改的是深度精度和 bias 的量纲）。实测确认过：调 range 对锯齿零效果。

真正能动的只有 `d`（场景布局）、`usableTexels`（已在上限）、`tan(outerHalf)`（锥角本身）。展开见 §八。

## 二、方向光已撞到单 tile 的上界

`halfExtent = 12` 换来 0.023 单位/texel，代价是**离相机 12 单位之外没有阴影**——采样落在 tile rect 之外，返回受光。这是一条硬边界，不是锯齿。

距离与密度在一块 tile 上不可兼得，要同时拿到必须有多个不同尺度的 tile，即 clipmap。**方向光的下一个台阶是结构性的，没有中间形态的调参。**

## 三、bias 的量纲（**失效条件已触发，待做**）

`m_shadowBias` 的单位是 NDC 深度，世界含义随投影变化：

| | 固定 5e-4 实际是 | 1 texel 应为 | 倍数 |
|---|---|---|---|
| 方向光 | 0.106 世界单位 | 0.023 | 4.5× |
| 聚光灯 d=1 | 0.010 | 0.0011 | 8.8× |
| 聚光灯 d=10 | 0.995 | 0.011 | 88× |

过度偏置 ∝ d，同一盏灯内部跨 10 倍，任何单一取值都必然在一端错。

推迟的依据：透视光的 `1/d²` 衰减与 range 窗函数让误差最大处恰好是能量最小处，远端阴影本就不可见；方向光那 4.5 倍是普通的「bias 调大了」，滑块可解。

**失效条件**，满足任一条就要做：

- ~~tile 分辨率不再统一（分辨率阶梯）~~：**已发生**。§七 落地后 512² 的灯纹素是 2048² 的灯的 4 倍大，两者共用同一个 NDC 数值，必然有一端错
- 高强度长射程聚光灯：作者拉高 intensity 补偿衰减后，远端不再暗

现在做比原计划便宜：这一步要的 `texelWorldSize` 已经由 §七 的 `ShadowUsableTexels(level)` 提供，剩下的就是改名和两次乘法。

肉眼可见的症状：接触阴影整片脱离物体（过度偏置 ∝ d，聚光灯 d=10 处是应有值的 88 倍）。

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

## 六、6a：光源级剔除 + 优先级预算

只剔**光源**，不碰物体，tile 大小仍统一。产出是「这一帧哪些光源持有 tile」。

物体级剔除属于 `TODO_MultiViewPlan.md` §八，前置是稠密稳定对象索引，与本节无关。

### 步骤

**1. `Math::Frustum`。**（已完成）

`Core/Math/Frustum.h`，六个内向平面 + `FromViewProjection` + `IntersectsSphere`。
`MathUtils.h` 补 `Tan`，`Matrix4x4.h` 补 `Row(m, i)`。

平面提取的两条硬约束：

- **LH_ZO 的近平面是 row2，不是 row3 + row2。** `[-1,1]` 的写法会把近平面放到约一半距离处，只在物体贴近相机时暴露。
- **必须按 `(a,b,c)` 模长归一化**，否则 `d` 不是度量距离，球测试失效。

覆盖测试在 `Test/Core/MathTest.cpp`（`MATH_TESTS` 选项）。构型取「原点看 +Z、90° fov、正方形」，此时侧平面即 `z = |x|` / `z = |y|`，期望值可手算。`NearPlaneUsesZeroToOneConvention` 专钉上面第一条。

**2. 光源体积。**（已完成）

包围体在**上游产生并作为组件承载**，不在 `ShadowViewSystem` 里构造：

| 组件 | 谁写 | 内容 |
|---|---|---|
| `Light::LightBounds` | `LightSystem::OnTick` | 点光源是 `(m_worldPosition, m_range)` 的球，聚光灯是锥的最小外接球。**方向光没有这个组件** |
| `Render::ViewFrustum` | `CameraViewSystem::Update` | 写完 `m_viewToClip` 后随即 `Frustum::FromViewProjection` |

`ViewFrustum` 挂在 **view 实体**而非世界相机实体：视锥需要投影，而投影在渲染层构建（aspect 是渲染目标属性）。同一个组件将来覆盖 shadow view 的面级剔除。

各 view 生产者各写各的视锥，不做集中派生——`ShadowViewSystem` 读主视角视锥且跑在编码步骤之前，集中派生会晚一帧。

`Math::Sphere::FromCone` 放 `Core/Math/`，锥（高 `h`，底半径 `R = h·tan(halfAngle)`）的最小外接球：

```
R >= h:  center = apex + axis·h,  radius = R
R <  h:  d = (h² + R²) / (2h),    center = apex + axis·d,  radius = d
```

不写锥-视锥精确相交，外接球够用。

`m_range` 是径向截断而锥的 `height` 是轴向，径向 ≤ range ⇒ 轴向 ≤ range，所以这样传是从外侧包住照亮区域，方向安全。

**3+4. 评分与 `Update` 重构。**（已完成，两步合并——评分函数与用它的循环分开评审看不出对错。）

`ShadowViewSystem::Update` 从「遍历光源 → find-or-create → 立刻分配 tile，先到先得」改成：

```
gate（atlas 就绪）+ sweep（死光源 / 停止投影的光源）        不变
ResolveMainView：eye、frustum、proj11、valid
遍历投影光源：  视锥外 / 低于阈值 → Deactivate      幸存 → push 进候选
sort（加权分降序）
第 kShadowTileCount 名之后 → Deactivate     前 N 名 → Activate
```

评分用 NDC 下的投影半径，不换算像素——这里跑在 render graph 之前，拿不到 attachment extent：

```
proj11    = mainView.m_viewToClip[1][1]        // 1/tan(fovY/2)
ndcRadius = radius · proj11 / sqrt(d² - radius²)     // d² <= radius² 时相机在体积内，取最大值
```

`kScoreEnter = 0.03`、`kScoreExit = 0.02`（约 1080p 下 20~30 像素高）、`kIncumbentBonus = 1.25`。

**被剔除的光源当场 `Deactivate`，不进候选数组。** 「不在视锥内」逐个光源即可判定，无需全局信息；只有预算裁剪需要排名。候选数组因此只装幸存者，也不需要「已拒绝」的哨兵分数。归还的四件事全封在 `Deactivate` 里，两个调用点共用同一份不变量。

**先失活再激活，两个循环分开**，败者让出的格子当帧即可被胜者取用。

方向光不需要任何特判：没有 `LightBounds` ⇒ `TryGet` 为空 ⇒ 分数恒为 `kScoreMax`，视锥测试整段跳过。

迟滞与占用加权只读 `ShadowViewRefs::m_index >= 0`，**不引入任何跨帧状态**。

**view 实体在首次激活时创建，失活时保留**，只有 sweep 才销毁。大场景里多数投影光源可能永远轮不到 tile，提前给它们建 SRG + constant buffer 是纯浪费；失活即销毁则会变成每帧 create/destroy 抖动。

**5. `ViewInactiveTag`。**（已完成）`View/ViewTags.h` 新增，`CollectViews` 改为 `Exclude<DeadTag, ViewInactiveTag>`。通用 tag，不是 shadow 专属——编辑器视口隐藏、反射探针降频同理。

### 6a 未决

`ViewBindingSystem` 扫 `<View, ViewShaderBindings>`，**失活的 view 仍被编码**——写常量 + 编译 shader 输入，而它这一帧不渲染。加 `Exclude<ViewInactiveTag>` 能省掉，但那是另一个系统的行为改变，且切换回激活时要确保编码不落后一帧。等剔除真的挡掉大量 view 再说。

### 决策

**归还 tile，不保留。** 今天 atlas 每帧整张 Clear，归还没有残留问题；部分清除是 tile 缓存那一层的前置，不是归还的前置。

**归还时 `ShadowViewRefs::m_baseIndex` 必须同帧置 -1。** `SceneBindingSystem` 当帧从它读，只要同源就不存在半拍错位；只打 `ViewInactiveTag` 而留着 `m_baseIndex`，那盏灯会去采一块当帧没画的 tile。

**`ShadowAtlasTile` / `ShadowViewIndex` 随归还一起摘除。** `PackShadowViews` 按 `row.m_index` 寻址，留着会让两个 view 写进同一下标，表现为某盏灯用了另一盏灯的矩阵。移除组件后该查询天然跳过。

**`ShadowViewIndex` 的不变量从「跨帧稳定」降级为「同帧一致」。** 跨帧稳定只有 tile 缓存需要。

**主视角不存在时不剔除。** 预热帧与无相机时全部当作可见——安全方向是漏而非错杀。现有 `MainViewPosition` 扩成返回 `{eye, frustum, proj11, valid}`，`valid = false` 时跳过剔除只保留排序。

### 验证

| 现象 | 说明 |
|---|---|
| >16 盏投影光时，谁有阴影取决于屏幕占比而非创建顺序 | 优先级预算 |
| 镜头转开，远处的灯让出 tile 给近处的 | 评分 |
| 阈值边界上缓慢推拉相机，阴影不逐帧闪烁 | 迟滞 |
| 方向光永远有阴影 | 无界体积特判 |
| 抓帧：任一 tile 至多被一个 view 写 | `ShadowAtlasTile` 随归还摘除 |

灯不够时把预算临时调小可复现全部路径。

### 本节不做

物体级视锥剔除、锥-视锥精确相交、分辨率阶梯（6b）、tile 缓存 / 部分清除、上一帧可见性的时序反馈（shadow 必须先于主 pass 渲染，该矛盾要等分簇光照）。

## 七、6b：分辨率阶梯（已完成，bias 量纲除外）

按屏幕占比给每盏灯不同尺寸的 tile。判据沿用 6a 已经算出的 `ScreenRadius`，不引入新的度量。

### 步骤

**0. 拆开 `m_slot` 的双重身份。**（已完成）

一个数字原本同时是图集位置、`g_ShadowViews` 行号、以及发布给 shader 的句柄。两股力量把它们撕开：

- 点光源要求**行号每盏灯内部连续**（`base + face`），而 tile 不需要连续
- 阶梯之后 tile 是 buddy 分配器的节点，**不再是稠密小整数**，拿它当数组下标就崩

拆成 `ShadowAtlasTile`（分配器凭据，不透明）与 `ShadowViewIndex`（`g_ShadowViews` 行号）。`ShadowViewRefs::m_index` → `m_baseIndex`，语义收窄为「这盏灯那段连续行号的起点」。

**收益在等级变化时兑现**：一盏灯换 tile 尺寸，行号一动不动，`m_shadowIndex` 跨越每次 resize 保持稳定。合并编号的话，每次换档都要重排发布句柄。

**1. `QuadTreeAllocator`。**（已完成）

放 `Core/Memory/`（`PoolAllocator` 已经在那儿，不算开新口子），模板参数 `MaxLevel`，341 节点 × 1 字节，编译期定尺寸不碰堆。单元测试在 `Test/Core/QuadTreeAllocatorTest.cpp`（`QUADTREEALLOCATOR_TESTS`）。

**词汇分层：Core 说 block，阴影层说 tile。** 界线在 `ShadowTileRect`——它是唯一同时出现两个词的地方，因为它正是翻译点：把 `Block{level,x,y}` 翻成带 border 的归一化 rect。border 是阴影的采样策略，不属于分配器。

两条实现上的硬约束：

- **同一父节点的四个孩子 id 连续**（合并时扫四个相邻项即可），这决定了层内是 **Morton 序而非行主序**，`Decode` 必须位解交织。写成除法/取模会让四个块两两重叠——`DecodeUsesMortonOrderNotRowMajor` 钉这条
- **只在确实分出去之后才标 `Split`**。先标再递归的话，一次失败的搜索会留下「已分裂但四个孩子全空」的节点，而 `Free` 永远清理不掉它（没东西被拿走，就没东西会还回来），整块区域从此切不出粗块——`FailedAllocationLeavesNoResidue` 钉这条

**2. 等级选择与迟滞。**（已完成）

```
r ≥ 0.50  → level 1 (2048²)
r ≥ 0.25  → level 2 (1024²)
否则       → level 3 ( 512²)
```

`r` 是 `ScreenRadius` 的 NDC 投影半径。**阈值成倍递进而非等距**——多一档等级是四倍纹素，就该要求屏幕占比翻倍；等距区间会让高分辨率几乎发不出去。

**刻意不引入假想屏幕高度。** 早先推导过 `分辨率 ≈ 2 · r · 屏幕高`，1080p 下解出的边界与上表几乎重合，但那个 1080 不干活——阈值本身就是策略，套一层没人维护的像素数只是把判断包装成推导。而且 `ShadowViewSystem` 跑在 render graph 之前，本来就拿不到 attachment extent（这也是 6a 用 NDC 的原因）。

迟滞沿用 0.67 比例（即 `kScoreEnter/kScoreExit` 那个比值，全系统一个迟滞宽度）：

| 边界 | 升 | 降 |
|---|---|---|
| 1024 → 2048 | 0.50 | 0.34 |
| 512 → 1024 | 0.25 | 0.17 |

**不需要任何新的跨帧状态**：当前档位从持有的 tile 反解（`Decode(node).m_level`）。6a 那条「不引入跨帧状态」的性质得以保持——状态就是那块地本身。

方向光依然零特判：`kScoreMax` 在第一个测试就通过，自然饱和到最粗档。

**3. 上下界。**（已完成）

| 界 | 值 | 理由 |
|---|---|---|
| 上 | 2048²（level 1） | level 0 是一盏灯吃掉整张图集，没有任何场景配得上这个交易 |
| 下 | 512²（level 3） | 它决定同时存在的 shadow view 上限 = 每帧 shadow pass 数。256² 会允许 256 个 pass，不可行 |

`kShadowViewCapacity` 随之 16 → 64。`ViewHandleList` 的内联容量保持 16——超出只是落堆，不会出错；为极端情况常驻放大每个 pass 的结构不划算。

**4. 预算改为面积。**（已完成）

以最细档为单位记账：`ShadowTileCost(level) = 4^(finest-level)`，总额 64。整数运算，无舍入。

**5. `kDirectionalTexelSize` 逐 view 化。**（已完成）

不是顺手扩范围，是第 2 步的**正确性前提**：方向光现在拿 2048² 而非 1024²，snap 网格若还按旧尺寸算就和真实纹素对不齐，爬行直接回来。

**6. bias 量纲。** 见 §三，唯一未做的一项。

### 决策

**超预算时降级，不跳过。** 跳过会让一盏灯的阴影取决于**另一盏灯的分数**：预算剩一块 1024，A（第 3 名）要 2048 放不下被跳过，B（第 7 名）要 512 拿到；镜头轻微移动 A 掉一档改要 1024，拿走那块，B 这帧突然没影子。B 自己什么都没变，6a 的迟滞对这种闪烁完全无效。

降级三个好处：名次单调（前面的一定拿到东西，只是可能糙）、利用率更高、**顺带治碎片**（buddy 可能有空闲 512 却切不出 1024）。

**最细一档也放不下时停止，这一步是精确的而非启发式**：512² 是原子，切不出说明图集真满了，名次更靠后的不可能有位置。跳过唯一想要的那个性质（不浪费），降级在这里免费拿到。

**两阶段：先按名次算出准入集合，再分配。** 开销可变之后「要分配完才知道谁输，可要知道谁输才能开始分配」成了循环。破法是先按**请求面积**累加、就地降级、算出刀口，刀口确定后再失活败者、再激活胜者——6a 那条「败者让出的格子当帧可被胜者取用」得以保持。

面积累加**忽略碎片，因而是乐观的**；多切进来的部分由分配阶段再次降级吸收。误差方向安全：不会发生「本来放得下却被切掉」。

**升档与降档的重分配方式必须不对称。**

- 升档：**先申请，成功了才释放**。想要的大块可能正压着旧块，此时申请失败，灯留着现有的——是错过一次升档，不是丢掉阴影
- 降档 / 空间不足降级：**先释放再申请**，更小的块必然拿得到

反过来写会有具体的抖动：一盏灯因空间不足降到 512 但分数够 1024，下一帧判定"该升档"→ 释放 512 → 申请 1024 失败 → 降级回 512，**每帧一次无谓的 free/alloc 最后回到原地**。先申请后释放让这个循环不成立。

### 与第 1 步的行为差异

buddy 首次适配是 **Z 序**，老的 bitset 是行主序。每盏灯拿到的**图集位置会变**（第 3 盏灯从格子 (2,0) 挪到 (0,1)），块数、尺寸、互不重叠不变。这是四叉树固有的，保留 buddy 语义就绕不开。

### 本节不做

tile 缓存 / 部分清除、点光源（§九 第 2 步）、离轴投影。

## 八、广角聚光灯撞到的是投影本身

`tan(outerHalf)` 在 90° 附近发散。代码里 `kSpotMaxHalfAngleDeg = 89°` 的钳位注释写的是「数值保护，不是质量上限」——**其实它同时也是质量上限**，89° 时投影已经荒谬。

按 2048² tile、range 20：

| 半角 | tan | 单位/texel | 相对方向光（0.0117） |
|---|---|---|---|
| 30° | 0.58 | 0.011 | 持平 |
| 45° | 1.00 | 0.020 | 1.7× |
| 60° | 1.73 | 0.034 | 2.9× |
| 75° | 3.73 | 0.073 | 6.2× |
| 89° | 57.3 | 1.12 | **96×** |

30° 半角时聚光灯与方向光相当——「广角 spot 最多也就是个方向光」的直觉来源于此，它在窄角时成立，在广角时完全不成立。

### 机制：透视在正切上均匀，光照需要在角度上均匀

`u = tan(a)/tan(θ)`，纹素在 `u` 上等距，于是一个纹素对应的角度正比于 `1/sec²(a)`：

| 离视轴 | sec² |
|---|---|
| 0° | 1.0 |
| 45° | 2.0 |
| 75° | 14.9 |
| 89° | 3283 |

**同一张图内部，视轴与边角的角分辨率相差 `sec²(θ)` 倍。** θ=45° 时是 2，基本均匀；θ=75° 时是 15，纹素堆在边角而**视轴附近被饿死**——而视轴正对着你在看的地方。

所以问题不是纹素不够，是**花在了错的地方**。

### 为什么加分辨率追不上

要让单张图在视轴处达到 90° 面的密度，线性分辨率乘 `tan(θ)`，**面积乘 `tan²(θ)`**：

| 全角 | tan²(θ)：需要多少倍面积 | 拆分需要几个面 |
|---|---|---|
| 90° | 1 | 1 |
| 120° | 3 | 5 |
| 150° | 14 | 5 |
| 178° | 3283 | 5 |

左边发散，右边**封顶在 6**。这就是「没有任何参数能治」的确切含义——不是量不够，是量的增长跟不上。

### 解法：广角 spot 就是被锥体裁剪过的点光源

拆成多个面之后每个面自带一根新视轴，任何采样方向离**它所属的轴**不超过 45°（对角 54.7°），`sec²` 被钉死在 2~3，与原锥角无关。

**90° 不是凑巧选的：cube face 就是 90° 全角的透视投影**，立方体贴图用 6 个 90° 面正是因为 90° 是 `tan` 还没开始跑的最大角度。

180° 全角（半球）= +Z 整面 + 四个侧面各一半 = **5 个面**。注意有跳变：91° 和 179° 都要 5 个面，所以「稍微超过 90°」的灯代价从 1 块 tile 直接跳到 5 块。缓解办法是侧面用离轴投影只覆盖锥体真正需要的薄环，代价是 shader 选面不再是「比较三个分量」——留到看见实际浪费之后再说。

spot 与点光的**唯一区别**：点光渲染 6 个面，spot 只渲染与锥相交的那几个，其余连 tile 都不分配。shader 端完全一样。

### 行业现状

主流引擎的聚光灯就是**一张透视图 + 编辑器里卡角度上限**（常见量级是半角 80° 左右），再宽不让调。真要 180° 的效果，作者被引导去**改用点光源**靠锥形衰减塑形——所以「广角 spot = 带锥形遮罩的点光」这个等价关系行业早在用，只是让美术手动转换。

没人把它自动化，是因为需求稀少：真实的聚光灯是反光碗加挡板，物理上就是窄的。VSM 之前的力气花在图集 + 按屏幕占比分配 + 静态缓存（本文 §六/§七 走的正是这条）、方向光级联、点光 cube map 三处。

**PSM / TSM / LiSPSM 不解决这个问题**，容易混：它们对付的是**观察者**透视造成的近处欠采样，目标是方向光。这里的失真来自**光源自己**的大视场角，与相机无关。

自动拆面对我们几乎不花额外成本——点光那套机器本来就要建，广角 spot 只是多一个 `if`。别人不做是因为要为它单独建一套。

### 立即可做的止血（不改变台阶大小，只改观感）

- `kSpotMaxHalfAngleDeg` 89° → 60~80°，与行业一致
- `kPcfRadiusPerspective` 1.0 → 2.0：透视纹素更大却给了更少滤波，方向是反的
- `m_shadowBias` 默认 5e-4 → 1e-4，§三 的止血

## 九、顺序

1. ~~**6a**：光源级剔除 + 优先级预算~~ 已完成
2. ~~**6b**：分辨率阶梯~~ 已完成，仅剩 bias 量纲
3. **点光源 6 面 / 广角 spot**（`TODO_MultiViewPlan.md` §五 第 7 步）——**提前到 bias 之前**，因为它同时解决 §八；6a/6b 是它的硬前置
4. **bias 量纲**（§三）——放在点光之后：6 个面全是透视投影，bias 误差按 `d` 放大，带着已知错误的 bias 调 6 面等于同时 debug 两件事
5. clipmap —— 方向光唯一的出路

### 第 3 步的清单

已经就绪：tile 编号与行号分离（面可散落图集任意位置，行号仍连续）、超预算降级策略、`Deactivate`/sweep 已按 `m_views` 循环。

还要做：

- 行号的**连续段**分配（现在 `AllocateViewIndex` 只发单个）
- tile 的**原子**分配：6 块要么全给要么不给，半个 cube 比没有阴影更难看
- 一盏灯 6 个面**等级统一**，降级整体降，否则面之间滤波质量不一致
- 预算把一盏点光当**一个不可分的候选**，成本 6×`ShadowTileCost`
- **padded fov 的 border**：面接缝处 PCF 断裂，靠把每面 fov 略微放大让 border 里装邻面的真实几何。正确性只到 border 宽度为止，所以这几盏灯的 border 要给 2 texel
- shader 端**选面**：按 `worldPos - lightPos` 的主轴，取 `m_shadowIndex + face`
- `Activate` 里的 `m_views[0]` 变成面循环
- spot 按锥角决定面数：≤90° 全角 1 个面，更宽 5 个面

**未决的策略**：一盏点光要 6 块但预算只剩 4 块时，是就此打住还是跳过它让后面要 1 块的灯用掉？跳过会让名次不再单调（与 §七 拒绝「跳过」的理由同源），但打住会浪费。§七 的降级在这里不直接适用——降级改变的是尺寸，而点光的**面数**不能降。
