# Pass / ProcessFeature 设计

世界 ECS 如何接到渲染 Pass 的整套方案。核心结论:**不引入 Atom 的 `RPI::Scene` 类;连接生产与消费的只有 `PassTag` 组件 + entt view,ECS 自己就是路由层,不需要 registry / EBus / 回调。**

相关文档:per-draw PSO 见 [TODO_PerDrawPSOVariant.md](TODO_PerDrawPSOVariant.md)。

---

## 0. 为什么不要 `RPI::Scene`

Atom 的 `Scene` 把 7 件事捆在一个 OOP 类里,逐条映射到 SparkEngine:

| Scene 职责 | SparkEngine |
|---|---|
| FeatureProcessor 注册表 | 已是 `ISystem` + `TickBus` |
| RenderPipeline 列表 / 默认 pipeline | `Pipeline` / `PassContext` |
| `ConfigurePipelineState(drawListTag,…)` | 多余——Spark PSO 是 pass 级,见第 6 节 |
| Scene SRG(time / prevTime) | 折进 ViewBindings 或低 space 的 SceneBindings,不要类 |
| CullingScene / VisibilityScene | 推迟(GPU-driven 方向) |
| DrawPacket 收集 | 被 ECS 取代(第 4 节) |
| View / DrawFilter tag 注册表 | 推迟 |

5/7 已覆盖或主动推迟。`Scene` 作为类是个上帝对象,违背"一个类要么是 System 要么是 Component"的原则,**不port**。它真正暴露的缺口是 mesh→draw 的桥,那是一个 **System**(见第 2 节),不是一个类。

---

## 1. Pass = 自包含单元

每个内置 pass 独立文件,以自己的 `PassTag` 为编译期身份,暴露:

- `Declare(PassContext&, config)` —— 一次性:`SPARK_RENDER_PASS` 声明 attachment / 输出配置(RT layout、MSAA)/ 默认 Execute。
- Execute 默认体 = `GetView<Tag, DrawItem>().each(submit)`;只有特殊 pass(如 ResolvePass)才覆盖。

**Pipeline = 这些单元的有序组合**,不是一个大函数:

```cpp
void BuildForwardPipeline(PassContext& ctx, const PipelineConfig& cfg)
{
    ShadowPass ::Declare(ctx, cfg.shadow);
    ScenePass  ::Declare(ctx, cfg.scene);
    ResolvePass::Declare(ctx, cfg.resolve);
}
```

加一个 pass = 新增一个文件 + 组合函数加一行。各单元只管自己的 attachment 和 PSO,互不污染。后续演化方向是数据驱动的 pass 资产(Atom 的 `.pass` JSON),但**先做代码侧组合**。

---

## 2. 生产侧 = ProcessFeature,就是普通 `ISystem`

collect 阶段跑,读 World,建 `DrawRequest` 实体到 RHIContext,盖上自己的**编译期** `PassTag`。**没有 registry、没有 EBus、没有 AttachFn、没有 hash 桥。**

```cpp
class ForwardFeature final : public ISystem  // collect 阶段
{
    void Collect()
    {
        auto& world = *WorldExecuteContext::Current();
        auto& rhi   = *RHI::RHIExecuteContext::Current();
        world.GetView<Mesh, MeshGPUComponent, Material, WorldTransformMatrix, VisibleTag>()
            .each([&](Entity src, auto& gpu, auto& mat, auto& xf, auto&)
        {
            if (!mat.Participates("forward")) { return; }   // 参与与否:读数据决定
            // …（生命周期/缓存见第 7 节,这里是概念形态）
            RHIHandle draw = rhi.CreateEntity();
            rhi.Add<DrawRequest>(draw, BuildDraw(gpu, mat.ShaderFor("forward"), xf));
            rhi.Add<SPARK_PASS_TAG("Forward")>(draw);        // 只盖自己这一个 tag
        });
    }
};
```

### 粒度原则(唯一硬规则)

> **某个实体进不进某个 pass,必须由实体的数据决定(材质声明的 role、`CastShadow` 这类能力组件),绝不能是 feature 里写死的 pass 名单。**

守住这条,feature 是"一个管一个 tag"(per-pass)还是"一个管多个 tag"(content-feature)就只是**组织/性能选择,不影响对错**:

- **起步 = per-pass feature**:`ForwardFeature` / `ShadowFeature` / …,各盖各的 tag。加 `VelocityPass` = 加 `VelocityFeature`(查 `<…, Moving>`,盖 velocity tag),已有 feature 一行不改。
- **content-feature 是以后可选优化**:若 profiling 发现"N 个 feature 各查一遍 mesh 集"真的耗(线性重读通常很便宜,大概率不耗),可把共享同一查询的几个 feature 合成一个,**数据驱动地遍历材质的 shader 列表**、每个变体盖它自带的 tag。前提仍是数据驱动,不能硬编码 tag。**现在不做。**

几何不重复浪费:顶点/索引 buffer 在 `MeshGPUComponent`(预备阶段建一次),各 feature 只**引用**同一份;它们产的 `DrawRequest` 不同,只因 shader/binding 不同——这本就该不同(forward draw ≠ shadow draw)。

---

## 3. 三阶段时序

```
阶段1  共享预备系统(写派生组件,各 system 间靠 Request() 排序)
       TransformSystem → WorldTransformMatrix
       MeshSystem      → MeshGPUComponent(GPU 常驻/上传)  ← 从"中心分发器"退成"共享服务"
       CullingSystem   → VisibleTag

阶段2  ProcessFeature(只读 World+派生,产 tagged DrawRequest)
       ── 可并行:读共享,写分片+串行尾(见第 8 节)

阶段3  RenderGraph: compile(DrawRequest→DrawItem) + execute(按 tag 提交)
```

- 阶段边界保证:所有 draw 在任何 pass execute 之前就已存在,feature 和"它的"pass 之间无需排序。
- feature 之间无序(各盖独立 tag);预备系统之间有序(靠 `Request()` 依赖)。

---

## 4. Tag 即路由(为什么不要 registry / EBus)

连接生产与消费的,**只有 `PassTag` 组件 + entt view**:

```cpp
// 消费侧(pass Execute):编译期,O(1) 分组,pass 知道自己的类型
GetView<SPARK_PASS_TAG("Forward"), DrawItem>().each(submit);
```

推演链:registry 和 EBus 都是为了"一次遍历扇出给多消费者,省重复遍历"。但这是 **OOP 的病**——胖对象散在堆里,N 个消费者 = N 遍虚分发遍历。**ECS 里不存在**:每个 feature 跑 `GetView<它要的组件>`,只扫相关稠密池,是**线性流式重读**,最便宜的那种重复。为省这点引入回调间接、类型擦除、per-entity 分发,是亏的。

所以:**ProcessFeature 塌缩成普通 System**,collect 阶段跑哪些、什么顺序 = 引擎 system 列表 + `Request()` 依赖,和别的系统同一套调度,不需要任何专属机制。tag 全程编译期,hash 桥不需要。

> 备忘:曾推演过 registry(hash → {attach 闭包, rule, shaderSource})和 EBus 广播(entity 作消息,handler 自己 `TryGet` 关心的列)两个中间方案。EBus + ECS 的"entity 作行主键、订阅者任意列投影"是 OOP 做不到的优势,但 **per-entity 广播 + `TryGet` 是 entt 最伤 cache 的访问模式**(E×P 次虚分发 + 随机 sparse-set 查),不如各 feature 跑批量 view。最终结论:连接器只需 PassTag + view,两个中间方案都不要。

---

## 5. Tag 的两个来源(都由数据决定)

- **A 类 外观 pass**(forward / transparent / gbuffer):tag 长在**材质类型的 shader** 上,作者写 shader 时声明一次。shader 声明 `forward` 是承诺一个**角色**(pub/sub 的 topic),不是绑某个具体 pass——多个订阅 `forward` 的 pass(主视图、反射探针)都能消费同一 shader。"有材质 → forward"只在 forward 管线里近乎全员;换延迟管线,全员的是 `gbuffer`。
- **B 类 系统 pass**(shadow / depth-prepass / velocity / picking):不问材质,feature 用一条**对世界组件的条件**(如 `HasComponent<CastShadow>`)筛,用 pass 自带的 shader 画。

加 pass 时:
- 复用已有 tag → 什么都不改。
- A 类新外观 → 给材质**类型**加 shader 变体(数据),材质实例不变。
- B 类 → 加一个 feature system。
- **任何情况 MeshSystem / 已有 feature 都不改。** tag 绝不是 per-instance 字面数据。

---

## 6. PSO 在 compile 阶段合成

per-draw PSO = **材质 shader 变体 ⊕ pass 输出配置**(= Atom `ConfigurePipelineState(tag,…)` 的合并)。

合成**落在 compile,不在 feature**:feature 产的 `DrawRequest` 只带 shader + render-state 意图;compile 拿 `DrawRequest` 和它的 PassTag → 按 tag 找到 pass → 取 pass 输出配置(RT layout / MSAA)→ 合成最终 PSO → DrawItem。这落在现有 `CompileDrawRequests` 里,**producer 全程不碰 pass 输出配置**,解耦保住。

注意 PSO 策略(见 [TODO_PerDrawPSOVariant.md](TODO_PerDrawPSOVariant.md)):**主线是多 draw 共享少 PSO**,把差异塞进 binding/常量/bindless,而非 PSO 排列;`DrawRequest::m_vertexShaderOverride` 等是**稀有逃生口**。若出现"一 draw 一 PSO",那是 PSO 爆炸的设计味道,在数据侧治,不靠缓存/淘汰救。

---

## 7. 数据生命周期:两套缓存(被混淆的两件事)

PSO 的生命周期和 draw 实体的生命周期**规则完全不同**,分开处理。

### 7.1 PSO 缓存 —— 内容寻址,共享,零通知

PSO 不归任何 draw 所有,是按 descriptor hash 的内容寻址缓存 `hash → Ptr<PipelineState>`。compile 算 descriptor → hash → 查;miss 才建。**可从 descriptor 重建**,所以淘汰随便淘,即使被淘汰下次查 miss 重建即可,**不需要通知任何上层**。主线 PSO 数量有界(第 6 节),几乎不 churn。

### 7.2 draw 实体缓存 —— 源绑定 + 变更检测 + 可选剔除淘汰

draw 实体多、但便宜(小组件,字段全是指针 / RHIHandle)。三种情形:

1. **源销毁** → producer 删它的 draw(trivial,无需通知)。
2. **源变了**(transform / 材质) → change detection,producer 更新对应 draw。
3. **源在但被剔除很久** → 可选 LRU 淘汰,省内存(大世界 99% 被剔除)。

**通知问题在 ECS 里不用"通知"——用组件状态通信。** 在**源实体**上挂一个链接组件当桥:

```cpp
struct DrawCacheRef
{
    RHI::RHIHandle drawEntity;
    uint32_t       builtVersion;   // 源数据版本,用于变更检测
    uint32_t       lastUsedFrame;  // 用于 LRU 淘汰
};
```

producer 每帧循环是**幂等**的——不是"每帧重建",是"确保可见源都有一份最新 draw":

```cpp
// ProcessFeature collect
GetView<Mesh, Material, WorldTransformMatrix, VisibleTag>().each([&](Entity src, ...)
{
    auto* ref = world.TryGet<DrawCacheRef>(src);
    if (!ref || ref->builtVersion != src.version)   // 没建过 / 被淘汰 / 源变了
    {
        RHIHandle draw = BuildDraw(src, ...);        // (重)建——昂贵的事只在这发生
        world.AddOrReplace<DrawCacheRef>(src, { draw, src.version, frame });
    }
    else
    {
        ref->lastUsedFrame = frame;                  // touch:本帧还在用
    }
});
```

淘汰系统(消费之后跑):

```cpp
GetView<DrawCacheRef>().each([&](Entity src, DrawCacheRef& ref)
{
    if (frame - ref.lastUsedFrame > kEvictFrames)
    {
        rhi.DestroyEntity(ref.drawEntity);   // 删 draw
        world.Remove<DrawCacheRef>(src);     // 清桥
    }
});
```

闭环:淘汰把 `DrawCacheRef` **删掉**;那个源下次再可见时,producer 的幂等循环发现 `!ref` → **自然重建**。淘汰和 producer **在 `DrawCacheRef` 的有/无上会合**,谁都不回调谁——"上层知道被淘汰了"变成"上层每帧检查桥还在不在"(pull,非 push)。

producer 只 touch **可见**源(`VisibleTag` 驱动),走出视野的源不再被 touch → draw 自然老化 → 超 N 帧淘汰;重新可见 → 重建。`VisibleTag` 顺手定义了"用没用"。

---

## 8. 并行

阶段 2 读 World 是只读,天然可并行。卡点在**写侧**:entt registry 变更默认非线程安全。所以并行版 = **读共享 + 写分片**:

- 读 World / 派生组件:随便并行。
- 建 draw 实体:每个 feature 写**线程局部缓冲**,阶段末**串行 merge** 进 RHIContext;或给 RHIContext 分片。
- 串行版结果顺序确定;并行版生成实体内部 id 顺序可能不同,但**最终图像不受影响**(消费按 tag 的 view,且不做 CPU 排序)。

后置优化,不动结构。

---

## 9. 数据分层小结

| 数据 | 谁填 | 放哪 |
|---|---|---|
| per-draw(model 矩阵、材质参数、贴图) | ProcessFeature,按 draw 的 shader 反射 | per-draw ShaderBindings(高 space) |
| per-view(g_ViewProjection) | View | 挂在 pass 上的 ViewBindings(space0) |
| per-pass 输出配置(RT layout / MSAA) | Pass 的 Declare | Pass entity,compile 时取来合 PSO |
| 共享派生(WorldXf、VisibleTag、GPU 常驻) | 阶段 1 预备系统 | World / RHIContext 组件,各 feature 只读 |

---

## 当前决策

整套骨架已闭环。落地顺序:

1. **Pass 自包含单元 + pipeline 组合**(第 1 节)——把 DrawCube 的 ScenePass/ResolvePass 重构成单元,验证 Declare + 默认 Execute。
2. **第一个 ProcessFeature(per-pass 粒度)+ 三阶段时序**(第 2、3 节)——依赖材质雏形,起步可退化成"一材质一 shader 声明一 tag"。
3. **draw 生命周期:先 source-tied + change-detection**(第 7.2 节情形 1+2),消灭"每帧重建"。
4. **PSO 缓存**(第 7.1 节)——独立并行,随时可做,见 [TODO_PerDrawPSOVariant.md](TODO_PerDrawPSOVariant.md)。

**推迟(留缝不挡路)**:cull-based 淘汰(第 7.2 情形 3)、并行(第 8 节)、content-feature 合并(第 2 节)、多视图、数据驱动 pass 资产、Scene 级常量(SceneBindings)。

**不做**:`RPI::Scene` 类(第 0 节)。
