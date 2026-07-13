# 资源状态模型:从 (usage, access) 到 AccessFlags 集合

> **状态:设计已定，待实现。** 这份取代了早先基于 usage-mask / 具名组合枚举 / FlushBarriers 的几版草案——那几版连同否决理由归档在 §12。

## 0. 起因

延迟渲染 Lighting pass 需要 SceneDepth **在同一个 pass 内**既做只读深度/模板测试(剔除无几何像素),又做 SRV 采样(重建世界坐标)。当前状态模型表达不了这个并发,只能靠 `discard`([Lighting.hlsl](Engine/Asset/Shaders/Lighting/Lighting.hlsl))绕开 DSV——代价是禁掉 early-Z、每像素先采样再判。

顺着这个需求深挖,发现根子不在 render graph,而在 `RHI::ResourceState`([ResourceState.h](Engine/Code/RunTime/Feature/RHI/Resource/ResourceState.h))这个类型本身**建模不完整**。本文重构它。

相关:两层编译模型见 [TODO_SlotBindingCompile.md](TODO_SlotBindingCompile.md)。

## 1. 根因:`(usage, access)` 是一张二维表的坐标,只能点一格

现在 `ResourceState` 用 `(AttachmentUsage m_usage, AttachmentAccess m_access)` 描述状态。看 DX12 的转换([Conversions.cpp:530-587](Engine/Code/RunTime/Feature/RHI/Backend/DX12/Conversions.cpp#L530)):

| usage \ access | Read | Write |
|---|---|---|
| Shader | 采样读(PIXEL_SR\|NON_PIXEL_SR) | UAV(UNORDERED_ACCESS) |
| DepthStencil | 深度只读(DEPTH_READ) | 深度写(DEPTH_WRITE) |
| RenderTarget | —(总是写 RENDER_TARGET) | |
| Copy | 拷贝源(COPY_SOURCE) | 拷贝目标(COPY_DEST) |

`(usage, access)` 这**一对坐标只能点中一个格子**。`m_usage` 标量 → 装不下并发的多个角色;`m_access` 只有 Read/Write → 太粗,推不出 `VkAccessFlags`,必须靠 usage 消歧(所以表才是二维的)。

判断"完不完整"的标尺是最严格后端的 barrier 端点——`VkImageMemoryBarrier2` 的一端 = `(stageMask, accessMask, layout)`,其中 `accessMask`、`stageMask` 都是**集合**,`layout` 可由 accessMask 推。对照下来,`ResourceState` 缺的就是"fine-grained 的 access 集合"这一维,`m_usage` 标量和 `m_access` 粗粒度是同一个缺陷的两面。

## 2. 解法:把二维表拍平成可 OR 的一维 bit 集(`AccessFlags`)

**每个有意义的格子 = 一个 access bit。** 单用途 = 选一个 bit(与 `(usage,access)` 信息等价);复合 = 选一个子集(`(usage,access)` 坐标写不出来的东西)。

切分判据:**两种用法只要映到不同 D3D12 state 或不同 Vulkan layout,就分不同 bit。**

命名与粒度对齐 **`VkAccessFlags2`**(synchronization2)——它专门补了 `SHADER_SAMPLED_READ` / `SHADER_STORAGE_READ` / `SHADER_STORAGE_WRITE` 的区分(旧 `VkAccessFlags` 只有笼统的 `SHADER_READ`,推不出 layout)。

```cpp
enum class AccessFlags : uint32_t
{
    None                = 0,          // = Uninitialized → COMMON / UNDEFINED

    // ---- 读 ----
    IndirectRead        = BIT(0),     // Indirect
    VertexIndexInput    = BIT(1),     // InputAssembly（VCB|INDEX，buffer only）
    ConstantBufferRead  = BIT(2),     // Shader-read 里的 CBV 部分（buffer）
    ShaderSampledRead   = BIT(3),     // Shader-read（SRV / 采样）
    ShaderStorageRead   = BIT(4),     // UAV 读（DX12 无独立读，Vulkan 需要）
    DepthStencilRead    = BIT(5),     // DepthStencil, Read
    ColorAttachmentRead = BIT(6),     // 预留：可编程混合读
    TransferRead        = BIT(7),     // Copy, Read
    ResolveRead         = BIT(8),     // Resolve, Read
    PredicationRead     = BIT(9),     // Predication
    ShadingRateRead     = BIT(10),    // ShadingRate
    InputAttachmentRead = BIT(11),    // SubpassInput
    AccelStructRead     = BIT(12),    // RayTracingAccelerationStructure

    // ---- 写（互斥）----
    ShaderStorageWrite  = BIT(16),    // Shader, Write（UAV）
    ColorAttachmentWrite= BIT(17),    // RenderTarget
    DepthStencilWrite   = BIT(18),    // DepthStencil, Write
    TransferWrite       = BIT(19),    // Copy, Write
    ResolveWrite        = BIT(20),    // Resolve, Write

    Present             = BIT(24),    // Present（既非读也非写，单独一档）

    WriteMask = ShaderStorageWrite | ColorAttachmentWrite | DepthStencilWrite
              | TransferWrite | ResolveWrite,
};
```

> 读写留 gap(16 起写),`& WriteMask` 一步判"含写"。`ShaderStorageRead` DX12 用不上(UAV 不分读写,都塌 `UNORDERED_ACCESS`),纯为 Vulkan 的 access 精度预留——按"现在就设计好、以后不改"原则一次到位。

**`AccessFlags` 不是 `VkAccessFlags2` 的纯镜像**:它多担一个职责——**推导 layout**(Vulkan barrier 里 layout 是另外单独给的)。所以它的 bit 必须"细到能区分 layout",这正是 sampled/storage 必须分开的根本原因。文件头注释须点明这一点。

## 3. 边界翻译:`AttachmentUsage(+读写) → AccessFlags`

**`AttachmentUsage` 保留作上层声明 attachment 时的角色词汇**(每个 binding 一个,标量,好用),只在这张表翻一次(替掉 `CompileResourceState` + DX12 那个 switch):

| AttachmentUsage | access | → AccessFlags |
|---|---|---|
| RenderTarget | * | `ColorAttachmentWrite` |
| DepthStencil | Read / Write | `DepthStencilRead` / `DepthStencilWrite` |
| Shader(image) | Read | `ShaderSampledRead` |
| Shader(buffer) | Read | `ConstantBufferRead \| ShaderSampledRead` |
| Shader | Write | `ShaderStorageWrite` |
| Copy | Read / Write | `TransferRead` / `TransferWrite` |
| InputAssembly | Read | `VertexIndexInput` |
| Indirect | * | `IndirectRead` |
| Predication | * | `PredicationRead` |
| Resolve | Read / Write | `ResolveRead` / `ResolveWrite` |
| ShadingRate | * | `ShadingRateRead` |
| Present | * | `Present` |
| RayTracingAS | * | `AccelStructRead` |
| SubpassInput | * | `InputAttachmentRead` |

## 4. `AccessFlags` 必须脱离 Attachment 也完备

翻译**不是**"从 Attachment 翻过来"——attachment 路径只是 `AccessFlags` 的**一个生产者**,直接构造是平级的另一个生产者。现在就有一堆调用方绕过 attachment 直接构造状态:`AsyncUploadSystem`、`EnvironmentBaker`、裸样例、`MakeImageBarrier` / `ConvertToImageShaderRead` 等 helper。

所以 `AccessFlags`(配上翻译点的上下文)必须自足,**尤其 PIXEL/NON_PIXEL 的区分不能挂在 AttachmentStage 上**(见 §5)。

## 5. 后端推导:队列驱动正确性 + stage 可选收窄

DX12 的 `PIXEL_SHADER_RESOURCE` **只在 Graphics 队列合法**;compute/copy 队列上出现它 = 非法(debug layer 报错)。`NON_PIXEL_SHADER_RESOURCE` 两者都合法。

关键:在 Graphics 队列上 `PIXEL_SR | NON_PIXEL_SR` 是**合法的**(只是保守),真正非法的只有"非 graphics 队列上的 `PIXEL_SR`"。所以**正确性只需一个队列门,和 stage、attachment 都无关**;而 `queue` 在翻译点永远可得(barrier 落在哪个 command list 上,`GetHardwareQueueClass()` 就是它)。

```cpp
D3D12_RESOURCE_STATES ToStates(AccessFlags a,
                               HardwareQueueClass queue,     // 必填，正确性轴
                               AttachmentStage    stage = Any) // 选填，收窄
{
    D3D12_RESOURCE_STATES s = D3D12_RESOURCE_STATE_COMMON;   // None
    if (a & ShaderSampledRead) {
        s |= NON_PIXEL_SR;                                   // 所有队列合法
        if (queue == Graphics) s |= PIXEL_SR;                // 只有 graphics 加 PIXEL
        // stage 精确时可进一步收窄：graphics 且仅 fragment 读 → 只 PIXEL_SR
    }
    if (a & DepthStencilRead)  s |= DEPTH_READ;
    if (a & DepthStencilWrite) s |= DEPTH_WRITE;
    if (a & (ShaderStorageRead|ShaderStorageWrite)) s |= UNORDERED_ACCESS;
    if (a & ColorAttachmentWrite) s |= RENDER_TARGET;
    if (a & TransferRead)  s |= COPY_SOURCE;
    if (a & TransferWrite) s |= COPY_DEST;
    // …每 bit 一行，OR 起来
    return s;
}
```

- **`queue`**:决定 `PIXEL_SR` 能否出现(正确性)。另做防御:graphics-only 的 access(`ColorAttachmentWrite`、`DepthStencil*`、`VertexIndexInput`、`ConstantBufferRead`、`ShadingRateRead`、`ResolveRead/Write`、`Present`)出现在 compute/copy 队列 = 程序错误,`if (Validation::isEnabled)` assert。Copy 队列几乎只认 `TransferRead/Write` + `COMMON`。
- **`stage`**:语义是同步作用域(Vulkan `srcStageMask`/`dstStageMask`),缺省(`Any`)时退回"该队列下保守但合法"的集合;精确时用于收窄,**不参与正确性**。

> `DEPTH_READ | SHADER_SAMPLED_READ` 自动 OR 成 `DEPTH_READ | PIXEL_SR | NON_PIXEL_SR`——正是 Lighting 要的复合态。
>
> `ResourceState.h` 里那句注释 "AttachmentStage is unused in DX12" 是这个 compute-队列 bug 的来源;新模型里 stage 参与(可选)收窄,queue 兜底正确性。

**Vulkan(未来,两支)**:`ToVkAccess(AccessFlags)`(OR 位)+ `ToLayout(AccessFlags)`(读集合 → 兼容 layout,如 `DepthStencilRead|ShaderSampledRead → DEPTH_STENCIL_READ_ONLY_OPTIMAL`,本身可采样)。

## 6. 校验规则(替掉早先的"白名单表")

翻译产出 `AccessFlags` 后,一处校验:

1. **写独占**:`popcount(a & WriteMask) ≤ 1`,且含写位时不得再有读位(depth 只读采样 + stencil 写那族属未来 aspect 扩展,见 §13)。
2. **读兼容(Vulkan 端)**:纯读集合必须能落到一个 layout;写+读组合会先被规则 1 拦掉。DX12 端读态永远能 OR,无需此检。

白名单不再是一张独立的表,而是"这个 `AccessFlags` 集合能不能落到合法态"的推导 + 校验。

## 7. Build 不动:图构建不受影响

`RenderGraphBuilder::BuildGraph` 已把同资源在同一 pass 的多条 use **按 access(Read/Write)OR 归并**([RenderGraphBuilder.cpp:77-95](Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraphBuilder.cpp#L77-L95)),且**连边只看读写、不看 usage**。所以 SceneDepth 声明两条(都 Read)→ 正常连"在 writer 之后"的边。**拓扑、topo sort、依赖边今天就对,build 一行不改。**

## 8. Compile 改:pass 感知的 tracker + 纯 OR 分组

根因(问题1/3)是"把并发当成了顺序":逐 attachment 推进单值 tracker,同一 pass 的多条 attachment 被当成不同时间点。修正——**tracker 记录"上一个 pass 把该资源用成的 `AccessFlags` 集合"**。`ResourceStateTracker` 已有 `m_lastPass`,正是它需要带 pass 信息的证据。

`CompileImageBarriers`([RenderGraphCompiler.cpp:511](Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraphCompiler.cpp#L511))从"逐 attachment 发 barrier"改成两趟:

```cpp
// 趟 1：按资源聚合本 pass 的目标态（pass 是函数参数=环境量，键只需资源）
eastl::hash_map<RHIHandle, RHI::AccessFlags> desired;   // + stage 的 OR
view.each([&](auto, const ImagePassAttachment& att) {
    desired[att.m_image] |= TranslateToAccess(att);      // 纯 OR，无查表、无具名组合
});

// 趟 2：每个资源发且仅发一条 barrier
for (auto& [resource, dstAccess] : desired) {
    // src = tracker.m_lastAccess（上一个 pass 的 AccessFlags 集合）
    // 校验 dstAccess（§6）→ 一条 prev→cur barrier → 更新 tracker
}
```

- 单用途资源:集合就 1 个 bit → 行为和今天一致,**向后兼容**。
- tracker 存 OR 后的 `AccessFlags`,下一个 pass 的 `StateBefore` 自动正确 → **问题1(两条 barrier 凑不出复合态)和问题3(tracker 记成单值)一并消失**。
- 白名单校验在此处,有 pass/资源上下文,报错带名字 → **问题2 解决**。

## 9. 结构改动

- `ResourceState`:`{usage, access}` → `{ AccessFlags m_access; AttachmentStage m_stage; HardwareQueueClass m_queue; }`。**去掉 `m_usage`,不存 layout**(各后端从 `m_access` 推)。
- `ImageBarrier` / `BufferBarrier`:`m_srcUsage + m_srcAccess` 两字段 → 一个 `AccessFlags m_srcAccess`(dst 同理),`m_*Usage` 删除。
- `ConvertImageAttachmentState(usage, access)` → `ToStates(AccessFlags, queue, stage)`。
- `MakeXxxBarrier` / `ConvertToXxx` helper、`AttachmentUsage`、声明层:**基本不动**,内部多一步 translate。
- `ResourceState` 的所有构造点:随字段变化清点(`CompileResourceState`、[RenderGraphCompiler.cpp:506/599](Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraphCompiler.cpp#L506)、[CommandList.cpp:592/614](Engine/Code/RunTime/Feature/RHI/Backend/DX12/Command/CommandList.cpp#L592) 等)。

## 10. Lighting pass 落地

- Build 里多声明一条 SceneDepth 的 `DepthStencil/Read` attachment(只读 DSV);原有 `Shader/Read` 保留。两条经 §8 分组 OR 成 `DepthStencilRead | ShaderSampledRead`。
- PSO:`depth.enable=1, writeMask=Zero, func=Greater`,配全屏三角形 z 从 0 改 1.0(正向 Z 下 `q>d` 只剔远平面=天空);或后续走 stencil。
- Shader 删掉 `discard`,剔除交给硬件 early-Z / early-stencil。

## 11. 落地顺序:先底层直接 barrier 验证,再接 render graph

关键前提——**模块边界支持隔离验证**:`HelloTriangle`/`DrawShape` 直接链 SparkRHI、**不链 SparkRender**;而 `ResourceState`/barrier/`ToStates`/`AsyncUploadSystem` 全在 SparkRHI 内。所以改完 SparkRHI 后,SparkRender 可以**暂时编不过、不构建**,只单独 build RHI 目标就能在"完全没有 render graph 编译期状态追踪"的环境里先把底层原语证清楚。

### Phase A — 只动 SparkRHI,用直接 barrier 验证

1. 加 `AccessFlags` + `ConvertBufferState`/`ConvertImageState`(access→D3D12 态)+ access-based `Validate*`(§2/§5/§6)。`TranslateToAccess`(usage→AccessFlags)**不在此**——它只在 render graph 的 `CompileResourceState` 出现,归 Phase B。
2. 翻转 `ResourceState` + `ImageBarrier`/`BufferBarrier` → `AccessFlags`;迁移 `ResourceState.cpp`(Make*/ConvertTo*/Validate*)、DX12 `CommandList`+`Conversions`、`AsyncUploadSystem`(§9)。`ConvertTo*` helper 直接写 `AccessFlags`,不经 usage。
3. **此刻 SparkRender 故意编不过,不 build 它。** 只 `--target` 单独构建 `HelloTriangle`、`DrawShape`、RHI 单测(不走 ALL_BUILD)。验证:
   - 样例渲染正常 → `QueueBarrier → ToStates` + `Get/SetResourceState` 往返正确。
   - **上传路径**(样例加载纹理/buffer 触发 `AsyncUpload` 的 `ConvertToCopyWrite/Read`)。
   - **跨队列**(AsyncUpload copy→graphics 的 COMMON 桥 release/acquire 两半)——底层最易错处,样例正好压到。

### Phase B — 接入 SparkRender / Attachment

1. 迁移 render graph 消费点:`CompileResourceState`(usage→`AccessFlags`,**先做 1:1 单-bit 翻译,不合并**)、barrier 组装的字段改名、`RenderGraph.cpp` 的 Present、tracker 构造(§3/§9)。
2. 构建全引擎 + RenderGraph 样例(DrawCube/MSAA/Triangle)+ 引擎 pass(DepthPre/GBuffer/Skybox)。验收标准:**渲染结果与改前完全一致**(仍是单-bit,零行为变更)。

### Phase C — 复合读(以后,连同 LightingPass,不在本次范围)

1. `CompileImageBarriers` 改两趟分组(§8),tracker 存"上一个 pass 的 `AccessFlags` 集合"。
2. Lighting pass 接 §10,删 `discard`,验证 early-Z 生效。

> 两个纪律点:① Phase A→B 之间 SparkRender 处于"故意不可编译"的中间态,须在连续工作段内推完,别把仓库长期搁在半迁移。② `CompileResourceState` 在 Phase B 只做 1:1 翻译,复合逻辑留到 Phase C——这样 Phase B 排错范围小,任何复合行为都还没上场。

### 风险速览(详见推导过程)

| 风险 | 级别 | 缓解 |
|---|---|---|
| `/WX-` 关着漏改静默通过 | 低 | `enum class` 强类型,漏改点几乎都编译报错,编译器牵引 |
| `Validate*Barrier` bind-flag 校验重写(按 access bit 查) | 中 | 手写逻辑 + 加测,是"资源能否进此态"唯一防线 |
| `ToStates` 逻辑正确性 | 中 | Phase A 先加、用旧 `ConvertXxxAttachmentState` 对拍等价性 |
| `CompileResourceState` 翻译 + `AdjustAccessBasedOnUsage` 折叠 | 中 | 与旧 `(usage,access)` 逐格等价,尤其 buffer Shader-read = `ConstantBufferRead\|ShaderSampledRead` |
| queue-gated PIXEL/NON_PIXEL / `operator==` 收窄 | 低 | graphics 队列行为不变;compute 队列是修复;冗余 barrier 少发是改善 |

## 12. 备选与否决(留档,免得重新纠结)

| 方案 | 否决理由 |
|---|---|
| **`ResourceState.m_usage` 升 usage-mask** | 血本波及全代码库所有 `ResourceState` 构造点 + barrier 字段 + 后端映射。且"usage 集合"配"独立 access"会有配对歧义(`{RenderTarget,Shader}×{Read,Write}` 分不清谁读谁写)。 |
| **具名组合枚举**(如 `DepthStencilReadShaderRead`) | 把 Vulkan 的离散 layout 模型硬塞给所有人:DX12 本可 OR,却要 `switch` 一个复合名;还把底层复合态提到上层作者面前。Vulkan-centric,不考虑 DX12。 |
| **`FlushBarriers` 静默合并** | 到 flush 时并发意图已丢(单值 tracker 已把并发顺序化成 `A→B`),要反推;FlushBarriers 无 pass 上下文报不了合法性错;**决定性**:compile 期 tracker 仍是单值,下一个 pass 的 `StateBefore` 错,而 barrier 是 compile 期提前算的,flush 太晚救不了。 |

三者的共同教训:复合态必须在**记录当前态的地方(compile 期 tracker)**被表示,而表示成"fine `AccessFlags` 集合"最干净——`ResourceState` 只描述访问,layout/state 由后端从集合推。

## 13. 待议 / 未来

1. **depth/stencil aspect 粒度**:`DepthStencilRead` 现在整块。未来"depth 只读采样 + stencil 可写"(Vulkan `DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL` 一族)需要拆 `DepthRead`/`StencilRead`/`StencilWrite`。注意:该组合在 DX12 无对应单一资源态(`DEPTH_WRITE` 不与 `PIXEL_SR` 共存),届时要专门处理,不是简单加 bit。
2. **buffer 的 uniform vs storage 读**:`ConstantBufferRead` 已分出;是否再分 `StorageBufferRead`(SSBO)按需要再加,枚举已留空间。
3. **stage 收窄的收益评估**:`ToStates` 里 stage 精确收窄 PIXEL/NON_PIXEL、以及 Vulkan 端更窄的 stage mask,值不值得,留到 Vulkan 后端落地时测。
