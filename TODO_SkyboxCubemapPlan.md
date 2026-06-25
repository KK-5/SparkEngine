# HDR Cubemap 天空盒落地方案

> 目标:从资产处理一路打通到正确渲染出 HDR 天空盒,顺带验收图片资产链路与渲染系统 Pass 能力是否有缺失。
> 本文档**是工作草稿**,未拍板项标 ❓,缺口逐个核对 → 补齐后转"已定"。
> 资产:`Engine/Asset/Image/Table_Defringed_4k2k.hdr`(equirectangular,RGBAF32,4096×2048)。

---

## 0. 架构决策(已拍板)

落地前先固化几条已讨论清楚的方向,避免中途反复:

1. **运行时表示用 cubemap,不用 equirectangular 直采。**
   - cubemap 是 runtime 工业标准:硬件原生 `TextureCube.Sample(dir)`(无 per-pixel `atan2/asin`)、texel 密度均匀(equirect 两极过采样+畸变)、跨面过滤无接缝、且是日后 IBL 预过滤/irradiance 的必需形态。
   - equirect 只是**作者/交换格式**(HDRI 都以 equirect 发布),它只在 bake 那一刻作为输入出现,运行时不再采样。
   - cube 网格(geometry)肯定不用;天空盒几何统一用**全屏三角形**(`SV_VertexID` 生成,深度=远平面)。

2. **bake(equirect→cubemap)概念上属于资产处理(cook)阶段,不属于 per-frame 渲染管线。**
   - 标准出货形态:HDRI 在离线/导入时 bake 一次 → 序列化 cubemap → 运行时**只 load cubemap**,游戏循环里没有 equirect→cubemap 这一步。
   - 「需要 GPU」≠「是帧管线的一个 Pass」。bake 是 GPU job,参照 `AsyncUploadSystem` 的形态(自带 command recorder + 队列 + fence,游离于 per-frame RenderGraph 之外)。但**「不是 Pass」≠「不用协调」**——协调模型见 §0.4。
   - **不**实现成"run-once 渲染图 Pass + `m_baked` 标志"——那是把 cook job 塞进帧管线,概念污染,已否决。

3. **触发点分阶段推进,GPU bake 代码同一份。**
   三种触发点(离线 cooker / 编辑器导入时 / 运行时 load 时)只是"何时跑 + 结果是否落盘"的区别,共用同一份 GPU bake 代码。
   - **先做运行时按需 bake**(无磁盘缓存)。资产何时被引擎识别**不可控**——HDRI 可能运行到一半才加载,**没有"启动期"窗口可阻塞**。所以触发是"观察到 equirect 上传完成、对应 cube 尚未烘焙 → 发一次 bake",**不是"启动即 bake"**。这是同一份 bake 代码的最早触发点,不是图省事。
   - **再 promote 成 cook 阶段**(编辑器导入 bake → 落盘 → 运行时有缓存就直接 load cooked cubemap)。这是 M5,先不做。

4. **bake 用 GPU,但必须和渲染系统在 GPU 原语层面协调。**
   - bake 走 **graphics 队列**的独立提交(render-to-texture 6 个 face),不进 per-frame RenderGraph;但它和帧共享 device / 同一条队列时间线,产物要跨帧交给 SkyboxPass 采样,因此协调不可免。
   - 协调四要素:① 共享 device / graphics 队列;② 生产者→消费者 fence + 资源状态转换 barrier;③ cube 是 **imported 持久资源**(不进 transient 分配器,跨帧存活);④ 独立 command recorder。全部复用 `AsyncUploadSystem` 现成契约(release barrier 生产者侧发,acquire barrier + fence wait 由 `RenderGraphExecuter` 在首次使用时补)。
   - **CPU cook 路线已评估并否决**:CPU 只在离线 HDRI 工具(cmft / CubeMapGen / cmgen)里算标准;实时引擎(本引擎对标的 O3DE/Atom、UE、Unity 导入)走 GPU,且 GPU 设施可与未来 IBL 预过滤复用。CPU 的优势只是当前落地风险低、方向约定可单测,**不是正统**。

---

## 1. 整体数据流(终态)

```
Table_Defringed_4k2k.hdr  (equirect, RGBAF32)
   │  ① 资产加载/上传 (M1)
   ▼
2D 纹理 (equirect, GPU)
   │  ② EnvironmentBaker:按需 GPU bake job(equirect 上传完成后触发),渲染进 6 个 face (M3)
   ▼
Cubemap 纹理 (GPU, 持久, bake 一次)
   │  ③ SkyboxPass:全屏三角形,按视线方向采样 TextureCube (M2)
   ▼
SceneColor → CopyFrameBufferPass → swapchain
```

插入点:`SkyboxPass` 位于 `DepthPrePass` 之后、`CopyFrameBufferPass` 之前。读 `SceneDepth`(只读、`LessEqual`、不写深度),写 `SceneColor`(Load)。这样以后有不透明几何时天空只填背景像素;现在没几何就铺满全屏。

---

## 2. 现状勘察(已确认能力 / 缺口)

落地前核对环境。**已具备,不需重造**:

- ✓ 纹理端到端链路已验证(SandBox `DrawCube.cpp`):`LoadAsset<ImageAsset>` → `RHI::CreateStaticImage` → `RHI::RequestImageUpload` → `CreateStaticImageAttachment` → `SetShaderImage`/`SetShaderSampler` + `GetOrCreateImageView`。
- ✓ HDR 解码:`ImageAssetLoader` 对 `.hdr` 走 `stbi_loadf` → `ImageFormat::RGBAF32`(`ImageAssetLoader.cpp`)。
- ✓ 异步上传:`AsyncUploadSystem`(Copy 队列 staging→device,跨队列 barrier + fence 已处理)。一次性 GPU job 的范本。
- ✓ RHI **cubemap 抽象层已声明**:`ImageDescriptor::CreateCubemap` / `CreateCubemapArray`、`m_isCubemap`、`ImageViewDescriptor::CreateCubemap` / `CreateCubemapFace`(单 face)。
- ✓ 全屏 draw 所需:`DrawLinear(vertexCount, vertexOffset)`(非索引线性 draw)存在。
- ✓ Pass/Processor 范式:`DepthPrePass` + `DepthPreProcessor`(plain helper,RenderSystem 拥有 + OnTick 驱动)。
- ✓ Pass 级 ShaderBindings:`CreatePassShaderBindings<PassTag>(passCtx, rhiCtx, spaceId)`(从反射出的 PassPipelineLayout 建 binding)。
- ✓ 相机矩阵:世界里的 `Camera::CameraViewMatrix`(editor 飞行相机在喂),`ViewBindingSystem` 已在读。
- ✓ KTX2 序列化:`ImageAssetCompiler::SerializeToKtx2`(KTX2 原生支持 cubemap+mip+array,M5 落盘不用换格式)。

**缺口 / 待核对**(实现到对应 M 时逐个压):

已核对(2026-06,基于当前代码):

- ✓ `MapToRHIFormat`:`None + RGBAF32` 正确产出 `R32G32B32A32_FLOAT`([ImageAssetCompiler.cpp:447-448]);BC6H/BC7 在 `ResolveCompression` 直接 fallback None,不会误走 BC。(M1)
- ⚠ **真实缺口**:`AsyncUploadSystem` 默认 staging 仅 **16MB**([AsyncUploadSystem.h:30]),而 128MB 单 subresource **不会按行拆分**——分块逻辑只在 subresource 之间切包([AsyncUploadSystem.cpp:628]),单 subresource > staging 会 memcpy 溢出。修法:快路把 staging 调到 ≥144MB;正路加 subresource 内按行段分块(推到 M4)。(M1)
- ✓ DX12 **cube SRV** 已实现:`m_isCubemap` 走 `TEXTURECUBE`/`TEXTURECUBEARRAY`([Conversions.cpp:734-751])。(M2)
- ✓ DX12 `ImagePool` 建 `arraySize=6` image 透明支持(cubemap-ness 全在 descriptor,后端不特判,[ImagePool.cpp:60-112])。(M2)
- ❓ **唯一未验证项**:空 input layout + `DrawLinear(3)` 的 PSO/DrawItem 编译路径(现有只跑过有顶点流的 indexed draw;DrawItem 能否表达"非索引、3 顶点、无 VB"待验)。M2/M3 全屏 draw 都依赖它,开工第一步先做最小验证。(M2/M3)
- ✓ DX12 **单 face RTV 已现成**(此前判断错误,**非"最可能要补"**):RTV 对 `bIsArray` 走 `Texture2DArray`,`CreateCubemapFace(f)` 设 `arraySliceMin=max=f` → `FirstArraySlice=f, ArraySize=1`,正是单 face RTV([Conversions.cpp:880-895] + [ImageViewDescriptor.cpp:88-90])。(M3)
- ✓ 离屏 render-to-texture 在帧管线之外的承载有现成先例:`AsyncUploadSystem` 即同形态 job(独立 recorder + 队列 + fence,release/acquire barrier 契约,[AsyncUploadSystem.cpp:663-668])。(M3)

---

## 3. 里程碑

每个里程碑**独立可验收**,把"cubemap 采样能不能跑"和"equirect 转换能不能跑"两个风险解耦。

### M1 — 资产处理:HDR equirect 正确加载并上传成 2D 纹理
**目标**:把 `.hdr` 当普通 2D 纹理跑通端到端,证明资产/上传链路对 HDR 无缺口。

- 用显式 `ImageAssetDescriptor`(`colorSpace = Linear`、`compression = None`,保留 RGBAF32)加载 `Image/Table_Defringed_4k2k.hdr`。
- 压缺口:`MapToRHIFormat` 的 HDR 路径;staging 容量 vs 128MB。
- **验收**:把这张 2D 纹理临时 blit 到屏幕(或用 SkyboxPass 雏形直采一帧)能看到 HDR 图。M2 起此直采废弃。

状态:⬜ 未开始

---

### M2 — RHI cubemap 采样路径(不依赖转换)
**目标**:证明"创建 cubemap 资源 + cube SRV + 全屏天空盒 Pass + 深度交互 + 合成进 SceneColor"整条**渲染路径**通。cube 内容先用**程序化占位**(6 个面填不同颜色/梯度,CPU 上传或简单 shader 写),不碰 equirect——隔离风险。

- 新建 `Feature/Skybox/SkyboxPass.{h,cpp}` + 占位 cubemap 资源。
- Shader `Engine/Asset/Shaders/Skybox/Skybox.hlsl`:空 input layout、全屏三角形 VS(`SV_VertexID`,深度=远平面)、PS 用 `inverse(viewProj)` 重建视线方向 → `TextureCube.Sample(dir)` → tonemap 写 SceneColor。space0 放 `cbuffer{ float4x4 g_InvViewProj; }` + `TextureCube g_SkyCube` + `SamplerState g_SkySampler`。
- 新建 `Feature/Skybox/SkyboxProcessor.{h,cpp}`(plain helper,仿 DepthPreProcessor):持占位 cubemap entity + pass ShaderBindings + 全屏 Drawable/DrawRequest;每帧从 `Camera::CameraViewMatrix` 取矩阵算逆 VP、绑 cube view + sampler、刷 viewport/scissor。
- 插进 `RenderSystem`:`SetUpDefaultPipeline` 在 DepthPre 之后调 `SkyboxPass::SetUp` + `m_skyboxProcessor.Init()`;`OnTick` depthPre 之后 `Process(renderSize)`;`ShutdownInternal` `Shutdown()`。
- 压缺口:空 input layout + `DrawLinear(3)`(**唯一未验证项**,见 §2);cube SRV 与 cubemap image 创建已确认现成。
- **验收**:相机转动,屏幕背景显示 6 面占位色天空盒,方向正确(左右上下对得上)。

状态:⬜ 未开始

---

### M3 — EnvironmentBaker:equirect → cubemap(按需 GPU bake job)
**目标**:用 M1 的 equirect 2D 纹理,GPU 渲染进 M2 的 cubemap 6 个面,烘一次,接上真实资产。

- 新建 `EnvironmentBaker`:graphics 队列上的**独立 GPU job**(自带 command recorder + fence,**不进 per-frame RenderGraph**,形态参照 `AsyncUploadSystem`)。
- **触发按需,不是启动期**(详见 §0.3):资产识别时机不可控,HDRI 可能运行到一半才加载,没有可阻塞的启动窗口。模型——观察到"equirect 上传完成 + 对应 cube 尚未烘焙" → 发一次 bake;一个资产烘一次,热重载让 cube 退回"未请求"重走。
- **两级 GPU 生产者依赖链**:资产加载(CPU) → equirect 上传(AsyncUpload, fenceA) → bake(等 fenceA,产出 fenceB) → SkyboxPass 首次采样(等 fenceB)。
- **cube 就绪状态机 + 回退是基线,非可选**:`未请求 → 烘焙中(fenceB 未 signal) → 就绪`。SkyboxPass 每帧查状态——就绪才绑真 cube,否则跳过天空盒绘制(露 clear 色)。"cube 这帧可能还没好"必须被设计成正确,不能假设它一定在。
- 6 次 fullscreen draw,每次把一个 face 当 RTV(`CreateCubemapFace`),PS 按该 face 的方向基重建世界方向 → equirect uv(`atan2/asin`)→ 采样 equirect → 写入对应 face。**方向约定与 M2 SkyboxPass 的 `Sample(dir)` 是同一套,成对锁定。**
- 产出 **imported 持久** cubemap 资源(不进 transient 分配器),交给 SkyboxPass 采样;M2 的占位 cube 退役。
- 压缺口:~~单 face RTV~~(已确认现成,见 §2);离屏 render-to-texture 复用 `AsyncUploadSystem` 的 release/acquire + fence 契约。
- **验收**:天空盒从占位色变成真实 HDR 天空,接缝/方向正确;且 cube 烘好之前的若干帧能稳定回退、不崩不闪。

状态:⬜ 未开始

---

### M4 — 正确性与收尾
- Cubemap mip 链(为 IBL 预过滤铺路,至少 trilinear 避免远处闪烁)。
- Tonemap/曝光参数化、采样 sampler 状态(线性、clamp-to-edge)。
- 清理 M1 临时直采;确认资源生命周期(reap / shutdown 顺序)。

状态:⬜ 未开始

---

### M5 — promote 成 cook 阶段(后续,可暂缓)
**目标**:落地"标准出货形态"——bake 产物落盘,运行时只 load cooked cubemap。

- `ImageAssetData` 表达 cube/array(`m_arrayLayers` 已有,需暴露+读写路径)。
- cube KTX2 读写(容器已支持)。
- 编辑器导入 HDRI 时触发 `EnvironmentBaker` bake → 缓存到磁盘 cook 产物。
- 运行时:有缓存直接 load cubemap,无则 fallback 到 load-time bake。
- 与未来 IBL(prefiltered specular + irradiance)共用同一 bake 设施。

状态:⬜ 未开始(等天空盒正确渲出来再上)

---

## 4. 待拍板项(❓)

- ❓ 占位 cubemap 的 6 面内容方式:CPU 上传 6 张纯色 vs 简单 shader 程序化写(M2 选其一,CPU 上传更省事)。
- ❓ EnvironmentBaker 6 face 渲染:6 次独立 draw(最稳,无 GS / `SV_RenderTargetArrayIndex` 依赖) vs 单 pass + array RTV + render-target-array-index(更快但依赖后端能力)。默认先 6 次独立 draw。
- ❓ 占位/最终 cubemap 分辨率(per face):先定 1024² 起步。
- ❓ SkyboxPass 与深度:确认 `SceneDepth` 以只读 DepthStencil attachment 绑定 + `LessEqual` + 关深度写 的组合在现有 attachment 系统里能正确声明。

---

## 5. 关键文件索引

- 渲染系统装配:`Engine/Code/RunTime/Feature/Render/RenderSystem.cpp`(`SetUpDefaultPipeline` / `OnTick`)
- Pass/Processor 范本:`Engine/Code/RunTime/Feature/Render/Feature/DepthPre/`
- 纹理端到端范本:`SandBox/Program/RenderGraph/DrawCube.cpp`
- 一次性 GPU job 范本:`Engine/Code/RunTime/Feature/RHI/System/AsyncUploadSystem.cpp`
- 资产加载/编译:`Engine/Code/RunTime/Resource/Image/{ImageAssetLoader,ImageAssetCompiler}.cpp`
- RHI cubemap 抽象:`Engine/Code/RunTime/Feature/RHI/Resource/Image/{ImageDescriptor,ImageViewDescriptor}.h`
- DX12 image 后端:`Engine/Code/RunTime/Feature/RHI/Backend/DX12/Resource/Image/`
- Pass binding 工具:`Engine/Code/RunTime/Feature/Render/Pass/PassAccess.h`、`Shader/ShaderBindingsUtils.h`
- 着色器目录:`Engine/Asset/Shaders/`(`ViewBindings.hlsl` 等)
