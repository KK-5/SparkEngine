# 基于 Attachment 的资源声明与 Shader Binding 编译

状态：第一阶段已实现并通过用户运行验证，见§八；绑定位置的统一表示已落地，见§九；资源发布与共享导入已落地，见§十；布局与 per-scope 绑定改由 space 推导、六个绑定 tag 与 `.Binds<>` 已全部退役，见§十；`ImportSharedSet` 已接入材质纹理与 UI 图标，`Uses` 路径整体删除，见§十一；attachment 的统一分组编译仍为设计草案。记录截至 2026-11-16 的设计共识与待定细节，设计代码片段仅用于说明结构，不代表最终 API。

本文是 [资源绑定模型](TODO_BindingModelPlan.md) 的后续设计讨论。原文记录的已完成改造仍是历史记录；本文已落地的资源发布见§十，attachment 统一分组仍为后续设计。

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

状态：资源发布已实现并通过用户运行验证（2026-11-12）。布局与 per-scope 的绑定对象按 space 推导，`.Binds<>` 已整体退役（见本节末）。

- 资源实体发布自己的 shader 用法：`ShaderAccelerationStructureUsage`（`Shader/RayTracing` 侧的发布点）与 `ShaderBufferUsage` / `ShaderImageUsage`，同级保存 `ShaderInputBinding`（space、输入名、arrayIndex）；Buffer 与 Image 另存**默认视图描述符**。对象与物化视图不发布，填值时从资源自身组件解析。
- `RenderPassScopes::ImportShared<Tag>(name)` 声明“这个资源按它发布的用法绑定”，与 `Import<Tag>` 对称，返回同样表示“资源是否就绪”的 `bool`。图中以**资源身份**记录意图，不以名字；一个 Pass 内同一资源只允许一种意图，混用即断言。
- `ShaderAttachment` 构造时经中性查询 `IsImportedAsShared` 判断，命中才应用发布的位置、视图或对象。入口原为 `BindSharedShaderInput`，§十一第一步改名为 `ApplySharedShaderUsage`。普通访问不进入该路径，也不承担任何共享语义。
- 首次消费者：`ShadowProjectionPass` 的 TLAS（space 7）、`GBufferPass` 的实例池（space 4）。`DeviceArray` 初始化时发布自己的结构化视图，Instance / Geometry / Material 三个数组因此具备被消费的条件。
- 编译期按 space 找到目标共享组并填值；per-pass 与共享在填值上走同一条代码，只是目标对象不同。
- `RayTracingBindingSystem::Update` 的填值随之删除：`g_RayTracingScene` 现在只由图在编译期写入。

### 待办：无人声明时的陈旧值

删掉系统侧填值之后，`g_RayTracingScene` 只剩图一个写入者。于是出现一个新的状态问题：TLAS 未构建的帧（`RayTracingSceneTag` 不出现）`ShadowProjectionPass` 不声明该访问，没有人写这个输入，共享组保留上一帧的 AS 指针。

当前**接受**这一行为：AS 对象随资源实体存活到 Shutdown，不会悬空；shader 由 `traceShadows` 常量门控，不会读到它。`g_Instances` 等后续迁移的输入会遇到同一问题（`DeviceArray::Shutdown` 目前显式写 null）。

若将来需要清除，候选方案：

1. 让 Pass 每帧都声明该访问，无对象时绑定空——需要“发布存在但对象未就绪”的显式路径，当前的初始化断言不允许；
2. 编译器记录本帧写过的共享输入，未写过的清空——需要给共享组加每帧的已写标记，机制变大。

### 按 space 推导与 `.Binds<>` 的退役（已完成）

状态：按 space 推导的两半于 2026-11-13 落地；2026-11-16 六个绑定 tag 全部退役，`.Binds<>` 本身随之删除。

- Pass 用到哪些共享 space，由 **shader 反射**决定。`BuildPassPipelineLayout` 对反射出现的每个共享 space 取对应组，把该 space 的完整布局并入 PSO layout，一个 space 只并一次。
- `CompileScopeState` 按 **PSO layout 里的每个共享 space** 取组对象放进 ScopeState，跳过 per-pass space 与根常量空间。这一条覆盖了“只用常量、没有任何资源访问”的 Pass（`SkyboxPass` 只用 `g_EnvIntensity`）：若只按“谁填过数据”推导，它的 space 0 不会被绑定，CBV 也就设不上。
- `FindSharedBindings(RHIContext&, spaceId)` 是 space → 组对象的唯一入口；发布填值、组值填值、Pass 布局、ScopeState 四处共用。将来组对象归图持有时，只改这一处。
- 反射出 space 但没有组时不做处理，仍由 `TakeReflected` 报 `"reads '{}' in space{}, but binds no group there."`，错误信息不退化。

**删除清单**：`GeometryBindingTag`、`InstanceBindingTag`、`MaterialBindingTag`、`MainSceneTag`、`ViewBindingTag`（连同各系统给组实体打的 tag）、`RayTracingSceneBindingTag`；`RenderPassBuilder::Binds<>` / `ComputePassBuilder::Binds<>`、`PassCapabilities::m_resolveSharedBindings`、`ResolveSharedBinding(s)`、`CompileScopeState` 里的解析调用，以及 `BuildPassPipelineLayout` 的 `groups` 参数。现在没有任何地方需要按 tag 找组。

### 上传承接与 Uses 的退役


`ScopeUses`（`BindingTag::Uses` → `TakeUploads`）承担两件事，新模型的覆盖情况不同：

- **用途固定**（`Tag::Access`）：已被替代，且更准确。acquire 的目标状态现在来自本次访问的 access 与 stage，而不是 tag 上写死的常量。
- **承接上传**：对**被显式访问**的资源等价。`CompileImageAccess` / `CompileBufferAccess` 调 `FindUploadsToAcquire`，最终与 Uses 走同一个 `AcquireUpload`（写 `QueueTransfer` 的 dst、记 `ScopeUpload`），障碍由执行器按 `ScopeUpload` 统一发 `QueueAcquire`。
- **跨队列等待**：Uses 在 `CompileScopeUploads` 里为“第二个队列的 scope 也想承接同一上传”补 `RecordCrossQueueWait`。显式访问不需要这条特例——消费者有 attachment 时，通用状态跟踪会生成 release/acquire 对并调用 `RecordCrossQueueWait`。前提是**任何跨队列的消费者都声明了 attachment**；Uses 承接但无 attachment 的资源与显式访问混用会缺少 producer。
- **丢失的保证**：Uses 保证“绑了组就 acquire”，显式访问只保证“被访问才 acquire”。上传完成却无人访问的资源会停在 `PendingAcquires`，而上传系统会因此拒绝它的下一次上传（`AsyncUploadSystem` 的 “A buffer is uploaded once”）。目前依赖“每帧都有人读”这一惯例。待办：帧末断言无残留 pending acquire，或在帧末做一次兜底承接。

两条路并存会**重复 acquire**：Uses 只安排障碍、不回写镜像状态，显式访问随后按 `GetSubresourceStates()` 播种，于是发出前置状态为 COMMON 的第二次转换，触发 `INCOMPATIBLE_BARRIER_LAYOUT`。因此迁移完成的组必须清空 `Uses`（`MainSceneTag` 已清空）。

上传链路的既有事实（两条路共用）：拷贝在 Copy 队列完成后，`AsyncUploadSystem::FinishUploads` 在帧首用上传 fence 的值把关，只有已完成的上传才转为待 acquire；因此 acquire 不需要队列侧的等待，跨队列顺序由 fence 与提交顺序保证。

**剩余的 Uses（已解决）**：`MaterialBindingTag::Uses = TagList<MaterialTextureTag>` 曾是唯一的组级 Uses，材质纹理按 GPU 数据动态索引、数量不定。已按§十一的批量导入方案接入集合，同批清空 `Uses`。UI 图标（`UIPass` 的 `s.Uses<UI::IconImageTag>()`）是另一处 scope 级 Uses，同样接入集合后删除。

### Uses 的退役（已完成）

截至 2026-11-16，`Uses` 这条路径已整体删除：

- `Scope::Uses<Tag>()`（`RenderScope` / `ComputeScope`）、`Pass/ScopeUses.h`、`PassCapabilities::m_uses` 与 `DeclareBindingUses`、`BindingTag::Uses` 的展开、`ScopeUses` / `UploadTake` / `AddScopeUse` 全部移除；各绑定 tag 上的 `using Uses = TagList<>` 一并删除。
- `RenderGraphCompiler::CompileScopeUploads` 删除。它的职责只剩“按 tag 挑出待 acquire 的上传”，而没有 tag 声明后这一遍无事可做。`ScopeUpload` / `ScopeUploadRange` / `GetScopeUploads` 保留：显式访问路径仍通过它们记录并执行 acquire。
- UI 图标改由 `UIPass` 发布 `ShaderImageUsage{Bindless, IconGPUComponent::m_viewDesc}`（渲染层发布，`SparkUI` 不依赖 `SparkRender`），再以 `ImportSharedSet<UI::IconImageTag>` + `ReadSet(Fragment)` 读取。
- 代价与材质纹理相同：新上传的图标若无任何消费者读集合会停在 `PendingAcquires`，直到被读取或资源销毁；这是§11.5 接受的取舍。

`Uses` 消失后，资源的上传承接只有一条路径：**被某个 Scope 的 attachment 首次访问时 acquire**。反过来说，一个“上传了但没人访问”的资源不会自动 acquire——需要在帧末做兜底或断言时，应加在这条唯一路径上（尚未实现）。

### 系统填值退场（已完成）

截至 2026-11-16，绑定系统不再向共享组写任何资源输入；组里的资源输入只有图一个写入者，按 attachment 的访问与资源发布填值。

- 最后两处是 `GeometryBindingSystem` 的 `g_MeshGeometry` 与 `InstanceBindingSystem` 的 `g_EmissiveInstanceIDs`：改为在资源实体上发布 `ShaderBufferUsage`（各自的 space、输入名、视图描述符），不再建视图、不再 `SetShaderBuffer`。
- 消费方随之改为共享导入：`ReferencePathTracerFeature` 对 `MeshGeometryPoolTag` 与 `EmissiveInstanceIDBufferTag` 改 `ImportShared`，读取时由 `ApplySharedShaderUsage` 填进组。工程内其余 Pass 只把 `MeshGeometryPoolTag` 当输入装配流（`InputAssembly`）使用，保持普通 `Import`，不经过组。
- 两个系统不再需要 `Shader/ShaderBindingsUtils.h`。
- 后果与既有约定一致：某一帧没人读这两个输入时，组里保留上一帧的值（见「待办：无人声明时的陈旧值」）。

### 未完成

- 上传承接现在只有“被访问才 acquire”一条路径；帧末对残留 `PendingAcquires` 的兜底或断言尚未实现。
- `TODO_BindingModelPlan.md` 的分层表仍按 `.Binds<...>()` 描述，需要按“按 space 推导”重写。

## 十一、实现提案：Bindless 共享 Image 与 ImportSharedSet

状态：第一步至第三步均已实现并通过用户验证，分别见§11.8、§11.9、§11.10（2026-11-15 与 2026-11-16）；第四步的验证分工见§11.11；Uses 已整体退役，见§十末。本步服务当前材质纹理，支持不指定 `.BindIndex` 的 bindless 共享用法；已有普通资源的 `.BindIndex` 保持原样，向共享 bindless 访问增加索引输出目标不在本步范围内。

### 11.1 共享用法与索引传递分别表达

绑定模式统一放进 `ShaderInputBinding`，Image / Buffer / AS 的发布结构继续引用它，不加入材质槽位、数据表字段或裸 bindless index：

```cpp
enum class ShaderBindingMode
{
    Direct,
    Bindless,
};

struct ShaderInputBinding
{
    uint32_t m_spaceId = 0;
    RHI::InputName m_input;
    uint32_t m_arrayIndex = 0;
    ShaderBindingMode m_mode = ShaderBindingMode::Direct;

    static ShaderInputBinding Bindless();
};
```

- 模式放在末尾并默认 Direct，现有 `{ space, input, arrayIndex }` 和 `{ binding, view }` 发布点不改变含义。`ShaderImageUsage` 本身不增加模式字段。
- `Direct` 使用输入位置；`Bindless` 没有固定输入位置，不读取 space / input / arrayIndex。通过 `ShaderInputBinding::Bindless()` 发布该模式；未知模式报错，缺少输入位置不自动解释为 bindless。
- 绑定描述的相等比较先区分模式：Direct 比较完整输入位置，Bindless 忽略无意义的位置字段。这不是资源身份或 View 的相等比较。
- 本步仅为共享 Image 应用 Bindless 模式；Buffer / AS 和固定输入填值入口仍要求 Direct，不开放 Scope 的 `.Bind(ShaderInputBinding::Bindless())`。
- 资源发布的是通过哪个 View、以什么方式提供给 shader；具体 index 从该 View 解析，不在发布组件中重复存储。
- index 写入哪个材质槽位仍由材质数据生产者负责。基础机制不推断索引传递已经完成，也不描述材质表布局。

### 11.2 单资源 ImportShared 的扩展

单资源 `ImportShared<Tag>(name)` 的参数、就绪判定与 `bool` 返回值沿用现有规则。`ImportSharedSet` 对每个成员使用同一套共享资源准备和用法应用逻辑。

将 `BindSharedShaderInput` 改名为 `ApplySharedShaderUsage`，明确其应用已发布用法的职责：

| 已发布用法 | 访问 attachment 上的记录 | 后续处理 |
|---|---|---|
| Image / Buffer / AS 的直接输入绑定 | 原有 View / AS 信息与 `ShaderInputBinding` | 按 space 填入输入，保留现有校验 |
| Image 的 bindless 用法 | 复制发布的默认 View，不登记固定输入位置 | 沿用普通 attachment 的资源访问、stage、acquire 和障碍处理 |

- 不增加 bindless attachment 标记、Scope bindless 功能或专用编译分支。发布模式由共享用法应用入口处理，编译器继续只对已登记的固定输入执行填值。
- 图不为这种用法创建 `ShaderInputBinding` 或 `IndexBinding`，不获取或传递 index，也不增加索引有效性验证。材质系统继续负责解析 View 的索引、处理不可用情况并写入材质数据。
- 不因本步增加 `.BindIndex`、View 覆盖等 Scope 操作限制；原有普通接口不变，材质迁移保证编码索引和访问声明使用同一发布 View。
- RenderScope 的此类访问显式指定 stage；ComputeScope 沿用固定 ComputeShader stage。直接绑定继续从输入推导 stage；后续批量指定 stage 时也应保持该路径的兼容性校验。

### 11.3 集合导入与读取接口

首版 API 提议如下，RenderPassScopes 与 ComputePassScopes 均提供导入入口：

```cpp
const RHI::AttachmentId textures("MaterialTextures");
p.ImportSharedSet<MaterialTextureTag>(textures);

auto s = p.Scope();
s.ReadSet(textures).Stage(RHI::AttachmentStage::FragmentShader);
// 其余 attachment 与 draw 声明……
s.Close();
```

- `ImportSharedSet<Tag>(name)` 返回 `void`。枚举所有携带 Tag、未标记删除的成员，检查 Image 类型并按 `IsResourceReady` 筛选；就绪成员在公共导入准备入口统一检查共享资格。空集合合法，不作为跳过绘制的条件。
- 首版集合只支持 Image，可包含直接绑定或 bindless 两种已发布用法；不能假定 SharedSet 等于 bindless。Buffer / AS 集合与集合写入后续再扩展，现有单资源共享接口继续支持它们。
- `ReadSet(name)` 在当前 Scope 为每个成员生成普通读取 attachment，并应用其已发布用法；返回批量访问对象，其 `.Stage` 只作用于本次生成的 attachment。ComputeScope 无须额外指定 stage。
- 导入不生成访问，也不 acquire。空集合的读取不生成 attachment，不凭空使 Scope 可执行；Scope 仍需满足现有声明规则。
- 保留原有 `m_resources`，另增 `m_resourceSets` 保存集合名到成员列表的映射；两张表共用名称空间，均在本帧构图结束时清理。单资源与集合使用同名、未声明集合便读取、将集合名当作单资源名使用均报错。
- 成员排序并按实体去重。同名集合可以在多个 Pass 中重复导入，但成员身份必须一致，不依赖 ECS 查询顺序；不同成员不得覆盖已有声明。每次导入仍为当前 Pass 登记所有成员的共享意图，读取时检查该意图。
- 不为集合创建 GPU 资源、独立 ECS 资源实体或跨帧句柄。成员列表只保存资源身份；资源与发布内容在本次构图至提交期间保持稳定。首版各 Pass 自行获取列表，不新增跨 Pass 的 Tag 查询缓存。
- 成员走与单资源相同的导入准备流程，设置 backing 并登记该 Pass 的共享意图。附件直接关联成员实体，不要求调用方为成员逐个命名，也不为每个成员生成公开的字符串别名；诊断使用资源原有名称。
- `PrepareImportedResource(name, resource, shared)` 统一两种入口的共享资格检查、backing 准备和导入意图登记；已合并原先的 `PrepareResourceImport` 薄包装，集合筛选不再重复检查共享发布信息和非 transient 资格。名称登记分别写入原单资源表与集合表；集合读取复用 `AddImageAttachment` 与 `ApplySharedShaderUsage`，之后图编译不区分访问来自单资源还是集合。
- 批量访问对象仅记录当前 Scope attachment 列表的起止下标，后续追加不使其失效，不额外保存一份所有成员 attachment 的数组；其使用限于 Scope 关闭之前。批量 stage 与单个访问使用相同的设置入口，不覆盖为与已绑定输入不兼容的阶段。
- 同一成员出现在多个集合时按资源身份处理。重复的相同读取可合并，兼容的不同读取按原规则合并状态；不得吞掉冲突、不同 View 或不同 stage。直接绑定的多个成员也必须遵守输入位置冲突校验。
- 单资源与集合导入共用现有“同一 Pass 内一个资源只有一种导入意图”规则。经集合共享导入的成员不能同时作为普通导入使用。

### 11.4 材质纹理接入

1. `MaterialTextureSystem::CreateTexture` 在资源实体上发布 `ShaderImageUsage`，模式为 Bindless，默认 View 与当前材质采样一致，覆盖完整 mip / layer 范围。`MaterialTextureTag` 收窄为集合成员标记，不再携带 `Access`。
2. `MaterialBindingSystem::ResolveTexIndex` 使用发布的默认 View 解析索引，替换其中独立写死的 `ImageViewDescriptor{}`，继续复用 RHI 已有的 View 创建与缓存入口，不新增纹理系统 getter。
3. 保持材质系统把 index 写入 `MaterialData.TexIndices`，shader 的 bindless 访问、材质数据布局和 sampler 来源不变。发布 bindless 用法不能替代这一步。
4. GBuffer、Translucency、ReferencePathTracer 的采样 Scope 导入并读取纹理集合，stage 为实际采样阶段；材质表仍由 `ImportShared<MaterialPoolTag>` 与 `ReadBuffer` 单独声明。其他实际采样材质纹理的入口也须核查，不能只凭是否使用 space3 判断。
5. 同一改动中将 `MaterialBindingTag::Uses` 清空，移除 `MaterialTextureTag::Access`；`MaterialBindingTag` 的对象查询适配可以暂留，继续按既定顺序退役各绑定 tag。

### 11.5 就绪、上传与生命周期

| 成员状态 | 集合处理 |
|---|---|
| 上传未完成、没有实际对象 | 不纳入；材质索引沿用现有 InvalidTextureIndex / 默认采样行为 |
| 拷贝完成、PendingAcquires | 纳入，由第一个读取 attachment 承接上传 |
| 以前已完成 acquire | 纳入，继续声明本帧可能访问，按实际状态决定障碍 |
| DeadTag / 无效实体 | 不纳入；沿用资源退役和孤立上传清理 |

- `CompileImageAccess`、`AcquireUpload` 与执行器的 `ScopeUpload` 路径继续处理交接，不为集合新增 acquire 回调。旧 Uses 必须随材质消费者接入一起清空。
- 集合保守包含所有存活、就绪的材质纹理，不按本帧可见材质筛选。当前材质编码先于图的帧首上传完成回调；新就绪成员可以已进入集合而尚未写入本帧材质索引，这是安全的访问范围扩大。
- 材质编码和 attachment 必须解析同一资源的同一个 View，且索引在材质数据使用期间有效。禁止集合读取自行换 View 后仍沿用旧材质索引。
- 无 Scope 读取时允许上传保持待 acquire，后续消费或资源销毁时结束；不增加“帧末必须全部 acquire”的断言或无条件兜底读取。
- 保留当前 Graphics 消费队列限制和资源池的延迟释放。此步不开放异步 Compute 消费，也不改变已导入资源拒绝再次异步上传的约束；mip 流送、原地更新与替换另行设计。

### 11.6 大集合的成本处理

- 展开仍产生约“成员数 × 消费 Scope 数”的访问记录；不会按每个材质槽位或每次 draw 再展开。
- `CloseScope` 已改为按实际资源分组，只在同资源的候选访问间检查重叠和冲突。保留未链接名称、同资源别名和历史读取的现有语义，不改 attachment 的声明顺序。未做性能基准，不将复杂度下降表述为已测得的速度提升。
- `FindUploadsToAcquire` 保留原有 ECS 扫描。只有资源首次访问且带有 `PendingAcquires` 时才查询，集合中所有纹理并非每帧都扫描上传。节点式 map 的构建、分配和缓存访问可能抵消扫描次数减少的收益；缺少实际测量，因此本步不引入上传查询索引。
- 不因去重而合并不同 Scope 的访问，也不将多个直接绑定成员覆盖到同一个输入。暂不引入压缩集合 attachment、常驻全局成员表或绑定结果缓存。

### 11.7 实施与验证顺序

1. 在 `ShaderInputBinding` 增加统一绑定模式和单资源共享 Image 应用路径，保持原有直接绑定，不生成 bindless attachment 标记、固定输入或索引常量写入。助手仅做编译检查，运行验证由用户负责；第一步已获用户验证确认，见§11.8。
2. 增加集合导入、成员 attachment 展开与批量 stage，完成按资源分组的冲突检查；上传查询保留原有扫描。第二步已获用户验证确认，见§11.9。
3. 材质侧发布并使用同一默认 View，接入全部材质纹理消费者；同批清空旧 Uses，核对上传只有一条承接路径。已完成并获用户验证确认，见§11.10。
4. 验证按“构建期语义用单元测试、运行时行为用现有示例场景”分工，见§11.11。原第 5 步（规模基准与附件数量记录）取消：每一步都有对应的验证画面，运行验证沿用现有示例，不再单独做性能基准。


### 11.8 第一步实现与验证记录

- `ShaderInputBinding` 增加 `ShaderBindingMode`、默认 Direct、`Bindless()` 工厂及按模式区分的相等比较；现有三字段聚合初始化保持兼容。
- 共享应用入口改名为 `ApplySharedShaderUsage`。共享 Image 的 Direct 用法沿用原填值路径，Bindless 用法只复制默认 View，不在 attachment 上登记固定输入位置。
- 固定输入入口拒绝非 Direct 模式；未增加 Scope bindless 标记、索引输出或编译器专用分支。`ShaderImageUsage` 数据结构、材质发布点、`Uses` 与集合接口均未在本步迁移。
- Debug 编译通过：`SparkRenderTest`、`ReferencePathTracer`、`ComputePass`、`TrianglePass`、`MSAAPass`、`DrawCube`、`MultiView`、`RayTracingScene`、`IndirectDispatch`。
- 按用户要求仅做编译检查，未运行测试程序或示例；具体运行与画面验证由用户负责。
- 用户已完成验证并确认无问题，同意提交第一步。

### 11.9 第二步实现与验证记录

- `RenderPassScopes` / `ComputePassScopes` 增加 `ImportSharedSet<Tag>`，`RenderScope` / `ComputeScope` 增加 `ReadSet`；首版支持共享 Image 的集合读取，RenderScope 可批量指定 stage，ComputeScope 使用固定计算阶段。
- `m_resources` 保持原有单资源记录，额外的 `m_resourceSets` 只记录名称到资源实体列表，随本帧构图结束清理。成员按身份排序去重，支持空集合和同成员集合重复导入，校验两种名称冲突、发布信息、就绪与共享意图。
- 单资源与集合复用合并后的 `PrepareImportedResource(name, resource, shared)`，共享资格只检查一次，仍登记并检查当前 Pass 的导入意图。集合读取复用 `AddImageAttachment` / `ApplySharedShaderUsage`。不创建集合资源实体、成员公开别名或集合专用同步路径；每次读取生成独立的普通 attachment。
- 批量访问用 Scope 内的下标范围表示，不复制 attachment 数组。stage 设置复用公共入口，保持已经绑定的输入阶段；Scope 关闭后不再使用该批量对象。
- `CloseScope` 按实际资源分组检查冲突，并对未链接访问保留按名称与 frameOffset 的原有匹配。
- 撤回曾加入的上传查询临时 map，恢复 `FindUploadsToAcquire` 的原有扫描；最终 `RenderGraphCompiler.cpp` 与第一步提交版本一致，不随第二步提交。
- 第二步初版 Debug 编译通过：`SparkRenderTest`、`ReferencePathTracer`、`ComputePass`、`TrianglePass`、`MSAAPass`、`DrawCube`、`MultiView`、`RayTracingScene`、`IndirectDispatch`。
- 另用构建目录中的临时编译单元，以 `MaterialTextureTag` 实例化 Render / Compute 两种 `ImportSharedSet` 并编译对应 `ReadSet` 调用，MSVC 语法与模板编译检查通过；未将临时单元加入项目源文件。
- 后续导入准备精简、上传查询索引撤回按用户要求未重新编译；助手未运行测试、示例或性能基准。用户已验证最终版本并确认无问题，同意更新文档并提交第二步。
- 材质资源发布、消费者接入与 `MaterialBindingTag::Uses` 清理留给第三步，尚未迁移。

### 11.10 第三步实现与验证记录

- `MaterialTextureSystem::CreateTexture` 在建好实体后发布 `ShaderImageUsage`：`ShaderInputBinding::Bindless()` 加 `ImageViewDescriptor{}`，即整张图的默认 View，与材质索引当前解析用的 View 一致。发布不等待上传完成，理由与 Scene 各图相同。
- `MaterialBindingSystem::ResolveTexIndex` 改为读该发布并使用 `usage->m_view` 建视图、取 bindless 索引；缺发布或模式不是 Bindless 时断言并返回 `InvalidTextureIndex`。`IsResourceReady` 判定与索引编码写入 `MaterialData.TexIndices` 均未改动。
- `MaterialTextureTag` 去掉 `Access`，收窄为集合成员标记；`MaterialBindingTag::Uses` 清空。
- 消费侧接入三处，均为 `ImportSharedSet<MaterialTextureTag>("MaterialTextures")` 加 `ReadSet(...).Stage(FragmentShader)`：`GBufferPass`、`TranslucencyPass`、`ReferencePathTracerFeature`。材质表仍由 `ImportShared<MaterialPoolTag>` 与 `ReadBuffer` 单独声明。
- 消费方核查按“是否真正消费 `TexIndices`”进行，而不是看是否 include `MaterialTemplate.hlsli`：`DepthOnly`、`ShadowDepth` 虽然 include 该模板，但不读索引、不采样材质纹理，因此不接入。
- 现存 `BindingTag::Uses` 已全部为空，按组声明 Uses 的路径无消费者，可整体删除；`ScopeUses` 底层仍由 `UIPass` 的 `s.Uses<UI::IconImageTag>()` 使用，不能一并删除。
- Debug 编译通过：`SparkRender`、`ReferencePathTracer`、`SparkRenderTest`、`ComputePass`、`TrianglePass`、`MSAAPass`、`DrawCube`、`MultiView`、`RayTracingScene`、`IndirectDispatch`。用户已运行验证材质与路径追踪画面，确认无问题。

### 11.11 第四步：验证分工

构建期语义放在 `SparkRenderTest` 里做单元测试，新增 `Engine/Code/Test/Render/ImportSharedSetTest.cpp`（选项 `IMPORTSHAREDSET_TESTS`）。`RenderGraphBuilder` 的入口只对各 Scope 类开放，测试沿用同一做法：`RenderGraphBuilder` 声明 `friend class ::ImportSharedSetTest`，测试以 fixture 驱动一帧（`Begin` → `BeginPass` → `ImportSharedResourceSet` → 开 Scope → `ReadSet` → `Close`），再从 builder 读回该 Scope 声明了什么。成员实体的构造沿用 `MaterialTextureSystemTest` 的假件：假 `Image`、`ShaderImageUsage{Bindless}`、`MaterialTextureTag`。

已覆盖并全部通过（`SparkRenderTest` 共 222 项全绿）：

- 空集合：导入并读取后 Scope 没有任何访问记录，导入本身不报错。
- 每个成员一条访问：两个成员展开两条。
- 重复成员：同一实体重复三次只展开一条。
- 就绪筛选：带 `PendingUploads` 的成员不展开，剩下的是就绪的那个。
- Bindless 不登记固定输入：访问上没有 `ShaderInputBinding` / `IndexBinding`，访问权限为 Read。
- 多 Scope：第二个 Scope 再读一次集合，各自展开、互不重复。
- 成员一致性：同一集合以不同顺序导入两次后仍能正常读取，说明收集结果与访问顺序无关。

未纳入单元测试的部分：名称冲突、未导入就读、集合名当单资源名是 `ASSERT` 路径，仓库没有 death test 先例，不为此引入；靠运行示例时暴露。

运行时行为沿用现有示例场景（每步都有对应画面）：

- 首次上传后的多消费者 acquire：新材质纹理在所在帧被 GBuffer / Translucency / ReferencePathTracer 读取时无残留 `PendingAcquires`。
- 索引与 View 一致性：材质球表面、半透明表面与路径追踪的纹理采样正确，无错图。
- 成员删除与跨帧读取：纹理被 GC 后不再展开，后续帧画面不崩、不黑。
- 规模：几百张纹理下的 attachment 数量与构图耗时只做观察，不再单独做基准或写入门槛。


回归沿用 `ctest` 与现有示例；名称冲突、未导入就读、集合名当单资源名这类断言路径不引入 death test（仓库暂无此先例），靠场景运行时暴露。


## 十二、方案边界

- 第一阶段仅解除图对绑定实体的依赖；资源上的共享用法组件已按§十落地，attachment 的统一绑定分组仍待逐项设计。§十一的单资源 bindless Image 用法与集合导入机制已完成前三步，材质纹理已接入，验证见§11.11。
- 不新增可供外部直接调用资源系统对象的访问接口。
- 不通过导入或发布资源隐式声明整组访问。
- 不以移除专用 ECS 实体为由移除 RHI 绑定对象或必要的布局信息。
- 旧方案的 per-pass/Scope 参数约束、View 重放和根常量行为，只有在后续明确决定时才调整。

## 关联文档

- [资源绑定模型](TODO_BindingModelPlan.md)：现有分层及已完成改造。
- [RenderGraph Item 方案](TODO_RenderGraphItemPlan.md)：Pass、Scope、attachment 与提交关系。
- [RenderGraph Resolve 方案](TODO_RenderGraphResolvePlan.md)：资源记录、链接、版本解析与 `.From` 语义。
- [上传交接方案](TODO_UploadHandoffPlan.md)：上传资源的 acquire 与访问声明。
