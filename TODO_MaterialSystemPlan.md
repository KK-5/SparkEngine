# 材质系统设计（Material System）

## 目标

给延迟渲染的光照提供**数据化的材质属性**。当前 GBuffer 的 albedo / metallic /
roughness / ao 全是硬编码（`GBuffer.hlsl` PSMain，注释 "Hardcoded material until
the material system lands"）。材质系统的第一目标是让不同物体能有不同的材质参数，
由 GBuffer 阶段写入 GBuffer，再喂给 LightingPass 的 BRDF。

## 总体决策

- **方案 B**：材质是**独立、可共享的资源**，instance 只存对材质的引用；不把材质参数
  塞进 per-instance 的 `InstanceData`（那样材质无法共享、数据冗余）。
- **分阶段**：
  1. 参数材质（baseColor + metallic + roughness + ao 标量）—— 本文档主体；
  2. 纹理材质（albedo / normal / metallic-roughness / ao 贴图 + sampler + UV）；
  3. Material asset（可序列化、编辑器可编辑、gltf 解析材质）。
- **先不走 gltf 解析材质**：首版材质由代码/编辑器手动创建，gltf 导入的 mesh 暂时全指
  默认材质。避免过早引入材质资产/序列化的配套复杂度。
- **分层**（codebase 已预留）：`space0` per-view → `space1` per-instance →
  **`space2` per-material**（见 `InstanceBindings.hlsl` 注释 "Per-material inputs
  must live in a HIGHER space than this"）。
- **存储：host per-frame + 每帧全量，与 InstanceBindingSystem 完全对称**（不用 device +
  dirty，理由见 §二开头）。

---

## 实现进度（截至 2026-07）

**已完成（编译通过，SparkEditor 可视验证）**：

- **§一 材质实体层** —— `Feature/Material/`（`SparkMaterial` 模块，`Spark::Material` 命名
  空间，与 `SparkMesh` **平级**：数据/实体层归 Material，Render 只做 GPU 绑定这一步）：
  - `MaterialHandle`（强类型 entt handle）+ `MaterialContext = BasicContext<MaterialHandle>`
    + `MaterialExecuteContext`（`MaterialSystem` Init 时 Push 一次）；
  - 组件：`MaterialParams`、`MaterialComponent{MaterialHandle}`（挂 world primitive）、
    `DefaultMaterialTag`；
  - `CreateMaterial(mc, params)` 自由函数（非工厂）、`GetDefaultMaterial(mc)`（查 tag）；
  - `MaterialSystem`（ISystem：持有 context、建默认材质并打 `DefaultMaterialTag`），在
    `Engine.cpp` RenderSystem **之前** 创建。
- **附录 C 编辑器反射** —— `Material/Reflect.h`（`MaterialParams` 字段反射 +
  `MaterialComponent` → `MaterialRefElement`）；`UIElement.h` 加 `MaterialRefElement`；
  ComponentView 抽出 `RenderFields`（字段循环复用）+ 加 `MaterialRefElement` 分支（跟随
  引用内联展开材质属性、无效则回退默认材质）+ 非 world 组件 guard（跳过没有 world
  `GetComponent` 的反射类型，如 `MaterialParams`）+ 组件子窗口改 `ImGuiChildFlags_AutoResizeY`
  （原按字段数估算固定高度，内联展开会溢出/触发 SetCursorPos assert）。
- **§二 GPU 绑定层（画面已生效）** —— Render 侧材质代码在 `Feature/Render/MaterialBind/`
  （include `<MaterialBind/...>`，与 SparkMaterial 的 `<Material/...>` 分开避免前缀撞车）：
  - `MaterialData`（GPU 镜像 struct，32B，`static_assert`）+ `MaterialData.hlsli` +
    `MaterialBindings.hlsl`（space2 `StructuredBuffer<MaterialData> g_Materials`）+
    `MaterialBindingsReflect.hlsl`（反射 host）；
  - `MaterialGPUSlot`（挂材质实体，每帧重写）、`MaterialBindingTag`（挂 space2 SRG 实体）；
  - `MaterialBindingSystem`（host per-frame `g_Materials`，每帧全量 scatter + `PendingBufferMap`，
    对称 InstanceBindingSystem 但无 ID buffer/slot table），RenderSystem 里 **在
    InstanceBindingSystem 之前** tick；
  - `InstanceData.m_materialIndex`（64→80B）；InstanceBindingSystem scatter 里解析
    `MaterialComponent`→handle→无效回退 `GetDefaultMaterial`→`MaterialGPUSlot`；
  - GBuffer.hlsl `#include MaterialBindings.hlsl`、VSOutput `nointerpolation uint materialIdx`、
    PS `GetMaterialData` 写 albedo/orm（occlusion 恒 1、**specular 暂不进 GBuffer**）；
    GBufferProcessor `AddShaderBindings<MaterialBindingTag>` 注入。
  - 验证：默认材质复现原外观；编辑器拖默认材质滑块，全体物体实时变（链路全通）。
- **per-object 私有材质（已验证）** —— `MaterialComponent` 开 `ComponentEventMask::Create`；
  `MaterialSystem` 兼 `ComponentEventBus::Handler`，`OnComponentConstruct` 里对 `m_material`
  为空的组件 `CreateMaterial`（拷默认材质当前参数）并回填。每物体独立材质，编辑互不影响；
  编辑器 `MaterialRefElement` 去掉默认回退（只显真实引用：有效"Material"+内联参数 / 无效
  "None"），悬空回退职责只留在 renderer（InstanceBindingSystem）。
- **材质 GC（mark-sweep，generation 版，已实现）** —— 见 §1.5。`MaterialSystem` 兼
  `TickBus::Handler`，`GetTickOrder = TICK_LAST + 1`（排在 `EntityReaper` 之后，死实体的
  `MaterialComponent` 已移除 → 当帧回收其私有材质）：
  - 标记位 = 挂材质实体的 `MaterialLiveMark{uint32_t m_gen}`（GC 内部组件），把"是否存活"
    变成整数比较，**零哈希**；首次 mark 才插入组件，之后退化成纯写值、不动 pool 结构；
  - `CollectGarbage`：`++m_gcGeneration` → **mark** 遍历 world `MaterialComponent` 对有效
    handle + pin 默认材质 `AddOrReplace<MaterialLiveMark>(gen)` → **sweep** 遍历
    `GetView<MaterialParams>()`，stamp 缺失/过期的收进 `eastl::vector`（非 set，全程无哈希），
    循环后一次性 `DestoryEntity`（先收集后销毁，避免迭代中改结构失效 view）；
  - 悬空引用 `Valid()` 过滤（渲染侧本就回退默认）；时序上 GC 在 `TICK_LAST+1`、
    `MaterialBindingSystem`（TICK_DEFAULT）本帧已消费完 → 下帧全量重扫，无悬空 GPU slot。
  - 调度：先**每帧扫**（材质量级小、扫描 O(实体+材质)），间隔/脏标记以后只动 `OnTick`
    调用点、不碰算法。已知边界：只被局部 `MaterialHandle` 持有、未塞进任何组件的材质会被
    回收——引擎内创建路径都同步回填组件，可接受。

**已定的设计点（补充/修正下文原稿）**：

- **AO → specular**：AO 是几何遮蔽（应来自贴图/烘焙，非材质标量常量），从 `MaterialParams`
  移除；换成 `m_specular`（电介质 F0 缩放，默认 0.5 → F0 0.04），在 GBuffer 阶段算进 F0，
  lighting 不受影响。
- **base color 贴图入 `MaterialParams`**：`m_baseColorTexture` 为 `Resource::AssetId`（作者
  引用；加载成 `Ptr<ImageAsset>` + 传 GPU 是阶段 2）。代价：`MaterialParams` 不再可裸
  memcpy → §二 scatter 逐字段填 GPU struct + 解析纹理 index（GPU `MaterialData` 本就与 CPU
  struct 不同布局）。
- **"未设置" vs "显式引用默认" 保持可区分（不写回默认 handle）**：`MaterialComponent` 未设置
  时保持 `NullMaterial`，消费侧（编辑器现在 / §二 GBuffer 之后）**实时解析回退默认材质**，
  不把默认 handle 预填进组件。理由：悬空引用兜底本来就是刚需（§1.5，真实材质被删 → 引用
  悬空 → 必须回退），预填默认属冗余、且会把"默认 handle"耦合进每条创建路径（含编辑器通用
  `construct()` + `AddOrReplace`，那条拿不到运行期 handle，需额外 construct 钩子）；区分性
  本身的收益（可切换默认 / 序列化 / prefab 继承覆盖）留待那些系统出现时。
- **默认材质发现机制**：`DefaultMaterialTag` 标记 + `GetDefaultMaterial(mc)` 查询，数据驱动、
  无工厂接口；§二 InstanceBindingSystem 解析 `materialIndex` 的回退复用同一机制。

**下一步候选（未定序）**：① 材质共享（picker——把一个实体的 material handle 拷到另一个实体，
asset-free 共享，验证 handle 复用 + GC 根扫描正确性）；② specular 进 GBuffer（需 F0 通道方案）；
③ 光源数据化（LightComponent + LightBindingSystem）；④ SceneColor 换 HDR（多光源前必须）；
⑤ 纹理材质（附录 A，契约已锁：MB 只消费解析后 RHIHandle + 组件 index）。

---

## 一、材质实体层（已确认，已实现）

核心思想：**材质数据本身是 ECS 里的一等公民（entity + component），不是某个 system
私有拥有的对象**。没有"材质工厂对象"，registry（这里是独立的 MaterialContext）本身就是
材质的容器。创建/修改材质是纯数据操作，任何 system（渲染同步、编辑器反射、序列化）都能
平等访问材质数据。

### 1.1 独立 MaterialContext

材质活在自己的 context 里，不和 world 混：

```cpp
enum class MaterialHandle : uint32_t {};
using MaterialContext = BasicContext<MaterialHandle>;
```

- 与现有 `WorldContext = BasicContext<Entity>` / `RHIContext = BasicContext<RHIHandle>`
  的双 context 模式一致——材质是第三类资源，配独立 context。
- **强类型 handle**：`MaterialHandle` 和 `Entity` / `RHIHandle` 编译期不混，一眼看出
  引用的是材质。
- context 内所有 entity 都是材质 → **不需要 `MaterialTag`** 来标识（归属、标识同时被
  独立 context 解决）。

### 1.2 组件

```cpp
struct MaterialParams {          // 材质的明文数据（编辑器反射/编辑的就是它）
    Vector4 m_baseColor;         // rgb(+a 备用)
    float   m_metallic;
    float   m_roughness;
    float   m_ao;
};
```

### 1.3 创建 = 纯数据操作

自由函数 helper（不是工厂类，就是"建 entity + 加 component"的便捷封装）：

```cpp
MaterialHandle CreateMaterial(MaterialContext& mc, const MaterialParams& p) {
    MaterialHandle h = mc.Create();
    mc.Add<MaterialParams>(h, p);
    return h;
}
```

编辑器"新建材质"、gameplay、场景加载都走这条：建 entity + 加组件，返回 handle。

### 1.4 引用与共享（挂在 primitive 实体上）

材质引用挂在 **primitive 实体**上。实体按 gltf **node + mesh + primitive** 组织，primitive
是几何 + 材质的最小单位，最终生效的材质就是 primitive 实体上的——所以**天然一 primitive 一
材质，根本不存在 "一个物体多材质 / multi-submesh" 的拆分难题**（它在实体拆分层面已经解决）。

```cpp
struct MaterialRef { MaterialHandle m_material {NullMaterial}; };
```

- `MaterialRef` 挂在带 `MeshGPUComponent` 的 primitive 实体上；InstanceBindingSystem 的散射
  循环（遍历 primitive 实体）顺手解析它、填 `materialIndex`（见 2.4），零额外结构；
- 跨 context 引用：world 的 primitive → MaterialContext 的 handle；handle 只是个 id，
  跨 context 持有没问题；
- 100 个 primitive 存同一个 `m_material` = 天然共享同一材质，改这份 `MaterialParams`
  它们一起变。

### 1.5 生命周期与悬空校验

不做 entity 引用计数（ECS 不常用）。材质 entity 销毁后，引用它的 `MaterialRef` 悬空，
在**消费侧兜底**：

```cpp
if (mc.IsValid(ref.m_material) && mc.Has<MaterialParams>(ref.m_material)) { 用 }
else { 回退默认材质 }
```

**为什么 valid 校验是安全的**：entt 的 entity 是 id + version。材质销毁后即使 id 被
新材质复用，version 也变了，旧 handle 对新材质 `valid()` 返回 false。所以 handle 天生
防 ABA——比裸指针还安全（裸指针检测不了 ABA）。

**回收机制：mark-sweep GC，不用引用计数（已实现）**。材质是运行期可变资源（编辑器在大量
不受控站点原地改写句柄），引用计数要求每个 inc/dec 站点都对，且 `MaterialHandle` 是裸的
版本化整数、无 RAII、`mc->m_material = h` 这种字段改写 entt 根本不发事件 → dec 抓不到；
而 mark-sweep 对"怎么引用的"无所谓，只扫此刻实际被引用的。对比：不可变共享资产（纹理/模型）
的 adopt/release 只在加载/卸载少数受控边界 → 用 asset refcount；材质则用扫描（这就是有
"material instance"、无"texture/model instance"的根因）。实现细节见上文实现进度的"材质 GC"
条目（generation 标记位、pin 默认、先收集后销毁、order = `TICK_LAST+1`）。

### 1.6 默认材质

校验失败（悬空）/ 未指定材质时的回退目标。引擎启动时在 MaterialContext 里 `CreateMaterial`
出一个常驻默认材质 handle。这让"删材质"永远不崩，引用者只是回落到默认外观。

### 1.7 Context 访问入口

材质 context 需要一个能被拿到的入口（创建、渲染同步、编辑器反射都要访问）。这**不是**
工厂接口——和 `WorldExecuteContext::Current()` / `RHIExecuteContext::Current()` 同性质，
是纯数据容器的访问入口。

**决定：ExecuteContext 模式** —— `MaterialExecuteContext::Current()`，与现有两个
context 完全对称（engine setup 时 Push 一次）。

---

## 二、GPU 绑定层（host per-frame，与 InstanceBindingSystem 完全对称）

**驱动方式：数据驱动，MaterialBindingSystem 是纯消费者，零对外接口**，和
InstanceBindingSystem 遍历 `<WorldTransformMatrix, MeshGPUComponent>` 写 GPU buffer
一套模式套两次。

**为什么是 host + 每帧全量，而不是 device + dirty**：材质是共享资源，数量远少于物体数
（材质种类几十~几百，vs instance 几万物体），全量才 KB 级——比 instance 每帧全量传的
MB 级小三四个数量级。instance 都能 host 每帧全量传，材质更没压力。device(VRAM) 唯一
的真实好处是 GBuffer PS 每像素高频读时延迟低，但 material buffer 只有 KB、几乎全进 GPU
cache、相邻像素常同材质（命中率极高），真正走 PCIe 的流量微乎其微 → device 的读速优势
在这里体现不出来，却要背上 staging / AsyncUpload / cross-queue handoff / dirty 追踪 /
稳定 slot 一整套复杂度，不划算。**host 完全够用，device + dirty 不采用**——材质是共享资源，
极难到"每帧全量传成瓶颈"的量级；即便真到那天再议，也不作为计划升级。

### 2.1 分层与缓冲

`g_Materials` : host `StructuredBuffer<MaterialData>` @ **space2**，**per-frame N 份**
（和 instance 一样，避免 GPU 读上帧时 CPU 写本帧的 in-flight race）。

```hlsl
struct MaterialData { float4 baseColor; float metallic; float roughness; float ao; float _pad; };
StructuredBuffer<MaterialData> g_Materials : register(t0, space2);
MaterialData GetMaterialData(uint idx) { return g_Materials[idx]; }
```

### 2.2 更新：每帧全量 scatter（复用 instance 的 host 上传机制）

材质数据小 + 数量少，每帧全量传 KB 级、微秒级，可忽略。所以**不做 dirty 追踪、不用
device/staging**，直接复用 instance 那套 host per-frame 上传：

- MaterialBindingSystem 每帧遍历材质 context → dense scatter 进 CPU 镜像 →
  `PendingBufferMap` 交给当前帧的 host buffer copy（RHIResourceSystem 做 map/memcpy/unmap）；
- CPU 镜像是 system 成员，活到 upload 完成（`PendingBufferMap` 契约，和
  `InstanceBindingSystem::m_instanceData` 一样）。

**因此不需要 `MaterialUpdateTag`、稳定 slot、freelist**——它们是 device+dirty 才需要的东西
（dirty 更新必须知道每条数据的固定位置）；host 每帧全量重传，slot 每帧重排也无所谓。

### 2.3 MaterialBindingSystem 遍历骨架（对称 instance 的 dense scatter）

```
每帧 Update：
  uint32_t slot = 0;
  遍历材质 context 的 <MaterialParams>：
    m_mirror[slot] = params;                              // dense scatter 进 CPU 镜像
    matCtx.AddOrReplace<MaterialGPUSlot>(h, {slot});      // slot = 迭代序号，每帧重写
    ++slot;
  若 slot > 0：PendingBufferMap(当前帧 g_Materials copy, m_mirror.data(), slot*stride);
```

`MaterialGPUSlot { uint32_t m_slot; }` 是 system 每帧产出的派生组件，挂在**材质 entity**
上（类比 instance 的 `InstanceSlotTable`：都是"当前帧这条数据在 GPU buffer 里的位置"）。
slot 每帧重排 → materialIndex 每帧由 InstanceBindingSystem 重新解析（见 2.4）。

**时序**：MaterialBindingSystem 必须在 InstanceBindingSystem **之前** tick（后者要读前者
写好的 `MaterialGPUSlot`）——RenderSystem 里排好顺序即可。

### 2.4 instance → material 的连接

`InstanceData` 加 `m_materialIndex`（注意对齐：`Matrix4X4` 64B + `uint` 需补齐到 16）：

```cpp
struct InstanceData {
    Matrix4X4 m_model;          // 64 B
    uint32_t  m_materialIndex;
    uint32_t  _pad[3];
};
```

InstanceBindingSystem 的散射循环（`InstanceBindingSystem.cpp` 填 `m_instanceData[slot]`
处）：renderable → `MaterialRef.m_material` →（1.5 的 valid 校验，失效回退默认材质）→
材质 entity 的**当前帧** `MaterialGPUSlot.m_slot` → 写 `InstanceData.m_materialIndex`。
**materialIndex 是运行期解析出来的，不是 renderable 存死的**（instance data 本来每帧全量
重填，顺手填 materialIndex，零额外成本）。

### 2.5 GBuffer shader

```hlsl
// #include <Shaders/MaterialBindings.hlsl>
// VSOutput 加：nointerpolation uint materialIdx : MATERIAL_INDEX;
// VS：output.materialIdx = inst.MaterialIndex;
// PS：
MaterialData mat = GetMaterialData(input.materialIdx);
output.albedo = float4(mat.baseColor.rgb, 1);
output.orm    = float4(mat.ao, mat.roughness, mat.metallic, 1);   // 替换硬编码
```

---

## 三、分阶段实施

1. **材质实体层**（第一章，可先独立验证，不掺 GPU）：MaterialContext + MaterialHandle
   + MaterialParams + 默认材质 + MaterialRef + valid 校验回退。验证：能建材质、被引用、
   编辑器反射 MaterialParams。
2. **GPU 绑定层**（第二章）：host per-frame g_Materials + MaterialBindingSystem 每帧
   scatter + MaterialGPUSlot + InstanceData.materialIndex + GBuffer 取值。验证：默认材质
   跑通，再手动加两个材质，物体显示不同 base color / roughness / metallic。
3. **纹理材质**（见附录 A）：先 Texture2DArray 起步（现有 RHI 零改动，材质存 array 层号），
   GBuffer PS `Sample`；bindless 作为"任意规格 / 海量纹理"的升级路径（需 RHI 先支持 bindless
   descriptor array）。这时 Sample vs Load 的区别用上。
4. **Material asset**：序列化、编辑器编辑、gltf 材质解析。

正交维度（不按阶段线性排，随需要引入）：**材质多态 / shading model** 见附录 B；
**编辑器集成（反射 MaterialParams + MaterialRefElement）** 见附录 C。

---

## 四、待定 / 未决

- `MaterialContext` 的持有者与 tick 位置（RenderSystem 里，且必须在 InstanceBindingSystem
  之前 tick，见 2.3 时序）。
- `InstanceData` 加 `m_materialIndex` 后的对齐（`Matrix4X4` 64B + `uint` + pad 到 16）。
- 默认材质的具体参数值（沿用当前硬编码：baseColor 0.8³ / metallic 0 / roughness 0.5 / ao 1）。
- 材质 context 的 host buffer 是否需要一个 ID / slot 上限（类比 InstanceBindingSystem 的
  `Capacity`）。
- **上层资产对接**（首版明确不做，阶段 4）：material asset 格式（种类 + 参数 + 纹理引用 +
  shading model）、加载器（asset → MaterialContext entity + component）、gltf primitive 的
  material → material entity → 回填该 primitive 的 `MaterialRef`、序列化。骨架已就位（primitive
  实体 + MaterialRef），是加法不是重构。
- **shading model id 的编码**（附录 B）：存 GBuffer 哪个通道、lighting 分支机制（首版恒 0）。
- **编辑器共享编辑语义**（附录 C）：内联编辑改的是共享材质、影响所有引用者；per-object 覆盖
  留待 material instance。

**已排除的伪缺口**：~~一个物体多材质 / multi-submesh~~ —— 实体按 gltf node+mesh+primitive
拆分，材质是 primitive 级，天然一对一，不存在此问题（见 1.4）。

---

## 五、被否决的设计（记录教训，勿重走）

- **MaterialBindingSystem 提供 `RegisterMaterial` / `UpdateMaterial` 接口** —— OOP 工厂
  思维，材质数据被 system 私有拥有，既不数据驱动、编辑器也反射不到。改为：材质是
  entity + component，system 只遍历消费。
- **device + dirty 存材质** —— 被"静态资源就该放 VRAM"的教条带偏。材质是共享资源、
  数量少（KB 级），host + 每帧全量传（像 instance 的矩阵）性能完全够；device(VRAM) 的高频读
  优势被"material buffer 极小、几乎全进 GPU cache、相邻像素同材质"抵消，却要背 staging /
  cross-queue / dirty 追踪 / 稳定 slot 一整套复杂度。**正解是 host per-frame + 对称 instance
  （§二），device + dirty 彻底不采用**——材质极难到"每帧全量传成瓶颈"的规模，不作为计划升级。
- **首版就引入 material handle 间接层（stable id → slot 映射）应对 slot 重排** —— 独立
  MaterialContext + entt version 的 handle 已提供稳定身份 + ABA 安全；host 每帧重排 slot 由
  `MaterialGPUSlot` 每帧重写 + materialIndex 每帧解析吸收，不需要额外的 id→slot 表。留到
  asset 阶段（材质可增删压缩）再说。

---

## 附录 A：纹理材质（阶段 2）—— 先 Texture2DArray，bindless 作升级路径

### A.0 解析流程、驻留与组件归属（定稿，约束首版契约）

首版没纹理，但这套契约现在就定死，保证阶段 2b 是**加法不是重构**。核心结论：

**(1) 三层驻留互相独立，GPU 驻留 ≠ 资产引用计数**

| 层 | 在哪 | owner | 驻留依据 |
|---|---|---|---|
| 磁盘 | 文件 | 文件系统 | 存在 |
| **RAM** | CPU（`ImageAssetData` 字节） | 资产管理器 | `Ptr<Asset>` refcount |
| **VRAM** | GPU（RHI::Image + SRV + 寻址） | **纹理 GPU 池** | **它自己的 GPU-usage 追踪** |

`Ptr<Asset>` 的 refcount 只表示"在 RAM"，**不能当 GPU 驻留依据**。VRAM 那层自己数"哪些纹理被活跃材质用到"；上传时临时 `LoadAsset` 拿 CPU 字节 → 上传 → 可释放 CPU 那份。

**(2) 引用驱动驻留（reference-driven residency）**：只有被**活着的材质引用到**的纹理才上传；引用归零可卸载。"用得到"靠引用判定，不枚举全库。可见性/mip 流式是很久以后的独立大工程。

**(3) 上传在 FrameBegin 前置、跨帧；MaterialBindingSystem 只引用已驻留资源**：资源创建/上传在 `OnFrameBegin`（deferred `PendingImageInit`/`PendingImageUpload`），MB 在 tick（FrameBegin 之后）跑。所以 MB **绝不发起上传**——这一帧发起的上传这一帧用不了。纹理就绪有 warmup 延迟，未就绪时解析回退默认（白图）。

**(4) 正确的解析边界是 `AssetId → RHIHandle（GPU 资源）`，不是 `AssetId → 标量 index`**：一个 AssetId 对应一个 **GPU 资源**（`RHI::Image` / 某 SubResource / 某 Tile）永远成立；对应"一个标量 index"在**流式 / 虚拟纹理**下会被打破（寻址变成 tile/page）。所以解析产出 **RHIHandle**（稳定锚点），寻址信息（array 层号 / bindless descriptor / tile 坐标）作为**挂在该 RHIHandle 上的组件**，随渲染模型演进只改那个组件，材质层与 MB 不动。

**(5) 解析在 MB 之前（resolver），MB 只消费解析后的 GPU 句柄**：`AssetId → RHIHandle` 由一个 resolver 做一次（材质纹理引用变化时），MB **永不碰 AssetId、永不查资产层表**。这与 InstanceBindingSystem 读 `Mesh::MeshGPUComponent`（RHIHandle）而非 model AssetId 逐字对齐——mesh 的 `AssetId → GPU buffer RHIHandle` 也是 MeshResolver 在 InstanceBindingSystem 之前做的。

**(6) `MaterialData` 仍在 MB 内打包（host 全量 scatter，§二）**：解析（→RHIHandle）在 MB 前；打包（params + 查到的 index → `MaterialData`）在 MB 内。因为 MB 本来就每帧遍历每个材质（dense slot 每帧重排 + 拼连续 buffer），内联打包零成本；提前缓存 `MaterialData` 反而要背"纹理 index 变化 → 反向失效引用材质"的跨实体 dirty 传播（正是 §二 否决的 device+dirty）。

**(7) 组件归属**（SparkMaterial 数据层**不含任何 RHIHandle**，全部 GPU 件在 Render，靠给材质实体加组件叠上去）：

| 组件 | 模块 | 内容 | 谁写 | 谁读 |
|---|---|---|---|---|
| `MaterialParams` | SparkMaterial | 标量 + `m_baseColorTexture`（AssetId，authored/可序列化） | 作者/编辑器 | resolver（拿 AssetId 去解析） |
| `MaterialGPUTextures`（名可换） | Render | 解析后的纹理 **RHIHandle**（定长内联、每槽一个，无堆分配） | 纹理 resolver | MaterialBindingSystem |
| `TextureIndex`（寻址） | Render | 挂在**纹理 RHIHandle（RHIContext 资源实体）**上：array 层号 / descriptor 号 / tile-page | 纹理 GPU 池 | MaterialBindingSystem |
| `MaterialGPUSlot` | Render | 本帧材质在 `g_Materials` 的 slot | MaterialBindingSystem | InstanceBindingSystem |

MB 的读法：`材质 → MaterialGPUTextures.m_baseColor(RHIHandle) → 该 RHIHandle 的 TextureIndex 组件 → MaterialData.texIndex`。**热路径全是 RHIHandle + 读组件，无 AssetId、无 map**。AssetId 只在 resolver 解析 + 纹理池去重（加载期）出现一次；且去重基本白送——AssetManager 已维护 `AssetId → Asset`。

**(8) 纹理 GPU 池是持久 registry，不是 tick 兄弟系统**：它有 FrameBegin materialize 职责 + 跨帧稳定 index 状态（性质像 RHIResourceSystem，不是每帧 scatter 的消费者）。slot 稳定性与材质数据**相反**——纹理 index 注册一次持久（append-only + freelist），材质 slot 每帧重排（见 A.5）。

**三层同构解析（无一层在热路径查 AssetId）**：

| 层 | 稳定引用（存哪） | GPU index（哪来） | 谁解析 |
|---|---|---|---|
| renderable → material | `MaterialComponent{MaterialHandle}` | material 实体的 `MaterialGPUSlot` | InstanceBindingSystem |
| material → texture | 材质的 `MaterialGPUTextures{RHIHandle}` | texture RHIHandle 的 `TextureIndex` | MaterialBindingSystem |

### B.1 三层对称结构

材质引用纹理，和 renderable 引用材质完全同构——每层都是"CPU 引用资源 + 运行期解析成
GPU index"：

> 组件与解析细节以 **A.0 为准**（本节是更早的概览，术语已被 A.0 收敛：`MaterialRef`→`MaterialComponent`；CPU 侧不再存 `Ptr<Image>`，而是 authored 的 `AssetId`（`MaterialParams`）+ 解析后的 `RHIHandle`（`MaterialGPUTextures`））。

| 层 | 稳定引用 | GPU index（运行期解析） | 全局池 |
|---|---|---|---|
| renderable → material | `MaterialComponent{MaterialHandle}` | `InstanceData.materialIndex` | material buffer |
| material → texture | `MaterialGPUTextures{RHIHandle...}` | `MaterialData.texIndex...`（读 RHIHandle 的 `TextureIndex`） | 纹理池（array 或 bindless heap） |

- CPU 侧材质：authored `AssetId`（`MaterialParams`）→ resolver 解析成 `RHIHandle`（`MaterialGPUTextures`，Render 层运行期组件）；
- GPU 侧 `MaterialData` 存纹理的 **index**，MB 从**解析后的 RHIHandle 上的 `TextureIndex` 组件**读，和 materialIndex 一个套路；
- 纹理上传是**纹理 GPU 池的职责**，材质只引用——没有"材质私有上传"，共享纹理只上传一次（A.0-(1)(2)(3)）。

### B.2 bindless 只限材质纹理，direct binding 共存

不是全系统纹理都 bindless。判断标准：**这张纹理需不需要"在一个不切 descriptor 的
GPU-driven draw 里，按运行期 index 从一堆纹理里动态选"？**

- 需要 → 材质纹理（每材质几张、按 material index 选）→ array / bindless；
- 不需要 → pass 固定读的几张（attachment SRV、后处理输入、UI）→ 现有 per-pass SRG
  direct binding（LightingPass 读 GBuffer 就是这条，不变）。

BindlessTextureSystem（若启用）是**材质子系统**的一部分，只管材质纹理，不是全局纹理管理器。
两条路径按需共存，正如 GPU-driven 间接绘制与直接绘制共存，不是二选一。

### B.3 起步方案：Texture2DArray（现有 RHI 零改动）

绕开"descriptor index"，改用"数据 index"：所有材质纹理打进一张 `Texture2DArray`，材质存
array **层号**（slice index）。

```hlsl
Texture2DArray g_Albedo : register(t?, space2);
float3 albedo = g_Albedo.Sample(g_Samp, float3(uv, mat.albedoSlice)).rgb;
```

- 一个 resource、一个 SRV，层是资源内维度，采样第三坐标选层 = 纯数据寻址（和 structured
  buffer 下标同性质）→ **不需要 bindless**，现有 per-pass SRG 绑定即可；
- GPU-driven 友好：draw 不切 descriptor，PS 用层号选纹理，和 material buffer + index 一致；
- 每种通道一个 array（`g_Albedo` / `g_Normal` / `g_MR` / `g_AO`），材质存 4 个层号。
- **硬限制**：一个 array 内所有层必须**同尺寸、同格式**（层数上限 D3D12 2048）。早期内容
  受控（如 albedo 统一 1024² RGBA8）完全够。

### B.4 为什么 Texture2DArray → bindless 是平滑升级，不是弯路

两版材质纹理架构几乎对称，只差 index 的含义和采样写法：

| | Texture2DArray（起步） | bindless（升级） |
|---|---|---|
| 资源 | 1 个 array，1 个 SRV | N 个纹理，N 个 descriptor |
| index 含义 | array 层号（数据 index） | descriptor 号 |
| shader | `g_Albedo.Sample(float3(uv,i))` | `g_Tex[i].Sample(uv)` |
| RHI 依赖 | 无 | bindless descriptor array |

升级时 `MaterialData.texIndex` 语义从"层号"变"descriptor 号"，shader 改采样一行，
材质层 / instance 层 / material buffer 全不动。所以先用 array 不浪费。

若纹理规格必须多样（进不了一个 array）而 bindless 又还没做，退回**传统 per-draw**：按材质
分组 draw、draw 前把该材质纹理绑到 per-pass SRG（现有机制）——不限规格，但放弃该 pass 的
GPU-driven 批处理（draw 数随材质数涨）。作为最后备选。

### B.5 bindless 是一块独立的 RHI 前置工作（升级时才立项）

bindless texture 不像 structured buffer 那样简单——区别在 index 索引的是 **descriptor**
（资源的 GPU 视图）而非数据，需要 descriptor indexing 硬件/API 支持。现有 RHI 是 per-pass
SRG 模型，bindless 需要额外扩展：

1. 持久、大容量的 bindless descriptor array/heap 抽象（跨帧稳定、slot append-only + freelist，
   不是 per-pass 每帧重编的小 set）；
2. PipelineLayout 支持 unbounded descriptor range（DX12 unbounded table / SM6.6
   `ResourceDescriptorHeap`；Vulkan `VK_EXT_descriptor_indexing`）；
3. 注册纹理 SRV 到 bindless slot 的 API；
4. shader 侧 unbounded array + `NonUniformResourceIndex` / `nonuniformEXT`。

跨后端（RHI 建模 Vulkan-strictness）：DX12 unbounded table / SM6.6 与 Vulkan
descriptor_indexing 要统一到一个抽象下，本身是不小的 RHI 设计工作。**结论**：bindless 是一个
独立立项的 RHI 任务，是"任意规格 / 海量纹理"时的升级；纹理材质**不必等它**，先 Texture2DArray
起步。

**注意（slot 稳定性相反）**：纹理 bindless slot 与材质 host 数据的 slot 稳定性正好相反——纹理
是 descriptor（注册一次持久），slot 必须**稳定**（append-only + freelist）；材质是 host data
buffer（每帧全量 scatter），slot **每帧重排**。`MaterialData` 里存的纹理 slot 是纹理的稳定
slot，别和材质自己的每帧 slot 搞混。

---

## 附录 B：材质多态与 shading model

### C.1 deferred 的硬约束（决定复杂度落在哪）

不管有多少种材质、参数多不同，**deferred 的 GBuffer 是固定 layout**。所以：

- **材质参数的多样性发生在 GBuffer pass（几何阶段）**：GBuffer PS 把各自种类的参数**算成
  固定的 GBuffer 通道**；
- **LightingPass 只看 GBuffer，看不到原始材质参数**——除非 GBuffer 里存一个 **shading
  model id**，lighting 按 id 分支（default-lit / subsurface / toon / ...）。这就是 UE 的
  `ShadingModelID`。

结论：**材质种类的复杂度集中在 GBuffer pass + shading model id，lighting 层几乎不受影响。**

### C.2 自定义材质的边界（GBuffer 前自由 / 后受限）

- **GBuffer 之前（材质 → GBuffer）：完全自由**。GBuffer PS 就是你的 shader，可自定义参数 +
  任意计算规则，只要输出到固定 GBuffer 通道（UE 的材质节点图就是编译成 GBuffer pass shader）；
- **GBuffer 之后（GBuffer → 光照）：受限**。lighting 只有 GBuffer + shading model id，
  自定义"全新光照模型"要么加引擎级 shading model（有限扩展），要么走 **forward pass**（材质和
  光照同一 shader、完全自定义，但不进 GBuffer）。业界（UE）是 hybrid：多数 deferred，特殊材质
  走 forward。

### C.3 存储骨架能用（ECS 正好擅长多态）

独立 MaterialContext / 材质是 entity / 三层对称**不用推翻**——不同材质种类 = 材质 entity 挂
不同 component（`MaterialParamsPBR` / `MaterialParamsToon` / ...）。三处按种类扩展：

| 层 | 单一种类（首版） | 多种类（扩展） |
|---|---|---|
| CPU 参数 component | 固定 `MaterialParams` | 按种类一族 component（ECS 多态） |
| GPU material buffer | 一个固定 `StructuredBuffer` | 每种一个 buffer（各自固定 struct），或 `ByteAddressBuffer` + 变长 offset |
| GBuffer shader | 单一 PS | 每种一个 GBuffer PS（按 PSO/材质分 draw）或 uber + type 分支；写 shading model id |

`MaterialRef` / 共享 / valid 校验 / lighting（只加一个 shading-model-id 分支）都不受影响。

### C.4 首版只需"不阻挡"多态

首版单一 PBR metallic-roughness：固定 `MaterialParams`、一个 material buffer、一个 GBuffer
shader、无 shading model id。设计只需不焊死：**GBuffer 预留一个 shading model id 字节（首版恒
0）**，material 参数是 ECS component（加新种类 = 加新 component + 新 GBuffer 分支），
`MaterialRef` 不关心种类。将来加卡通材质是**加法**不是重构。

---

## 附录 C：编辑器集成（反射 MaterialParams + MaterialRefElement）

### D.1 MaterialParams 是普通组件反射（现成 element 够用）

```cpp
context.Reflect<Render::MaterialParams>()
    .Type("Material")
    .Data<&MaterialParams::m_baseColor>("Base Color").Custom<Spark::ColorElement>(false)          // Vector4→ColorEdit4
    .Data<&MaterialParams::m_metallic>("Metallic").Custom<Spark::FloatSliderElement>(0.f,1.f,0.01f,false)
    .Data<&MaterialParams::m_roughness>("Roughness").Custom<Spark::FloatSliderElement>(0.f,1.f,0.01f,false)
    .Data<&MaterialParams::m_ao>("AO").Custom<Spark::FloatSliderElement>(0.f,1.f,0.01f,false)
    // 多态时：.Data<&...::m_shadingModel>("Shading Model").Custom<Spark::EnumElement>(false)
    ;
```

`baseColor`(Vector4)命中 `ColorElement`，标量用 `FloatSliderElement`，shading model 用
`EnumElement`——**全是现有 element，零新增**。材质属性**属于材质本身**（共享资源），所以它
显示在"检视 material" 时，不是检视 primitive 时。

### D.2 引用递归解析：`MaterialRefElement`（内联展开引用材质的属性）

primitive 上的 `MaterialComponent` 只存一个引用；要在它下面**内联看到/编辑材质属性**，需要
"跟随引用 + 递归渲染引用目标的反射字段"——现有 `AssetElement` 只画引用槽，做不到。方案：

**前置（使能能力）**：把 ComponentView 那套"遍历一个反射实例的字段、逐个 element if-else
渲染"抽成对任意实例可调的函数：

```cpp
void RenderFields(entt::meta_any& instance, float width);  // 组件渲染与引用展开共用
```

**新增 `MaterialRefElement`**（UIElement.h，类比 AssetElement）+ ComponentView 分支：

```cpp
.Data<&MaterialComponent::m_material>("Material").Custom<Spark::MaterialRefElement>(false)

// ComponentView：
else if (static_cast<MaterialRefElement*>(uiElement)) {
    if (MaterialHandle* h = fieldValue.try_cast<MaterialHandle>()) {
        auto& matCtx = MaterialExecuteContext::Current();          // 跨 context 解析
        // 引用槽（picker/拖放改 *h）
        if (matCtx.IsValid(*h) && matCtx.Has<MaterialParams>(*h)) {
            MaterialParams& p = matCtx.Get<MaterialParams>(*h);
            entt::meta_any inst = entt::forward_as_meta(p);
            RenderFields(inst, width);                              // 内联展开材质属性
        } else { /* None → 落默认材质 */ }
    }
}
```

### D.3 A（材质编辑器面板）/ B（primitive 内联）不冲突

`RenderFields(materialParamsInstance)` 渲染在哪个 ImGui 容器无关：塞当前窗口 = **内联(B，
首版)**；开材质编辑器面板对选中材质的 `MaterialParams` 调同一个 `RenderFields` = **独立
面板(A)**。解析 + 渲染共用，A/B 只是换容器，以后加材质编辑器是"换 window"不是重写。

### D.4 顺带落位的三个点

- **编辑实时生效、零额外机制**：`RenderFields` 的 `data.set` 写回 material entity 的
  `MaterialParams`；GPU 是 host per-frame 每帧全量 scatter → 下一帧自动反映，拖滑块实时可见。
  这是 host 方案又一个白送好处（不需要 dirty 通知）；
- **跨 context 用 `MaterialExecuteContext::Current()`**：之前定的访问入口正好用于"从 world 的
  MaterialComponent 跳到 MaterialContext 拿 MaterialParams"；
- **共享语义**：内联编辑改的是共享材质、影响所有引用者，UI 标提示；per-object 覆盖留待 material
  instance（复制成独立材质）。

### D.5 推广性

`MaterialRefElement` 本质是"**引用 + 递归渲染引用目标的反射字段**"的具体化。将来可泛化成通用
`EntityRefElement`（引用任意 entity + 展开指定组件）或 asset 版。首版**具体化成
`MaterialRefElement` 即可**，泛化留到有第二个用例时再抽，不过度设计。

---

## 附录 D：材质参数的定义方式（静态 vs 数据驱动）

回答"能不能自动根据资产定义的参数来定义 MaterialParams"。

### E.1 三档

| | 参数定义在哪 | 参数存储 | 反射 UI | shader | 复杂度 |
|---|---|---|---|---|---|
| **A. 静态（首版）** | 引擎 C++ struct | 固定 `MaterialParams` | entt 静态反射（附录 C） | 每种手写 | 最低 |
| **B. 半数据驱动** | 资产（name+type） | type-erased blob + registry | 运行期按参数定义生成 | 手写，按 offset 读 blob | 中 |
| **C. 全数据驱动（UE material）** | 资产 + shader graph | 动态 | 全动态 | graph 编译出来 | 极高 |

首版 A：`MaterialParams` 是编译期 struct，资产只能填已知字段的值，**新增材质种类 = 改引擎
（新 struct + 反射 + GBuffer shader）**。

### E.2 关键：C++ 不能运行时生成类型，数据驱动 ≠ 生成 struct

C++ 类型是**编译期**概念——没有任何机制能运行时凭描述生成一个 struct（模板要编译期已知；
entt meta / RTTR 只能反射已存在的类型，不能造新类型）。所以"根据资产生成 MaterialParams 类型"
**做不到**。

但数据驱动**根本不需要生成类型**——它绕开类型系统，用无类型字节 + 运行时布局描述 + 手动解释：

```cpp
uint8_t blob[MAX];   // 原始内存，不是任何"类型"
// registry（运行时数据）："baseColor"→{off=0,Vector4}, "roughness"→{off=16,Float}
*reinterpret_cast<Vector4*>(blob + 0) = color;      // 查 registry 拿 offset，手动 reinterpret
```

`blob` 永远是 `uint8_t[]`，"参数长什么样"不在 C++ 类型里，而在 registry（offset + type tag）。
所以 B 的复杂度本质是**放弃类型系统、手动模拟**（手动 offset / reinterpret / 运行时 type
检查 / 动态 UI / 自己和 shader 约定布局），不是"生成类型"的黑魔法。这也是 A 简单（借编译器）、
B 复杂（手动模拟）的根源。

### E.3 无堆分配落地（component 内不放动态容器）

ECS 大忌是 component 内 `eastl::vector` 之类堆分配。变长参数外置为定长载荷或索引：

- **形态一（固定 inline blob，材质少时首选）**：
  ```cpp
  struct MaterialParams { uint16_t m_typeId; uint8_t m_data[MAX_MATERIAL_PARAM_BYTES]; };
  ```
  定长 POD、零堆分配、数据在 component；代价是 MAX 浪费（材质少，KB 级无所谓）。最简。
- **形态二（component 存 offset，数据在集中池，规模大时）**：
  ```cpp
  struct MaterialParams { uint16_t m_typeId; uint32_t m_paramOffset; };
  ```
  两个整数、零堆分配；变长数据在集中池（可**就是 GPU material buffer 的 CPU 镜像**）。省内存，
  代价是池管理 + offset 间接。
- **布局元数据 = `MaterialTypeRegistry`**（不在 component 里）：typeId → 参数定义（name / type /
  offset / default），从资产或代码注册。反射 UI 遍历它动态生成控件；GPU 打包用同一份 offset。
  新增材质类型 = registry 加一条，不用新 C++ struct。

### E.4 blob 和 GPU 天然对称（数据驱动时反而更省）

GPU 侧 material buffer 本来就是纯字节、shader 按 offset 取值——GPU 从不认识类型。所以 CPU blob
和它**同构**（都是"字节 + 布局约定手动解释"），blob 不是妥协。而且数据驱动时**一份
`MaterialTypeRegistry` 布局同时定义 CPU blob 和 GPU buffer**，CPU blob 直接 memcpy 成 GPU 内容
（offset 一致）——消除了静态方案里"CPU struct ↔ GPU struct 两份布局手动对齐（std140/padding）"
的维护。

### E.5 结论

- **首版 + 相当长期用 A**：材质种类有限时，用 C++ struct 让编译器帮忙，比手动模拟字节省太多；
- **要"资产自由加参数、不改引擎" → B**（type-erased blob + registry），是中等独立工程，等真有
  需求（做给美术用的材质编辑器）时立项；连编辑器反射都不同（A 静态反射，B 动态 UI）；
- **要"美术自由定义 shader+参数" → C**（UE material），巨型系统；
- B/C 不是改 A，是**并行的另一条材质路径**；当前设计（材质是 entity、`MaterialParams` 是
  component、host 传输、`MaterialRef`）不阻挡以后并行加它——把 `MaterialParams` 从固定 struct
  换成 blob component 即往 B 走，骨架都在。
