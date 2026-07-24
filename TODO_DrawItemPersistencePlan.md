# DrawItem 持久化 + Tag 分类设计（Draw Submission 重构）

## 背景与问题

当前提交路径每帧**全量重建** DrawItem,是实测瓶颈。

- `RenderGraphCompiler::CompileDrawRequests`（`RenderGraph/RenderGraphCompiler.cpp`）
  每帧 `GetView<DrawRequest>().each`（无门控）对**每个** DrawRequest 从零翻译出一个
  `RHI::DrawItem`：多次 `TryGet` + 重建内嵌 4 个 `fixed_vector` 的 KB 级重值 + `AddOrReplace`。
  成本 ∝ draw request 数（≈ 可见 Drawable × 几何 pass 数）。
- `GBufferProcessor::Process` 每帧还 `clear()` + refill 每个 DrawRequest 的 bindings/viewport。
- VS CPU Usage 采样（复杂模型导入后 Debug）：`RenderSystem::OnTick` 78%,其中
  `RenderGraphExecuter::Execute` 27% + `CompileDrawRequests` 20%。RelWithDebInfo 流畅
  → Debug 未优化 + 迭代器检查放大,不是算法 bug,但**每帧重建静态数据这件事本身是浪费**。

**记忆化只压稳态、压不了峰值**（突发大量可见 → ΔV→V 又回到全量）；真正治本是把**翻译时机
前移到构建期**,让每帧只做筛选 + 提交,不翻译。

## 核心决策

**DrawItem 翻译时机从「每帧 × 每 pass」前移到「构建期 × 每 (Drawable, pass)」。**
DrawItem 骨架持久化、跟宿主 Drawable 同生命周期；每帧只 `GetView<PassTag>` 取骨架 →
可见性过滤 → 注入每帧瞬态（`startInstance`）→ 提交。稳态零翻译、零 PSO 编译、零中间实体新建。

这条同时化解了之前纠结的「DrawRequest 独立生命周期追踪」——把翻译前移后,DrawRequest 这个
每帧中间物消失,生命周期只剩宿主 Drawable 一处。

---

## 一、两层 Tag

分开「物体是什么」和「draw item 给谁」——这是把方案理顺的关键。

| | 分类 tag | pass tag |
|---|---|---|
| 例子 | `Opaque` / `Transparent` / `ShadowCaster` | `GBufferPass` / `DepthPrePass` / `ShadowPass` |
| 打在 | **Drawable** 实体 | **DrawItem 骨架**实体 |
| 含义 | 物体的渲染性质 | 这个 draw item 由哪个 pass 取用 |
| 谁定 | 构建期由物体属性（材质 blend mode → Opaque/Transparent；castShadow 标志 → ShadowCaster） | 构建 DrawItem 时打上 |
| 怎么用 | 构建期查「哪些 pass 消费这个分类」 | 每帧 `GetView<PassTag, DrawItem>` 取 |

pass tag 这一层**现在就有**：`GBufferPass::Execute` 已经是 `GetView<PASS_TAG("GBufferPass"),
DrawItem>().each(Submit)`。

**为什么必须两层、不能合一**：一个分类（Opaque）被**多个 pass**消费（DepthPre + GBuffer），
而每个 pass 的 DrawItem 带**各自的 PSO**（depth-only PSO ≠ gbuffer PSO）。PSO per-pass 不同
→ DrawItem 必须 per-pass 一个；而分类 tag 是跨 pass 共享的物体性质。粒度不同,分两层。

### 分类 tag 的维度：正交可叠加 vs 同维度互斥

一个 Drawable **可以带多个分类 tag**,消费其中任一 tag 的 pass 都会匹配到、各建一个骨架
（如 `Opaque` + `ShadowCaster`）。但分类 tag **不是扁平集合,内部分「维度」**,这决定
「物体属性 → 分类 tag」的推导规则：

- **正交、可叠加**（不同维度）：`Opaque`（着色路径）+ `ShadowCaster`（投影）互不相干,能并存。
- **同维度、互斥**：`Opaque` vs `Transparent` 是同一维度（着色路径）的两个值,一个物体只能选
  一个,**不能同时带**。

所以推导不是「把满足的 tag 都堆上」,而是按维度走：

- 着色维度：材质 blend mode → **二选一** `Opaque` / `Transparent`；
- 投影维度：castShadow → **可选**加 `ShadowCaster`；
- 以后每多一个正交性质 = 多一个可选维度。

**展开成骨架**（以 Opaque + ShadowCaster 为例）：

```
Opaque       → 消费它的 pass: {DepthPrePass, GBufferPass}   ← 一分类多 pass
ShadowCaster → 消费它的 pass: {ShadowPass}
合并去重 → {DepthPrePass, GBufferPass, ShadowPass} → 建 3 个 DrawItem 骨架
```

匹配是「消费的分类 ∈ 物体的分类集」的 **OR** 语义；跨分类展开后要对 pass **去重**（防两个
分类被同一 pass 消费时建出重复骨架，虽然一般一个 pass 只消费一个分类）。

## 二、分类 → pass 映射（pass-driven pull）

**pass 定义时声明它消费哪个分类**（GBufferPass 消费 Opaque、ShadowPass 消费 ShadowCaster）。

**方向是 pass 主动 pull,不是 Drawable 反查**：不需要一张「分类→pass」反向表,而是每个 pass 用
`GetView<它消费的分类Tag>` 认领 Drawable、为其建骨架。「被哪几个 pass 消费」是各 pass 各自 pull
的自然结果——一个 Opaque + ShadowCaster 的 Drawable 被 GBuffer/DepthPre（消费 Opaque）和
Shadow（消费 ShadowCaster）各自认领 → 自然产出多个骨架。

这**复用现有 `AssembleDrawRequests` 的 pass-driven + find-or-create 结构**,改两处：

- **门控从可见性换成分类**：`GetView<ClassTag, Drawable>(Exclude<PassBuiltTag>)`,`PassBuiltTag`
  保证每 (Drawable, pass) 只建一次；每帧照跑,稳态 GetView 是空集、零成本。
- **建立与可见性分离**：建骨架只 gate 分类、不看 `Visible`（骨架持久,与本帧可见性无关）；
  `Visible<V>` 挪到每帧提交筛选（第六节）。现有 assemble 把两者混在一起,culling 就位前要拆开。
- 反向引用由建骨架的 pass 顺手 append 进宿主 Drawable 的 `DerivedDrawItems`——谁建谁登记。

## 三、DrawItem 骨架：每 (Drawable, pass) 一个,持久

- **内容（全静态,构建期填）**：
  - 几何：引用宿主 Drawable 的 vertex/index view（不复制顶点数据）；
  - **合成好的 PSO**（见第四节）；
  - 该 pass 的 viewport/scissor、pass binding（指针稳定）。
- **唯一每帧变的 `startInstance`**：**晚绑定,不写进骨架**——提交前从 slot table
  （`InstanceSlotTable.m_slots[slotRef]`）现取。`startInstance` 每帧由
  `InstanceBindingSystem` dense scatter 重算,是硬约束,绝不能进缓存的骨架,否则失效边界
  退化成每帧。
- **结论：稳态下 DrawItem 骨架完全只读**,连 startInstance 那一下都不写它。
- 骨架带 pass tag + 宿主引用（`m_host`）。

## 四、PSO 的构建：物体侧提示 + 运行期按 pass 合成

PSO = 物体半 × pass 半,构建期编不出完整的（缺 pass 那半）：

| PSO 组成 | 谁提供 | 构建期知道 |
|---|---|---|
| RenderTargetLayout（RT 格式/MSAA/depth 格式） | **Pass** | ❌ |
| 基线 RenderState（depth test/write） | **Pass** | ❌ |
| Shader 变体选择、RenderState 覆盖、InputLayout | **物体/材质** | ✅ |

流程：
1. **构建期**：物体侧存「PSO 提示」——shader 变体 key + render state 覆盖 + input layout。
2. **合成期**（建骨架时,已知落到哪个 pass）：物体提示 + pass 的 RT layout + 基线 state
   → 组完整 PSO descriptor → `PipelineLibrary` 查：命中拿指针,未命中**编译一次并缓存** →
   填进骨架的 `m_pipelineState`。
3. **编译只发生一次**（某个 `物体变体 × pass RT layout` 组合首见时）,之后 cache 命中 O(1)。

per-pass 骨架和 per-pass PSO 正好对齐：每个骨架的 PSO 就是它所属 pass 的完整 PSO。

## 五、生命周期：锚在 Drawable

DrawItem 骨架不再有独立生命周期,全跟宿主 Drawable 走：

- **创建**：Drawable compose 时 → 读它的分类 tag → 展开消费它的 pass 集 → 每个 pass 建一个
  骨架实体（合成 PSO、打 pass tag、写 `m_host`）→ 登记进 Drawable 的反向引用。
- **更新**：Drawable recompose（几何/材质变）→ 重建它的骨架们；窗口 resize → 更新 viewport；
  分类变（罕见）→ 增删对应骨架。均为稀疏事件,静态帧零更新。
- **失效**：Drawable dead → 顺反向引用删掉它派生的所有骨架。

### 反向引用（级联删除的前提）

正向 `DrawItem.m_host` 只让骨架知道「我属于谁」,不足以在 Drawable 死时**高效**删除它的骨架
（否则要全扫所有 DrawItem 找 host==它,O(总数)）。需要 Drawable → 它派生骨架的反向路径。

**采用定长 inline 数组**（零堆分配,组件内不放动态容器）：

```cpp
struct DerivedDrawItems {
    eastl::fixed_vector<RHI::RHIHandle, MaxPassesPerDrawable> m_items;
};
```

- 一个物体参与的 pass 数有明确小上限（个位数量级,给 shadow cascade 一 pass 多 view 留余量
  也就十几）,`fixed_vector` inline 存储,永不溢出堆。
- 先例：`DrawRequest.m_shaderBindings` 就是 `fixed_vector<RHIHandle, ShaderInputGroupCountMax>`,
  同一手法。
- 与「DrawItem 内嵌大 fixed_vector 拖慢」的坑区分：那个是 KB 级重值每帧全量拷贝；这个是只存
  句柄的冷索引,只在增删骨架时动,绝不每帧拷贝。

## 六、每帧流程

```
每个 pass:
  GetView<PassTag, DrawItem>.each(item):
     可见性过滤（item 的宿主 Drawable 对该 view 是否 Visible<V>）
     注入 startInstance（宿主 slotRef → slot table 现取）
     Submit(item)
```

零翻译、零 PSO 编译（cache 命中）、零中间实体新建。峰值来临也只是「筛更多」,不是「译更多」。

## 七、可见性接入点（culling 的口）

`Visible<V>` 是 per-view 标在 Drawable 上的（现有 `View/ViewTags.h`）。每帧筛选时,pass 的
`GetView<PassTag, DrawItem>` 要 join「宿主 Drawable 对该 view 可见」。culling 系统是
`Visible<V>` 增删的**生产者**,接上这个口即可,提交机制不改。今天 DrawableComposer 无条件
`Add<Visible<MainViewTag>>` 且永不移除 = culling 未就位时的「恒真」占位。

## 八、当前落地状态与接缝

现在只有 **Opaque 一个分类**、**DepthPre + GBuffer 两个 pass** 都消费它,所以分类层暂时退化
（就一类）。分类 tag、分类→pass 映射先立着但内容最小；加 Shadow/Transparent 时是「加一个分类
tag + 加一条映射 + 加一个 pass」,加法不是重构。

## 九、与现有代码的映射 / 预留

引擎已把接口预留好,这个方案主要是**接线 + 把翻译从每帧挪到构建期**,不是凭空造：

- `DrawRequest.m_vertexShaderOverride` / `m_fragmentShaderOverride` / `m_renderStatesOverride`
  （`Request/DrawRequest.h`,注释 "Lifecycle TBD with the Material system"）= **物体侧 PSO 提示**的预留位。
- `CompileDrawRequests(RHI::Device& /*device*/, RHIContext&, RHI::PipelineLibrary* /*pipelineLibrary*/)`
  两个参数都传进来但注释未用 = **PSO cache 合成**的预留入口。
- `GBufferPass::Execute` 的 `GetView<PassTag, DrawItem>` = pass 按 tag 取用**已存在**。
- `DrawRequest.m_shaderBindings`（`fixed_vector<RHIHandle, N>`）= 反向引用 inline 数组的先例。
- `DrawableComposer`（`Drawable/DrawableComposer.cpp`,当前 find-or-create Drawable 处）=
  骨架创建 + 生命周期锚的落点。
- 现有 `AssembleDrawRequests` + 每帧 `CompileDrawRequests` 全量翻译路径 = 被本方案取代/改造。

## 十、Pass 层标准化（落地后的形态）

DrawItem 骨架化 + tag 分类后,pass 层每帧代码从「每 pass 手写雷同逻辑」塌成三个 tag 参数化的
标准步骤:

- **建骨架（一次）** — `AssembleDrawItems<PassTag, ClassTag, BindingTags...>`:pass 声明消费的
  分类 + 要注入的共享 binding tags；通用流程认领 Drawable、建骨架、合成 PSO、`GetView<BindingTags>`
  取指针填、填 viewport。GBuffer 与 DepthPre 的差异退化成模板参数列表（DepthPre 少 material）。
- **绑定（Compile hook）** — 统一成「把 pass 的 per-pass 采样资源按 slot 找 view +
  `SetPassShaderImage`」。Compile 是所有资源（瞬态已 materialize + 静态一直在）就绪的第一个统一点;
  LightingPass 现有 Compile hook（binding 表 + `FindPassAttachmentImageView` + `SetPassShaderImage`）
  已是雏形。**边界:只统一 per-pass 采样资源（pass 自己 SRG 的 image/buffer slot,瞬态 gbuffer +
  静态纹理）；共享 SRG（view/material/instance）不并入——它们整个 SRG 挂 draw、建骨架时注入,粒度
  不同。**
- **提交（Execute）** — 先用简单统一的 `SubmitPassDrawItems<PassTag>`（`GetView<PassTag, DrawItem>`
  → 可见性过滤 → 注入 startInstance → Submit）。culling 未接入前不做进一步标准化。

**标准化替不掉的一小块**:pass 私有 binding 要绑什么具体资源（sampler 值、绑哪些 gbuffer SRV）是
「绑什么」的数据,tag 表达不了,仍由 pass 在 Compile 给一小段。（声明表把 slot→input→space 数据化
只是形式,不强求。）

## 十一、待定 / 未决

- **可见性过滤的具体接法**：骨架上冗余一个可见标记（culling 时同步）vs `GetView` 时 join
  宿主 Drawable 的 `Visible<V>`。
- **shadow cascade（一 pass 多 view）**：DrawItem 是否要下探到 per-(Drawable, pass, view)；
  `MaxPassesPerDrawable` 上限据此留余量。
- **排序**：draw list 按 PSO/深度排序放哪、是否跨帧缓存（排序是新的每帧主成本,但对紧凑数组、
  可并行）。
- **分类 → pass 映射的载体**：pass 定义处声明 vs 一张中心注册表。
- **更新触发信号**：recompose / 材质变 / resize 各自怎么标脏到具体骨架。
- **反向引用删单个骨架**：`fixed_vector` 里移除一个句柄的处理（swap-erase）。
- **PSO 提示的具体形态**：shader 变体 key 怎么编码、cache key 怎么组（衔接材质系统的 shader
  变体能力,当前纯 deferred 单一 GBuffer PS 还没有变体,见 `TODO_MaterialSystemPlan.md`）。
- **StaticImageAttachment 类资源的注册归属（未定,以后再思考）**:`CreateStaticImageAttachment`
  （静态资源 upload→shader-read barrier 的依据）有两个硬约束:① 必须**早于** render graph 编译
  （`CompileStaticResourceBarriers` 消费它）,放 pass Compile hook 时机不对;② 必须留在 **render 侧**
  （不能上移 feature,否则 feature 反依赖 SparkRender,违反 "features produce, render decides"）。
  现状塞在每帧 Process（如 `SkyboxProcessor::GetCubeImageView`,`Has<ImagePassAttachment>` 门控幂等、
  一次性）,Process 标准化后要从每帧模板里拆出来,但归属先不定死。相关:要拆开现状 `GetCubeImageView`
  里「注册 attachment（要早）」与「拿 view 绑定（可放 Compile）」的耦合;候选归属是「render 侧静态资源
  驻留 setup」（类比 `MaterialTextureSystem::EnsureResident` 里 create+upload+attachment 一起）,待
  相关代码稳定再议。
