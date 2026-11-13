# 基于 Attachment 的资源声明与 Shader Binding 编译

状态：第一阶段已实现并通过用户运行验证，见§八；绑定位置的统一表示已落地，见§九；资源发布与共享导入已落地，见§十；attachment 的统一分组编译仍为设计草案。记录截至 2026-11-12 的设计共识与待定细节，设计代码片段仅用于说明结构，不代表最终 API。

本文是 [资源绑定模型](TODO_BindingModelPlan.md) 的后续设计讨论。原文记录的已完成改造仍是历史记录；本文的资源发布与 attachment 统一分组方案尚未实现。

## 一、问题与目标

`ReferencePathTracerFeature.cpp` 当前通过 `.Binds<...>()` 选择共享绑定组，又分别导入 TLAS、实例池、几何池、网格池、材质池等资源，在 Scope 中声明读取。

这些访问通常只有资源和 stage，没有描述实际使用的 View 和 shader 输入位置。View 和绑定关系由各个系统另外维护，读 Pass 代码时难以把两者对应起来。

目标是减少独立概念，并统一资源使用与绑定关系的记录方式：

- 系统拥有资源，在资源实体上发布其共享 shader 用法，包括 View 和绑定位置。
- Pass 显式声明访问，attachment 同时承载资源依赖和本次 shader 绑定关系。
- 普通资源通过显式指定 View、绑定位置形成 attachment；共享资源从自身发布的信息中取得这些默认值。
- 渲染图编译阶段收集这些记录，分组、生成或复用 `ShaderBindings`，统一编译并用于提交。
- 业务侧不再需要为了共享绑定而专门创建、查询和维护 ShaderBindings 实体。

## 二、已确定的设计方向

### D1：资源及其生命周期仍由所属系统管理

实例池、材质池、几何池等资源的创建、更新与销毁继续由原系统负责。渲染图使用已有资源身份登记访问。

不为共享资源额外建立一套由 `RenderGraphBuilder` 持有的 shader 引用注册表，不引入专门的构建编号和引用句柄体系。渲染图仍然保存本帧的 attachment 和必要的编译结果。

### D2：共享用法由资源实体上的组件发布

资源实体本身已经提供资源身份，组件描述它如何被提供给 shader：

```text
资源实体
  ├─ 实际资源及原有管理组件
  └─ 共享 shader 用法
       ├─ View 或对应的 AS 信息
       └─ shader 绑定位置
```

这份组件是用法描述，不因存在就自动给每个 Pass 添加访问，也不转移资源所有权。

外部通过共享 ECS 数据和独立查询方法取得描述，不新增 `InstanceBindingSystem::GetInstances()` 之类的系统访问接口。各系统维护描述与实际资源的一致性。

组件的名称、字段和查询形式尚未确定。此前讨论的 `ShaderBufferRef`、`InstanceShaderResources` 和独立绑定实体查询方案不作为实现前提。

### D3：Attachment 表达本次资源使用及绑定关系

attachment 归属于 Scope，也关联所属 Pass，描述该次访问：

```text
attachment
  ├─ 资源身份及版本选择
  ├─ View / AS 对象
  ├─ usage、access、stage
  └─ shader 绑定位置（存在 shader 绑定时）
```

普通资源显式填写 View 和绑定位置；共享资源使用发布的默认信息。两条路径生成同一种 attachment 记录，后续绑定编译不再按资源来源分为两条流程。

资源发布的是可用路径和默认信息；Read、Write、ReadWrite 由具体使用方声明，并检查与输入类型及 View 能力兼容。

### D4：ShaderBindings 由编译阶段统一生成

编译器根据本次使用所需的布局和 attachment 绑定内容创建或复用 `ShaderBindings` 对象。业务层独立的绑定实体不再作为共享机制的必需部分。

RHI 的 `ShaderBindings` 对象及其统一编译、提交能力仍然保留。生成对象如何持有、缓存和回收属于编译实现细节，尚未确定为某种 ECS 组件或额外实体。

### D5：资源共享与绑定结果复用分别表达

资源上的共享描述表示系统发布了一种公共用法。是否能复用同一个编译后的绑定对象，需要比较完整的组布局及绑定内容。

同一个资源使用不同 View、绑定到不同输入，可以形成不同的绑定结果；不同 Pass 使用相同布局及内容，可以复用结果。

共享不意味着把多个 Pass 的访问自动合并，也不意味着绑定了一个组就访问组内所有资源。

## 三、资源与绑定位置如何汇合到 Attachment

普通资源沿用显式声明思路：

```cpp
scope.ReadBuffer(buffer)
    .View(view)
    .Bind(input);
```

共享资源使用带有发布信息的 shader 资源声明入口。命名倾向按类型拆分：

```text
ReadShaderBuffer / WriteShaderBuffer / ReadWriteShaderBuffer
ReadShaderImage  / WriteShaderImage  / ReadWriteShaderImage
ReadShaderAccelerationStructure
```

参数如何定位资源、是否提供批量接口，留待后续决定。AS 的构建和更新仍通过 AS Build 访问声明，不设计 shader 写入 AS 的对应接口。

绑定位置已确定并实现为输入空间、输入名称和输入数组下标三个字段：

```cpp
struct ShaderInputBinding
{
    uint32_t m_spaceId = 0;
    RHI::InputName m_input;
    uint32_t m_arrayIndex = 0;
};
```

register、输入类型等尽量从 shader 布局解析，不要求调用方重复填写。仅有变量名或 space 均不足以在所有使用上下文中确定绑定位置。

`m_arrayIndex` 选择输入数组中的描述符元素，例如 `Texture2D g_Textures[4]` 中的 `g_Textures[1]`；它不表示 Buffer 内的数据下标，也不表示 Image 的 mip 或 layer。非数组输入使用下标 0。三个字段共同参与绑定位置比较。

同一个资源存在多个 View 或绑定用途时，需要允许多条用法描述。首版采用单条组件、集合还是其他 ECS 关系表达，尚未定案。

## 四、编译与分组思路

候选流程如下：

```text
系统在资源实体上发布默认 View 与绑定位置
                    ↓
Pass 声明访问，生成带绑定信息的 attachment
                    ↓
解析资源身份、版本和实际 View
                    ↓
在本次使用上下文内按 space 收集绑定内容
                    ↓
结合布局进行校验，查找可复用结果或创建 ShaderBindings
                    ↓
统一编译绑定，写入提交状态
```

### 4.1 分组边界

不能把整张图中相同 space 的所有 attachment 合成一个可变绑定组。不同 Pass 或 Scope 可以对相同输入位置提供不同内容。

按 `(Scope, space)` 收集是一种自然的候选方式，但是否同时调整现有 per-pass 内容一致性约束仍需决定。当前实现中，同一 Pass 的多个 Scope 共用 per-pass 绑定，并要求同一输入的值一致。

若支持不同 Scope 使用不同绑定内容，必须生成独立或不可变复用的结果，避免后面的填写覆盖已经用于前面提交记录的内容。

同一使用上下文内，相同输入位置重复指定相同 View 可以合并；指定不同 View 应报告冲突，不能采用最后一次覆盖的规则。

### 4.2 布局来源

attachment 描述输入应放入什么；shader 布局描述输入的位置、类型、数量以及组的组织方式。两者分别提供信息。

相同 space 不代表布局兼容。不同入口点的反射会裁掉未使用输入，不能直接把所有同 space 的组视为相同布局。

需要决定共享组继续采用稳定的完整布局声明，还是使用每个 Pass 的反射布局，以及如何判断兼容。布局获取应独立于已经创建好的 `ShaderBindings` 对象。

### 4.3 绑定结果复用

复用条件需要覆盖：

- 绑定组的布局兼容性。
- 各输入位置及数组元素对应的实际 View、AS 和描述符类型。
- sampler 状态、非根常量内容等其他组内绑定数据。

资源实体身份相同但实际 View 或当前 backing 不同，不能误判为相同结果。比较对象应是解析完成后的实际绑定内容。

Pass 身份以及 attachment 的 Read/Write 不必天然进入内容复用条件；SRV/UAV 路径及相关布局、描述符差异则必须体现。访问依赖仍由 attachment 单独表达。

缓存键、相等比较、首版是否仅限帧内复用，以及在途 GPU 数据的生命周期管理尚待确定。

## 五、Sampler、Constant 与 Bindless

Sampler 和 constant 同样属于绑定内容，但通常不产生图内资源访问依赖。可以继续由 Scope 或系统公开的数据组件提供，收集时与 attachment 的资源绑定一起组成完整组。

不为凑齐绑定输入而给 sampler、constant 制造虚假的资源 attachment。Scope 根常量继续保留自己的提交路径，不必转成描述符组。

现有系统绑定实体还承载场景常量和 sampler 等数据。移除实体前，需要为这些数据确定公开来源和更新方式，不能只迁移 Buffer/Image。

Bindless 的 `.BindIndex` 路径需要保留：它记录将某个 View 的索引传到哪个常量，与直接向某个资源输入写描述符有所区别。动态索引背后的资源集合和上传同步也不能仅凭一个 shader 变量推导出来。

这些路径如何统一表示，属于后续细节，不要求首版引入一个覆盖所有情况的大型联合类型。

## 六、与当前代码的连接点

下表记录第一阶段改造前的连接点及后续目标；第一阶段的具体调整见§八。

| 当前位置 | 现有职责 | 本方案涉及的调整 |
|---|---|---|
| `Pass/Component/RHIComponents.h`：`ImagePassAttachment` / `BufferPassAttachment` | 保存资源、View、usage、access、stage | 继续作为访问记录 |
| 同文件：`ShaderInputBinding` | attachment 上的 per-pass 输入名称 | 扩展为完整绑定位置，统一处理普通与共享资源 |
| `RenderGraphBuilder::BindShaderInput` | 校验输入，限制为 per-pass space，登记绑定 | 调整空间限制与校验方式；共享声明最终登记相同绑定信息 |
| `RenderGraphCompiler::CompileScopeBindings` | 遍历 attachment，把 View 填入预先创建的 per-pass 绑定 | 改为收集、分组、校验，并生成或复用绑定对象 |
| `CompileScopeState` | 拼接 per-pass 与 `.Binds<...>()` 解析出的共享绑定 | 引用统一生成的本次绑定结果 |
| `CompileShaderInputs` | 遍历带更新标记的 ShaderBindings 实体并编译 | 调整为编译统一收集到的对象，保持 RHI 编译入口 |
| `BuildPassPipelineLayout` | 从已有共享 ShaderBindings 读取组布局 | 拆开布局信息与绑定对象之间的依赖 |
| `CreatePassBindings` | 提前创建绑定对象及专用实体 | 创建时机与对象持有方式随统一编译流程调整 |
| 各 BindingSystem | 创建共享绑定实体、设置 View 和其他数据 | 改为发布资源用法及必要的其他绑定数据 |

当前共享组布局将 stage 扩展为 Pass 的全部 shader 阶段，并不代表每个输入的精确使用阶段。若希望自动推导访问 stage，需要保留逐输入的原始反射使用信息；此前仍可显式声明，不能无依据缩小同步范围。

## 七、待逐项讨论的细节

| 编号 | 问题 | 需要明确的内容 |
|---|---|---|
| Q1 | 资源实体上的共享用法组件 | Buffer、Image、AS 各保存什么；实际 View 还是 View 描述；单条还是多条用法 |
| Q2 | 绑定位置的表示 | 已实现 space + 输入名称 + arrayIndex；按空间、资源种类和反射数量校验，当前写入范围仍限于 per-pass space，见§九 |
| Q3 | 共享资源声明入口 | 参数与查询方式、Read/Write 接口、默认用法能否显式覆盖 |
| Q4 | 布局权威与兼容性 | 完整共享组布局和每个 Pass 的反射子集如何处理；避免布局与绑定对象循环依赖 |
| Q5 | 收集粒度 | 首版保持 per-pass 一致性，还是支持逐 Scope 的描述符内容快照 |
| Q6 | 编译对象的共享与生命周期 | 复用键、内容比较、帧内/跨帧缓存、GPU 在途数据回收 |
| Q7 | 非资源输入 | 系统公开的常量、sampler 如何迁移；根常量与 bindless 索引如何接入 |
| Q8 | 可用性与默认值 | 未就绪资源、缺失输入、显式空描述符或 fallback；禁止继承无意留下的旧绑定 |
| Q9 | 上传与同步 | `.Binds<...>()` 附带的 `BindingTag::Uses` 如何迁移，尤其是动态 bindless 材质纹理等未逐个命名的资源 |
| Q10 | 多次访问与覆盖 | 同资源多输入、多 View；输入位置冲突；描述快照和资源更新的一致性 |

`BindingTag::Uses` 当前主要处理匹配标签资源的上传 acquire，并非完整的图内访问声明。移除 `.Binds<...>()` 时必须保留所需上传同步能力，不能只删除绑定选择部分。

SRV/UAV 是访问路径能力，Read/Write/ReadWrite 是本次访问契约；读取 UAV 仍需使用相应的 UAV 路径。部分写入是否需要保留前一版本，继续遵循图的现有资源版本语义。

Q2 的绑定位置已先行落地；后续从 Q1 的资源发布组件继续讨论，再推进共享资源迁移与统一编译。

## 八、第一阶段：解除 RenderGraph 对绑定实体的依赖

状态：已完成，用户确认运行验证无问题（2026-11-10）。

本阶段调整对象持有、填值和编译入口，保持现有绑定布局、`.Binds<...>()`、上传同步及 per-pass/Scope 内容约束。

- `PassBindings` 直接持有 `Ptr<RHI::ShaderBindings>`，不再为每个 Pass 创建专用的 RHI 绑定实体。
- RenderGraph 填值直接操作绑定对象；现有系统的实体版填值函数保留为过渡包装。
- `.Binds<...>()` 暂时仍通过适配回调从旧系统取得对象；图的编译和提交不解析绑定实体。
- `ScopeState` 保留固定容量的非 owning 对象指针数组，Pass 或系统保证对象覆盖本次使用的生命周期。
- 编译阶段遍历 `ScopeState`，用函数内临时 `eastl::vector` 记录已成功编译的对象地址。重复对象跳过，下一次编译调用重新建立记录。
- 不以实体 dirty tag 决定是否编译，不给 RHI 对象增加 dirty、批次号或其他调度状态。原有 RHI 对象和编译接口保持不变。
- 对本次引用的不同对象分别编译，不在此阶段比较内容、构造复用缓存；没有被 ScopeState 引用的对象不编译。
- Pass 组件负责持有绑定对象；关闭流程显式释放 per-pass 绑定，保证其持有的 View 在渲染器资源池关闭前释放。

临时 vector 是过渡妥协，不作为最终共享机制。当前按对象地址线性查找，最坏比较次数仍为引用数与不同对象数的乘积，但不再反复遍历 ECS 视图。后续根据 attachment 统一生成对象、明确对象持有位置后，再调整这部分。

所有绑定内容先填写完，再统一编译并提交。本阶段不支持在一次编译遍历期间修改同一个对象，也不改变后端 GPU 描述符的在途生命周期规则。

### 过渡接口与后续移除条件

`ReleasePassBindings(PassContext&)` 是本阶段的过渡接口。当前 PassContext 直接持有绑定对象，生命周期可能长于 RenderGraph 的资源池，因此 RenderSystem 和相关示例在关闭流程中显式释放这些对象，提前解除对 View 的持有。

后续根据 attachment 生成绑定对象时，由 RenderGraph 统一持有、复用和回收编译结果；PassContext 保存声明信息。绑定结果及其 View 引用在资源池关闭前由统一关闭流程释放，届时移除 `PassBindings` 的对象持有职责、`ReleasePassBindings` 及各 Feature 的手工调用。

`.Binds<...>()` 的共享实体适配回调、实体版 `SetShader*` 包装和临时编译去重 vector 也保留为过渡部分，随着资源发布与统一绑定编译的落地逐项替换。

### 验证记录

- Debug 编译通过：`SparkRenderTest`、`ReferencePathTracer`、`TrianglePass`、`MSAAPass`、`DrawCube`、`MultiView`、`ComputePass`。
- Render 测试 207 项通过，其中新增 3 项覆盖 Pass 直接持有绑定对象、不同 PassContext 独立释放，以及最后一个 Pass 释放共享对象。
- 用户完成运行检查并确认没有问题；运行和画面验证由用户负责。
- RHI 对象及其编译接口未修改，未增加 dirty 或编译批次状态。

## 九、绑定位置的统一表示

本步骤只落实已确定的输入定位方式及资源输入数组支持，不迁移共享资源，也不开放向已有共享绑定组写入。

- attachment 上的 `ShaderInputBinding` 保存 `m_spaceId`、`m_input`、`m_arrayIndex`。
- `.Bind(name)` 保持原有行为，选择 per-pass space 的第 0 个元素。
- `.Bind(name, arrayIndex)` 可选择 per-pass 输入数组中的元素。
- `.Bind(ShaderInputBinding{ spaceId, name, arrayIndex })` 接受完整位置描述；当前仍检查 space 必须为 per-pass，后续再开放共享组。
- builder 在指定 space 的输入组内按资源种类和名称查找输入，校验数组范围，并保留访问类型与 stage 的现有校验。
- 编译时将 arrayIndex 传入 Buffer、Image、AS 的填值接口；跨 Scope 的内容一致性和已绑定记录按完整位置比较，不同元素可以保存不同值。
- 对由图填写的 per-pass 绑定逐元素清理本帧未绑定的资源输入，避免上帧某个数组元素残留。没有声明填值的 Pass 继续保留现有外部填值行为。
- sampler、constant、bindless 索引和历史有效标记的公开接口保持原样；它们沿用原来的编译路径。
- RHI 对象、shader、共享系统和已有绑定组布局均未修改。

调用形式：

```cpp
scope.ReadImage(image).Bind(RHI::InputName("g_Textures"), 1);
scope.ReadImage(image).Bind(ShaderInputBinding{
    kPerPassSpaceId, RHI::InputName("g_Textures"), 1 });
```

此步骤按要求仅做编译检查。Debug 编译通过：`SparkRenderTest`、`ReferencePathTracer`、`ComputePass`、`TrianglePass`、`MSAAPass`、`DrawCube`、`MultiView`、`RayTracingScene`、`IndirectDispatch`。未运行测试程序或示例，运行和画面验证由用户负责。

## 十、资源发布与共享导入

状态：已实现并通过用户运行验证（2026-11-12）。`.Binds<...>()` 未改动，布局仍由它提供。

- 资源实体发布自己的 shader 用法：`ShaderAccelerationStructureUsage`（`Shader/RayTracing` 侧的发布点）与 `ShaderBufferUsage` / `ShaderImageUsage`，同级保存 `ShaderInputBinding`（space、输入名、arrayIndex）；Buffer 与 Image 另存**默认视图描述符**。对象与物化视图不发布，填值时从资源自身组件解析。
- `RenderPassScopes::ImportShared<Tag>(name)` 声明“这个资源按它发布的用法绑定”，与 `Import<Tag>` 对称，返回同样表示“资源是否就绪”的 `bool`。图中以**资源身份**记录意图，不以名字；一个 Pass 内同一资源只允许一种意图，混用即断言。
- `ShaderAttachment` 构造时经中性查询 `IsImportedAsShared` 判断，命中才由 `BindSharedShaderInput` 把发布的位置、视图或对象写到这次访问上。普通访问不进入该路径，也不承担任何共享语义。
- 首次消费者：`ShadowProjectionPass` 的 TLAS（space 7）、`GBufferPass` 的实例池（space 4）。`DeviceArray` 初始化时发布自己的结构化视图，Instance / Geometry / Material 三个数组因此具备被消费的条件。
- 编译期按 space 找到目标共享组并填值；per-pass 与共享在填值上走同一条代码，只是目标对象不同。
- `RayTracingBindingSystem::Update` 的填值随之删除：`g_RayTracingScene` 现在只由图在编译期写入。

### 待办：无人声明时的陈旧值

删掉系统侧填值之后，`g_RayTracingScene` 只剩图一个写入者。于是出现一个新的状态问题：TLAS 未构建的帧（`RayTracingSceneTag` 不出现）`ShadowProjectionPass` 不声明该访问，没有人写这个输入，共享组保留上一帧的 AS 指针。

当前**接受**这一行为：AS 对象随资源实体存活到 Shutdown，不会悬空；shader 由 `traceShadows` 常量门控，不会读到它。`g_Instances` 等后续迁移的输入会遇到同一问题（`DeviceArray::Shutdown` 目前显式写 null）。

若将来需要清除，候选方案：

1. 让 Pass 每帧都声明该访问，无对象时绑定空——需要“发布存在但对象未就绪”的显式路径，当前的初始化断言不允许；
2. 编译器记录本帧写过的共享输入，未写过的清空——需要给共享组加每帧的已写标记，机制变大。

### 未完成

- `.Binds<RayTracingSceneBindingTag>()`、`.Binds<InstanceBindingTag>()` 仍保留：前者提供 space 7 的布局，后者提供 space 4 及组内其他输入。摘除它们需要先解决“没有 tag 时布局从哪来”。
- `InstanceBindingSystem` 等仍在向组内填值（与图的填值相同）。逐项删除是后续步骤。
- `SceneBindingSystem` 已改为发布 buffer 与 image、由 Pass 显式访问，`MainSceneTag::Uses` 随之清空；但 `.Binds<MainSceneTag>()` 仍提供 space 0 的布局，Scene 的五张图与三张 buffer 的上传承接目前依赖“每帧都有 Pass 读取”这一惯例。

### 上传承接与 Uses 的退役

`ScopeUses`（`BindingTag::Uses` → `TakeUploads`）承担两件事，新模型的覆盖情况不同：

- **用途固定**（`Tag::Access`）：已被替代，且更准确。acquire 的目标状态现在来自本次访问的 access 与 stage，而不是 tag 上写死的常量。
- **承接上传**：对**被显式访问**的资源等价。`CompileImageAccess` / `CompileBufferAccess` 调 `FindUploadsToAcquire`，最终与 Uses 走同一个 `AcquireUpload`（写 `QueueTransfer` 的 dst、记 `ScopeUpload`），障碍由执行器按 `ScopeUpload` 统一发 `QueueAcquire`。
- **跨队列等待**：Uses 在 `CompileScopeUploads` 里为“第二个队列的 scope 也想承接同一上传”补 `RecordCrossQueueWait`。显式访问不需要这条特例——消费者有 attachment 时，通用状态跟踪会生成 release/acquire 对并调用 `RecordCrossQueueWait`。前提是**任何跨队列的消费者都声明了 attachment**；Uses 承接但无 attachment 的资源与显式访问混用会缺少 producer。
- **丢失的保证**：Uses 保证“绑了组就 acquire”，显式访问只保证“被访问才 acquire”。上传完成却无人访问的资源会停在 `PendingAcquires`，而上传系统会因此拒绝它的下一次上传（`AsyncUploadSystem` 的 “A buffer is uploaded once”）。目前依赖“每帧都有人读”这一惯例。待办：帧末断言无残留 pending acquire，或在帧末做一次兜底承接。

两条路并存会**重复 acquire**：Uses 只安排障碍、不回写镜像状态，显式访问随后按 `GetSubresourceStates()` 播种，于是发出前置状态为 COMMON 的第二次转换，触发 `INCOMPATIBLE_BARRIER_LAYOUT`。因此迁移完成的组必须清空 `Uses`（`MainSceneTag` 已清空）。

上传链路的既有事实（两条路共用）：拷贝在 Copy 队列完成后，`AsyncUploadSystem::FinishUploads` 在帧首用上传 fence 的值把关，只有已完成的上传才转为待 acquire；因此 acquire 不需要队列侧的等待，跨队列顺序由 fence 与提交顺序保证。

**剩余的 Uses**：`MaterialBindingTag::Uses = TagList<MaterialTextureTag>`。材质纹理按 GPU 数据动态索引、数量不定、没有逐资源的访问可声明，无法走 attachment。可选方向（未定）：

1. 材质纹理改在图形队列上传，取消跨队列交接，从而不需要 acquire；
2. 保留组级承接，但由发布驱动（按 space 收集发布资源），而不是手写 tag 列表；
3. 收窄并改名（例如 `MaterialTextureAcquire`），明确它只服务动态资源。

在方向确定前，不要给能被显式访问的资源再添加 `Uses`。

## 十一、方案边界

- 第一阶段仅解除图对绑定实体的依赖；资源上的共享用法组件及 attachment 的统一绑定分组仍待逐项设计。
- 不新增可供外部直接调用资源系统对象的访问接口。
- 不通过导入或发布资源隐式声明整组访问。
- 不以移除专用 ECS 实体为由移除 RHI 绑定对象或必要的布局信息。
- 旧方案的 per-pass/Scope 参数约束、View 重放和根常量行为，只有在后续明确决定时才调整。

## 关联文档

- [资源绑定模型](TODO_BindingModelPlan.md)：现有分层及已完成改造。
- [RenderGraph Item 方案](TODO_RenderGraphItemPlan.md)：Pass、Scope、attachment 与提交关系。
- [RenderGraph Resolve 方案](TODO_RenderGraphResolvePlan.md)：资源记录、链接、版本解析与 `.From` 语义。
- [上传交接方案](TODO_UploadHandoffPlan.md)：上传资源的 acquire 与访问声明。
