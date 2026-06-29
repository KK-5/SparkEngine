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
   │  ① 资产加载/上传 (并入 M3 第一步)
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

- ✓ `MapToRHIFormat`:`None + RGBAF32` 正确产出 `R32G32B32A32_FLOAT`([ImageAssetCompiler.cpp:447-448]);BC6H/BC7 在 `ResolveCompression` 直接 fallback None,不会误走 BC。(M3)
- ⚠ **真实缺口**:`AsyncUploadSystem` 默认 staging 仅 **16MB**([AsyncUploadSystem.h:30]),而 128MB 单 subresource **不会按行拆分**——分块逻辑只在 subresource 之间切包([AsyncUploadSystem.cpp:628]),单 subresource > staging 会 memcpy 溢出。临时加大 staging 即可;正路加 subresource 内按行段分块(推到 M4)。(M3)
- ✓ DX12 **cube SRV** 已实现:`m_isCubemap` 走 `TEXTURECUBE`/`TEXTURECUBEARRAY`([Conversions.cpp:734-751])。(M2)
- ✓ DX12 `ImagePool` 建 `arraySize=6` image 透明支持(cubemap-ness 全在 descriptor,后端不特判,[ImagePool.cpp:60-112])。(M2)
- ✓ **DrawItem 编译 + 命令提交两层已确认支持全屏 draw**:`CompileDrawRequests` 对空 `m_streams` 不进流循环、`NullHandle` index 被 guard 跳过、`DrawLinear` 直接透传([RenderGraphCompiler.cpp:1354/1405/1414]);DX12 `Submit` 的 `DrawType::Linear` → `DrawInstanced`,不碰 VB/IB,仅 `Indexed` 分支断言 index view([CommandList.cpp:497-500]);`SetVertexBuffers` 对空 vertex-input 集合是 no-op([CommandList.cpp:856-887])。(M2/M3)
- ❓ **唯一残留未验证项(已收窄)**:PSO 用**空 `InputStreamLayout`** 建管线是否通过(D3D12 原生允许空 input layout,大概率 OK,但未实测)。M2/M3 全屏 draw 依赖它,开工第一步先做最小验证。(M2/M3)
- ✓ DX12 **单 face RTV 已现成**(此前判断错误,**非"最可能要补"**):RTV 对 `bIsArray` 走 `Texture2DArray`,`CreateCubemapFace(f)` 设 `arraySliceMin=max=f` → `FirstArraySlice=f, ArraySize=1`,正是单 face RTV([Conversions.cpp:880-895] + [ImageViewDescriptor.cpp:88-90])。(M3)
- ✓ 离屏 render-to-texture 在帧管线之外的承载有现成先例:`AsyncUploadSystem` 即同形态 job(独立 recorder + 队列 + fence,release/acquire barrier 契约,[AsyncUploadSystem.cpp:663-668])。(M3)

---

## 2.5 前置条件 P0 — 编辑器:拖拽资产到组件字段(异步)

> M3a 的数据驱动天空盒**必须靠这个能力来赋值资产**:把 HDRI 从资产浏览器拖到实体 `SkyboxComponent` 的 "Image Asset" 字段。这是个**通用编辑器能力**(任意资产类型 → 任意组件的 AssetElement 字段),不是天空盒专属;但它当前不存在,所以列为 skybox 方案的前置条件。**先于 M3a 验证完成。**

**现状缺口**:
- `SceneView` 只支持"拖资产到场景**建新实体**"(`OnModelAssetDragToScene` → `ExtractMeshToWorld`);
- `ComponentView` 的 `AssetElement` 字段只是只读显示路径,**不接受拖放**;
- 没有"把已加载资产赋到**现有组件字段**"的通路。

**分层铁律**(本方案据此设计):
- **组件 = 纯数据**,只持有 `AssetId`,且任何时刻持有的 id 都指向**已加载完成**的资产——未加载的 id 不写进组件(否则系统忽略/报错)。
- **加载是上层(编辑器)的职责**;管理系统(如 `SkyboxSystem`)只 `FindAsset(id)` 拿已加载资产、整理成 GPU 资源,**绝不 LoadAsset**。
- **UI 不碰 ECS、不调下层非 const 操作**(`LoadAsset`/`Replace` 等):UI 只做 const 读 + 发事件。
- **异步加载**,不在 UI 线程同步阻塞。

**流程(四步,身份贯穿、实例不保存)**:
1. **UI(ComponentView)** 落下 → 按 `AssetElement.expectType` 类型校验(const)→ 广播 `AssetEditBus::OnAssetDragToComponent(entity, 组件TypeId, 字段Id, assetId, assetType)`。**不设字段、不加载、不触发组件事件。**
2. **`AssetHandler`(编辑器侧,主线程)** 收事件 → `RequestAsset` 异步加载 + 记 pending `{assetId → (entity, 组件TypeId, 字段Id, assetType)}`;若该资产已 ready,直接走第 4 步(仍走 QueueBroadcast,主线程执行)。
3. **`AssetHandler::OnAssetReady`(资产管理器 worker 线程,禁碰 ECS)** → 按 assetId 匹配 pending → `AssetResolveBus::QueueBroadcast(ResolveAssetToComponent, entity, 组件TypeId, 字段Id, assetId, assetType)`(主线程队列)。
4. **`ComponentAssetResolver::ResolveAssetToComponent`(主线程)** → 反射:`ReflectContext::Resolve(组件TypeId)` → `GetComponent(entity)` 现取活组件 → `MetaType::data(字段Id).set(instance, assetId)` **此刻才写字段** → `ReplaceComponent(entity, instance)` 触发 `OnComponentUpdated` → 管理系统 `FindAsset`(此时 ready)→ 建 GPU。

**关键结论**:
- **resolve 阶段才写 `assetId` 进组件**(加载完成后),不是拖放那一刻 —— 保证组件只持有有效已加载资产。
- **只保存身份 `{entity, 组件TypeId, 字段Id, assetId, assetType}`,不保存组件实例**(实例跨异步间隙会悬空/失效);resolve 时用反射现取活组件。`TypeId`/`字段Id` 均为 `entt::id_type`,`组件TypeId == GetTypeId<T>() == MetaType::id()`,可用 `ReflectContext::Resolve(id)` 反查。

**总线改动**:
- `AssetEditBus`(Editor 层)+= `OnAssetDragToComponent(Entity, TypeId 组件, TypeId 字段, AssetId, AssetType)`。
- `AssetResolveBus`(Resource 层)+= `ResolveAssetToComponent(Entity, TypeId 组件, TypeId 字段, AssetId, AssetType)` —— 载荷全是 Core/Resource 类型(`Entity`/`TypeId`/`AssetId`/`AssetType`),层级干净;反射更新在编辑器侧的 `ComponentAssetResolver` 里做。

**多地址接入(已解)**:`AssetBus` 是 `ById(AssetType)`,单地址 `IdHandler` 只能监听一种类型。改用 `AssetBus::MultiHandler`(`Handlers.h` 内部以 map 记录多地址)即可监听多种资产类型的 `OnAssetReady/Error`。`AssetHandler` 现按需连 `Model` + `Image`,后续加类型在构造函数补一行 `BusConnect`。

**已就位 / 待办**:
- ✓ `AssetElement` 已加 `uint32_t expectType`(裸 uint 避免 Core→Resource 反向依赖);Skybox 反射已传 `AssetType::Image`。
- ✓ `SkyboxComponent`/`SkyboxSystem` 已回退为"纯数据 id + `FindAsset`"(不加载)。
- ✓ `ComponentView` 拖放已替换为**纯广播** `OnAssetDragToComponent`(const 类型校验 + 携 `fieldId`),不再同步 `LoadAsset`、不碰 ECS。
- ✓ `AssetEditBus` / `AssetResolveBus` 事件已加;`AssetHandler` 改 `MultiHandler` + pending 绑定 + 就绪/失败分发;新 `ComponentAssetResolver` 类(反射回写)+ Editor 注册。

状态:✅ 已落地(异步正式版,`SparkEditor` 编译通过)。落地决定:
- 多类型监听落在 `AssetHandler`(`MultiHandler`,按需连 `Model`+`Image`),而非新开监听类。
- `ComponentAssetResolver` 与 `Mesh::MeshResolver` 同层、同总线(`AssetResolveBus`,`Multiple` handler 各管各的事件)。
- resolve 时再校验 `FindAsset` 已 ready + 类型匹配,守住"组件只持有已加载资产"的铁律;实体在异步间隙若丢组件(`HasComponent` 假)则跳过。

---

## 3. 里程碑

**执行顺序为 M3 → M2 → M4 → M5**(编号保留历史,不重排):M3 先把资产处理好并烘成真实 cubemap,M2 紧接着采样它。线性流,不再走"占位 cube 隔离风险"那套——资产处理直接接入采样。

> ~~M1(独立的"HDR 加载并 blit 成 2D 纹理"验收)已删除~~:SRV 上传链路 DrawCube 早已验证,单拎出来 blit 看一眼无价值。equirect 的加载+上传是 bake 的**输入**,已折进 M3 第一步。

### M3 — 数据驱动的天空盒特性:component + 管理系统 + bake(工作起点)
**目标**:建立**数据驱动**的天空盒特性(对标 `Feature/Mesh/` 四层架构):`SkyboxComponent` 挂 HDRI 资产 → `SkyboxSystem` 监听并驱动解析(加载上传 equirect → 触发 bake → 产出 cubemap)。**加载/上传/bake 都是"系统响应组件"的一环,不属于渲染 Processor。**

参照范式(Mesh 四层):Resolver(`AssetResolveBus`)→ 数据组件(world, editable, `ComponentEventBus`)→ 管理系统(`ComponentEventBus::Handler`)→ GPU 组件(下游消费)。

#### M3a — 组件 + 管理系统骨架(含 equirect 加载+上传)← **第一步**

- 新模块 `Feature/Skybox/`(`SparkSkybox`,链 SparkCore/RHI/Render/AssetManager),对标 `Feature/Mesh/`:`Components.h` / `SkyboxSystem.{h,cpp}` / `Reflect.h` / `CMakeLists.txt`;在 [Engine.cpp:85] 旁 `CreateSystem<Skybox::SkyboxSystem>()`。
- `SkyboxComponent`(world, editable/reflected,对标 `MeshComponent`):`m_imageAssetId` + `Ptr<ImageAsset> m_imageAsset`(后续加 intensity / 旋转)。
- `SkyboxGPUComponent`(系统解析后写入,对标 `MeshGPUComponent`):`m_equirectImage`(上传好的 2D 源)+ `m_cubemap` + bake 就绪状态机。
- `SkyboxSystem`(`ComponentEventBus::Handler`,`SPARK_COMPONENT_ACCESS(ReadWrite<SkyboxComponent>, Write<SkyboxGPUComponent>)`):`OnComponentConstruct` → 资产就绪则解析——用显式 `ImageAssetDescriptor`(`colorSpace = Linear`、`compression = None`,保留 RGBAF32)、`CreateStaticImage`(equirect)+ `RequestImageUpload`,写 `SkyboxGPUComponent::m_equirectImage`。`OnComponentDestory/Updated` → 清/重建。**不做** `CreateStaticImageAttachment`(消费者是游离于渲染图外的 baker,获取屏障是 baker 的活)。
- `MapToRHIFormat` 的 HDR 路径已确认 ✓(见 §2);staging 不够就**临时**加大(脚手架,正路留 M4)。
- **验收**:编辑器/上层给一个 world 实体挂 `SkyboxComponent` 并设 HDRI 资产 → `SkyboxSystem` 触发加载+上传,`SkyboxGPUComponent::m_cubemap` 被建出(RenderDoc 见 GPU 纹理)。

状态:✅ 已落地(SparkSkybox / SparkRuntime 编译通过)。落地决定:
- `SkyboxSystem` **自己从 `m_imageAssetId` 加载资产**(若 Ptr 未设),实现"设资产即触发加载流程";指针直改不回触发组件事件,无递归。
- GPU 描述符**从资产推导**(`GetArrayLayers()==6` 走 `CreateCubemap`,否则 `Create2D`)——当前 equirect 上传成 2D,资产层 bake 产出真 cube 后同代码自动建 cube,下游零改。
- 走 `CreateStaticImageAttachment`(Read/Shader/FragmentShader):消费者是渲染图里的 SkyboxPass(M2),需静态屏障编译发 upload→shader-read,否则 graphics 队列 race 异步上传(同 DrawCube/MeshSystem)。
- 字段定为 `m_cubemap` + 预留 `m_cubemapAsset` 占位;无 equirect、无 bake 状态——特性对 bake 无感知。

#### M3b — EnvironmentBaker:equirect → cubemap(资产层,后续)

> 注:按 §0 修正,bake 归**资产 / Image-Compile 层**,不在 Skybox 特性里。本节移到资产层工程,Skybox 这边零改动(占位已留)。

- 新建 `EnvironmentBaker`:graphics 队列上的**独立 GPU job**(自带 command recorder + fence,**不进 per-frame RenderGraph**,形态参照 `AsyncUploadSystem`),由 `SkyboxSystem` 持有/驱动。
- **触发按需,不是启动期**(详见 §0.3):`SkyboxSystem` 观察到"equirect 上传完成 + cube 尚未烘焙" → 发一次 bake;一个资产烘一次,组件 Updated(换图)让 cube 退回"未请求"重走。
- **两级 GPU 生产者依赖链**:资产加载(CPU) → equirect 上传(AsyncUpload, fenceA) → bake(等 fenceA,产出 fenceB) → SkyboxPass 首次采样(等 fenceB)。
- **cube 就绪状态机 + 回退是基线,非可选**:`未请求 → 烘焙中(fenceB 未 signal) → 就绪`(存于 `SkyboxGPUComponent`)。消费侧(M2 SkyboxPass)每帧查状态——就绪才绑真 cube,否则跳过绘制(露 clear 色)。
- 6 次 fullscreen draw,每次把一个 face 当 RTV(`CreateCubemapFace`),PS 按该 face 的方向基重建世界方向 → equirect uv(`atan2/asin`)→ 采样 equirect → 写入对应 face。**方向约定与 M2 SkyboxPass 的 `Sample(dir)` 同一套,成对锁定。**
- 产出 **imported 持久** cubemap 资源(不进 transient 分配器),填回 `SkyboxGPUComponent::m_cubemap`。
- 压缺口:~~单 face RTV~~(已确认现成,见 §2);离屏 render-to-texture 复用 `AsyncUploadSystem` 的 release/acquire + fence 契约。
- **验收**:RenderDoc 抓帧确认 6 个 face 内容正确(方向对、接缝连续);完整上屏验证随 M2 落地。

状态:⬜ 未开始

---

### M2 — RHI cubemap 采样路径:SkyboxPass 采样真实烘焙 cube
**目标**:全屏天空盒 Pass 采样 M3 烘出的真实 cubemap,深度交互 + 合成进 SceneColor,把天空盒真正画上屏。直接采真实 cube,不用占位。

- 新建 `Feature/Skybox/SkyboxPass.{h,cpp}`,采样 M3 产出的 imported 持久 cube。
- Shader `Engine/Asset/Shaders/Skybox/Skybox.hlsl`:空 input layout、全屏三角形 VS(`SV_VertexID`,深度=远平面)、PS 用 `inverse(viewProj)` 重建视线方向 → `TextureCube.Sample(dir)` → tonemap 写 SceneColor。space0 放 `cbuffer{ float4x4 g_InvViewProj; }` + `TextureCube g_SkyCube` + `SamplerState g_SkySampler`。
- 新建 `Feature/Skybox/SkyboxProcessor.{h,cpp}`(plain helper,仿 DepthPreProcessor):持 M3 cube 句柄 + pass ShaderBindings + 全屏 Drawable/DrawRequest;每帧从 `Camera::CameraViewMatrix` 取矩阵算逆 VP、查 cube 就绪状态(未就绪跳过绘制)、绑 cube view + sampler、刷 viewport/scissor。
- 插进 `RenderSystem`:`SetUpDefaultPipeline` 在 DepthPre 之后调 `SkyboxPass::SetUp` + `m_skyboxProcessor.Init()`;`OnTick` depthPre 之后 `Process(renderSize)`;`ShutdownInternal` `Shutdown()`。
- 压缺口:空 input layout + `DrawLinear(3)`(**唯一未验证项**,见 §2);cube SRV 与 cubemap image 创建已确认现成。
- **验收**:相机转动,屏幕背景显示真实 HDR 天空,方向正确(左右上下对得上);cube 未就绪的帧稳定回退(露 clear 色)不崩不闪。

状态:⬜ 未开始

---

### M4 — 正确性与收尾
- Cubemap mip 链(为 IBL 预过滤铺路,至少 trilinear 避免远处闪烁)。
- Tonemap/曝光参数化、采样 sampler 状态(线性、clamp-to-edge)。
- staging 按行分块的正路(替换 M3 临时加大的 staging);确认资源生命周期(reap / shutdown 顺序)。

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

- ❓ EnvironmentBaker 6 face 渲染:6 次独立 draw(最稳,无 GS / `SV_RenderTargetArrayIndex` 依赖) vs 单 pass + array RTV + render-target-array-index(更快但依赖后端能力)。默认先 6 次独立 draw。
- ❓ cubemap 分辨率(per face):先定 1024² 起步。
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
