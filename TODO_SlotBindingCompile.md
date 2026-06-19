# Slot 绑定与两层编译模型

Pass 内部用 slot 名引用 attachment(如 `CopyFrameBufferPass` 用 `CopySource` 读 `SceneColor`)。slot 是 **pass-scoped 别名**,而它指向的 transient 资源/视图**只在 compile 期存在**。本文定义"slot → 资源/视图"的解析机制,以及它如何把 compile 阶段切成两层。

相关:Pass/ProcessFeature 整体见 [TODO_PassFeatureDesign.md](TODO_PassFeatureDesign.md);ShaderInput 见 [TODO_ShaderInputDesign.md](TODO_ShaderInputDesign.md)。

---

## 1. 关系模型:资源是 hub,(pass,slot) 是 alias-edge

- **资源 = 纯内存**(`SceneColor` 资源实体:descriptor + materialize 后的 `BackingImage`),本身不带 view。
- **每个 (pass, slot) = 一条 alias-edge**(`ImagePassAttachment`),带自己的 `ViewDescriptor` + 自己的 view 实体,按名指向资源。生产者(写 `SceneColor`,slot=`SceneColor`)和消费者(读,slot=`CopySource`)**对称**,都只是别名,没有"默认 view"这回事。
- **edge 是唯一真相源**:`m_attachmentId.m_id`=资源、`m_slotName`=别名、`m_view`=view 实体、`m_pass`=归属。alias→resource、resource→aliases(`ImageLifetime.m_attachments`)双向都可达。

> edge 生命周期:Build 建 → Compile 消费 → **Execute 末销毁**(在 `RenderGraphExecuter::End()`,和 transient view / transient resource 实体一起销毁——四类按帧实体生命周期对齐)。

## 2. 查找:用 PassTag(entt 原生索引),不手搓 (pass,slot) map

`GetView<PassTag, X>` 就是 entt 用类型 pool 做的 O(本 pass) 索引——**零维护**。自建 `(pass,slot)→edge` map 是在它之外再造一个索引,还得自管生命周期(清得太早就在 compile 用到前没了、太晚就泄漏)、加 `BasicContext::ctx()` 出口、pair-hash……全是自找的负担。**不做。**

> "量小所以扫描就行"也不构成理由——机制按对错定,不按当前规模定。扫描的真正毛病不是慢,是"在知道答案的注册处把它丢掉、到消费处再扫一遍重建"的冗余。结论:**用 entt 已有的类型索引(PassTag),不另造。**

代价:泛型遍历(`GetView<CopyRequest>`)拿不到编译期 `PassTag`。**所以 slot 解析不做泛型版,放进 Pass 的 `Compile` 函数**——那里 `PassTag` 静态已知,所有查找走模板版 `FindPassAttachmentImage<PassTag>` / `...ImageView<PassTag>` / `...Buffer<PassTag>`,全程 tag-filtered,无索引、无 `m_pass` 字段。

## 3. 两层编译模型(地基)

| 层 | 在哪 | 按什么解析 | 例子 |
|---|---|---|---|
| **全局层** | 泛型 compile 步骤 | handle / 指针 / 资源(pass-agnostic) | transient 物化、DrawItem 组装、PSO 合成、ShaderBindings→descriptor |
| **per-pass 层** | Pass 的 `Compile` 函数(现在空着,正好用) | **slot**(= PassTag 作用域) | CopyRequest→CopyItem、attachment view 注入 ShaderBindings |

不变 ordering(view-cache 重构后):

```
全局-前 : 物化 transient 资源(CompileTransientResources;view 懒建不再预物化)
全局   : CompileDrawRequests(DrawItem 组装,handle/指针)—— 留全局不动
per-pass: slot 解析 + 接线(CopyItem、SetImage 进 bindings)
全局-后 : CompileShaderInputs(bindings→descriptor)—— 从 per-pass 循环前 挪到 之后
Execute
```

判据一句话:**按 slot 解析 → 进 Pass 的 Compile 函数;按 handle/指针解析 → 留全局泛型。** slot 天然 pass-scoped,这不是妥协,是 slot 作用域决定的。

- `CompileShaderInputs` 位置自由(只要 Execute 前完成),为支持 per-pass 的 SetImage 接线,**挪到 per-pass 循环之后**。
- `CompileDrawRequests`(DrawItem 组装,handle/指针)**留全局不动**——DrawItem 只持 binding **指针**,binding **内容**何时接线/编译与组装无关。draw 的 slot-绑定 ShaderBinding(B 类)搬进 Compile 函数的**只是注入那一步**,不是整个 DrawRequest 编译(见 §5)。

> **view-cache 重构带来的前提变化**:① attachment 不再有 `m_view` 实体,改为 `m_image`(资源)+ `m_viewDescriptor`,view 经各 resource 的 cache 懒建 → 「transient view 提前物化到 CompileShaderInputs 前」这条**作废**,view 现在 on-demand。② `FindPassAttachmentImageView<PassTag>` 现在**需要 frameIndex**(per-frame 分流)→ Compile 函数必须能拿到 frameIndex(从 `RenderGraphCompiler::GetFrameIndex()`)。

## 4. 第一个落地:CopyRequest / Copy pass

```cpp
struct CopyRequest          // 实体由 Processor 打 PassTag;无 m_pass(tag 已锁定 pass)
{
    RHI::InputName m_sourceSlot;
    RHI::InputName m_destSlot;
    RHI::CopyItem  m_template;   // Processor 填 type + 标量参数,资源指针留 null
};
```

- **Processor**(ISystem):收集上层数据(UI framebuffer pos/size…),产/更新 `CopyRequest`,**填全 `m_template` 的所有标量(含 source size),只留资源指针为 null**,change-driven。**只碰 slot 名,从不解析资源**。
- **Pass.Compile**:`CompilePassCopyRequests<PassTag>(ctx)`——`GetView<PassTag, CopyRequest>` 逐条按 `m_template.m_type` 分派,用模板版 `FindPassAttachmentImage/Buffer<PassTag>` **只补资源指针**,`AddOrReplace<CopyItem>`。覆盖全部 4 种 `CopyItemType`(Buffer / Image / BufferToImage / ImageToBuffer)。
- **Pass.Execute**:`GetView<PassTag, CopyItem>().each(submit)`,纯 submit,无外部依赖(`UIBaseSystem` 等只出现在 Processor)。

`CopyItem` 每帧重建(swapchain/transient backing 轮转),成本 = 两次 tag-filtered 查找,可忽略;和 `DrawRequest→DrawItem` 同构。`CopyRequest` 复用 `RHI::CopyItem` 当模板载体,避免复刻那个 4-variant union。

> **原则:标量在 Request 里就完备,compile 不反推。** 早先设想过"source size 留零、compile 从 descriptor 全图填"当便利,但这让 Pass 反向理解了上层意图(资源多大→拷多少),违背"业务数据拦在 Pass 外、Pass 只执行 Request"的初衷。且 `RHI::Size` 默认是 `{1,1,1}` 不是 0,那个 zero-fill 判据根本不触发(会拷成 1×1)。现在:Processor 用 `ui->GetFrameBufferSize()` 把 source size 一并填进 `m_template`,compile 只解析按名标识的资源,绝不碰标量。

## 5. DrawRequest → DrawItem 的拆分(B 类:attachment view 注入 ShaderBindings)

DrawItem 组装**绝大部分 pass-agnostic**(draw args、viewport、按实体的 vertex/index buffer、binding **指针**)→ 留全局 `CompileDrawRequests` 不动。

需要 PassTag 的**只有一块**:把 Pass attachment 的 view 注入 DrawRequest 的 ShaderBindings。比如光照 pass 采样 `SceneColor`/`SceneDepth` —— 这些 transient view 在 Build 时还不存在(资源懒物化、view 懒建),DrawRequest 拿不到,只能 compile 期按 slot 解析。

**关键:这块不碰 DrawItem 组装。** DrawItem 只持 binding **指针**;per-pass 只是往那个 binding 里 `SetImage`(注入 view),之后 `CompileShaderInputs`(已挪到 per-pass 之后)把 binding 编成 descriptor,DrawItem 顺指针就拿到。

### per-pass Compile 函数(render 默认体)

```cpp
template<typename PassTag>
void CompilePassDrawBindings(RenderGraphCompiler& compiler)
{
    auto& ctx = *RHI::RHIExecuteContext::Current();
    const uint32_t frameIndex = compiler.GetFrameIndex();
    ctx.GetView<PassTag, DrawRequest>().each([&](RHIHandle, const DrawRequest& req)
    {
        for (auto& b : req.m_attachmentBindings)
        {
            auto* view = FindPassAttachmentImageView<PassTag>(ctx, b.m_slot, frameIndex);
            auto* sb   = ctx.TryGet<Components::ShaderBindings>(
                             req.m_shaderBindingEntities[b.m_groupIndex]);
            sb->m_bindings->SetImage(b.m_shaderInput, view);
            MarkShaderBindingsUpdate(ctx, req.m_shaderBindingEntities[b.m_groupIndex]);
        }
    });
}
```

### DrawRequest 新增 slot→binding 声明(就是之前说的「DrawRequest 缺失」)

```cpp
struct AttachmentBinding
{
    RHI::InputName m_slot;          // pass attachment slot,如 "SceneColor"
    RHI::InputName m_shaderInput;   // shader 资源名/register,如 "g_SceneColor"  —— 显式声明(见 §决策)
    uint8_t        m_groupIndex;    // 注入 m_shaderBindingEntities 的哪个 binding
};
eastl::fixed_vector<AttachmentBinding, N> m_attachmentBindings;
```

**前提**:DrawRequest 实体要打 `PassTag`(像 attachment 一样),这样全局 `GetView<DrawRequest>` 和 per-pass `GetView<PassTag, DrawRequest>` 都成立。

### 决策(已和作者确认)

1. **slot→shader register**:**显式声明**(`AttachmentBinding::m_shaderInput`),不做按 shader 资源名自动匹配 —— 简单无歧义,不引反射 magic。
2. **vertex/index buffer 按 slot 引用**:**先不做**。当前按实体引用(mesh 几何是 feature 自有静态 buffer,够用)。GPU 生成几何(buffer 是 attachment、按 slot)罕见,真出现再做(它要 per-pass 在全局组装前 patch DrawRequest,会动 ordering)。
3. **render 的默认 Compile**:就是 `CompilePassDrawBindings<PassTag>`。没有 `m_attachmentBindings` 的 pass(如 DepthPre 只写不采)自然空转,无需特殊处理。

**当前状态**:DepthPrePass 只写不采,还没有 attachment-view binding,`m_attachmentBindings` 为空;首个采样图内纹理的 pass(光照采 `SceneColor`/`SceneDepth`)才真正用上这条。

## 6. 默认 Compile/Execute 体 + 函数形态

补全 `TODO_PassFeatureDesign.md` 第 1 节的设计意图("Execute 默认体,特殊 pass 才覆盖"),并推广到 Compile。Copy 是第一个落地。

### 6.1 builder 在 Finalize 装默认,用户给了就用用户的

CopyPassBuilder 模板化在 `PassTag` 上,Finalize 里 tag 静态可知,能合出默认:

```cpp
funcs.m_buildFunction   = eastl::move(m_buildFunction);          // 仍必填:slot 声明 pass-specific
funcs.m_compileFunction = m_compileFunction ? eastl::move(m_compileFunction)
                                            : CompilePassCopyRequests<PassTag>;   // 默认
funcs.m_executeFunction = m_executeFunction ? eastl::move(m_executeFunction)
                                            : SubmitPassCopyItems<PassTag>;       // 默认
```

(去掉 copy 的 `ASSERT(m_executeFunction)`。)结果:copy pass 退化成只声明 slot:

```cpp
SPARK_COPY_PASS(ctx, "CopyFrameBufferPass")
    .Queue(RHI::HardwareQueueClass::Graphics)
    .Build([](RenderGraphBuilder& b){ /* 声明 CopySource / CopyWrite */ })
    .Finalize();                  // Compile / Execute 全默认
```

需要特殊处理(如 clear-to-black + barrier)就 `.Execute()` 覆盖;默认管常见、override 管特殊。

### 6.2 函数形态:对齐回调签名 + 内部自取 ctx

默认体写成"就是一个 compile/execute 回调",直接可插、零 wrapper:

```cpp
template<typename PassTag>
void CompilePassCopyRequests(RenderGraphCompiler&)   // 形参不命名:符合回调契约,用不到 compiler
{
    auto& ctx = *RHI::RHIExecuteContext::Current();   // 自取
    ctx.GetView<PassTag, CopyRequest>().each([&](RHIHandle h, const CopyRequest& req){
        RHI::CopyItem item = req.m_template;
        switch (item.m_type) { /* 4 种,用 FindPassAttachment*<PassTag> 补指针 */ }
        ctx.AddOrReplace<RHI::CopyItem>(h, item);
    });
}

template<typename PassTag>
void SubmitPassCopyItems(ExecuteWork& work, RenderGraphExecuter&)
{
    auto& ctx = *RHI::RHIExecuteContext::Current();
    ctx.GetView<PassTag, RHI::CopyItem>().each(
        [&](RHIHandle, const RHI::CopyItem& item){ work.m_commandList->Submit(item); });
}
```

用户复用默认也是一行,无 lambda:`.Compile(CompilePassCopyRequests<SPARK_PASS_TAG("...")>)`。

对比"无参版 `CompilePassCopyRequests<Tag>()`":签名更纯,但不匹配回调,装默认/调用都得包一层 `[](RenderGraphCompiler&){ ...(); }`。所以取**带 `RenderGraphCompiler&`** 版,换"直接可插、零 wrapper"。`/WX-` 下未命名形参无 warning。

> 更新:copy 用不到 compiler、形参可不命名;但 **draw 的 `CompilePassDrawBindings` 要 `compiler.GetFrameIndex()`** 给 per-frame view 解析 → 形参命名、就用它。同一个 `void(RenderGraphCompiler&)` 签名,用不用随实现。

### 6.3 两类函数两种 ctx 约定

- **顶层 pass 步骤**(默认体:`CompilePassCopyRequests` / `SubmitPassCopyItems`):**自取 ctx**,签名对齐回调,调用方零样板。
- **底层可复用原语**(`FindPassAttachmentImage<PassTag>(ctx, slot)` 等):**显式收 ctx**,因为常在已持有 ctx 处反复调(顶层取一次往下传),显式更灵活、好单测。

### 6.4 include 架构

默认体要拉 `CopyRequest.h` / `CopyItem.h` / `FindPassAttachment*`(在重的 `PassAccess.h`)。`PassBuilder.h` 被**每个 pass** include,直接塞会拖慢全体编译。所以 **CopyPassBuilder + 默认体拆到独立头**(如 `Pass/CopyPass.h`,`SPARK_COPY_PASS` 一并搬过去),只让写 copy pass 的文件吃这些 include;通用 builder 保持轻。

### 6.5 推广到 Render/Compute

Copy 的 Request→Item 整个是 per-pass(默认 Compile = `CompilePassCopyRequests`)。Render 的 DrawItem **组装**在全局 `CompileDrawRequests`,但它**仍需一个默认 Compile** —— 即 B 类的 `CompilePassDrawBindings<PassTag>`(把 attachment view 注入 binding);没有 `m_attachmentBindings` 的 pass 空转。两者默认 Execute 都是 `GetView<Tag, DrawItem|CopyItem>().each(submit)`。

> 修正文档早先「render 不需要默认 Compile」的说法 —— 那是 B 类落地之前。

---

## 已就绪的前置(本设计依赖,已在仓库)

- attachment(edge)生命周期延长到 Execute 末(`RenderGraphExecuter::End()` 销毁),slot 解析在 compile/execute 都可用。
- transient view / transient resource 实体每帧销毁,四类按帧实体生命周期对齐。
- 相位重排:`CompileTransientResources` 提到 `CompileShaderInputs` 之前。
- `FindPassAttachmentImage<PassTag>` / `FindPassAttachmentImageView<PassTag>`(返回 `RHI::Image*` / `RHI::ImageView*`)。
- `CopyPassBuilder` / `SPARK_COPY_PASS` / `CopyPassTag`。
- `Request/CopyRequest.h`(结构骨架)。

## 待写代码(分步实现,出问题再改方案)

**C 类(CopyRequest,先做):**
1. 默认体 `CompilePassCopyRequests<PassTag>(RenderGraphCompiler&)` + `SubmitPassCopyItems<PassTag>(ExecuteWork&, RenderGraphExecuter&)`,内部自取 ctx,4 种 CopyItemType 分派,`FindPassAttachment*<PassTag>` 补指针。
2. 删 `CopyRequest::m_pass`(PassTag 已锁 pass)。
3. `CopyPassBuilder`:Finalize 没给 Compile/Execute 就装默认;去掉 `ASSERT(m_executeFunction)`。连同默认体拆到独立头 `Pass/CopyPass.h`(`SPARK_COPY_PASS` 搬过去),通用 `PassBuilder.h` 保持轻。
4. `CopyFrameBufferPass`:塌成 `.Build` + `.Finalize`(clear/barrier 仍需则 `.Execute()` override)。
5. 最小 `CopyFrameBufferProcessor`(ISystem):产/更新 `CopyRequest`,change-driven 填 UI framebuffer pos。

**B 类(DrawRequest attachment binding,后做):**
6. `DrawRequest` 加 `AttachmentBinding` + `m_attachmentBindings`(§5);DrawRequest 实体打 `PassTag`。
7. 默认体 `CompilePassDrawBindings<PassTag>(RenderGraphCompiler&)`(§5),`compiler.GetFrameIndex()` + `FindPassAttachmentImageView<PassTag>` 注入。
8. RenderPassBuilder Finalize 装 `CompilePassDrawBindings` 默认 + `GetView<Tag, DrawItem>().each(submit)` 默认 Execute。

**公共前置:**
9. 把 `CompileShaderInputs` 挪到 per-pass 循环之后(C 类不依赖、B 类依赖;可等 B 类做时再挪)。

## 待定

- `CopyRequest` 实体归属:`CopyFrameBufferProcessor` 自己拥有,还是统一进 `UIProcessFeature` 那个洞。
- Processor change-detection 粒度(比对 last pos / 走 UI 事件)。
- `m_attachmentBindings` 的 `N`(每 draw 采样的 attachment 上限)取值。
