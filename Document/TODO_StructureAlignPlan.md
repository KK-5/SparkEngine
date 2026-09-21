# 结构对齐实施计划（路线图 P2）

## 背景与目标

路线图 `TODO_RenderPipelineRoadmap.md` 的 P2。P1 把时序流打通了，但延迟管线的**数据契约**还停在雏形：
GBuffer 是 Albedo / Normal / ORM / Emissive 四张自定义布局，没有 `ShadingModelID`、没有 Specular（F0 硬编码
0.04）；`LightingPass` 一个 shader 同时做直接光、IBL diffuse、IBL specular 并自己采阴影 atlas；SceneColor
由它创建。

目标：把 GBuffer 布局、SceneColor 语义、光照 Pass 的职责切分对齐到 UE，使 P3~P8 的每个功能都是往既有接缝上
挂东西。**本阶段基本不改画面**（例外见 §验证），产出全是结构。

---

## 状态

| 步骤 | 内容 | 状态 |
|---|---|---|
| 1 | GBuffer 布局重排 + `GetGBufferData()` | 未开始 |
| 2 | SceneColor 创建者移到 GBufferPass，Emissive 并入 | 未开始 |
| 3 | PreExposure | 未开始 |
| 前置 | RHI：`m_layerCount` 写入 + array 渲染能力位（步骤 5 的前提） | 未开始 |
| 4 | LightingPass 拆为 Lights / IndirectDiffuse / Reflections | 未开始 |
| 5 | ShadowProjection → `ShadowMask` | 未开始 |

执行顺序 1 → 2 → 3 → 前置 → 5 → 4，逐文件清单见 §六。

---

## 决策记录

六条决策，全部已定。正文按结论写。

### D1　GBufferA 法线编码　✅ 已定：A（UE 默认直存）

`GBufferNormal` = `R10G10B10A2_UNORM`，rgb = `WorldNormal * 0.5 + 0.5`（法线分量在 `[-1,1]`，UNORM 只存 `[0,1]`，
所以写时重映射、读时 `* 2 - 1` 还原），a 留给 `PerObjectGBufferData`。与 UE 的 `FGBufferData` 逐位一致。

| 选项 | 格式 | 字节 | 最大角误差 | A 通道 |
|---|---|---|---|---|
| **A** UE 默认直存 | R10G10B10A2 | 4 | ~0.1° | 有 |
| B 八面体 | R16G16_UNORM | 4 | ~0.002° | 无 |
| C 维持现状 | R16G16B16A16_FLOAT | 8 | ~0 | 有 |

带宽不是 A 与 B 的区别，两者都是 4B——`8B → 4B` 是离开现状 C 的收益，A 和 B 平分。A 精度低两个数量级的原因
是三个分量各自独立量化，合法的法线只落在 1024³ 立方格点中单位球面那一层壳上，绝大多数码是废的；八面体把
球面双射到正方形，每个码都是一个不同的方向。

选 A 的理由是**这个接缝比 UE 自己的还细**：

- 误差只在**锐利反射**上可见。`reflect()` 把法线误差放大一倍，粗糙度接近 0 时表现为大平面上反射内容的阶梯
  条带。直接光、diffuse、PCF 阴影（法线只用来做 normal offset）、split-sum IBL（预滤波本身就模糊）都表现不出
  0.1°。所以受影响的是 P4 的 SSR 与 P9 的 RT 反射，P2/P3 一定看不见。
- UE 没有按信号精修，它的逃生开关是 `r.GBufferFormat`，粒度为**整张 GBuffer** 抬到 FP16。默认档位就是
  R10G10B10A2，靠 TAA 的时域抖动把条带抹成噪声——P1 之后我们也有 TAA。
- 我们换 B 只需替换 `EncodeNormal` / `DecodeNormal` 两个函数体，GBufferA 变 RG16_UNORM，其余三张不动，
  那 2 bit 的 `PerObjectGBufferData` 挪去 `GBufferSurface.a` 的 `SelectiveOutputMask` 半字节。没有调用点改动。

**复查点**：P4 做 SSR 时用一个低粗糙度大平面的场景实测，出条带就换 B。

### D2　GBufferBaseColor 是否走 sRGB　✅ 已定：走

`GBufferBaseColor` = `R8G8B8A8_UNORM_SRGB`。

sRGB 格式是**存储编码，不是色彩空间切换**：写入时硬件把 shader 输出的线性值套上 sRGB 编码存进 8 bit，
读取时解码回线性。两端的 shader 看到的都是线性值，光照数学不变。它的作用只是把 256 个档位非均匀分布，
暗部密、亮部疏。

这不是为对齐 UE 付的代价，而是**修掉一个现存的精度损失**：base color 的源纹理本就以 `_SRGB` 格式加载
（`ImageAssetCompiler.cpp`），采样时解码成线性；再存进线性 8 bit 的 GBuffer 就降了一次精度——反射率 0.02
这类深色材质在线性 8 bit 里只剩约 5 个档位。改成 sRGB 后 GBuffer 的编码与源纹理对齐，原样往返。亮端档位
约粗一倍，但反射率接近 1 的材质少，且那个区间分辨不出 0.8% 的差别。

**代价是本阶段画面不再逐像素一致**（暗部变准），验证标准相应放宽，见 §验证。全链路见 §五。

注意：

- **只有 GBufferBaseColor 能用 sRGB。** `GBufferSurface` 装的是 Metallic / Specular / Roughness / ShadingModelID，
  不是颜色，套上感知曲线全错，必须保持 `UNORM`。
- `Load` 一样会解码——格式转换属于读取路径而非过滤，不需要改用 `Sample`。
- **UAV 不能是 sRGB**（D3D12 类型化 UAV 不支持）。将来若有 compute pass 要写 GBufferC（贴花之类），
  得自己手动编码。目前只有 base pass 的 PS 写它。
- clear 值 `(0,0,0,1)` 在 sRGB 变换下不动，无歧义。

### D3　`GetGBufferData()` 的绑定契约　✅ 已定：纹理作为参数传入

`Lib/DeferredShadingCommon.hlsli` 只定义 `GBufferData` 与解码函数，**不声明任何绑定**：

```hlsl
GBufferData GetGBufferData(
    Texture2D gbufferNormal, Texture2D gbufferSurface, Texture2D gbufferBaseColor,
    Texture2D sceneDepth, int2 pixelPos);
```

消费 Pass 自己声明这几张纹理、自己选寄存器。多 tap 的消费者（P4 的 SSR）在 Pass 内包一层薄 wrapper。
C++ 侧一对 helper（`DeclareSceneTextures<PassTag>(builder)` / `BindSceneTextures<PassTag>(compiler)`）承担
声明与绑定的样板——这部分与选哪种做法无关。

判据是**寄存器归谁**。共享头写死 `space2 t0..t4` 的做法会把一个全局寄存器命名空间越撑越大（现在
`Lib/Lights.hlsli` 已占 t5 / s0，每加一个共享头都要先查还剩什么没被占），而代码库已写明的偏好正相反：
`Lib/Shadow/ShadowSampling.hlsli` 与 `Material/MaterialTemplate.hlsli` 都把资源作为参数传入，让 Pass 拥有
自己的寄存器；`Lib/Lights.hlsli` 走的是写死寄存器那条，而它自己的头注释把这件事标注为缺陷。

代价是调用点多几个参数。

#### 以后可能扩展的形态（不在 P2）

把 SceneTextures 做成一个共享绑定组。**要做就按 UE 的形状做**（`FSceneTextureUniformParameters` + RDG，
细节待核实源码）：

- 绑定组里存的是**图句柄**，不是解析好的 `ImageView*`——我们的绑定系统在 `ExecutePipeline` 之前就写完了，
  而瞬态资源的 view 在 `CompileTransientResources` 才诞生，存句柄才不会撞上这个时序。
- **声明该组即声明依赖**：图内省组的内容，据此排序并插屏障。否则 Pass 仍要单独 `ReadImageAttachment`
  （那才是图的边与 `RenderTarget → ShaderRead` 屏障的来源），共享组就只省下几行 `SetPassShaderImage`，
  不值得建新机制。
- 寄存器由参数系统分配，不手工写死。

别做成"绑定系统在图编译中途插一脚"的半吊子版——那是给现有机制打补丁，省不掉声明，也解决不了时序。

### D4　`ShadowMask` 的形状　✅ 已定

`Texture2DArray`、`R8G8B8A8_UNORM`、渲染尺寸，**一个 slice 打包 4 盏灯**（`index >> 2` 选 slice，
`index & 3` 选通道，宽度写成常量）。预算 4 个 slice = **16 盏投影灯**；`arraySize` 每帧按实际投影灯数
取 `min(⌈N/4⌉, 4)`，图里的瞬态资源描述符本就是 Build 期逐帧算出来的。

**所有投射阴影的灯一视同仁**，不分层级。是否需要 mask 不是内容特征——降噪（RT 阴影）、contact shadow、
换阴影来源都是逐灯可开关的属性，任何一盏投影灯都可能开，所以结构必须对所有灯成立。

存的是每(像素, 灯)一个 `[0,1]` 可见度标量，等于今天 `SampleShadow` 的返回值。屏幕空间只消掉了几何那一维：
同一像素对不同灯的可见度互不相干，且 `vis` 在光照求和号**里面**（各乘各的 `BRDF × radiance`），没有合并后的
vis 能提到外面。要合并只能合并成最终颜色，而那就是今天的内联采样——**mask 存在的唯一理由，是在"算出可见度"
与"用掉可见度"之间留一个插入环节**（降噪 / contact shadow / 换来源）。

#### 显存

每灯每像素固定 1 字节，打包与否不改变这个数。**跟渲染分辨率走**：

| 16 盏灯 | 1080p | 1440p | 4K |
|---|---|---|---|
| 占用 | 31.6 MiB | 56 MiB | 127 MiB |

4K 下需要重新取预算，或届时分簇已到位。

#### 随之必须做的

- **RHI 两处补齐**（array 渲染无条件必需）：`RenderPassBeginInfo::m_layerCount` 目前全代码库无写入点，
  DX12 从 RTV 的 ArraySize 推得出，Vulkan 的 `VkRenderingInfo::layerCount` 必须显式给；`DeviceFeatures`
  缺从 VS 写 `SV_RenderTargetArrayIndex` 的能力位（DX12
  `VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation` / Vulkan `shaderOutputLayer`），
  没有它只能走 GS 放大。已具备的是 `ImageDescriptor::Create2DArray`、RTV 的 array 分支，以及默认
  `ImageViewDescriptor` 覆盖全部 slice。
- **投影 draw 加 scissor**，按灯的包围球算屏幕包围盒。否则 16 盏灯就是 16 次全屏 4~16 tap PCF；
  一盏局部灯照亮的只是一小块，其余像素的 PCF 是纯浪费。

#### 为分簇留的路

接 LightGrid 后，槽位索引从**全局灯序号**改为**簇内灯序号**：一盏只照亮小块的灯只在它覆盖的簇里占槽，
别处那个槽归别的灯。显存从 `O(N × 屏幕)` 变成 `O(K × 屏幕)`，K = 每像素灯数上限，与场景总灯数无关。
纹理形状与 `SampleShadowMask(px, slot)` 的签名都不变。

为此只需守住一条：**接口传的是槽位，不是全局灯号**（`LightData::m_shadowMaskIndex` 同理）。分簇是加法。

K 仍是预算而非常数，过载时的降级同样留到那时定——届时着色循环里已有本簇灯表，可按"槽位 ≥ K 就当场采
atlas"分流，且槽位优先给**没有回退路径**的灯（RT 阴影的灯没有 atlas tile 可采，光栅阴影的有）。

#### 与 UE 的差异（刻意）

UE 的 `ScreenShadowMaskTexture` 是单张 RGBA8 在逐灯循环里复用，四个通道是**同一盏灯**的四项
（整场景 / per-object × 次表面 / 非次表面）。它的复用依赖投影与着色按灯交错，即运行期 N 对 pass，
而本框架的 pass 是启动时声明、编译期 tag；D5 定的循环 draw 也要求所有 vis 同时就绪。

我们每盏灯存 1 项，即 UE 的 `.x`。缺的三项今天都没有可填的内容：无次表面 shading model，per-object 阴影
属于 UE 的 stationary light + 烘焙 lightmap 架构，我们明确不走。

通道在两边的价格相反：UE 一张图只服务一盏灯，多用一个通道白送；我们的通道是灯位。因此不照抄它的通道分配。

### D5　Lights 要不要逐灯绘制　✅ 已定：不做

`LightsPass` 是一个全屏 draw，shader 里循环所有灯，有阴影的按 `shadowMaskIndex` 采 `ShadowMask`。
路线图 P2 第 4 步原本写的"有阴影的灯逐盏绘制、无阴影的灯合批"作废，路线图已同步。

不是"推迟到有收益时再做"，是**现在做就是负优化**：一个全屏 additive draw 覆盖整屏，N 盏灯就是 N 次完整的
GBuffer 重读加 N 次 blend 读改写，而循环只读一次 GBuffer、在寄存器里累加完。着色算术一样，访存 N 倍。

UE 的逐灯能成立是因为它不画全屏：点光/聚光光栅化的是球/锥包围几何，配 stencil 剔掉体积外的像素。
逐灯绘制和光体积+stencil 在 UE 那里是捆在一起的一件事，拆开只剩坏处。

| | GBuffer 访存 | 剔除 | 工作量 |
|---|---|---|---|
| **循环（本阶段）** | 1 次 | 无 | 0 |
| 逐灯全屏 draw | N 次 | 无 | 中 |
| 逐灯光体积 + stencil | ~1 次 | 有 | 大 |
| 分簇 LightGrid | 1 次 | 有 | 大 |

第二行没有存在价值。而灯数真成为瓶颈时要建的是第四行而不是第三行——光体积是前分簇时代的技术，分簇还能
同时服务 P5 的前向透明着色与以后的 VolumetricFog。所以第三行是一个我们大概率会整个跳过的形态。

循环确实付一笔与剔除无关的钱：没有按光源类型特化 shader，要承担分支发散和所有灯类型寄存器占用的并集。
触发条件仍是同一个，而且分簇之后每簇的灯类型通常只有一两种，发散反而更小。

D5 不影响本阶段要立的接缝：`ShadowMask` 把可见性从光照里摘出去（§四）、`EvaluateLight` 不再碰 atlas，
这两条与逐灯与否无关。将来改 `LightsPass` 内部的 draw 组织不牵动别的 Pass。

但 D4 的 ShadowProjection 要逐 slice 绘制（instanced 全屏 draw，`SV_RenderTargetArrayIndex =
SV_InstanceID`），所以动态 instance 数的 procedural draw 机制还是要建一个最小版，见 §四——它正好是将来
逐灯绘制的接缝。

### D6　GBufferCustomData 现在建不建　✅ 已定：不建

MRT 顺序 `0=SceneColor, 1=GBufferNormal, 2=GBufferSurface, 3=GBufferBaseColor, 4=Velocity`，CustomData 以后接 MRT5。
`GBufferData` 结构体里现在就留 `CustomData` 字段，解码恒为 0。

UE 的 GBufferD 装的是 `CustomData`——GBuffer 里的一个**带标签的联合体**：`GBufferSurface.a` 的 `ShadingModelID`
是标签，`CustomData` 的四个通道是载荷，由具体着色模型解释（次表面颜色 / 清漆强度 / 虹膜遮罩……）。
它存在的原因是 GBuffer 布局对整帧固定，所有物体写同一组 MRT，没法让一部分像素多写一张。

而 `DefaultLit` 恰好是唯一**不用** CustomData 的模型，路线图 P1~P9 也没有安排第二个着色模型
（最早的是 §六「路线图之后」里水体带来的 `SingleLayerWater`）。所以 GBufferD 现在没有任何计划中的消费者，
建了就是每帧白占一张 RGBA8（1080p 7.9 MiB）加一次 clear 带宽。

要立的接缝是**标签**而不是载荷：`ShadingModelID` 本阶段就加（哪怕只有一个取值），`CustomData` 字段先留着。
载荷的通道含义现在设计不了——它取决于第二个模型是谁。第二个模型到来时是加法：接一张 MRT5、在
`EncodeGBuffer` 里按模型写、在解码里填字段，光照 shader 加一个 `switch`。

MRT 序号只出现在 `GBuffer.hlsl` 内部，解码侧按纹理名读，所以序号与 UE 不一致不影响抄 shader。

## 核心决策（路线图已定，此处只记它在本阶段的含义）

### 1. SceneColor 的生产者是 GBufferPass

`GBufferPass` 创建 SceneColor 并把 Emissive 写进 MRT0；`GBufferEmissive` 这张 RT 消失。之后 Lights /
IndirectDiffuse / Reflections / Skybox 全部以 `One / One` additive 混合累加进去。

被 GBufferPass 深度测试（`Equal`）跳过的像素——天空——保留 clear 值，交给 Skybox 覆盖，这与今天
LightingPass 的行为一致，只是 clear 的发生点前移。

### 2. 每个光照 Pass 都是"读信号、加一份贡献"

三个 Pass 的形状完全相同：全屏三角形、`z=0` + 深度测试 `Less` 剔天空、additive 混合、读 space2 的
SceneTextures。差别只在读哪些信号、算什么。AO 是 IndirectDiffuse 与 Reflections 的共同输入，P2 里还没有
生产者。

---

## 一、GBuffer 布局重排

### 布局

| MRT | 名字 | 格式 | 内容 |
|---|---|---|---|
| 0 | `SceneColor` | R16G16B16A16_FLOAT | `EmissiveColor * PreExposure`，a = 1 |
| 1 | `GBufferNormal` | R10G10B10A2_UNORM | rgb = `WorldNormal * 0.5 + 0.5`；a 预留 `PerObjectGBufferData` |
| 2 | `GBufferSurface` | R8G8B8A8_UNORM | r = Metallic，g = Specular，b = Roughness，a = ShadingModelID / SelectiveOutputMask |
| 3 | `GBufferBaseColor` | R8G8B8A8_UNORM_SRGB | rgb = BaseColor，a = GenericAO（材质 AO 贴图） |
| 4 | `Velocity` | R16G16_FLOAT | 不变 |
| — | `GBufferCustomData` | RGBA8 | 不建，见 D6 |

`GBufferSurface.a` 按 UE：低 4 位 `ShadingModelID`，高 4 位 `SelectiveOutputMask`。P2 只有
`SHADINGMODELID_DEFAULT_LIT = 1`（0 是 Unlit，本阶段没有产生者），`SelectiveOutputMask` 恒 0。

旧 `GBufferORM` 的三个通道就此散开：Roughness / Metallic 去 GBufferB，Occlusion 去 GBufferC.a。

### 命名

纹理用语义名，不跟 UE 的字母：`GBufferNormal` ↔ UE GBufferA，`GBufferSurface` ↔ GBufferB，
`GBufferBaseColor` ↔ GBufferC，`GBufferCustomData` ↔ GBufferD。

抄 UE shader 时值钱的是**结构体字段名**而非纹理名——UE 的 shader 调 `GetGBufferData()` 后用
`GBuffer.Roughness` / `.ShadingModelID` / `.DiffuseColor`，裸纹理名只出现在 `DeferredShadingCommon.ush`
自己的编解码里，而那个文件我们本来就要自己写。所以 `GBufferData` / `GetGBufferData` 与全部字段名保持与 UE
逐字一致，纹理名随我们。

`GBufferSurface` 不精确，但一张图装四样东西本来就没有自然名字（UE 转向字母多半就是因为这个）；按通道首字母
凑（`MSR` 之类）在加了 ShadingModelID 之后已经走不通，`ORM` 的教训就在眼前。C 叫 `BaseColor` 不叫 `Albedo`：
UE 里 `DiffuseColor` 是派生量 `BaseColor * (1 - Metallic)`，两者含义不同。

### 解码

`Shaders/Lib/DeferredShadingCommon.hlsli`，对应 UE 的 `DeferredShadingCommon.ush`：

```hlsl
struct GBufferData
{
    float3 WorldNormal;
    float3 BaseColor;
    float  Metallic, Specular, Roughness, GBufferAO;
    float4 CustomData;        // 恒 0，GBufferD 到位前
    uint   ShadingModelID, SelectiveOutputMask;
    float  Depth;             // 设备 Z
    float3 DiffuseColor, SpecularColor;   // 由上面派生
};

// 纹理作为参数传入，本文件不声明任何绑定（D3）。
GBufferData GetGBufferData(
    Texture2D gbufferNormal, Texture2D gbufferSurface, Texture2D gbufferBaseColor,
    Texture2D sceneDepth, int2 pixelPos);
```

`DiffuseColor = BaseColor * (1 - Metallic)`、`SpecularColor = lerp(0.08 * Specular, BaseColor, Metallic)`
在解码里算完，与 UE 的 `GetGBufferData` 一致，调用方不再各自推导。当前 F0 恒 0.04 等价于 `Specular = 0.5`，
而 `MaterialData::m_specular` 的默认值就是 0.5——所以 Specular 接通后默认材质画面不变。

`Roughness` 的下限钳制（今天 Lighting 里的 `max(orm.g, 0.045)`）**不进解码**：它是 BRDF 的 V 项除零保护，
IBL 路径反而不要它。保持在各自的消费点。

### 改动点

- `GBufferPass.cpp`：`RenderTargetLayout` 与五个 `createColor` 按上表改；SceneColor 的 clear 值取
  LightingPass 现在那个 `(0.1, 0.1, 0.15, 1)`。
- `GBuffer.hlsl`：`PSOutput` 与 `EncodeGBuffer` 按上表改，`inputs.Specular` 接通。Specular AA 的位置不动。
- 新建 `Lib/DeferredShadingCommon.hlsli`。
- `Lighting.hlsl` 的 GBuffer 读取全部换成 `GetGBufferData`（随即在步骤 4 被拆散）。

---

## 二、PreExposure

写 SceneColor 的 shader 一律乘 `g_PreExposure`，Tonemap 开头除回。P3 之前 `g_PreExposure` 恒 1，所以本步
是纯接缝。

- `ViewBindings`：新增 `g_PreExposure` 与 `g_OneOverPreExposure`（两个都存，shader 侧不做除法）。
  `ViewBindingSystem` 写入 1.0；P3 的 EyeAdaptation 从上一帧的曝光结果填它。
- 乘的地方：`GBuffer.hlsl` 的 emissive、Lights / IndirectDiffuse / Reflections 的输出、`Skybox.hlsl`。
- 除的地方：`Tonemap.hlsl` 第一行，`hdr *= g_OneOverPreExposure`。它与已有的 `g_Exposure` 是两件事——
  PreExposure 是编码尺度（把 FP16 的有效范围挪到场景亮度上），`g_Exposure` 是美术意图。

`TemporalAA.hlsl` 不需要改，但**不是因为它对尺度不敏感**——它的 `ToPerceptual`（`c / (1 + luma)`）依赖绝对量级。
不改是因为它本来就该工作在 pre-exposed 域里：UE 同样把 TAA 放在这个位置，PreExposure 的作用之一正是把场景
量级挪到这个加权函数生效的区间。

`GBufferPass` 里 SceneColor 的 clear 值是唯一一处没乘 PreExposure 的 SceneColor 写入。它只在没有天空盒时可见，
今天 PreExposure 恒 1 所以无影响，但 P3 驱动它之后必须一起缩放，否则回退背景会偏。代码里已标注。

这两个乘除落在哪一段、和 `g_Exposure` 的分工，见 §五。

---

## 三、光照拆分

`LightingPass` 拆成三个 Pass，插在原位置，顺序 Lights → IndirectDiffuse → Reflections。三者共享的形状抽成
`Feature/Lighting/DeferredLightingCommon.h`：RT 布局（单 RT，SceneColor，additive）、渲染状态、
`DeclareSceneTextures` / `BindSceneTextures`。

| Pass | 读 | 算 |
|---|---|---|
| `LightsPass` | SceneTextures、`ShadowMask`、`g_Lights` | 逐灯 `EvaluateBRDF × radiance × shadowMask`，累加 |
| `IndirectDiffusePass` | SceneTextures、`AmbientOcclusion`、`g_IrradianceCube` | `DiffuseColor * Fd_Lambert() * irradiance * AO * g_EnvIntensity`；无环境时退回常量 ambient |
| `ReflectionsPass` | SceneTextures、`AmbientOcclusion`、`g_PrefilteredCube`、`g_BRDFLut` | `prefiltered * EnvBRDF(SpecularColor, roughness, NoV) * AO * g_EnvIntensity` |

**渲染状态**：`m_blendState.m_targets[0]` 为 `One / One / Add`，深度 `Less` + `writeMask Zero`（沿用今天
LightingPass 剔天空的办法），`cullMode None`。SceneColor 的 `loadAction` 对三者都是 `Load`。

**AO 输入**（D3 未覆盖的一条）：P2 没有 `AmbientOcclusion` 的生产者，而图里 `Read` 一个没人声明的名字会
断言。所以两个消费 Pass 用 per-pass 常量 `g_AmbientOcclusionValid` 门住：

```hlsl
float ssao = g_AmbientOcclusionValid ? g_AmbientOcclusion.Load(px).r : 1.0;
float ao   = ssao * gbuffer.GBufferAO;
```

P4 接上 GTAO 时删掉常量、把声明改成无条件 `Read`。材质 AO（`GBufferAO`）无论有没有屏幕空间 AO 都乘。

**Emissive 不再在这里加**：它已经在 SceneColor 里了。

---

## 四、阴影拆分

### ShadowMask 的生产

新 Pass `ShadowProjectionPass`，位置在 GBuffer 之后、Lights 之前，`RendersView<MainViewTag>`。

- 输出 `ShadowMask`：`Texture2DArray`、`R8G8B8A8_UNORM`、渲染尺寸，`arraySize = min(⌈投影灯数 / 4⌉, 4)`。
- **视图必须显式设 `ImageViewDescriptor::m_isArray = 1`。** DX12 的视图维度由
  `arraySize > 1 || m_isArray` 决定（`Conversions.cpp`，SRV/UAV/RTV/DSV 四处同款），而 1~4 盏投影灯时
  `arraySize` 恰好是 1——不设这个标志，SRV 会被建成 `TEXTURE2D`，与 shader 里 `Texture2DArray` 的声明不匹配，
  debug layer 报错、行为未定义。也就是灯少的常见情形反而先炸。`m_isArray` 存在的理由正是"array 可以只有一层"。
- 一个 instanced draw，`instanceCount = arraySize`，VS 输出 `SV_RenderTargetArrayIndex = SV_InstanceID`。
  **不画全屏三角形，画一个贴合本 slice 四盏灯屏幕包围盒并集的 quad**：VS 用 `SV_InstanceID` 从一个小 buffer
  取该并集，直接算出四角。`SetScissors` 是命令列表状态、由 executer 按 view 设，`DrawItem` 不带 scissor，
  所以 scissor 最细只能到整个 draw——quad 拿到同样的粒度，却不需要 scissor 状态也不需要自定义 Execute。
  并集若太散就退化成全屏，缓解办法是**按屏幕位置就近把灯分进 slice**，让同一 slice 的四盏灯尽量挨着。
- PS：从 SceneDepth 重建世界位置，对本 slice 的 4 盏灯各采一次 atlas（`SampleShadow`，就是今天
  `Lib/Lights.hlsli` 里那段），写成 float4。法线来自 `GetGBufferData`（`SampleShadow` 要它做 normal offset）。
- 没有有阴影的灯时不声明输出，Pass 被 executer 跳过——与 ShadowPass 对 atlas 的处理同构。

`m_layerCount` 与 `SV_RenderTargetArrayIndex` 能力位的补齐见 D4。

### 逐 slice draw 的机制

`ShadowViewSystem` 多持有一个 procedural 实体：`GeometrySpec{ DrawLinear(3), NoInstanceBinding }` +
`ShadowMaskSliceTag`，`ShadowProjectionPass` 用 `.Accepts<ShadowMaskSliceTag>()` 拉它。

`DrawItemRouter` 以 `Exclude<DrawItem>` 做幂等，只派生一次，所以每帧变化的 slice 数由 `ShadowViewSystem`
直接改这个实体上的 `GeometrySpec::m_instanceCount` 与 `DrawItem::m_drawInstanceArgs`（走
`ReplaceComponent`）。共享的 `FullScreenTriangleTag` 实体不能复用——它的 instanceCount 是所有全屏 Pass 共用的 1。

### 索引

`LightData` 新增 `int32_t m_shadowMaskIndex`——**槽位，不是全局灯号**（分簇后它变成簇内序号，见 D4），
-1 = 不采 mask。由 `SceneBindingSystem` 在给灯分配 `m_shadowIndex` 的同一趟里发放，超出容量的发 -1。
容量 = `kShadowMaskPackWidth * kShadowMaskSliceMax` = 16。`LightData` 64B → 需要补到下一个 16B 边界，
同步改 `LightData.hlsli` 与 `static_assert`。

### 消费

`Lib/Lights.hlsli` 的 `EvaluateLight` 去掉 `SampleShadow` 调用与 `g_ShadowAtlas` / `g_ShadowSampler` 声明，
只返回未遮蔽的入射辐射；遮蔽由调用方乘。`LightsPass` 里：

```hlsl
float3 radiance = EvaluateLight(light, worldPos, N, L);
if (light.shadowMaskIndex >= 0) { radiance *= SampleShadowMask(px, light.shadowMaskIndex); }
```

`SampleShadowMask` 内部承担 `>> 2` / `& 3`，调用方不知道打包方式——换宽度、换成簇内索引都只动这一个函数。

`Lib/Shadow/`（`ShadowSampling.hlsli`、`BicubicPcf.hlsli`、`ShadowViewData.hlsli`）本阶段只被
`ShadowProjection.hlsl` 包含，但**不会一直如此**：屏幕空间 mask 是在不透明深度上算的，P5 的透明前向着色
表面在别的深度，mask 对它是错的值，必须直接采 atlas。所以采 atlas 的着色路径是一个独立的、必然存在的
消费者，`Lib/Lights.hlsli` 头部那段 t5 / s0 的绑定契约挪到 `Lib/Shadow/` 自己的头部，不要删。

---

## 五、色彩流水线（参考）

本阶段同时动 GBufferC 的编码（D2）与 PreExposure（§二），两者都落在这条链上，所以把全链路记在一处。

先分开三件常被混为一谈的事：**色彩空间**（基色 / 白点，本引擎全程 Rec.709，没变过）、**传递函数**
（gamma / OETF，"线性 vs sRGB"说的是这个）、**存储编码**（`_SRGB` 后缀的格式让硬件在读写时自动套那条曲线）。
下文只关乎第二件。

非线性存储的理由只有一个：8 bit 均匀分档在暗部不够用，而人眼对暗部的相对变化敏感。sRGB 曲线把档位往暗部挤，
等效约 12 bit。**浮点不需要**——指数部分天然给出恒定的相对精度，且 HDR 超过 1.0 的值 sRGB 表示不了。
这就是 SceneColor 用 `R16G16B16A16_FLOAT` 而不套 sRGB 的原因。

### 全链路

| 阶段 | 格式 | 谁做转换 | shader 里是什么 |
|---|---|---|---|
| 纹理烘焙 | base color / emissive → `_SRGB`；normal / MR / AO → `UNORM` | `ImageAssetCompiler` | — |
| 材质采样 | `Sample()` on `_SRGB` SRV | 硬件解码 | 线性 |
| GBuffer 写 | GBufferC `_SRGB`（D2 之后） | 硬件编码 | 输出线性 |
| GBuffer 读 | `Load()` on `_SRGB` SRV | 硬件解码 | 线性 |
| 光照 → SceneColor | `RGBA16F` | 无 | 线性 HDR |
| Skybox / TAA | `RGBA16F` | 无 | 线性 HDR |
| Tonemap | 入 `RGBA16F`，出 `R8G8B8A8_UNORM` | **手工 `pow(1/2.2)`** | 出去即显示空间 |
| UI | `R8G8B8A8_UNORM` | 无（刻意） | 显示空间 |
| Present | `R8G8B8A8_UNORM` | 无 | — |

整条链上只有**一次**线性 → 显示空间的转换，即 `Tonemap.hlsl` 的 `OETF()`。它之前全是线性，之后全是显示空间。

### 规则

**只有"给眼睛看的颜色"走感知曲线，一切"数据"保持线性。** 法线（向量）、Metallic / Roughness（材质参数）、
AO（遮蔽率）、Velocity、深度、mask 都是数据。`ImageAssetCompiler` 里有断言专门挡住"法线贴图被标成 sRGB"。
D2 的"只有 GBufferC 能用 sRGB、GBufferB 必须 UNORM"是同一条规则。

### 两处刻意的"不转换"

- **UI 纹理用 `UNORM` 而非 `_SRGB`**：UI 画在 `R8G8B8A8_UNORM` 的目标上，出去时不编码；源若是 `_SRGB`，
  采样时会被解码成线性再原样写出，等于凭空掉一次 gamma、画面发暗。UI 是在显示空间里作画，前后都不转换，
  自洽——它也因此必须画在 Tonemap 之后。
- **Skybox 写线性 HDR**，与光照结果在同一个域里累加，一起过 Tonemap。

### 曝光的两层

都在线性域、都在 OETF 之前，但不是一回事：

- `PreExposure`（本阶段加）：**编码尺度**。写 SceneColor 时乘上，Tonemap 开头除回，目的是把 FP16 的有效范围
  挪到场景亮度上。画面上看不出变化。
- `g_Exposure`（P3 由 EyeAdaptation 驱动）：**美术意图**。作为 tone curve 之前的线性缩放。

Tonemap 里"曝光 → tone curve → OETF"三段的顺序不能乱，原因就在这。

### 将来会动的

- **swap chain 现在是 `R8G8B8A8_UNORM`**（`RenderSystem.cpp`），所以 Tonemap 必须手工做 gamma。改成 `_SRGB`
  让硬件做时，**必须同时删掉 `pow(1/2.2)`**，否则双重编码、画面发白。
- `pow(1/2.2)` 只是 sRGB 曲线的近似——真正的 sRGB 传递函数在暗部有一段线性趾。换 `_SRGB` swap chain 会顺带修掉。
- HDR 输出（HDR10 / scRGB）时 OETF 变成 PQ，tone curve 的目标范围也不再是 [0,1]。P3 之后的事。

---

## 六、改动清单

规模：新建约 12 个文件，修改约 16 处。真正的新机制只有两处——RHI 的 array 渲染补齐，以及 `ShadowViewSystem`
的动态 instanceCount procedural 实体；其余是搬运与拆分。耦合面很窄：`GBufferAlbedo` / `Normal` / `ORM` /
`Emissive` 这四个名字全仓库只出现在 `GBufferPass.cpp` 与 `LightingPass.cpp` 两个文件里。

### 步骤 1　GBuffer 布局重排

| | 文件 |
|---|---|
| 新建 | `Shaders/Lib/DeferredShadingCommon.hlsli` —— `GBufferData` + `GetGBufferData(纹理…, px)` |
| 改 | `Shaders/GBuffer/GBuffer.hlsl` —— `PSOutput` / `EncodeGBuffer`，接通 `Specular`，写 `ShadingModelID` |
| 改 | `GBufferPass.cpp` —— `RenderTargetLayout` + 五个 `createColor` |
| 改 | `Lighting.hlsl` / `LightingPass.cpp` —— 改读新名字（步骤 4 会拆散，此处只求能跑） |

### 步骤 2　SceneColor 前移

| | 文件 |
|---|---|
| 改 | `GBufferPass.cpp` —— 创建 SceneColor 作 MRT0，clear 值从 LightingPass 搬来 |
| 改 | `GBuffer.hlsl` —— emissive 写 MRT0 |
| 改 | `LightingPass.cpp` —— 不再 Create，改 Read + `One/One` additive；删 `g_Emissive` |
| 改 | `Lighting.hlsl` —— 删 emissive 加法 |
| 改 | `DepthPrePass.cpp` —— 一句注释（现写着 SceneColor 归 LightingPass 所有） |

### 步骤 3　PreExposure

| | 文件 |
|---|---|
| 改 | `ViewBindings.hlsli` + `ViewBindingsReflect.hlsl` —— `g_PreExposure` / `g_OneOverPreExposure` |
| 改 | `ViewBindingSystem.cpp` —— 写 1.0 |
| 改 | `GBuffer.hlsl` / `Lighting.hlsl` / `Skybox.hlsl` 乘，`Tonemap.hlsl` 除回 |

纯接缝，画面不变，可单独验证。

### 前置　RHI array 渲染

| | 文件 |
|---|---|
| 改 | `RHI/Device/DeviceFeatures.h` —— 加能力位 |
| 改 | `Backend/DX12/Device/Device.cpp` —— `D3D12_FEATURE_DATA_D3D12_OPTIONS` 已在查询，加一行赋值 |
| 改 | `RenderGraphCompiler.cpp` 的 `CompileRenderPassBeginInfo` —— 从附件 `arraySize` 填 `m_layerCount`，约 5 行 |

比预想的小：查询与 RTV 的 array 分支都是现成的。

### 步骤 5　ShadowProjection

| | 文件 |
|---|---|
| 新建 | `Feature/ShadowProjection/ShadowProjectionPass.{h,cpp}` |
| 新建 | `Shaders/Shadow/ShadowProjection.hlsl` |
| 新建 | `Shaders/Lib/Shadow/ShadowMask.hlsli` —— `SampleShadowMask` + 打包宽度常量 |
| 改 | `LightData.h` / `LightData.hlsli` —— `m_shadowMaskIndex` + 补齐 16B 边界 |
| 改 | `SceneBindingSystem.cpp` —— 发放槽位 |
| 改 | `ShadowViewSystem.{h,cpp}` —— procedural 实体、每帧 slice 数、屏幕包围盒并集、按屏幕位置就近分 slice |
| 改 | `Lib/Lights.hlsli` —— 摘掉 `SampleShadow` 与 atlas 声明 |
| 改 | `RenderSystem.cpp` —— 注册 |

### 步骤 4　光照拆分

| | 文件 |
|---|---|
| 新建 | `Feature/Lighting/DeferredLightingCommon.h` —— RT 布局 / 渲染状态 / `DeclareSceneTextures` / `BindSceneTextures` |
| 新建 | `LightsPass.{h,cpp}` + `Lights.hlsl` |
| 新建 | `IndirectDiffusePass.{h,cpp}` + `IndirectDiffuse.hlsl` |
| 新建 | `ReflectionsPass.{h,cpp}` + `Reflections.hlsl` |
| 删 | `LightingPass.{h,cpp}` + `Lighting.hlsl` |
| 改 | `RenderSystem.cpp` —— 注册三个 pass |

`TonemapPass` / `TemporalAAPass` 不用改——它们按名字读 SceneColor，名字没变。

### 风险点

- **三个 additive pass 的等价性**。分步截图：先只开 Lights、再加 IndirectDiffuse、再加 Reflections。
- **`LightData` 加字段要补 16B 边界**，`static_assert` 会挡住写错。
- **sRGB 只给 GBufferBaseColor**，`GBufferSurface` 必须 `UNORM`。

---

## 依赖关系

```
1 GBuffer 重排 ──┬──► 2 SceneColor 前移 ──┐
                 │                        ├──► 4 光照拆分
                 └──► 5 ShadowProjection ─┘
3 PreExposure（与 1/2 无关，但要在 4 之前落，否则三个新 Pass 要改两遍）
```

建议顺序：1 → 2 → 3 → 前置 → 5 → 4（逐文件清单见 §六）。步骤 5 先于 4，是因为 4 要顺手把
`Lib/Lights.hlsli` 的阴影采样摘掉——
mask 还没有生产者时摘掉它，中间会有一次"全场无阴影"的状态。

每一步都能独立跑起来并截图，不需要凑成一次大改。

---

## 验证

逐步骤截图对比。**除以下三处外应逐像素一致**：

| 差异 | 来源 | 预期表现 |
|---|---|---|
| 法线精度 | RGBA16F → R10G10B10A2（D1） | 高光边缘极细微的抖动，静态对比下 1~2 LSB |
| BaseColor 量化 | 线性 8bit → sRGB 8bit（D2） | 暗部更准（**变好**，不是回归）；亮端档位约粗一倍，不可见 |
| Specular | 从硬编码 0.04 改为材质驱动 | 默认材质 `m_specular = 0.5` → F0 = 0.04，无变化；显式改过 Specular 的材质会变 |

另外要确认的：

- 三个光照 Pass additive 累加的结果与拆分前单 Pass 一致（先关 IBL 只看直接光，再逐个打开）。
- `g_PreExposure` 人为设成 8 或 0.125 时画面不变（除回正确）。
- `ShadowMask` 在 RenderDoc 中：slice 数与有阴影的灯数匹配，无灯的通道为 1，超预算的灯确实不投影。
- DX12 validation 零警告；GBufferPass 的 5 个 MRT 在 `AttachmentColorCountMax = 8` 之内。

---

## 未决

- **`SelectiveOutputMask` 的语义**：UE 用它标记"这个像素不写某些 GBuffer 通道"，与延迟贴花、
  `PRECOMPUTED_IRRADIANCE` 等特性绑定。P2 只留位，等第二个 ShadingModel 或贴花出现时再定它的位分配。
- **`PerObjectGBufferData`（GBufferNormal.a）**：UE 存的是 per-object 的阴影/贴花接收标志。当前没有对应概念，
  留空。D1 的复查点若判定换八面体，这个字段要搬去 GBufferSurface.a。
- **次表面阴影的存放形式**：透射项要在投影时比较深度求得，着色时拿不到，所以次表面 shading model 落地时
  每盏灯需要第二个标量。两条路：打包宽度 4 → 2（slice 数翻倍，显存翻倍，容量不变），或另开一张只覆盖次表面
  灯的窄透射图。后者更省——次表面材质通常只占屏幕很小一块，为它把所有灯加宽一倍，99% 的像素填的是废值。
  届时再定。
- **ShadowMask 的分辨率**：现在跟渲染尺寸 1:1。半分辨率 + 双边上采样是 P4/P6 的降噪器落地时一并考虑的事，
  接缝在 `SampleShadowMask` 一个函数里。
- **超过 16 盏投影灯**：现在按分配顺序静默降级为不投影。应按重要度（屏幕占比 × 强度）排序，且槽位优先给
  没有回退路径的灯。接 LightGrid 后上限变成簇内的（K × 屏幕，与总灯数无关），过载只影响过载的那几个簇、
  可就地回退采 atlas，这条问题的形状届时完全不同，所以留到那时定。

---

## 关联文档

- `TODO_RenderPipelineRoadmap.md` §二「GBuffer 布局」、§四 P2 —— 本文档的出处
- `TODO_TemporalPlan.md` —— P1，Velocity MRT 与 ViewBindings 的现状
- `TODO_ShadowOptimizePlan.md` §三 —— 阴影 bias 量纲的待办，步骤 5 动 `ShadowViewData` 时一并处理
- `TODO_IBLPlan.md` —— IndirectDiffuse / Reflections 消费的两张 cube 的来源
