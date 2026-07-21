# Feature / Render 依赖边界与 GPU 资源产出分层

> 记录「上层 feature 系统与 render 系统之间,GPU 资源的创建 / 上传 / 使用如何分工」的**背景、方案与论证**,避免日后重推。
> 只记结论与依据,不含实现细节。
> **优先级:待细化。** 方向已定,具体切分待展开。

---

## 1. 背景:上层反向依赖 render

现状里若干 feature 模块(SparkMesh / SparkSkybox)在**自己的系统内**直接建 GPU 资源并注册 render graph attachment:

- `MeshSystem`:`RHI::CreateStaticBuffer` + `RHI::RequestBufferUpload` + `Render::CreateStaticBufferAttachment`
- `SkyboxSystem`:`RHI::CreateStaticImage` + `RHI::RequestImageUpload` + `Render::CreateStaticImageAttachment`

问题:feature 依赖了 **SparkRender**(render graph / attachment 概念),这是架构倒挂——上层不该反向依赖渲染层。

---

## 2. 否决的方案:resolver 全下沉

曾考虑把「解析上层组件 → 建 GPU 资源」整体做成 render 层的 resolver 系统:feature 只产数据组件(AssetId / CPU data),render 侧 resolver 扫组件、建资源、写回 GPU handle。

**否决理由**:这会把 render 从「取数据、渲染」的系统膨胀成一个**超级系统** —— 所有和渲染有关的组件、连**组件生命周期管理**都塞进它。资源的产出与生命周期不该归 render 持有。

---

## 3. 选定方案:按命名空间切,依赖边界画在 SparkRender

关键观察:三个调用分属两层,真正「向上」的依赖只有 `Render::` 那一个。

| 调用 | 所属模块 | 层级性质 |
|---|---|---|
| `RHI::CreateStaticImage/Buffer` | **SparkRHI** | 基础资源抽象(与内存分配器同级),feature 依赖它是正当的 |
| `RHI::RequestImageUpload/BufferUpload` | **SparkRHI** | 同上 |
| `Render::CreateStatic*Attachment` | **SparkRender** | render graph 概念,feature 依赖它 = 倒挂 |

### 切分
- **create + upload → 留在上层 feature 系统**(只碰 SparkRHI)。资源的**存在与生命周期**归产出方。
- **attachment → 下沉到 render**。attachment 本就是「render 如何使用这份资源」(barrier / 哪个 pass / stage / layout),归 render 决定,时序上属于 render graph 组建期。

这样:上层对 SparkRender 的反向依赖消失,render 也不膨胀成超级系统,生命周期仍归产出方。

---

## 4. 这是「features produce, render decides usage」的精确形态

- **produce(feature)**:CPU data → GPU 资源(create + upload)。
- **decide usage(render)**:render graph 怎么消费它(attachment、barrier、pass、stage/layout)。

attachment 需要的只是 handle + 用法参数(InputName / access / usage / stage),这些正是 render 该决定的东西。之前 resolver 方案把「produce」也算给了 render,是过界;此方案把边界精确切在 produce / usage 之间。

---

## 5. 一条可固定的规则

> **feature 可以依赖 SparkRHI(产出 GPU 资源),但绝不依赖 SparkRender(render graph / pass / attachment 概念不上溯)。依赖边界画在 SparkRender,不画在 SparkRHI。**

与 CLAUDE.md「RHI 里不许有 render 概念」对称:一条管「render 概念不下沉进 RHI」,一条管「render 概念不上溯进 feature」。合起来用于判定「某个资源相关调用该放哪一层」。

---

## 6. 要落实时需处理的点(仅备忘)

- **Material 要对称**:`MaterialTextureSystem` 现在在 SparkRender 里、且自己调 `Render::CreateStaticImageAttachment`。按本方案:attachment 那句 → render;create + upload + pool → 挪出 render,放到依赖 SparkRHI(不依赖 SparkRender)的 feature 侧,与 `SkyboxSystem` 对称。
  - 区分:**SparkMaterial 授权层(`MaterialParams`/`MaterialComponent`)保持 RHI-free**(既有约定);纹理 create+upload 是**另一个**依赖 RHI 的产出系统,不塞进授权层。
- **render 需要一个接缝组件**:feature 产出后递给 render 一个 `{ RHIHandle + 用法提示(静态上传纹理,需 import barrier)}`;render 扫这类组件注册 attachment。
- **时序**:attachment 必须赶在「首次采样它的那帧 render graph 编译 barrier」之前;拆成两个系统后要保证这个顺序。
- **与句柄生命周期议题正交**:见 `TODO_HandleLifetimePlan.md`。谁 create 资源不影响引用回收机制;本文只管「create/upload/attachment 分在哪一层」。

---

## 7. 待细化

（留待补充:具体接缝组件形态、Mesh/Skybox/Material 各自的迁移步骤、attachment 下沉后的 render 侧系统职责边界。）
