# IBL 实现计划（基于图像的光照）

## 背景与目标

HDR 天空盒已跑通：equirect HDRI → GPU compute 烘成 cubemap（`EnvironmentBaker`）→ 作为 import
资源喂给 `SkyboxPass`。但光照仍是常量环境光——`Shaders/Lighting/Lighting.hlsl:31` 的
`g_Ambient = 0.03` 是个占位，注释里就写着 "until IBL lands"。

目标：补齐 IBL 的两张预计算贴图（diffuse irradiance cube + specular prefiltered cube），
接入延迟光照，替换掉常量 ambient。

**结论：不需要新建基础设施，是沿三条既有路径各走一段延长线**——烘焙作业（EnvironmentBaker）、
多 subresource 上传（AsyncUploadSystem）、scene 级绑定（SceneBindingSystem）。

---

## 状态

**阶段 1 ~ 5 全部完成，IBL 已在画面上验证正常。**`Lighting.hlsl` 的常量 ambient 现在只是
「没有环境时」的回退分支。

剩下的都是可选的替换项，互不依赖、都以现有画面为基线：**阶段 6（真 BRDF LUT，方案已定
待实施）**、阶段 6-b（diffuse 换球谐，仍只有构想）。另有两项已知欠账不在本计划内：资产
落盘缓存（每次启动重烘）、barrier 的 per-subresource range。

> 阶段 6 的第 2 步（KTX2 读取路径）**同时是落盘缓存的第一块砖**——两件事在这里交汇，见
> 阶段 6 的「关键结构点」。

---

## 核心决策

### 1. IBL 数据走 **scene 级（space0）**，不走 pass 级

环境光是场景属性，将来 forward / 透明 / 反射都要用，pass 级会导致每个消费方各绑一遍。

而且 scene 级**实际改动更少**：`LightingPass.cpp:112` 已经是
`.Binds<MainViewTag, MainSceneTag>()`，走 scene 级则 LightingPass 的 C++ 侧**一行都不用改**，
只改 shader。走 pass 级反而要在 Build 里加三个 import、Compile 里加三次 `SetPassShaderImage`。

绑定工具也已齐备：`Render/Shader/ShaderBindingsUtils.h` 的 `SetShaderImage` /
`SetShaderSampler` / `SetShaderConstant` 都在，且都带 change-detection（不会每帧脏 SRG）。

### 2. 资源用 `CreateStaticImage`（`StaticImportTag`），不用 `CreateImportedImage`

这是 scene 级绕过 render graph attachment 声明后，**barrier 由谁负责**的答案。

`RHI/ResourceBuilder.h:61-64` 的注释已经写明 `StaticImportTag` 就是为这类资源设计的：

> *"material textures, cubemaps, and other images that are uploaded once and sampled every
> frame **without explicit per-pass registration**... the compiler detects the resource via
> StaticImportTag and emits a one-time CopyDst→target-usage barrier on first access."*

材质贴图已经是这条路的先例。IBL 三张图性质完全相同：烘一次、永不变、每帧被采样。

> 顺带：sky cube 本身（`SkyboxSystem.cpp:131` 的 `CreateImportedImage`）其实也符合
> StaticImportTag 语义，现在走 ImportedTag + SkyboxPass 显式 import 算是多绕一圈。
> **本计划不改它**，但 IBL 落地后两条路会并存，是后续可清理项。

### 3. 一个 `EnvironmentCubemap` 资产产出**多产物**

sky cube（带 mip）+ irradiance cube + prefiltered cube 共享同一次 HDRI 输入、同一次烘焙作业、
同一个失效时机，拆成三个独立顶层资产反而要额外处理三者一致性。

走既有的 **sub-asset 机制**（`AssetBuildContext` 的 `parentId` + `db`，
`AssetId::IsSubAsset()` / `GetSubLabel()`），`ModelAssetBuilder.cpp:34-101` 的
`DispatchImageSubAsset` 是现成先例。

### 4. BRDF LUT 后置，先用解析近似

BRDF LUT（DFG 积分）是与场景无关的常量表，可以外部获取。但外部 LUT 有三个必须钉死的约定，
搞错会得到"看着差不多但金属高光系统性偏暗"的难定位 bug：

- **模型要匹配**：本仓库 `Lib/BRDF/BRDF.hlsli:30` 用的是 `V_SmithGGXCorrelated`
  （height-correlated）。流传最广的 Karis/UE4 LUT 用的是非 correlated Smith + `k = α/2`
  的 IBL 重映射，中等粗糙度差几个百分点，掠射角更明显。
- **精度**：至少 RG16F。很多现成的是 8-bit PNG，低粗糙度端 bias 通道会量化出断层。
- **约定**：uv 是 `(NoV, perceptualRoughness)` 还是 `(NoV, α)`？v 轴方向？只能采样验证。

所以**阶段 5 先用 Lazarov 解析近似**（`EnvBRDFApprox`，四五行 ALU、零资源依赖），把整条链
跑通拿到正确画面做基线；**阶段 6 再换真 LUT**，此时有基线可以对比验证约定。这样 BRDF LUT
从阻塞项变成可替换项，且阶段 5 不需要碰新的 ImageUsage / 资产加载路径 / scene 绑定槽位。

> **后续修正**：上面这三条约定仍然成立（搜索时实测到两个最流行的来源 uv 轴是反的），但
> 结论从"外部获取"改成了**自己烘**——阶段 3 之后引擎已有四个 compute bake，自己积分能让
> 模型和精度两个陷阱直接消失。详见阶段 6。

---

## 现状盘点

### 已具备（直接复用）

| 能力 | 位置 |
|---|---|
| 离线 GPU compute 作业闭环 | `Resource/Image/EnvironmentBaker.cpp`：自建 PSO/queue/recorder/fence/pool，upload → dispatch → readback → 阻塞 |
| 反射驱动 compute 绑定 | `BuildShaderInputList` → `PipelineLayoutDescriptor` → `ShaderBindings`，已验证 |
| Cubemap 视图形状 | `ImageViewDescriptor::CreateCubemap(fmt, mipMin, mipMax)` / `CreateCubemapFace(...)`——prefilter 要的「单 mip 单面 UAV」都有 |
| 多 subresource 上传 | `AsyncUploadSystem.cpp:598-615` 已按 arraySlice × mipSlice 全遍历 + 行 band 分块 |
| scene 级绑定 | `SceneBindingSystem` + `ShaderBindingsUtils.h`（Image/Sampler/Constant 齐全） |
| 静态资源 barrier | `StaticImportTag` 一次性 CopyDst→ShaderRead |
| BRDF 拆分 | `D_GGX` / `F_Schlick` / `V_SmithGGXCorrelated` 独立 hlsli，split-sum 直接复用 |
| sub-asset 分发 | `ModelAssetBuilder.cpp:34` `DispatchImageSubAsset` |

### 缺口（本计划要补的）

1. ~~**`AsyncUploadSystem.cpp:591` 栈数组会溢出**~~ ✅ **已完成**（阶段 1）
2. ~~**EnvironmentBaker 只写 mip 0 / 无 constant 传递**~~ ✅ **已完成**（阶段 3b / 3d）
3. ~~**`SkyboxGPUComponent` 只有一个 handle**~~ ✅ **已完成**（阶段 5a，扩到三个）

### 缺口（已知但**本计划不处理**，归入后续统一的资产缓存工作）

- **`ImageAssetData::m_mips` 没有 layer 维度**（`ImageAsset.h:112`）
- **`SerializeToKtx2` 只写 layer 0 / face 0**（`ImageAssetCompiler.cpp:291`）——多层从未被序列化；
  且 cubemap 在 KTX2 语义里应是 `numFaces=6` 而非 `numLayers=6`，现在 `numFaces` 写死 1
- **cube 路径根本没落盘缓存**：`AssembleCubemapData` 不调用 `SerializeToKtx2`，每次启动重烘

**为什么可以不处理**——见下面「阶段 2 已取消」。

---

## 阶段 1：修 AsyncUpload 的栈数组溢出 ✅ 已完成

**这是 cube 加 mip 链之前的硬前置。**

原 `Feature/RHI/System/AsyncUploadSystem.cpp:591`：

```cpp
ImageSubresourceLayout layouts[RHI::Limits::Image::MipCountMax];   // 15
upload.m_targetImage->GetSubresourceLayouts(upload.m_range, layouts, nullptr);
```

**根因**：`GetSubresourceLayouts` 按**全局** subresource 索引写入
（`mip + arraySlice * mipLevels`），见 `Backend/DX12/Resource/Image/Image.cpp:316-318`。
今天 cube 是 6 面 × 1 mip，最大索引 5 < 15，**侥幸没事**；prefiltered cube 是
6 面 × ~6-9 mip，索引到 50+，直接踩栈。

`Image.h` 原本的文档注释是误导源头——它写"数组大小至少为 range 内的 subresource 数
（mip slices × array slices）"，但实现按全局索引写。range 从非零 array slice 起步时，
按注释分配必然越界。

**已实施**：

- `AsyncUploadSystem.cpp`：改为 `eastl::vector`，按 `mipLevels * m_arraySize`（全局索引
  空间）`assign`，且声明在 upload 循环**外**复用分配 —— 最坏情况 `15 * 2048` 个 layout
  放不下栈，必须堆分配，循环外复用避免每次上传都分配一次。
- `Image.h:36-41`：改正该文档注释，明确「全局索引、必须覆盖完整 mip*array 网格」。

**验证结果**：

- `SparkRHI` / `SparkEditor` 编译通过
- SparkEditor 实跑 12 秒无崩溃、stderr 干净；日志显示 2D 多 mip 贴图
  （256×256、9 mips）上传正常 —— 正是本次改动的路径
- ⚠️ **cube 路径本次未实跑覆盖**：默认场景无 skybox。但对现存所有情形
  （2D 为 `mips*1 ≤ 15`，cube 为 `1*6 = 6`）新旧容量都足够、全局索引语义不变，
  行为严格等价。真正的「6 面 × 多 mip」压力要到阶段 3 才存在，届时一并验证。

---

## 阶段 2：~~ImageAssetData 补 layer × mip 布局~~ —— **已取消**

原计划要给 `ImageAssetData::m_mips` 补 layer 维度、修 `SerializeToKtx2` 的多层写入、
让 cube 落盘缓存。**决定整体后置到后续统一的资产缓存工作**，本计划不做。

**为什么能省掉**：`m_mips` 根本不在 IBL 的运行时路径上。

- **上传路径不读 `m_mips`** —— `AsyncUploadSystem` 自己用
  `GetImageSubresourceLayout(imageDesc, ImageSubresource(mip, arraySlice))` 逐 subresource
  重算 CPU 侧紧凑布局（`AsyncUploadSystem.cpp:614-617`），只把 `m_textureBytes` 当作一整块
  连续字节按遍历顺序推进。
- `m_mips` 的现有消费者只有 `GetMipRange` 和 `SerializeToKtx2`，**两者都只服务缓存序列化**。
- `ImageAssetCompiler.cpp:463-468` 那句 "placeholder until the multi-layer m_mips layout
  lands (M5)" 的注释本身也确认了：上传路径不依赖它。

**但换来一条必须遵守的契约**（转入阶段 3）：

> baker 产出的字节顺序，必须与上传路径的遍历顺序严格一致 ——
> **arraySlice 外层、mipSlice 内层**（`AsyncUploadSystem.cpp:598-600`），
> 即 `[face0mip0, face0mip1, ..., face0mipN, face1mip0, ...]`。

现有 sky cube 是 face-major、单 mip，天然满足。加 mip 链后必须保持「每面的完整 mip 链
连续存放」而不是「每 mip 的六个面连续存放」——**这两种顺序都很自然，写反了不会崩，
只会得到面/mip 错位的诡异画面**，是本计划最容易踩且最难查的坑。

**代价**：每次启动都要重烘（含更贵的 prefilter），启动耗时会上升。这是明确接受的取舍——
先把功能跑通，缓存后面对所有资产统一做。

---

## 阶段 3：EnvironmentBaker 泛化

现在是「单 PSO + 单 dispatch + 无 constant」。IBL 需要多 PSO、每 mip 一次 dispatch、带常量。

### 3a. 每个 dispatch 一个 ShaderBindings ✅ 结论已确定

原本这里记的是「同一条命令列表内多次 Compile 的语义」这个未知项。**答案已经查清，而且
问题本身问错了方向**——不该有「复用一个实例塞 N 份数据」这个候选。

`Backend/DX12/.../ShaderInputCompiler.cpp:130-134`：

```cpp
b.m_compiledDataIndex = (b.m_compiledDataIndex + 1) % frameCountMax;
```

descriptor table 确实是 ring buffer，但 **ring 的格数 == `frameCountMax`**（通常 2~3），
分配时也是 `ringSize = viewsPerFrame * frameCountMax`。语义是 **per-frame**（每帧 Compile
一次、N 帧之间不互踩），不是 per-dispatch。prefilter 的 6~9 个 mip 连续 Compile 必然回绕，
前面的描述符被后面覆盖。

**所以：每个 dispatch 一个独立的 `ShaderBindings` 实例。**这不是引擎的怪癖——D3D12 本身就
要求每次 draw/dispatch 用描述符堆里不同的位置，或把数据用 root constant 编进命令流。
「一个 SRG 实例 = 一份数据」是这类抽象的固有语义。

已在 3b 验证：10 个局部 `ShaderBindings`，每个绑不同的 per-mip UAV，一次提交全部 dispatch，
结果正确。3d 的 roughness 常量沿用同一形状（`FindConstantInput` → `SetData`，每实例一份）。

> **副作用**：这是引擎里第一次在**运行期**批量析构 ShaderBindings（此前都活到关闭时释放，
> 走 `isPoolShutdown` 早退分支），因此第一次进入 descriptor 的延迟回收路径，撞出一个既存的
> 悬垂 bug。已修，见 [TODO_DescriptorPoolValueSemantics.md](TODO_DescriptorPoolValueSemantics.md)。

### 3b. 源 cube 生成 mip 链 ✅ 已完成

标准 prefilter 要按 solid angle 选源 mip，抑制高粗糙度下的萤火虫噪点。所以 sky cube 本身
也要 mip 链——这同时是阶段 1、2 的动机汇合点。

**已实施**：

- `cubeImg` 的 `m_mipLevels` 改为 `ImageAsset::ComputeMipLevels(faceSize, faceSize, 0)`
  （该函数从 `ImageAssetCompiler` 的匿名命名空间提为 `ImageAsset` 的公开 static）
- 新增 `Engine/Asset/Shaders/Image/CubemapDownsample.hlsl`：2×2 box filter，每级一次
  dispatch，`Dispatch(dstRes, dstRes, 6)`
- `EnvironmentBaker` 加第二个 PSO（`m_cubeMipsPSO` / `m_cubeMipsLayout`）+ per-mip UAV 视图
  + per-mip ShaderBindings
- `BakedCubemap` 加 `mipLevels` 字段，`AssembleCubemapData` 传递（原先硬编码 1）

⚠️ **源和目标都是 UAV，不是 SRV + SampleLevel**——这是本阶段最反直觉的决定。引擎的 barrier
是整资源粒度（`D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES`，见
`CommandListBase::QueueTransitionBarrier`），**单个 mip 无法转成 shader-read 而让兄弟 mip
保持可写**。所以全链保持 UAV 状态、用 UAV barrier 分隔每级。代价为零：box filter 读 4 个
texel 求平均，本来就不需要 sampler 的双线性插值。

后端在 `StateBefore == StateAfter == UNORDERED_ACCESS` 时会自动降级成
`D3D12_RESOURCE_BARRIER_TYPE_UAV`（`CommandListBase.cpp:218-232`），所以逐级之间再发一次
同状态的 `ConvertToImageShaderWrite` 即可。

> 在 barrier 支持 per-subresource range 之前，**不要**把这里"优化"成 SRV。
> 该能力的缺失是独立议题，见下方「不在本计划范围」。

### 3c. 字节顺序契约 ✅ 已完成（随 3b 一起）

由于不再维护 `m_mips` 的多层布局，**baker 的 readback 字节顺序就是上传路径的唯一真相**。
必须是 **arraySlice 外层、mipSlice 内层**：

```
[face0 mip0][face0 mip1]...[face0 mipN][face1 mip0]...[face5 mipN]
```

readback 循环已改为 face 外层、mip 内层，并在 `EnvironmentBaker.cpp` 就地写死了这条契约
（明确标注「交换两层循环照样编译、照样运行，只会静默产出面/mip 错乱的数据」）。

**验证方式**：`SandBox/Program/RHI/BakeCubemap.cpp` 改造成 mip 链验证工具——

- 自动校验 mip 数与完整链的精确字节总数（不符则非零退出）
- 每个面输出一张竖排 mip 条带 PNG

实测：`mips=11 (expect 11)`、`bytes=67108848 (expect 67108848)` 精确匹配；六张条带均为
逐级平滑模糊、无噪点（证明 UAV barrier 生效）；posX（会议室侧视）与 posY（天花板 + EXIT
标志）内容截然不同（证明面索引没串）。上传侧也已实跑验证通过。

### 3d. 多产物烘焙 ✅ 已完成

三个 shader（`Cubemap.hlsli` / `IrradianceBake.hlsl` / `PrefilterBake.hlsl`）与
`EnvironmentBaker` 的扩展均已落地。`BakeCubemap` 实测三张图的形状与字节总数精确匹配
（`32²×1`、`128²×5`、sky 全链），阶段 5 出画面后效果正常，反向印证了两个卷积的正确性。
下面保留原始设计说明。

#### 数据格式（已定）

三个产物在数据形状上**同构**——都是 6 面 cube、都是 RGBA16F，只是尺寸和 mip 数不同。
所以**每个产物单独就是一个完整的 `BakedCubemap`**，该结构不需要加字段；需要的只是一个
聚合返回值：

```cpp
//! 一次环境烘焙的全部产物。三者共享同一次 HDRI 输入、同一次 GPU 作业、同一个失效时机，
//! 所以一起产出、一起返回（决策 3）。
struct BakedEnvironment
{
    BakedCubemap sky;          //!< 完整 mip 链；天空盒采样 + prefilter 的采样源
    BakedCubemap irradiance;   //!< 32^2 单 mip；diffuse 项
    BakedCubemap prefiltered;  //!< mip N == roughness N/(mipLevels-1)；specular 项

    bool IsValid() const
    {
        return sky.IsValid() && irradiance.IsValid() && prefiltered.IsValid();
    }
};
```

**`ImageAssetData` 不需要任何改动**：每个产物各自走一次 `AssembleCubemapData` 成为独立的
`ImageAssetData`（`arrayLayers=6`），再由阶段 4 的 sub-asset 机制变成三个资产。现有的
`(width, height, mipLevels, arrayLayers, format, textureBytes)` 足以描述其中任何一个。

⚠️ **字节顺序契约（3c）对新产物同样适用**：face-major、mip-inner。prefiltered 是
6 面 × N mip，与 sky cube 同等风险；irradiance 单 mip 天然满足。

⚠️ **prefiltered 的 mip 与普通 mip 链只是形状相同，语义完全不同**：

- sky cube 的 mip N = mip N-1 的 2×2 平均（细节层级）
- prefiltered 的 mip N = **一次独立的 GGX 积分**，对应 `roughness = N/(mipLevels-1)`

数据结构区分不了这一点，所以运行时必须显式 LOD 采样
（`SampleLevel(R, roughness*(mipCount-1))`），**绝不能让硬件自动选 mip**。阶段 5 传的
`g_IBLPrefilteredMipCount` 因此不只是"有几级"，而是 **roughness 的量化步长**。

#### 参数（已定）

| 产物 | faceSize | mipLevels | 说明 |
|---|---|---|---|
| irradiance | 32 | 1 | 余弦卷积后极低频，再大纯属浪费 |
| prefiltered | 128 | 5 | roughness = 0 / 0.25 / 0.5 / 0.75 / 1.0 |

prefiltered **不用**完整 mip 链：切到 1×1 没有收益——roughness 接近 1 时反射已近似均匀，
多出的级数只是浪费烘焙时间。参考值：UE4 128²×7、Filament 256² 起。先按 128²×5，若高粗糙度
过渡不够平滑再加。

注意 `prefiltered.mipLevels` 是**独立参数**，不能像 sky cube 那样用
`ComputeMipLevels(faceSize, faceSize, 0)` 推完整链。

#### 3d-1. 抽公共基准 `Shaders/Lib/Cubemap.hlsli`

把 `EnvironmentBake.hlsl:24-35` 的 `FaceDirection` 抽出来，三个 shader 共用。面基准必须
**完全一致**，否则三张图朝向对不上——症状是"环境光方向不对"，极难定位。留三份拷贝迟早漂移。

#### 3d-2. `IrradianceBake.hlsl`

```
E(N) = ∫ L(ω)·(N·ω) dω        （以 N 为中心的半球）
```

只依赖 N（不依赖视线 / roughness / 位置），所以可预计算成一张 cube。一次 dispatch 即可。

每个输出 texel：`FaceDirection` 得到 N → 构造切线正交基 → 半球均匀网格采样累加。

```hlsl
float3 up    = abs(N.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);  // 避开与 N 平行的退化
float3 right = normalize(cross(up, N));
up           = normalize(cross(N, right));

// 内层累加（delta ≈ 0.025）
float3 tangentDir = float3(sin(theta)*cos(phi), sin(theta)*sin(phi), cos(theta));
float3 worldDir   = tangentDir.x*right + tangentDir.y*up + tangentDir.z*N;
sum += SampleSrc(worldDir).rgb * cos(theta) * sin(theta);
```

两个三角因子来源不同，容易混淆：

- `cos(theta)`：**物理**上的 Lambert 余弦权重
- `sin(theta)`：**数学**上的球坐标雅可比（`dω = sinθ dθ dφ`）。漏掉会让极点方向被严重高估

**π 约定（最易错，必须写进 shader 文件头）**

黎曼和还原成积分：`Δθ = Δφ = delta`，`count = (2π/Δ)·(π/2/Δ)`，故 `Δ² = π²/count`：

```
E = Σ[L·cosθ·sinθ] · π² / count
```

最终 diffuse 是 `albedo/π · E`。两种存法：

| | cube 里存 | shader 里 |
|---|---|---|
| **A. 存纯物理量** ✅ 选这个 | `E`（乘 `π²/count`） | `diffuseColor * Fd_Lambert() * E` |
| B. 预乘 1/π | `E/π`（乘 `π/count`） | `diffuseColor * irradiance` |

B 是 LearnOpenGL 等教程的做法，少一次除法。**本仓库选 A**：`Lib/BRDF/Diffuse.hlsli` 已经
把 `1/π` 明确放在 `Fd_Lambert()` 里，归一化是 BRDF 库的职责；B 等于把 BRDF 的一半烘进数据，
以后换 diffuse 模型（如 Burley）会撞车。

> 搞错的症状是漫反射整体亮/暗 π 倍。tonemap 之后看起来只是"稍微亮了点"，几乎不可能靠肉眼
> 定位——所以必须在注释里写死，别指望事后看出来。

**从源 cube 的高 mip 采样**（`srcMip` 不该是 0）——这是 3b 的第二个用途：

- 结果几乎不变（余弦卷积本就是低通）
- **显著降噪**：环境中若有小而亮的光源，mip 0 采样会因命中/不命中在相邻 texel 间跳变
- 更快、cache 更友好

**采样方式**：先用均匀网格（好懂好调试）。`delta=0.025` 时每 texel 约 15800 次采样，
32²×6=6144 texel 共约 9700 万次——离线可接受。若嫌慢再换 cosine-weighted 重要性采样
（PDF `cosθ/π`，估计量简化为 `π·ΣL/N`，同质量下采样数降一个数量级），届时可复用 prefilter
的 Hammersley 工具函数。

#### 3d-3. `PrefilterBake.hlsl`

split-sum 的左项。每 mip 一个 roughness，GGX 重要性采样，per-mip UAV 用
`ImageViewDescriptor` 的单 mip 范围（**不要**用 `CreateCubemap()`，那会设 `m_isCubemap=1`，
是给 SRV 的；UAV 要按 Texture2DArray 看——同 3b 的坑）。

两件 3b 埋下的伏笔必须在此兑现：

1. **constant 传递**（3a 的剩余部分）：shader 声明 cbuffer（roughness + sampleCount），
   `FindConstantInput` → `SetData`，**每个 mip 的 ShaderBindings 各带一份**。不需要自建
   BufferPool——`ShaderBindings` 内部管理 constant buffer。
2. **按 solid angle 选源 mip**：高粗糙度的 GGX 波瓣很宽，固定采样数下从 mip 0 采样是严重
   欠采样，画面上是一片萤火虫噪点。**这是 3b 做完整 mip 链的全部动机**，不用上等于 3b 白做。

Karis 的简化 `N = V = R` 照常采用，代价是丢掉掠射角的各向异性拉伸高光——所有主流引擎都接受。

#### 3d-4. `EnvironmentBaker` 扩展

`Bake()` 拆成可复用的私有步骤，返回 `BakedEnvironment`。新增两个 PSO（形状照抄 3b 的
`m_cubeMipsPSO`）。

**三个产物必须在同一次 `Bake()` 调用内完成**：prefilter/irradiance 直接消费 GPU 上的
`cubeImg`（含 3b 刚生成的 mip 链），不经过 CPU 往返。readback 在全部 dispatch 之后统一做。

#### 验收

扩展 `SandBox/Program/RHI/BakeCubemap.cpp`（已具备形状自动校验 + mip 条带输出）：

- 三张图都能烘出并 readback，形状与字节总数精确匹配
- irradiance：柔和的低频环境色，无高频细节
- prefiltered：条带逐级更模糊，且**每级都是独立积分的结果**（不是简单降采样——可与 sky cube
  的同级 mip 对比，两者应明显不同）
- 无萤火虫噪点（若有，先查 solid-angle mip 选择是否真的生效）

---

## 阶段 4：多产物接入资产层 ✅ 已完成

三个产物成为三个独立 `AssetId`：主资产是 sky cube，两张 IBL 图是它的子资产。

### 与原计划的两处偏离

**1. 子资产不进 `Load`/`Compile`，直接发布已编译数据**（原计划：预置 `rawData` 走「直通分支」）

直通分支需要一个只为满足 `Compile(ctx)` 签名而存在的 raw 载体——`BakedCubemap` 在父
`Compile` 里已是最终形态，`AssembleCubemapData` 就是「编译」的全部内容，而父资产自己
已经在直接调它。为了走完整的轨把成品拆回半成品再拼一次，不值得。

改为 `ImageAssetBuilder::PublishSubAsset`：`CreateAsset → InsertOrGet → SetDataReady →
OnAssetReady`，四步。**递归风险随之从「需要小心处理的分支」变成结构上不可能。**

⚠️ 与 `ModelAssetBuilder::DispatchImageSubAsset` 有一处必须不同：那个函数开头是
`if (db->Find(subId)) return;`（已存在就跳过）。这里必须**覆盖**——父被重新 Process 只
可能是 HDRI 变了，早退会留下旧 IBL 图配新天空。

**2. 父→子的引用存 `Ptr<ImageAsset>` 而非 `AssetId`**（原计划未定）

放在 `ImageAssetData` 上（不为此派生子类——本仓库全局无 `dynamic_cast`、`Object` 也没有
type id，派生类会逼消费方引入一个没有失败模式的向下转型）。存强引用而非 id：id 对应的
资产若被 `ReleaseAsset`，`FindAsset` 返回 null，**与「这张图没有 IBL 产物」完全同症状**，
而后者正是阶段 5 的正常门控信号。无环——子不指回父。

### 实际改动

| 位置 | 内容 |
|---|---|
| `EnvironmentBaker.h` | `kIrradianceSize` / `kPrefilterSize` / `kPrefilterMips` 提为 public static constexpr，供 descriptor 引用（descriptor 与 bake 形状不一致不会在任何地方报错） |
| `ImageAsset.h` | `ImageUsage` 末尾追加 `IrradianceCubemap` / `PrefilteredCubemap`；`IsCubemapUsage()` helper |
| `ImageAsset.cpp` | `Hash()` 的 faceSize 折入条件 → `IsCubemapUsage`（不改现有两个 usage 的哈希值）；`DescriptorForUsage` 两个新 case |
| `ImageAsset.h/.cpp` | `ImageAssetData` 两个 `Ptr<ImageAsset>` + 访问器，析构 out-of-line；`ImageAsset` 转发访问器 |
| `ImageAsset.h/.cpp` | **`ImageAsset::MakeSubId(parentId, subLabel, usage)`** —— 图片子资产 id 构造的唯一入口，含 `!IsSubAsset()` 断言 |
| `ImageAssetBuilder` | `PublishSubAsset` + `CompileEnvironmentCubemap`；`env.IsValid()` 三者全有才算成功 |
| `ModelAssetBuilder.cpp` | 内嵌图那 4 行 `AssetId::OfSub` 换成 `MakeSubId`，id 值不变 |
| `ImageAssetCompiler.cpp` | `perFaceBytes` 的 `kCubeBytesPP = 8` 换成 `RHI::GetFormatSize(baked.format)`——同一函数里 `m_format` 已是从该字段读的，写死等于给一个事实两个真相来源 |

### 守卫：派生 usage 不得独立构建

`Load` / `Compile` 开头各一道：usage 是派生值 → `LOG_ERROR` + return。

这不是防御性洁癖。两个子 id 的 `path` 指向父 HDRI，所以对它们调 `LoadAsset` 会**成功**读到
文件并解码，然后因 usage 不是 `EnvironmentCubemap` 落进通用 2D 分支，用一张「把 HDRI 当
2D 编译」的图覆盖掉烘焙结果。整条路径没有任何一步会失败。

### 顺序契约

两个子资产在父 `Compile` 返回**之前**发布完毕。`ProcessAsset` 在 `Compile` 返回后才
`SetDataReady(父)` + 广播，所以消费方看到 sky cube 变 Ready 时，两个子资产必然已 Ready ——
阶段 5 不需要任何等待逻辑。

### 验收结果

`BakeCubemap.cpp` 扩了 `VerifyAssetLayer`（原先只驱动 baker，绕过资产层）：走
`AssetManager::LoadAsset` 完整跑一遍，校验形状 + Ready + **父持有的实例就是 db 里注册的
那个**（两者若分叉，按 id 解析和经父访问会拿到不同对象）。

- 退出码 0：`irradiance = 32x32 x1 mips x6 layers, ready=true`、
  `prefiltered = 128x128 x5 mips x6 layers, ready=true`
- builder 日志：`sky 512^2 x10 mips, irradiance 32^2 x1, prefiltered 128^2 x5 (6 faces each)`
- 全量 Debug 编译零错误零警告；`SparkAssetTest` 45/45

> 顺带修了一个既有失败：`ModelAssetTests.cpp:307` 的 subLabel 期望值停留在 `1a0cfbb`
> （2026-07-27，*glb subid dedup*）改格式之前的 `"image/<name>"`，实际已是
> `"image/<index>/<name>"`。索引恒在前是去重所需（glTF 允许多图重名/空名），所以代码是
> 对的、测试过期，改测试。

---

## 阶段 5：接入 scene 绑定与光照

### 5a. Skybox feature 侧

### 5a. Skybox feature 侧 ✅ 已完成

`SkyboxGPUComponent` 从一个 handle 扩到三个（`m_cubemap` / `m_irradiance` /
`m_prefiltered`）。`BuildGPUResources` 里建 desc + 建 image + upload 那段抽成匿名
namespace 的 `CreateAndUploadImage(rhiCtx, asset, name, staticImport)`，调三次：sky 仍走
`CreateImportedImage`，两张 IBL 走 **`CreateStaticImage`**（决策 2）。

**偏离原计划：不给两张 IBL 图加 active tag。**`ActiveSkyCubeTag` 存在的理由是让
`SkyboxPass` 找到 cube 做 **render graph import**；IBL 图走 `StaticImportTag`，不进
render graph、没有 attachment 声明，不需要这个入口。而 `SceneBindingSystem` 本来就是
直接读世界组件的（`world->GetView<Light::LightRenderData>()`），读 `SkyboxGPUComponent`
是同一个模式——SparkRHI 把 `Feature/` 暴露成 include root，纯头文件组件不需要 link，
**CMake 一行都不用改**。

多 skybox 时的判别复用 `ActiveSkyCubeTag`：找 `m_cubemap` 带该 tag 的那个组件，三个
资源从同一组件取，天空与环境光结构上不可能来自两个 HDRI。

IBL 是**可选的**：`GetIrradianceAsset()` 为空则 handle 保持 `NullHandle`，光照回退常量
ambient。取子资产不检查 Ready —— 阶段 4 的顺序契约保证父 Ready 时子必已 Ready。

### 5a-2. 天空盒亮度参数 ✅ 已完成

`SkyboxComponent` 加 `float m_intensity = 1.0f`（反射为 `FloatElement(0, 10, 0.01)`）。

**为什么可以在采样时乘而不用重烘**：irradiance 与 prefiltered 都是入射辐亮度的**线性**
积分，radiance × k ⟹ 两个积分精确地 × k。所以运行时一次乘法严格等价于用 k 倍亮的 HDRI
重烘，不是近似。必须同时作用于**天空、diffuse、specular 三处**，否则会出现"调亮了天空
但场景没变亮"这种极难定位的不一致。

**顺带修的**：`OnComponentUpdated` 原本无条件 `Cleanup + Resolve`。亮度是可编辑字段，
拖一次滑条会销毁重传三张 cube（sky 那张就有几十 MB）。加了短路：asset id 未变**且**资源
已存在 → 直接返回。第二个条件不能省——「资产加载完成后上层重发 update」那条路径正是
id 相同但资源尚未建立。

> 色调（tint）不做：给照片来源的光重新上色没有物理依据，同样效果在已有的 `TonemapPass`
> 里做更合适。旋转**留接缝不建**：它是三个里对内容制作最有价值的（对齐 HDRI 的太阳与
> 方向光），运行时也便宜，但有个前置的内容管线问题——转了环境不转太阳灯就脱钩了；且它
> 是唯一需要三个采样点方向严格一致的参数，逆矩阵搞反一处就会天空与光照朝向不一。接缝
> 即 5b 的 `.Binds<MainSceneTag>()` 与具名的环境参数区域，届时是加法不是重构。

### 5b. SceneBindings ✅ 已完成

`Engine/Asset/Shaders/SceneBindings.hlsl` 新增：

```hlsl
TextureCube  g_IrradianceCube  : register(t1, space0);
TextureCube  g_PrefilteredCube : register(t2, space0);
SamplerState g_IBLSampler      : register(s0, space0);
```

`SceneConstants` cbuffer 追加 `uint g_IBLPrefilteredMipCount;` 与 `float g_EnvIntensity;`
（复用现有的 `g_ScenePad0` / `g_ScenePad1` 槽位）。

⚠️ **必须同步改 `SceneBindingsReflect.hlsl`**：那个反射宿主的注释自己写着，`VSMain` 同时
引用 `g_Lights` 和 `g_LightCount` 是「so neither is optimized out of the reflected layout」。
新加的 `TextureCube` / `SamplerState` 若不在 `VSMain` 里被真正采样，会被 DXC 从反射结果里
剥掉 → `FindImageInput` 返回 null → 每帧刷 "Image input not found"、画面无 IBL。

⚠️ `SkyboxPass` 目前只 `.Binds<MainViewTag>()`，**没有 space0**。`g_EnvIntensity` 要作用到
天空盒必须给它补上 `MainSceneTag`（`LightingPass` 已经是 `<MainViewTag, MainSceneTag>`）。

亮度从活跃 skybox 的 `SkyboxComponent::m_intensity` 读（5a-2），与两张图取自同一个组件。

**`0` 同时是「IBL 未就绪」的门控信号**——这是必须的，不是可选优化：
`SceneBindingSystem.cpp:154-158` 的注释说明了 CBV 未绑定会硬断言、SRV table 未绑定会被
跳过。没有 skybox / 烘焙未完成时，shader 若无条件采样会读到脏描述符。门控让 shader
回退到现在的常量 ambient，与 `g_LightCount` 恒写的处理是同一套路。

`SceneBindingSystem::BindEnvironmentIBL()` 形状照抄 `BindFrameLights`：遍历
`SkyboxGPUComponent` 找 `m_cubemap` 带 `ActiveSkyCubeTag` 的那个 → 从 `SkyboxComponent`
读 intensity → 两张图**都** `IsResourceReady`（复用 `RenderGraphUtils.h` 里 `SkyboxPass`
用的同一个就绪定义）才绑 → `GetOrCreateImageView` + `SetShaderImage`/`SetShaderSampler`。
两个常量恒写。

mip count 取自 GPU image 的 `GetDescriptor().m_mipLevels`（它本就是从资产的 mip 数建的），
不用 `EnvironmentBaker::kPrefilterMips` —— 改了烘焙参数重烘后不用改这里。

**两张图必须同时就绪才绑**：只绑一张会让另一张的描述符槽位是脏的，而 mip count 已经在说
"可以采样了"。

#### 反射自检（新增，非原计划）

上面那个「必须同步改 reflect host」的坑有个特别阴的性质：**常量被剥掉会每帧刷
`Constant input not found`，而图像被剥掉是完全静默的**——图像只在有人要用时才绑，所以一张
被丢掉的 cube 会一直沉默到有人纳闷 IBL 为什么没效果。

所以 `Init` 末尾按名探测两个 cube + sampler，缺失就 `LOG_ERROR` 并指名要改哪个文件；同时
补一行成功日志。那行日志不是装饰——验证时它立刻就有用了：没有它，「自检零报错」和
「`Init` 提前 return 了」这两种情况**在日志上完全一样**（`Update` 会因
`m_bindings == NullHandle` 静默早退）。

### 5c. Lighting.hlsl ✅ 已完成

新增 `Lib/BRDF/EnvBRDF.hlsli`（Lazarov 解析近似）；`Lighting.hlsl` 加 `EvaluateIBL()`，
原来的常量 ambient 变成 `HasEnvironmentIBL()` 的二选一分支；`Skybox.hlsl` 乘
`g_EnvIntensity`，`SkyboxPass` 补 `MainSceneTag`。

sampler：`FilterMode::Linear` + `AddressMode::Clamp`（在 `SceneBindingSystem` 侧建）。

#### π 约定 —— 全阶段最高风险的一行

已查证本仓库的约定是确定的：`Fd_Lambert()` 返回 `1/π`，且直接光路径的 `Fd_Burley` 也把
`1/π` 包在自己内部。**归一化住在 BRDF 库里，数据存纯物理量**，正是决策 3d-2 选的方案 A。
irradiance cube 存的是 `E`，所以：

```hlsl
float3 diffuse = diffuseColor * Fd_Lambert() * irradiance;
```

漏掉 `Fd_Lambert()` 会让漫反射**整体亮 π 倍（≈3.14×）**，tonemap 之后读起来只是"稍微亮
了点"，肉眼基本判定不了。shader 里压了一段注释写明这一点，并直说**那条注释就是唯一防线**。

用 Lambert 而非 Burley：Burley 需要单条光线的 `L`（经 `LoH`），预积分环境没有单一入射方向。

#### 另外两处与原计划不同

- **用 `diffuseColor` 而不是 `albedo`**。原来的常量 ambient 乘的是 `albedo`，对 0.03 的
  常数无所谓，对 IBL 是错的——金属没有漫反射。
- **`ao` 乘在 diffuse 和 specular 两项上**。严格说 specular 需要独立的 specular occlusion
  项（由 `NoV, ao, roughness` 推），但不乘会让金属表面的 AO 整个消失，比不严谨更难看。
  留作后续细化。

#### `Skybox.hlsl` 与 `SkyboxPass` 必须同时改

加了 `#include <Shaders/SceneBindings.hlsl>`，space0 就进了天空盒的 pipeline layout；此时
pass 若不绑 `MainSceneTag`，**CBV 未绑定是硬断言**。中间态会崩，不是顺序问题。
（实现时 `SkyboxPass.cpp` 还需补 `#include <SceneBind/SceneBinding.h>` 才能拿到该 tag。）

#### 验收结果

**已实跑确认效果正常**（用户目视）：金属随粗糙度出现环境反射，功能正常。

工程侧另外确认：全量编译零错误零警告；编辑器实跑 shader 全部编译通过（**DXC 是运行时编译
的，C++ 构建发现不了 HLSL 错误**，所以这一步不能省）、零 assert、stderr 干净；无 skybox
时走 `g_Ambient` 回退分支。

---

## 阶段 6-b（可选，后置）：diffuse irradiance 换球谐（SH）

同样是「先有基线再替换」的思路，与阶段 6 的 BRDF LUT 并列，互不依赖。

**动机**：余弦卷积是极强的低通滤波器，irradiance 函数天生低频。按 Ramamoorthi &
Hanrahan 2001，**前 3 阶（L2）共 9 个系数**即可达到 1% 以内精度。

| 表示 | 大小 |
|---|---|
| 32² cube × 6 面 × RGBA16F | 49,152 B |
| L2 球谐，9 个 `float3` | 108 B |

差约 450 倍，且 SH 质量略优——没有 cube 面接缝，也没有纹理插值误差。运行时省掉一次纹理
采样和一个 SRV 槽位，改为几行 ALU 求值。Unity 的 light probe、UE 的 sky light 都是 L2 SH。

**只能用于 diffuse**：specular 依赖 roughness 维度，且低粗糙度时是高频信号（接近镜面），
低阶 SH 表示不了，高阶又系数爆炸。所以标准做法是混合的——diffuse 走 SH，specular 仍走
prefiltered mip 链。

**为什么后置而不是一开始就做**：

1. cube 能直接 dump 成 PNG 目视检查（`BakeCubemap` 已有这条路）；SH 是 9 个抽象系数，
   早期排错只能靠数值比对
2. cube 走现成的「资产 → 上传 → scene 绑定」路径，与 prefiltered 完全一致；SH 系数要走
   constant buffer，是另一套传递方式
3. 阶段 5 的 shader 改动更小（一次 `SampleLevel(N)` vs 一个 SH 求值函数）

**替换时的形状**：烘焙侧把半球积分的结果投影成系数（积分循环本身不变，只是累加目标从
texel 变成系数）；shader 侧把 `SampleLevel` 换成 SH 求值；中间的 irradiance 资产/上传/
绑定路径整段删掉。届时 cube 版本仍在，可直接比对两者结果验证正确性。

---

## 阶段 6（可选，后置）：换真 BRDF LUT —— **方案已定，待实施**

有了阶段 5 的正确画面做基线之后再做，此时可以对比验证约定是否搞错。修的是风险表里那条
「Lazarov 近似拟合的是 UE4 的非 correlated Smith，与本仓库 `V_SmithGGXCorrelated` 存在
系统性偏差」。

### 决策 1：自己烘，不用外部 LUT

写决策 4 时的判断（外部 LUT 有模型 / 精度 / uv 三个必须钉死的约定）**仍然成立，而且搜索时
实测到了**：LearnOpenGL 的 `ibl_brdf_lut.png` 是 `X=NoV, Y=roughness`，而
HectorMF/BRDFGenerator 是 `X=roughness, Y=NoV`——**两个最流行的来源轴是反的**，拿错了画面
不崩、只会金属高光系统性偏暗。

但情况和写决策 4 时不一样了：`EnvironmentBaker` 已经跑通四个 compute bake，而 BRDF LUT
是其中最简单的一个（无源贴图、无 cubemap、无 per-mip 循环，一次 2D dispatch，约 40 行
HLSL）。**用仓库现成的 `V_SmithGGXCorrelated` 积分，模型天然对齐**——三个陷阱一次消失，
比"下载一张图再反复采样验证它的约定"工作量还小。

### 决策 2：离线生成 + 签入文件，运行时只加载

LUT 与场景无关，是数学常量，一个进程只需要一份、跨项目永不变。所以生成器放
`SandBox/Program/RHI/`（与 `BakeCubemap` 并列），**手动跑一次**，把 `.ktx2` 签进
`Engine/Asset/`。

**运行时因此完全不需要烘焙代码**——`EnvironmentBaker` 不加第 5 个 PSO，启动不多一次
dispatch。

### 决策 3：不新增 `ImageUsage`，它就是一张普通 2D 资产

曾考虑过给它一种新的图片资产类型。结论是不需要——它与现有资产的差别只有一条真正成立：
**既没有源文件、也没有父资产**（irradiance / prefiltered 没有源文件，但它们借父 HDRI 的
路径拿身份）。而一旦走"签入文件"这条路，它**有了自己的文件**，这条差别也消失了。

格式也不是问题：`ImageFormat`（R8/RG8/RGBA8/RGBAF32）是**解码侧**枚举，只服务从图片文件
解出来的路径；烘焙产物直接往 `ImageAssetData::m_format` 写 `RHI::Format`，LUT 同理写
`R16G16_FLOAT`。

> 反过来说，如果将来要加**更多生成型贴图**（默认白图 / 法线图 / 噪声图这类材质 fallback），
> 那"生成资产"就有了第二个用例，届时值得建统一机制并把 LUT 收编。现在只有一个，不值得造。

### 决策 4：容器用 KTX2

**写入侧几乎现成**：`SerializeToKtx2` 已存在且被验证过（`ImageAssetCompiler.cpp` 每次编译
都在生成 blob，只是拿来打印大小后丢掉）。它的已知缺陷（只写 layer 0 / face 0、`numFaces`
写死 1）**全是 cube / 多层的问题**，而 LUT 是 2D 单层单面，正落在它做对的那一档。
只差一个格式映射：`VkFormatValue` 里没有 `R16G16_SFLOAT`（Vulkan 枚举 83，实现时对头文件）。

**读取侧要新建，但不是一次性投入**：`ImageAssetLoader` 只有 stb 和 SVG 两条路，读不了
KTX2。而**落盘缓存本来就必须补这条路**（现在 `SerializeToKtx2` 的产物根本没人读）。LUT 会
成为它的第一个消费者，而不是为它单开一条以后要废弃的路。

**排除的选项**：

| 容器 | 为什么不行 |
|---|---|
| Radiance `.hdr` | 零改动最诱人（`.hdr` 已在 `GetSupportAssetType`，`stbi_loadf` 直出 RGBAF32），但 **RGBE 是 RGB 共享一个指数**：A≈0.9 与 B≈0.02 同处一个 texel 时，B 只剩约 5 个量化级——比 8-bit PNG 还糟，正是决策 4 警告的那种断层 |
| 16-bit PNG | 精度够（[0,1] 上 65536 级），但 `stbi_loadf` 读 16-bit PNG 内部走 8-bit 路径会截断，得改用 `stbi_load_16` 并新增 16-bit unorm 的 `ImageFormat`——工作量与 KTX2 相当、复用性低得多 |
| 自定义 `.bin` | 读写各二十行最省事，但等于在 KTX2 之外另造一个容器 |

### 关键结构点：KTX2 就是「已编译形式」

加载一个 `.ktx2` **不应该再走 `ImageAssetCompiler::Compile`**（mip 生成 + BCn），那会把一张
烘好的 RG16F 表当原图重新处理。正确形状是：识别 KTX2 → 直接反序列化成 `ImageAssetData`
→ 跳过 Compile。

**这恰好就是缓存要的形状**（磁盘上是编译产物、加载绕过编译），所以为 LUT 建的这一小块，
缓存可以整段复用。副作用：走这条路之后，descriptor 里驱动编译的字段（compression /
maxMipLevels / colorSpace）对该资产不再参与，格式由文件本身决定。

### 三步（可分别验证）

1. **生成器** `BRDFLutGen`（沙盒工具）：compute 积分 DFG → `SerializeToKtx2` → 写文件。
   附带补 `R16G16_SFLOAT` 的格式映射。**跑完就能用 KTX2 查看器确认，不必等接入**
2. **读取路径**：`ImageAssetLoader` 认 `.ktx2` → 反序列化 → `Compile` 直通
3. **接入**：作为普通 2D 资产加载，绑到 `space0` 的 `t3`，`EnvBRDF.hlsli` 里把解析近似换成
   查表（调用点不变）

### 验收

与解析近似的差值应在几个百分点内。**若差异明显，先怀疑 uv 约定和 v 轴方向**——自己烘虽然
消掉了模型和精度两个陷阱，uv 约定仍然是生成器和 shader 两处必须一致的东西（和 roughness
阶梯是同一类：两份独立代码，写反了不报错）。

---

## 风险与未决项

| 项 | 说明 |
|---|---|
| ~~字节顺序契约~~ | ✅ 阶段 3c 已落地并由 `BakeCubemap` 的条带图验证。契约本身仍然成立，3d 新增的两张图**同样要遵守** |
| ~~SRG 多次 Compile 语义~~ | ✅ 已查清：ring 是 per-frame（`frameCountMax` 格），必须每 dispatch 一个实例。见 3a |
| ~~cube 多 mip 上传未实测~~ | ✅ 已实跑验证 |
| ~~子资产递归~~ | ✅ 不再存在：阶段 4 改为「发布已编译数据」，子资产根本不进 `Load`/`Compile`。取而代之的风险是**派生 usage 被独立加载**（会静默成功），已由 `Load`/`Compile` 的守卫挡住 |
| ImageUsage 枚举顺序 | 数值进 `Hash()` → AssetId 身份，只能追加 |
| 启动耗时 | prefilter 是阻塞式 CPU-wait（`EnvironmentBaker` 的设计前提），多 mip 多 dispatch 后启动会明显变慢。**本计划明确接受**——缓存归后续统一的资产缓存工作。3d 之后应实测一次，若过慢再决定是否提前做缓存 |
| ~~采样数 vs 噪点~~ | ✅ 阶段 3d 已按 solid angle 选源 mip（1024 采样），阶段 5 画面无萤火虫噪点 |
| ~~SceneBindings 反射被剥~~ | ✅ 阶段 5b 加了启动期自检。**图像被剥掉是完全静默的**（不像常量会每帧报错），所以这道自检不是冗余 |
| specular occlusion | 阶段 5c 新增。`ao` 目前直接乘在 IBL specular 上，严格说需要由 `NoV, ao, roughness` 推导的独立项。不乘则金属上的 AO 完全消失，所以现状是两害相权 |
| EnvBRDF 与本仓库 BRDF 不同源 | 阶段 5c 新增。Lazarov 近似拟合的是 UE4 的非 correlated Smith，而 `EvaluateBRDF` 用 `V_SmithGGXCorrelated`，IBL specular 与直接光 specular 存在系统性小偏差。阶段 6 的真 LUT 正是修这个 |

## 不在本计划范围

- **资产落盘缓存**（原阶段 2）：`ImageAssetData::m_mips` 的 layer 维度、
  `SerializeToKtx2` 的多层写入、KTX2 的 `numFaces` vs `numLayers` 语义、cube 缓存命中。
  归入后续对**所有**资产统一的缓存机制。
- 运行时动态 IBL（天空变化时重烘）。`ComputePassBuilder` / `ComputePassTag` 已经存在，
  将来要做有路，但现在烘焙走的是 off-frame 阻塞作业，不碰帧循环。
- sky cube 从 `ImportedTag` 迁到 `StaticImportTag`（见决策 2 的旁注）。
- 多探针 / 局部反射探针。
- **barrier 的 per-subresource range 支持**。3b 因为缺这个能力才走「源目标都用 UAV +
  UAV barrier」的路子。IBL 全线只有 3b 会碰到它（3d 的 prefilter/irradiance 是**跨资源**
  读写，各自整资源转换即可），且已有等价解法，所以不值得为 IBL 做。真正需要时的触发条件：
  render graph 里的 transient 资源做 mip 链、同一张图部分 mip 当 RT 部分当 SRV（hi-z、
  级联阴影）、texture array 的部分层需要独立状态。届时成本主要在
  `RenderGraphCompiler` 的状态推导要按 range 传播，而不是给 `ImageBarrier` 加字段。

## 待观察（不阻塞，阶段 5 画面出来后回看）

- **sky cube 的 mip 链是否需要上传**。它的主要用途是烘焙时给 prefilter 做 solid-angle
  采样，而那发生在 GPU 上、readback 之前就用完了；运行时天空盒只采 mip 0。目前这 33% 的
  额外显存与上传带宽是为一个已结束的用途付的钱。潜在价值是模糊天空 / 将来的动态重烘，且
  改动会牵扯 readback 逻辑，所以先留着。
