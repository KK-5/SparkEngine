# GPU 资源常驻管线:声明 / 策略 / 执行三层

> 本文是「资产 → GPU 资源」这条线的**总纲**,记录经过长时间推演后确定的**分层模型、两条轴、声明 schema 要素**,以及现在做什么、将来怎么无缝扩展。
> 相关联文档:`TODO_FeatureRenderBoundary.md`(feature/render 依赖边界)、`TODO_HandleLifetimePlan.md`(句柄生命周期 —— 其中 `Ref<H>` 的结论已被本文修正,见 §6)。
> **优先级:方向已定,待落地。** 近期只在上层做去重 + 搬家(见 §5),中间策略层(流式/VT)是前瞻接缝、暂不建。

---

## 1. 核心结论:pool(去重追踪)不可省

只要想保证「**一个资产只上传一次**」,就必须有一个**追踪机制**记住「哪些内容已经常驻」。这个 map 无法省略——它是**上层 feature producer 的状态**(见 §2:去重属于上层,不是中间策略层)。争论过的所有形态(放系统成员、ECS 组件当 pool、扫描 Pending)最终都绕不开它,所以问题不是「要不要」,而是「放哪层、怎么维护」。

---

## 2. 三层模型:声明 / 策略 / 执行

整条管线是 **上层(feature)→ 策略(policy)→ 执行(execution)** 的数据驱动流水。**关键:去重在上层,中间策略层只管 streaming/VT 的驻留细节。**

| 层 | 职责 | 现状对应 |
|---|---|---|
| **上层(feature producer)** | 扫自己引用的资产 → **去重(find-then-create)** → 用 `CreateStaticImage` / `RequestImageUpload` **声明**创建需求 → **GC**(卸载自己不再被引用的资源)。**它以为资源已按要求备好。** | 基本已是——`MaterialTextureSystem` 的 `m_pool` + `EnsureResident` + `CollectGarbage`;需搬家 + 甩掉 attachment(见 §5) |
| **中间(策略)** | 给一份**已声明需要**的资源决定**驻留多少**:流式(选哪些 subresource 进 GPU)、VT(建页表、不整传)。**不做去重。** | **暂无**,且**近期不建**——流式/VT 才需要 |
| **RHI(执行)** | 按最终信息分配 + 上传指定 subresource。**只执行,不问用途。** | 已是——`RHIResourceSystem::OnFrameBegin` + `AsyncUploadSystem` |

### 2.1 支点:handle 抽象跨策略稳定
上层引用一个**稳定 handle**,它解析成什么(全驻留 / 流式 / VT)由驻留细节决定。材质不知道差别,照样采样。这是「上层以为资源已备好」能成立的唯一前提。

### 2.2 契合现状,近期几乎不是「插层」而是「搬家」
`CreateStaticImage`/`RequestImageUpload` 本就是数据驱动声明(产 Pending、延迟物化),`RHIResourceSystem` 本就是执行器,而**去重现在就在上层**(`MaterialTextureSystem`)。所以**近期没有新的中间系统要建**:只把上层那套搬到正确的家、甩掉 attachment(§5)。中间策略 stage 是**将来** streaming/VT 落地时才插入的接缝——届时它跑在 `RHIResourceSystem::OnFrameBegin` 之前、扫 `Pending*` 标注驻留范围,上层和 RHI 都不动。

### 2.3 上层用 find-then-create,天然无重复 handle、无反向更新
去重在上层做「**先查再建**」:命中 pool → 直接返回既有 handle;miss → 才 `CreateStaticImage`。**从不产生重复 handle**,所以既不需要「共享物理 Ptr」那套,也不存在反向更新问题——这就是 `MaterialTextureSystem` 现在的行为,是对的。
> (曾讨论过的「先建再扫去重 → 让重复 handle 的 `Image` 指向同一 `Ptr<Image>`」是**另一条路**,只在「上层不去重、由中间层事后扫 Pending 坍缩」时才需要。既然去重放上层 find-then-create,这条路用不上,留档备忘即可。)

---

## 3. 两条轴:身份(asset)与 驻留(render demand)

推演中最容易混淆、也是被纠正两次的点:**GPU 资源的「身份」和「驻留」是两条独立的轴**,真实引擎(UE / O3DE)把它们解耦。

| 轴 | 由什么驱动 | 回答 | 例子 |
|---|---|---|---|
| **身份 / 去重** | **资产**(内容 key) | 「这是哪份资源、去哪找/重建」 | O3DE `InstanceDatabase` 按 AssetId intern;UE `UTexture2D` |
| **驻留 liveness** | **渲染需求**(组件引用 / 可见性) | 「何时提交 / 驱逐 VRAM」 | UE `FRenderAssetStreamingManager`;O3DE `StreamingImagePool` 预算 |

### 3.1 为什么不能用「资产引用计数」做驻留判据(已否决)
资产计数回答「CPU 资产还有没有人引用」——**包含资产浏览器、缩略图这些跟渲染无关的持有者**。拿它决定 GPU 驻留会**过度保留**:没有材质用了、但浏览器还揣着资产 → VRAM 被钉死。真实引擎靠**解耦**解决这个场景:资产对象继续活着,streaming 把 VRAM **换出**(留缩略图 mip 或不驻留)。所以驻留必须由**渲染需求**驱动,资产只是身份/去重锚点。

### 3.2 现状其实两条轴都摆对了
`MaterialTextureSystem` 现在:`m_pool` 按 `AssetId` 索引(身份轴 ✓)、`CollectGarbage` root 在 `MaterialParams` 的使用(渲染需求轴 ✓)。它是「按资产去重 **且** 按渲染需求驱动驻留」的**朴素但轴正确**版本,只是缺 streaming 的精细度(无部分驻留、无预算)。**核心模型不用换。**

---

## 4. 声明 schema 要素(现在唯一必须前瞻设计的接缝)

中间层能否**无缝**加流式/VT,取决于**声明里带没带足够信息**。策略层今天可以不读,但声明格式不能把它们挡在门外——否则将来加策略要改声明 API(破坏性)。声明**现在就应能携带**:

- **content key(不透明 uint64)** —— 去重。由上层传 `assetId.GetHash()`,RHI/中间层不认识资产系统,保持 asset-agnostic;
- **完整描述符 / 完整 mip 链** —— 流式需要知道资源全貌,才能决定先传哪几级;
- **subresource 粒度的上传源** —— 流式 / VT 要按 subresource(mip / tile)分别取字节,不能只给「一整块」。

> 这是唯一需要现在认真敲的东西,正对「提前开好接缝、推迟造通用性」的做法:今天只做去重,但 schema 要能承载未来策略所需。

---

## 5. 现在做什么 vs 将来,及诚实的边界

### 现在(近期落地)—— 主要是搬家,不建新系统
- **上层 producer 就位**:`MaterialTextureSystem` 的 `pool` + `find-then-create` + `CollectGarbage` **核心几乎不动**,只做两件事:
  - **搬家**:从 `SparkRender` 移到**材质侧、依赖 SparkRHI 的 producer 系统**。注意 `SparkMaterial` 的授权层 `MaterialSystem` **故意 RHI-free**,创建不能进它——放一个独立的材质纹理 producer(依赖 SparkRHI,不依赖 SparkRender)。
  - **甩掉 attachment**:`CreateStaticImageAttachment`(用途/render 概念)从 producer 移走,下沉到 render 侧(见 §6 / 边界文档);producer 只把 handle 经组件递给 render,render binding 时取用 + 注册 import attachment。
- **skybox 同构**:同样是「上层 producer:扫引用 → 去重 → 声明 → GC」。去重/GC 的**模式抽成可复用工具/模板**,material 和 skybox **各自实例化一份**,而**不是**共享一个系统(见 §6 per-feature 说明)。
- **声明 schema 按 §4 设计到位**(content key + 完整 mip + subresource 粒度),给将来 streaming 留口。
- 驻留 liveness **保留渲染需求根**(现状 `CollectGarbage` 即可,精确且够用)。

### 将来(同一 stage 里加策略,两端不动)
- **流式**:标注只传低 mip,其余按需。**对 shader 透明**(mip clamp,模糊等高 mip 流入)。
- **VT**:标为虚拟、建页表、按 tile 页入。

### 边界(别过度承诺「无缝」)
- **VT 对 shader 不透明**:VT 采样要采页表、算物理页、写 feedback,不是普通 `Texture2D.Sample`。**上传/驻留管线无缝,但着色器采样那端需材质配合**。
- **RHI「只上传」略窄**:流式够用现有 subresource range;**VT 需要 RHI 长出 tiled/reserved 资源 + 页映射机制**。仍是「机制,非策略」,但 RHI 得补这个能力。

---

## 6. 与既有决定 / 文档的一致性 & 修正

- **上层 producer 属材质侧,不是授权层**:`SparkMaterial` 的 `MaterialSystem`(授权/数据层)**故意 RHI-free**;去重/创建/GC 放一个**依赖 SparkRHI 的材质纹理 producer**,不塞进授权层。
- **per-feature 去重 + 共享工具(非共享系统)**:material 和 skybox **各自拥有**自己的 scan+dedup+GC,把这个模式抽成**可复用工具/模板**、各自实例化。全局唯一性靠「feature 之间不共用同一贴图资产」(实际如此);若哪天两 feature 引用同一资产,per-feature 会各建一份——现在可接受。
- **RHI 是唯一 GPU 内存 owner**:producer **不持 `ImagePool`**、不碰内存,只持**去重 map + GC 决策**(何时 `DeadTag`);分配/物化全走 `RHIResourceSystem`,释放归 reaper。持 pool 会把 producer 重新养大。
- **不把去重塞进 `RHIResourceSystem`**:它是执行器,保持愚蠢;去重在**上层 producer**,GC 也在上层。
- **依赖边界**:create+upload 是 feature-side「produce」(依赖 SparkRHI,不依赖 SparkRender),attachment 下沉 render——见 `TODO_FeatureRenderBoundary.md`。
- **对 `TODO_HandleLifetimePlan.md` 的修正**:那份以 `Ref<H>`(通用实体句柄引用计数)为核心;经本轮推演,**纹理这条线不需要 `Ref<H>`**——liveness 根在渲染需求,现状 scan-GC 已精确。`Ref<H>` 降级为「若要消灭扫描时的可选优化」,**暂不做**;`MaterialHandle` GC 等非资产句柄是**独立议题**,不用同一把锤子。资产引用计数做驻留判据的路(该文档没写,但本轮讨论出现过)**已否决**(见 §3.1)。

---

## 7. 待细化 / 待定

- 材质纹理 producer 的落点:新建独立系统,还是 `MaterialTextureSystem` 原地加 SparkRHI 依赖并从 `SparkRender` 迁出;与授权层 `MaterialSystem` 的边界。
- 去重/GC 可复用工具的形态(模板 / 基类),material 与 skybox 如何各自实例化。
- 去重 map 的存放形态与反查删除(内容 key ↔ handle),及它与渲染需求 liveness(`CollectGarbage`)的衔接。
- attachment 下沉后 render 侧的承接:producer 用什么组件把 handle + 用法提示递给 render,render 在哪一步注册 import attachment。
- 声明 schema 的具体字段与 C++/Pending 组件表达。
- 将来中间策略 stage 的形态与时机(仅 streaming/VT 落地时):扫 `Pending*` 标注驻留范围的 frame hook 位置。
- skybox 接入后,cubemap(多 face / 多 array layer)在 subresource 粒度声明上的表达。
