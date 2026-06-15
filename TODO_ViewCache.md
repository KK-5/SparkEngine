# View Cache:View 归 Resource 的缓存,不做独立 Entity

承接 [TODO_SlotBindingCompile.md](TODO_SlotBindingCompile.md)(attachment 按 slot 引用 + 两层编译)。那份里把"resource 是 hub、view 是别名"讲清楚了;本文定**view 到底怎么存**:**view 不是独立 entity,而是 resource 实体上一个有界的缓存组件**。

---

## 1. 决策

- **Resource = 声明式**(`RHIResourceSystem` 材质化 Image/Buffer Ptr;创建重,留 aliasing/pooling 优化空间)。
- **View = 立即/懒 + 去重,存在 resource 的缓存里**(创建轻,不共享,只 dedup 复用)。
- attachment 持 `m_image`(资源实体)+ `m_viewDescriptor`,**不持 view**;要 view 时 `GetOrCreateImageView(m_image, descriptor)` 现取。

### 为什么不是 entity(litmus test)

> "组件里塞容器"只有当组件代表**跨实体关系、你会 `GetView<T>()` across entities** 时才是反模式。若组件是**某实体自有、有界、只通过它本身访问**的数据(`GetView<ResourceViewEntry>()` 没意义),容器就是合理的内禀数据,不该 promote 成 entity。

view 通过这个测试:没人跨资源 query view、没人持 view handle、view 不共享。entity 的三大理由(外部 handle 引用 / 跨实体 query / 独立 compose)对 view 都很弱。`ViewHierarchy`/`TransientViewTag` 这些其实是 entity 化的**成本**,不是理由。

### View 上的数据盘点(都是封闭小字段,撑不起 entity)

| 数据 | cache 里怎么放 |
|---|---|
| Ptr(view 对象) | entry 字段 |
| ViewDescriptor(key) | entry 字段 |
| BackingImageView(帧内裸指针) | 折进 entry 的 `m_current`(单帧 == `m_view.get()`) |
| ResourceName | entry 字段(或省) |
| resource version / LRU lastUsedFrame | entry 字段,天然支持 |
| bindless slot index | entry 字段(且现已存在,不需 query) |
| view 类型/aspect(SRV/RTV/UAV/DSV) | 几乎不需要 |
| ~~per-view barrier/state~~ | **不存在**——D3D12 状态是 per-subresource,挂 resource |

---

## 2. 具体形态

multiplicity 跟随资源,和 `Image`/`ImagePerFrame` 对齐,拆成两套(**不要 `m_current`**——它把帧可变状态塞进 entry,是 smell):

```cpp
// 单帧:Image → ImageViewCache
struct ImageViewCacheEntry
{
    RHI::ImageViewDescriptor m_descriptor;   // key
    Ptr<RHI::ImageView>      m_view;         // 拥有;m_view.get() 终生稳定,可烘进 compile 产物
    // 将来:uint32_t m_bindlessIndex / m_lastUsedFrame / m_resourceVersion
};
struct ImageViewCache { eastl::fixed_vector<ImageViewCacheEntry, N> m_entries; };

// per-frame:ImagePerFrame → ImageViewCachePerFrame
struct ImageViewCachePerFrameEntry
{
    RHI::ImageViewDescriptor        m_descriptor;
    FrameArray<Ptr<RHI::ImageView>> m_views;   // 一 descriptor 对 N 个 view,execute 按 frameIndex 取
};
struct ImageViewCachePerFrame { eastl::fixed_vector<ImageViewCachePerFrameEntry, N> m_entries; };
```

`GetOrCreateImageView(ctx, resource, image, descriptor) -> RHI::ImageView*`(单帧):
1. 在 `ImageViewCache.m_entries` 按 descriptor 找 → 命中返回 `m_view.get()`。
2. miss → `factory->CreateImageView()` + `Init(image, descriptor)` + `push_back`,返回裸指针。

**返回 `RHI::ImageView*`,不再是 entity handle。**

`GetOrCreateImageViewPerFrame(...)`:per-frame 路径**留空 stub(`ASSERT(false)`)**,目前无消费点;落地时一次 Init 填满 `FrameCountMax` 槽,返回按帧解析的 view。没有稳定指针可烘——必须 execute 按 frameIndex 取。

### 轻轻量轻量级 ≠ 免费(为什么仍要 dedup)

DX12 view = 往 descriptor heap slot 写 descriptor(极轻,但 slot 有限要管理);Vulkan = `vkCreateImageView` 小对象(轻,但要销毁、最佳实践是复用)。所以"立即建"成立、"去重"必要——现在"每条 attachment 建一个新 view"是在白烧 slot/对象。

---

## 3. 并行模型(关键:cache-append 是原地写,不是 registry 结构性写)

compile 以后会并行(per-pass 填各自的 DrawItem/CopyItem,互不干涉)。entt 里能否并行看的是**结构性写 vs 原地写**:

| 操作 | 并行安全 |
|---|---|
| `CreateEntity` / `Add` / `Remove`(emplace) | 否(动共享 entity 池 / sparse set,锁整个 registry) |
| `Get<T>(e).field = x`(原地改已存在组件,不同 entity) | 是 |

- **view = entity** → 创建是 `CreateEntity`,**锁整个 RHIContext**。✗
- **view = cache** → 创建是往 resource 自有的 `ImageViewCache` 组件 **append**(原地写),不同 resource 并发安全,**per-resource 粒度**。✓

**前提**:**资源创建时就 `Add<ImageViewCache>`(空)**,这样 `GetOrCreate` 永远是原地 append(`fixed_vector` 不 realloc),零结构性写。

### compile 的并行形状(两段)

```
串行结构 pre-pass:  ① 给每个 Request 预建空的 DrawItem/CopyItem 组件
                     ② GetOrCreate 出所有 attachment 声明的 view(append 进各 resource 的 cache)
并行填充:           每 pass 并行——查(读 view 的裸指针)+ 原地填自己的 DrawItem/CopyItem
```

view 集合在并行前已知(attachment 的 descriptor 在 Build 单线程产、binding descriptor 在 Processor 声明),所以串行 pre-pass 能一把建完。**规则:结构性写(建 view、emplace item 槽)在串行段;并行段只原地填 + 读。execute 永不建 view。**

---

## 4. 这一改退役掉的旧设施

- **view entity 整个消失**;`CompileTransientImageViews` 建实体 → 改成往 cache append。
- `ViewHierarchy` / `ResourceHierarchy` 对 view 的链 —— 退役(cache 取代链)。
- `BackingImageView` 组件 —— 折进 entry 的 `m_current`,退役。
- `TransientViewTag` —— 不需要(cache 随 resource 销毁)。
- `RHIResourceSystem` 里那段 view 材质化(建 view entity)—— 删,只留**资源**材质化。
- `CreateImageView`(返回 entity)/ DrawCube 的 `m_imageViewEntity` —— 改成 `GetOrCreateImageView(resource, descriptor)` 拿 `RHI::ImageView*` 直接 `SetView`。

---

## 5. 唯一的特例:per-frame

swapchain / dynamic 的 view 是 per-frame(一个 descriptor 对应 N 个真 view)。已落成独立组件 `ImageViewCachePerFrame`(entry 存 `FrameArray<Ptr>`)+ stub helper `GetOrCreateImageViewPerFrame`,和 `Image`/`ImagePerFrame` 对齐。关键差异:**没有终生稳定指针可烘进 compile**,必须 execute 按 frameIndex 解析。helper 实现留 TODO(无消费点)。

---

## 6. 当前工作树状态(半成品,下次接着改)

**已做(保留):**
- `ImagePassAttachment` 加了 `RHIHandle m_image`(资源实体),在 `CreateImageAttachment` / `ImportImageAttachment` / compiler lifetime sweep 三处填好。`m_view` 仍在,**没有消费点读 `m_image`**,行为不变。命名按类型:image 用 `m_image`,buffer 将来用 `m_buffer`,不统一成 `m_resource`。

**需返工(当前是错的形态):**
- `Engine/Code/RunTime/Feature/RHI/ResourceBuilder.h` 里的 `GetOrCreateImageView` 现在是"建 view **entity** 的声明式版"——**要改成本文第 2 节的 cache 版**(resource 上 `ImageViewCache` find-or-append、返回 `RHI::ImageView*`)。

---

## 7. 待写 TODO(cache 版,按可增量落地排序)

### 阶段 1 — cache 原语
- [x] 定义 `ImageViewCacheEntry` / `ImageViewCache`(单帧)+ `ImageViewCachePerFrameEntry` / `ImageViewCachePerFrame`(无 `m_current`)。
- [x] `GetOrCreateImageView(ctx, resource, image, descriptor) -> RHI::ImageView*`(find-or-append,返回 `m_view.get()`)。
- [x] `GetOrCreateImageViewPerFrame` stub(`ASSERT(false)`,占位防忘)。
- [ ] 资源创建处(transient `CreateTransientImageResource`、`CreateStaticImage`、ImportSwapChain 等)`Add<ImageViewCache>` 空缓存,保证并行 append。(暂缓:helper 自带 TryGet/Add 兜底,等并行或消费点接上再做)

### 阶段 2 — 消费点切到 cache(m_view 仍在,逐个切)
- [x] `FindPassAttachmentImage` → `att.m_image → BackingImage`(1 跳)。
- [x] `FindPassAttachmentImageView` → `GetOrCreateImageView(att.m_image, *BackingImage.m_image, att.m_viewDescriptor)`。
- [x] `CompileRenderPassBeginInfo` → `resolveView` lambda 经 `GetOrCreateImageView` 取裸指针(RT/DS/Resolve 三处)。**`BackingImageView` 已无读取方,只剩写入方**(Phase 3 删)。

> ⚠️ per-frame 陷阱(**Phase 4a 已修**):Phase 2 一度让 `CompileRenderPassBeginInfo`/`FindPassAttachmentImageView` 对所有 attachment 走单帧 cache,但 swapchain 是 per-frame(sample 拿它当 RT/Resolve),会返回第 0 帧旧 view。已改成**按 `att.m_view` 是否为空分流**:imported(带 view 实体,swapchain)走 `BackingImageView`(`RefreshPerFrameBackings` 每帧刷);transient 走 cache。

### 阶段 3 — 切懒建 + 删 view entity 设施
- [x] `CompileImageBarriers` 资源解析 `ResolveResource(att.m_view)` → `att.m_image`(删 `CompileTransientImageViews` 的前置;buffer barrier 仍用 `m_view`,等 buffer 对称)。
- [x] `CompileTransientImageViews` → **已删**(含调用点)。排序坑是虚惊:`CompileShaderInputs` 不读 transient view,只编译已填好的 binding;现状无任何 pass 把 transient view `SetImage` 进 binding(那是未做的 B 类注入)。transient 图像 view 现纯懒建,消费点 `CompileRenderPassBeginInfo`/`FindPassAttachmentImageView` 走 cache。注:将来真有「采样 transient 图像」的 binding 时,需保证其 `FindPassAttachmentImageView` 在 `CompileShaderInputs` 前调用(已在 RenderGraph.cpp 注释标注)。buffer view 仍走 `CompileTransientBufferViews`。
- [x] 图像 lifetime sweep 闸门已删(冗余,name-lookup 已过滤 imported)。
- [x] **删 `ImagePassAttachment::m_view`**:图像 attachment 的 view 解析全部走 cache(`att.m_image` + `m_viewDescriptor`),不再有任何读取方。连带删 `CreateStaticImageAttachment`/`ImportImageAttachment` 的 `a.m_view` 写入、image-static 的 `ASSERT(att.m_view==Null)`。**决策(作者):swapchain 的旧 `SwapChainViews` 路径是临时保留,现在被绕过(对 attachment 解析已是死代码),其 per-frame view bug 暂时接受,随 swapchain 迁移到新架构一起修——不为这个临时方案把新代码往回掰。**
- [ ] 退役 `BackingImageView` / `TransientViewTag` / view 的 `ViewHierarchy`(图像侧已无读取方;写入方:transient 的已随 `CompileTransientImageViews` 删除,swapchain 的留在 `ImportSwapChain`/`RefreshPerFrameBackings`,等 swapchain 迁移)。`BufferPassAttachment::m_view` 仍在用(buffer 未迁)。

### 阶段 4 — imported 统一(swapchain view 暂保留特例,已和作者确认)

**关键现状**:render graph 里**所有 import 都是 swapchain**(引擎 `CopyFrameBufferPass`/UIPass + 全部 sample 的 `m_view = GetCurrentSwapChainView()`);**没有非 swapchain 的单帧 import,也没有 buffer import**。swapchain 是 per-frame 特例:resource 实体(`SwapChainImages` N 张裸图)+ view 实体(`SwapChainViews` N 个 view),`RefreshPerFrameBackings` 每帧刷 `BackingImage`/`BackingImageView`。

- [x] **4a**:resolve 路径按「`att.m_view` 是否为空」分流——imported(swapchain)走 view 实体的 `BackingImageView`(per-frame 正确),transient 走 cache。改了 `CompileRenderPassBeginInfo::resolveView` + `FindPassAttachmentImageView`。**修了 Phase 2 的 per-frame 回归,并把 swapchain 特例落到明确分支。**
- 决策:**swapchain view 暂保留自有 `SwapChainViews` + view 实体路径**,不强行塞进 cache。
- [ ] **4d(延后)**:真正实现 `GetOrCreateImageViewPerFrame`(显式收 N 张 image)+ execute 按 frameIndex 解析,等出现「swapchain/dynamic 作可采样 view」需求再做。

**重要后果**:因为 swapchain 仍用 view 实体 + `BackingImageView` + `ViewHierarchy`,这些设施**现在还不能删**(它们对 swapchain 是 load-bearing)。Phase 3 里「删 `m_view` 字段 / `BackingImageView` / view `ViewHierarchy`」要等 4d 把 swapchain 也迁走之后。`RHIResourceSystem` 的 view 材质化同理——但它服务的是渲染系统外部资源,需单独评估是否仍有非 swapchain 用户。

### 阶段 5 — sample / 收尾
- [ ] DrawCube:`CreateImage` 删 eager `CreateImageView`;`UpdateViewBindings` 改 `GetOrCreateImageView`。
- [ ] UIPass 注释版 `WriteImageAttachment(SwapChain,…)` 跑通。
- [ ] 跑测试,修引用 `m_view` 的地方。

### 阶段 6 — buffer 对称(已完成)
- [x] `BufferPassAttachment` 加 `m_buffer`、删 `m_view`;Create/Import 在 build 填、Read/Write 在 lifetime sweep 填。
- [x] `FindPassAttachmentBuffer` → `att.m_buffer → BackingBuffer`;`CompileBufferBarriers` → `att.m_buffer`;buffer lifetime sweep 删 `m_view` 闸门、填 `m_buffer`。
- [x] 删 `CompileTransientBufferViews` + 调用点 + static-buffer 的 `ASSERT(att.m_view==Null)`。
- 注:buffer **view** 当前无任何消费点(没人读 `BackingBufferView`),所以**不建 buffer view**,也暂不加 `BufferViewCache`/`GetOrCreateBufferView`(避免死代码,等出现 buffer-view 消费点再加)。
- [x] 连带清理:删无用的 `ResolveResource` 函数、sweep 里无用的 `factory`、`Executer::End` 的 transient-view 销毁块(view 随 transient 资源实体销毁而释放)、完全无引用的 `TransientViewTag`。

### 阶段 7 — per-frame 原语(进行中)
- [x] `GetOrCreateImageViewPerFrame(ctx, resource, Image& image, viewDesc, frameIndex)`:`ImageViewCachePerFrame` 按 descriptor 找/建 entry,懒填 `m_views[frameIndex]`(用传入的 `image`=当前 `BackingImage`),返回该帧 view。
  - 契约 `image == image[frameIndex]`:命中时用 `ImageView::GetImage()` 地址比较校验(抓「帧配错」/「swapchain 重建未失效」)。未命中直接建。
  - 判据约定:单帧 vs per-frame 看**资源**(`PerFrameTag`/`ImagePerFrame`/`SwapChainImages`),不用 `m_view`。
  - 未来约束:现 compile 每帧跑,可编译期 resolve 当前帧;若 compile 输出被缓存,per-frame 必须 execute 按 frameIndex 解析(cache 天然支持)。
- [x] resolve 路径按资源 multiplicity 分流(`Has<PerFrameTag>(att.m_image)`):`CompileRenderPassBeginInfo::resolveView` + `FindPassAttachmentImageView`(改成带 `frameIndex` 的 frame-aware 单一函数)。

### 阶段 8 — swapchain 迁移(已完成)
- [x] `ImportSwapChain` 瘦身:只建 `m_swapchainResource`(`SwapChainImages`+`ImportedTag`+`PerFrameTag`+`ResourceName`);删 `m_swapchainView` 实体、`SwapChainViews`、预建 view 循环、`ViewHierarchy`/`ResourceHierarchy`。
- [x] `RefreshPerFrameBackings`:删 `SwapChainViews→BackingImageView`(保留 `SwapChainImages→BackingImage`、`ImageViewPerFrame→BackingImageView`)。
- [x] import API:`ImportedImageAttachmentBindInfo` 的 `m_view`(实体)→ `m_resource`+`m_viewDescriptor`;`GetCurrentSwapChainView`→`GetCurrentSwapChainResource`;`RenderGraph::GetSwapchainView` 删;`Begin(…, swapChainResource, …)`。
- [x] `ImportImageAttachment`:`resource=bind.m_resource`,`a.m_viewDescriptor=bind.m_viewDescriptor`;删 view 实体反查 + `BackingImageView` materialize;单帧 imported 仍从 `Image` materialize `BackingImage`,per-frame 靠 `RefreshPerFrameBackings`。
- [x] 调用点:`CopyFrameBufferPass`(bind + execute 用 `executer.GetFrameIndex()`)、`RenderSystem` UIPass、3 个 sample(Triangle/MSAA/DrawCube)。
- [x] 删死组件 `SwapChainViews`。全量(引擎+5 sample+editor+test)编译通过。**注:运行时未实跑验证(需窗口)。**

### 阶段 9 — view 实体彻底退役(已完成)
- [x] 迁移最后两个旧 `CreateImageView` 消费方:DrawCube(存 `m_baseColorViewDesc`、bind 时 `GetOrCreateImageView`)、ImGui 图标(`IconGPUComponent` 存 `m_viewDesc`、`UIProcessFeature` 走 `GetOrCreateImageView`)。
- [x] 删 `RHI::CreateImageView`/`CreateBufferView`(返回实体的 helper)。
- [x] 删 `RHIResourceSystem::CreateImageViews`/`CreateBufferViews`/`LinkViewToResource` + 调用(只留资源材质化)。
- [x] `ImportBufferAttachment` 迁到 `bind.m_buffer`+`m_viewDescriptor`(镜像 image);删 `ValidateImportedView`。
- [x] 删 `RefreshPerFrameBackings` 的 `ImageViewPerFrame`/`BufferViewPerFrame` 死分支。
- [x] **删组件**:`Components::ImageView`/`BufferView`/`ImageViewPerFrame`/`BufferViewPerFrame`、`ViewHierarchy`/`ResourceHierarchy`、`BackingImageView`/`BackingBufferView`,及 RHIComponents.h 的 re-export。
- 全量(引擎+5 sample+editor+test)编译通过。**至此 view 不再以任何形式作为 entity 存在,全部走 resource 的 view cache。**

### Follow-up / 剩余
- B 类 ShaderBindings 注入复用 `GetOrCreateImageView`;compile 并行(串行结构 pre-pass + 并行填充)。
- buffer-view cache:出现 buffer-view 消费点时再做。
- 运行时未实跑验证(需窗口),建议跑 sample/editor 确认。
- 旧 `CreateImageView`/`CreateBufferView`(返回 view 实体)+ `RHIResourceSystem` 的 view 材质化:评估非 swapchain 用户后退役。
- B 类 ShaderBindings 注入复用 `GetOrCreateImageView`;compile 并行(串行结构 pre-pass + 并行填充)。
- buffer-view cache:出现消费点时再做。
