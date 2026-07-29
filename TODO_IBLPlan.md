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
2. **EnvironmentBaker 只写 mip 0、无 constant 传递**
3. **`SkyboxGPUComponent` 只有一个 handle**（`Skybox/Components.h:32`）

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

### 3a. constant 传递

bake shader 现在只有 SRV/sampler/UAV，没有 cbuffer。`ShaderBindings` 内部管理 constant
buffer（见 `ShaderBindingsUtils.h:44-64` 的 `FindConstantInput` → `SetData`），所以
Baker 里加常量**不需要**自建 BufferPool，只要：shader 声明 cbuffer → `FindConstantInput`
→ `SetData` → 重新 `Compile(*m_bindings)`。

⚠️ 每次 dispatch 间改常量要重新 compile SRG，需确认 `ShaderInputCompiler` 在同一条命令列表内
多次 Compile + 多次 Submit 的语义（描述符是否被后续 Compile 覆盖）。**这是本阶段唯一的未知
项**，若不成立则退回「每 mip 一个 ShaderBindings 实例」。

### 3b. 源 cube 生成 mip 链

标准 prefilter 要按 solid angle 选源 mip，抑制高粗糙度下的萤火虫噪点。所以 sky cube 本身
也要 mip 链——这同时是阶段 1、2 的动机汇合点。

`cubeImg` 的 `m_mipLevels`（`EnvironmentBaker.cpp:311`）从 1 改为完整链；加一个
downsample compute（或逐 mip box filter dispatch）。

### 3c. 字节顺序契约（阶段 2 取消后转入此处）

由于不再维护 `m_mips` 的多层布局，**baker 的 readback 字节顺序就是上传路径的唯一真相**。
必须是 **arraySlice 外层、mipSlice 内层**：

```
[face0 mip0][face0 mip1]...[face0 mipN][face1 mip0]...[face5 mipN]
```

现有 `EnvironmentBaker::Bake` 的 readback 循环（`EnvironmentBaker.cpp:527-550`）是
face-major 单 mip，天然满足；加 mip 链后在面循环内嵌 mip 循环即可。

⚠️ 写成「每 mip 的六个面连续」（mip-major）同样自然，且**不会崩、不会报错**，只会得到
面/mip 错位的诡异画面。建议在 baker 里就地加一行注释锁死这个约定，并在
`AsyncUploadSystem.cpp:598-600` 的遍历处反向引用它。

### 3d. 多产物烘焙

`Bake()` 拆成可复用的私有步骤，新增：

- `BakeIrradiance(srcCube, size)`：cosine-weighted 半球积分，输出小尺寸 cube（32² 足够）
- `BakePrefiltered(srcCube, size, mipCount)`：每 mip 一个 roughness，GGX 重要性采样，
  per-mip UAV 用 `ImageViewDescriptor::CreateCubemap(fmt, mip, mip)`

新增 shader：`Engine/Asset/Shaders/Image/IrradianceBake.hlsl`、
`Engine/Asset/Shaders/Image/PrefilterBake.hlsl`。两者都复用
`Lib/BRDF/Distribution.hlsli` 的 `D_GGX`，且都需要与 `EnvironmentBake.hlsl:24-35`
**完全一致的 `FaceDirection` 面基准**——建议把它抽到
`Shaders/Lib/Cubemap.hlsli` 共用，避免三份拷贝漂移。

**验收**：三张图都能烘出来并 readback；irradiance 目视为柔和的低频环境色；prefiltered
的高 mip 逐级模糊。

---

## 阶段 4：多产物接入资产层

`ImageUsage`（`ImageAsset.h:42-47`）新增两个值——**必须追加在末尾**，该枚举的数值进
`ImageAssetDescriptor::Hash`，重排会静默失效所有已缓存的 image sub-asset id：

```cpp
IrradianceCubemap,   // 由 EnvironmentCubemap 烘焙派生
PrefilteredCubemap,  // 同上，带 mip 链，每 mip 一个 roughness
```

`ImageAssetBuilder::Compile`（`ImageAssetBuilder.cpp:64-88`）的 EnvironmentCubemap 分支：
主资产仍是 sky cube，之后按 `DispatchImageSubAsset` 的模式分发两个子资产
（sub-label `"Irradiance"` / `"Prefiltered"`）。

⚠️ 子资产会**再次**进入 `ImageAssetBuilder::Compile`，若又落入 EnvironmentCubemap 分支就
递归了。两个新 usage 需要走「直通」分支：rawData 已是烘好的 cube 数据，Compile 只做
`AssembleCubemapData`。实现时确认是预置 `child.rawData` 后直接调 Compile（跳过 Load，
`AssetBuildContext.h:26` 注释支持这种用法），还是走 `sourceData` 路径。

**验收**：加载一个 HDRI → AssetManager 里出现 1 主 + 2 子共 3 个 image 资产，状态均 Ready。

---

## 阶段 5：接入 scene 绑定与光照

### 5a. Skybox feature 侧

`Skybox/Components.h` 的 `SkyboxGPUComponent` 从一个 handle 扩到三个：

```cpp
RHI::RHIHandle m_cubemap    = RHI::NullHandle;   // 已有
RHI::RHIHandle m_irradiance = RHI::NullHandle;
RHI::RHIHandle m_prefiltered = RHI::NullHandle;
```

`SkyboxSystem::BuildGPUResources` 对两张新图用 **`CreateStaticImage`**（决策 2），
并各打一个 active tag（沿用 `ActiveSkyCubeTag` 的单例模式：新建前先
`rhiCtx->Clear<T>()`）。

### 5b. SceneBindings

`Engine/Asset/Shaders/SceneBindings.hlsl` 新增：

```hlsl
TextureCube  g_IrradianceCube  : register(t1, space0);
TextureCube  g_PrefilteredCube : register(t2, space0);
SamplerState g_IBLSampler      : register(s0, space0);
```

`SceneConstants` cbuffer 追加 `uint g_IBLPrefilteredMipCount;`（复用现有的
`g_ScenePad0` 槽位）。

**`0` 同时是「IBL 未就绪」的门控信号**——这是必须的，不是可选优化：
`SceneBindingSystem.cpp:154-158` 的注释说明了 CBV 未绑定会硬断言、SRV table 未绑定会被
跳过。没有 skybox / 烘焙未完成时，shader 若无条件采样会读到脏描述符。门控让 shader
回退到现在的常量 ambient，与 `g_LightCount` 恒写的处理是同一套路。

`SceneBindingSystem` 加一个 IBL 更新步骤，形状照抄 `BindFrameLights`：
从 RHIContext 找 active tag 的资源实体 → `GetOrCreateImageView` → `SetShaderImage`。
mip count 恒写（未就绪时写 0）。

### 5c. Lighting.hlsl

替换 `Lighting.hlsl:31` 的常量 ambient：

- diffuse：`g_IrradianceCube.SampleLevel(g_IBLSampler, N, 0) * diffuseColor * ao`
- specular：`g_PrefilteredCube.SampleLevel(g_IBLSampler, reflect(-V, N),
  perceptualRoughness * (g_IBLPrefilteredMipCount - 1))`，乘上
  **`EnvBRDFApprox(NoV, perceptualRoughness, F0)`**（Lazarov 解析近似，见决策 4），
  放进新文件 `Lib/BRDF/EnvBRDF.hlsli`
- `g_IBLPrefilteredMipCount == 0` 时走原来的常量 ambient 分支

sampler 用 `FilterMode::Linear` mip 过滤 + `AddressMode::Clamp`，且
`m_mipLodMax` 要覆盖完整链（`SamplerState` 默认是 `MipCountMax`，OK）。

**验收**：金属球在 HDRI 环境下出现随粗糙度变化的环境反射；粗糙面呈现方向性环境色而非
均匀灰；无 skybox 的场景画面与改动前一致（门控回退生效）。

---

## 阶段 6（可选，后置）：换真 BRDF LUT

有了阶段 5 的正确画面做基线之后再做，此时可以对比验证约定是否搞错。

- 新增 `ImageUsage::BRDFLut`（追加在枚举末尾），或直接作为普通 2D 资产加载
- 绑到 `space0` 的 `t3`
- `EnvBRDF.hlsli` 里把解析近似替换为 LUT 采样（调用点不变）
- 校验：与解析近似的差值应在几个百分点内；若差异明显，先怀疑 uv 约定和 v 轴方向

---

## 风险与未决项

| 项 | 说明 |
|---|---|
| **字节顺序契约** | 阶段 3c。face-major vs mip-major 写反不会崩、不会报错，只会画面错位——本计划最难查的坑。阶段 2 取消后这是唯一的布局真相 |
| SRG 多次 Compile 语义 | 阶段 3a 的唯一未知项。不成立则退回「每 mip 一个 ShaderBindings」 |
| 子资产递归 | 阶段 4，两个新 usage 必须走直通分支 |
| ImageUsage 枚举顺序 | 数值进 `Hash()` → AssetId 身份，只能追加 |
| 启动耗时 | prefilter 是阻塞式 CPU-wait（`EnvironmentBaker` 的设计前提），多 mip 多 dispatch 后启动会明显变慢。**本计划明确接受**——缓存归后续统一的资产缓存工作 |
| cube 多 mip 上传未实测 | 阶段 1 的修复对 6 面 × 多 mip 在逻辑上正确，但该场景要到阶段 3 才首次出现，届时是第一个真实验证点 |

## 不在本计划范围

- **资产落盘缓存**（原阶段 2）：`ImageAssetData::m_mips` 的 layer 维度、
  `SerializeToKtx2` 的多层写入、KTX2 的 `numFaces` vs `numLayers` 语义、cube 缓存命中。
  归入后续对**所有**资产统一的缓存机制。
- 运行时动态 IBL（天空变化时重烘）。`ComputePassBuilder` / `ComputePassTag` 已经存在，
  将来要做有路，但现在烘焙走的是 off-frame 阻塞作业，不碰帧循环。
- sky cube 从 `ImportedTag` 迁到 `StaticImportTag`（见决策 2 的旁注）。
- 多探针 / 局部反射探针。
