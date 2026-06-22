# Binding 频率系统设计

> 记录 view / instance / material 等"频率绑定系统"的设计推演结论与**论证依据**,避免日后重推。
> 核心问题:如何把世界实体(WorldContext)上的数据转换成下游渲染(RHIContext)所需的 shader binding,并维护两者的生命周期关系。

---

## 1. 统一心智模型:extractor

MeshSystem、InstanceBindingSystem、ViewBindingSystem 本质是**同一类系统**:把世界实体的某类组件 *extract* 成"渲染部件",供 DrawRequest 组装。

| 系统 | 源组件 | 产出部件 |
|---|---|---|
| MeshSystem | `Mesh` | `MeshGPUComponent`(顶点/索引 GPU 资源) |
| InstanceBindingSystem | `WorldTransformMatrix` | per-instance shader binding(`g_Model` …) |
| ViewBindingSystem | 相机 | per-view shader binding(`g_ViewProjection`) |

**DrawRequest 组装 = 收集这些部件。** binding 系统不是特殊物种,和 MeshSystem 同构。每个被渲染的物体都隐含需要一个 view 输入,所以 view 也只是一个 shader 输入部件。

---

## 2. 频率分层

按"数据变化频率 / 来源频率"分 HLSL space,每个频率 = 一个 group = 一个 space:

- `space0` = **view**(最低频,一个 pass 基本一个 view)
- `space1` = **instance**(per-instance,如 model matrix)
- `space2+` = **material** 等(更高频)

---

## 3. 统一全集布局,不按 pass 裁剪(关键)

### 结论
每个频率提供一个**统一全集** schema(producer),consumer 各取所需;**producer 不感知下游**。

### 论证
- 担心点:不同 pass 需要不同子集(DepthPre 不要 normal matrix / bounds / prev matrix,GBuffer 要)。
- **错误推论**:"不同子集 → binding 系统按 pass 裁剪布局" → 把下游耦合引回(binding 系统要感知谁用什么)。
- **正解**:统一全集 struct,所有 pass 绑同一个,**pass 在 shader 里只读自己需要的字段**(UE GPUScene / `FPrimitiveSceneData` 模式)。
  - *不浪费*:绑定整个 struct 的成本 = 绑一个 buffer/descriptor,**与读多少字段无关**;pass 少读 = shader 少几条 load 指令,没有"绑定浪费"。
  - *不耦合*:producer 写全集,根本不知道谁读了哪几个字段;"用哪些字段"的决定权完全在 consumer shader。

> **口号:producer 提供能力(全集),consumer 决定用多少(读字段 / 选 group);producer 永不问 consumer 要什么。**

### 全集 vs 独立 group 判据(按成本,不按 pass)
- 便宜 + 通用(model matrix、object id、bounds、prev matrix)→ 进统一全集 struct。
- 昂贵 + 特定(蒙皮 palette,每 object 几百矩阵)→ 独立 group,谁需要谁挂。

### pass 差异落在两层,均不碰 binding 系统
- **字段级**(读哪些字段)→ shader 自选。
- **group 级**(挂不挂蒙皮 group)→ processor 组装 DrawRequest 时选(它本就在决定 push 哪些 binding)。

---

## 4. 传播方向:由"基数 + 形态"决定,不是全局二选一

三类部件、三种基数关系:

- **共享部件**(view,以后部分 material):不属于任何单个世界实体 → 独立实体被引用,create-once。
- **1:1 私有**(instance matrix):派生自单个世界实体 → 正向成立。
- **N:1 聚合**(合批 DrawRequest):多世界实体 → 一 draw,世界实体指向所属 batch → 正向自然(反向单 source 不够)。

---

## 5. 方向与数据形态绑定:两个一致组合(不可混搭)

**硬约束**:ShaderBindings 最终要进 RHIContext 被 compiler 扫描编译,所以它得在 RHIContext 有承载体。这把"方向"和"per-instance 数据做成什么"锁成两个一致组合:

### 组合 1(过渡 / 当前):每实例一个独立 ShaderBindings 实体
- 实体独立、有自己生命周期 → **只能反向**(binding 持 `SourceEntity`)对账解 destroy。
- 独立实体 + 正向 = **destroy 漏**:世界实体销毁,它身上的 ref 组件随之没,但独立 binding 实体仍留在 RHIContext。这正是最初 DepthPre 的漏洞。

### 组合 2(终态 / bindless):per-instance 数据是世界实体的组件
- 世界实体挂 `InstanceSlot { uint32 index }`,数据进全局共享 structured buffer,一个共享 binding 引用整段。
- **正向 + destroy 几乎免费**:实体销毁,`InstanceSlot` 组件随之没,只剩一个 slot 还给 freelist。
- 组装 = 读世界实体的 `MeshGPUComponent + InstanceSlot` + 共享 view/buffer binding —— 完全是"收集同一实体身上的组件"。

> 正向洞察的终点其实是组合 2。当前先走组合 1,但 `InstanceBindings` 的数据布局要按"**将来是 structured buffer 里的一条记录**"来设计,别绑死在"每实例一个 cbuffer 实体"。

---

## 6. CUD 三段分离

- **create**:正向。世界侧遍历可渲染实体,谓词满足 + 未标记 → 建。`GetView<...>(Exclude<MirroredTag>)` 让 create **只命中新实体**,判重免费。标记位是挂在世界实体上的**空 tag**,只表示"已镜像",不含对应关系(权威对应仍在资源侧)。
  - 反向无法做 create(遍历资源发现不了新实体),所以 **create 必须正向**。
- **update**:从源组件拉数据写进派生资源。
- **destroy**:按**源组件谓词**(见 §7),不按实体 valid。

---

## 7. 有效性判据:源组件存在性,不是实体 valid(关键)

资源 `R = extract(C)`,`C` 是它的源组件。

- **R 有效 ⟺ C 存在。**
- `Exists(C) ⟹ Valid(entity)`,反之不成立 → `Valid(entity)` 太宽松(实体还在但组件被单独移除时,会误判资源有效)。
- `!Valid(entity)` 只是"源组件消失"的一个**特例**(实体没了 = 它所有组件都没了);**按源组件判断是它的精确推广**。
- 不同派生资源有不同源组件,**有效边界独立**:实体保留 `Transform` 但移除 `Mesh` → 顶点资源失效、instance binding 仍有效。这恰恰证明"按实体"不准。

create 谓词与 destroy 判据**共用同一谓词**,两个方向:
- create:`谓词满足 && 未标记` → 建。
- destroy:`谓词不满足`(含实体销毁)→ 回收;**若实体仍在,移除其标记位**(否则它再次满足条件时无法被重新 create)。

---

## 8. 三层管线:extract / assemble / compile

整体数据流分三层,各层职责与生命周期不同:

```
extract  : 世界组件 ──正向, 源组件谓词──► 资源(顶点/索引、per-instance binding)
                                          [source 关系只在此层]
assemble : 资源 ──按“可渲染条件”create──► DrawRequest
                                          [retained, pass-无关, 资源句柄 + material 元信息]
compile  : DrawRequest ──每 pass、每帧──► DrawItem
                                          [transient, 资源句柄解引用 + PSO cache]
```

### 8.1 source 关系只在 extract 层
- **DrawRequest 不持有“我来自哪个世界实体”**;它引用的每个资源各自知道自己的源组件,link 下沉到资源层。
- 所以不需要 `DrawEntity`,也不需要每 pass 一个 `MatrixBindEntity`。
- 合批后一个 DrawRequest 可能对应多个世界实体,这种“无源 link”正好让它们天然隔离。

### 8.2 DrawRequest:retained,pass-无关
- **create 判据 = 可渲染条件**(组件谓词,如 `Has<MeshGPUComponent> && Has<WorldTransformMatrix>`),**不是**“某 pass 决定要画它”。这把 create 的耦合从“pass 意图”(真耦合)降级成“可渲染组件谓词”(和 extract 同源,无害)。
- **“渲染系统眼里的世界 = DrawRequest 集合”**;pass 的意图(剔除、选择)推到后面的 compile 步骤,不在 create。
- DrawRequest 携带让**任意 pass 能翻译**的 **material 元信息**(透明性、material 引用、双面…),**但不含 PSO**。例:DrawRequest 标自己透明,主场景 pass 自然用透明 PSO 去翻译它。
- **有效性**挂靠“它引用的资源是否都还 valid”——extract 层已把源组件谓词折叠进资源的存在性,所以 assemble 层看 `Valid(资源)` 不再宽松,也无需再碰源组件。
- **合批(N:1)**:create 从“映射”升级为“聚合”(相同 mesh/material 的实体合成一个 instanced DrawRequest),DrawRequest 仍只依赖(合批后的)资源,前向兼容。

### 8.3 DrawItem:transient,每帧重建
- 一个 DrawRequest → **多个 DrawItem(每 pass 一个)**,不再一一对应。
- DrawItem 是纯函数 `f(DrawRequest, pass)`:解引用资源句柄 + 从该 pass 的 PSO cache 取 PSO。**它不是 ECS 实体**,所以“每帧重建”**不涉及 entt `create`**——这与几轮前反对“每帧重建实体”并不矛盾(那次反对的是 create entity 串行 + 自包含实体复用;DrawItem 无状态、无实体)。
- **廉价/昂贵分层**:每帧只重做廉价组装(句柄拼装 + cache 查询);昂贵的(PSO 编译、binding compile)在 retained cache 复用。Bevy queue 每帧 + pipeline cache retained 同款。
- **可并行**:写预分配数组的不同 slot,无依赖。(“每帧重建可并行”在 DrawItem 上成立,因为它不碰实体创建。)
- **取舍**:UE 的 FMeshDrawCommand 缓存 static draw;每帧重建更简单。两者可统一——“每帧重建”是逻辑模型,`DrawCacheRef` 是其下的 memoization(DrawRequest+pass 未变则命中跳过)。**先每帧重建**,`DrawCacheRef` 留作 CPU 编译成瓶颈时的优化;终态 GPU-driven 把编译移到 GPU。

### 8.4 可见性
- per-frame 派生(相机每帧动)→ 天然 **transient**,属于 compile 层每帧重算,**不**进 retained 的 extract/谓词框架。
- 每帧流水线:**剔除 → 可见 DrawRequest → 编译 DrawItem**。

---

## 9. 系统按可预测性分级

- 数量**可预测**(View,现在一个)→ create-once + 每帧 update;destroy 退化为关闭时统一毁。
- 数量**不可预测**(Instance)→ 完整 CUD。

> 判据:能预测就 create-once,不能预测才上完整生命周期。后续 Material 等照此判断,不必所有频率系统都背全套。

---

## 10. 待定 / 遗留

- **DrawRequest 里 material 元信息的具体表达**:透明性 / material 引用怎么编码,pass 怎么据此从 cache 选 PSO。
- **destroy 的级联销毁执行机制**:判据已定(源组件谓词),但"谁执行"取决于形态——组件形态(随实体/组件消失,但 entt 不自动级联,需一个清理步骤)vs 独立实体形态(需反向找源组件)。
- **DrawItem 瞬态化的实现**:当前 DrawItem 是带 `PassTag` 持久化的组件,目标改为 compile 阶段产出的 per-pass 瞬态数组。

---

## 实现调整(从当前到目标)

- **DrawItem**:当前是带 `PassTag` 持久化在 RHIContext 的组件(pass 用 `GetView<PassTag, DrawItem>` 提交);目标是 compile 阶段产出的 **per-pass 瞬态数组**,pass Execute 直接遍历提交,DrawItem 不再挂实体。
- **DrawRequest**:仍是 retained 组件,但 create 判据迁移为"可渲染条件"(pass-无关),不再带 pass tag。
- 当前 DepthPre 的 `DrawEntity` / `MatrixBindEntity` 临时记账将被本设计取代。

---

## 当前实现状态(过渡)

- **ViewBindingSystem**:create-once 共享实体(`MainViewTag`),每帧 update;Shutdown 打 `DeadTag`。已落地。
- **InstanceBindingSystem**:待落地(组合 1:独立实体 + 反向 `SourceEntity` + 源组件谓词)。
- **InstanceBindings.hlsl**:`space1`,统一全集(当前仅 `g_Model`)。
