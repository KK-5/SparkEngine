# 渲染图的记录与解析分离　实现方案

出处：`TODO_ScreenSpacePlan.md` §五。SSR 要读上一帧的 `TemporalAA`，而这一帧的 TemporalAA 在 SSR 之后才声明，现在的
`ReadPrevious` 要求先声明。顺着这个问题往下看，限制的根源是 build 回调边声明边解析：名字、版本、资源都在声明的那一刻
确定。这也是 `TODO_RenderGraphItemPlan.md`「待优化」里"版本解析依赖 pass 的注册顺序"那一条。

目标：build 回调只记录，全部 pass 记录完之后统一解析。做成之后：

- 读上一帧不再受声明先后限制；
- 后注册的 pass 可以插到管线中间，读写中间版本的资源（例如插一个处理 SceneColor 的 pass）；
- 依赖边补全，执行顺序确定。

决策 D1~D9 均已确认。

---

## 状态

| 步骤 | 内容 | 状态 |
|---|---|---|
| 1 | 补读后写（WAR）边；执行顺序确定：就绪的 pass 里取声明最早的（D4） | ✅ 完成 |
| 2 | 解析逻辑抽成纯函数，补单元测试（D8） | 未开始 |
| 3 | 记录与解析分离；校验迁到解析；上一帧读取在解析时完成（D1、D5、D6、D7） | 未开始 |
| 4 | 访问上的 `.From`：读写指定 pass 写出的版本（D2） | 未开始 |

1 独立于其余几步，是现存的正确性漏洞，先做、单独提交。2 是 3 的准备。4 依赖 3。

---

## 一、现状

| 环节 | 现状 | 位置 |
|---|---|---|
| build 的顺序 | `GetPassesInDeclOrder()`，即各 pass `SetUp` 的调用顺序（`RenderSystem.cpp`） | `PassContext.h`、`RenderGraph::ExecutePipeline` |
| 访问的解析 | `AddScopeAttachment` 当场按名字查 `m_resources`（找不到即断言），读取用当时的最新版本，写入登记"读了上一版本"后把版本 +1；当场建 attachment 实体，填好资源与版本 | `RenderGraphBuilder.cpp` |
| 上一帧读取 | `AddPreviousFrameAttachment` 当场找本帧已声明的 transient image（找不到即断言），给它加 `ShaderRead`、挂 `ExtractedImage`；用它的描述符判断上一帧的副本能否用、缺失时分配替身；当场挂 `PreviousFrameMissingTag` | 同上 |
| 当场查询解析结果 | `ShaderAttachment::IsPreviousFrameMissing()`，TAA 据此写 `g_TemporalAAHistoryValid` | `PassScopes.cpp`、`TemporalAAPass.cpp` |
| Scope 的校验 | `CloseScope`：至少一个 attachment、root constant 全部写过、shader 访问有 stage、深度模板覆盖所有 aspect、同一子资源的访问不冲突。后两条要用到资源 | `RenderGraphBuilder::CloseScope` |
| 清除值 | 第一个 Clear 的访问当场把清除值写到 transient 资源上 | `AddScopeAttachment` |
| 建边 | `BuildGraph` 按 (名字, 版本) 建两种边：写者 → 读者、读写者 → 只读者 | `RenderGraphBuilder::BuildGraph` |
| 执行顺序 | `TopoSort` 的 Kahn 算法用栈取就绪节点，结果写进 `PassGlobalTimeline`，Scope 按它排序执行 | `TopoSort`、`RenderGraphCompiler::SortScopes` |
| 编译阶段的输入 | attachment 实体上的 `ImagePassAttachment` / `BufferPassAttachment`、`ScopeAttachment`，资源实体，`PassGlobalTimeline` | `RenderGraphCompiler` |
| 测试 | builder 没有单元测试 | `Engine/Code/Test/Render/` |

## 二、问题

1. **读上一帧必须先声明**：上一帧读取要从本帧的声明里取描述符，并给它挂 `ExtractedImage`。生产者排在读者之后就断言。
2. **顺序只能靠注册先后**：版本由声明顺序决定，一个 pass 只能读到"在它之前注册的 pass 写出的最新版本"。后注册的功能
   模块只能排在最后，比如只能读到管线最后的 SceneColor。
3. **缺读后写（WAR）边**：版本 v 的读者与版本 v+1 的写者都只依赖 v 的生产者，两者之间没有边。拓扑排序用栈取就绪节点，
   可能先执行写 v+1、后执行读 v，读到被覆盖的内容。现有管线里没有"读中间版本、之后被原地改写"的资源，所以还没出问题。
4. **执行顺序不确定**：互不依赖的 pass 的先后取决于栈与哈希表的遍历顺序，不是声明顺序。RenderDoc 里看到的顺序可能随
   改动漂移。
5. **校验与声明交错**：一部分校验要用资源，只能在声明时就有资源的前提下进行，进一步把"先声明"绑死。

---

## 决策记录

### D1　build 只记录，`End()` 统一解析　✅ 已定

- 访问在 build 时照常建 attachment 实体，但只记下名字、访问方式、用途、stage、动作和视图，**资源与版本留空**。
  `.Format` / `.View` / `.Stage` / `.Bind` / `.BindIndex` 都只改记录，不受影响。
- `CreateImage` / `CreateBuffer` / `Import` 照常登记资源。同一名字只能登记一次的断言不变；登记出现在哪个 pass、在访问之前
  还是之后，都不再重要。
- `End()` 依次：逐资源为每个访问定资源与版本（D2、D3）→ 处理上一帧读取（D5）→ 校验（D6、D7）→
  建边与排序（D4）→ 写回 attachment 实体上的资源与版本。建图与排序本来就在 `End()` 里，解析放在它们前面。
- **编译阶段不变**：`End()` 之后 attachment 与资源实体上的组件和现在完全一样，`RenderGraphCompiler` 不用改。

### D2　访问可以指明"谁写出的版本"；pass 之间不声明顺序　✅ 已定

pass 只关心资源，不关心别的 pass。要读写中间版本是避不开的特例，它的声明放在**访问**上，说的是"我要 LightsPass 写出
的 SceneColor"，不是"我排在 LightsPass 之后"：

```cpp
s.Read("SceneColor").From("LightsPass").Bind("g_SceneColor");          // 读 Lights 写出的那一版
s.RenderTarget("SceneColor", load).From("LightsPass");                 // 接在 Lights 的写之后，原地改写
```

**每个资源一条写者链**，版本沿链递增（D3）：

- **不带 `.From` 的访问**按声明顺序解析，与现在相同，现有 pass 一行不改。
- **带 `.From(P)` 的读**：精确读 P 写出的版本（P 在多个 Scope 里写这个资源时，取它最后一次写出的）。之后有没有别的
  pass 插进来改写，都不影响它读到的内容。
- **带 `.From(P)` 的写 / 读写**：接在 P 的写之后，消费 P 的版本、产生新版本。链上原来接在 P 后面的写者改为消费它的
  输出；声明在 P 之后、不带 `.From` 的读者也读到它的输出。这正是"插一个处理 SceneColor 的 pass"要的效果。
- 多个写接在同一个 P 之后：按声明顺序依次相接，后一个消费前一个的输出。原地改写会毁掉被消费的版本，一个版本只能有
  一个写者接手，所以它们只能排成链。
- P 用 `PassName` 引用（字符串：外部模块拿不到别的 pass 的句柄）。P 不存在，或这一帧没有写这个资源，即断言：读者与
  生产者的开关条件应当一致。
- **`.From` 只为中途插入而设，不能让别的 pass 跟着改**：
  - 带 `.From` 的写产生的就是同名资源的下一版本，后面的 pass 不用改就拿到它。
  - 带 `.From` 的只读不改变写者链，后面的 pass 读写到的内容与没有它时完全相同；它只多出一条"先于下一个写者执行"的
    边。所以只读不要求这个 pass 再写出同名资源（调试视图、截取中间结果都是只读）。
  - **P 必须声明在使用 `.From` 的 pass 之前**，否则断言。`.From` 只能往回够一个中间版本。读一个声明在自己
    之后的 pass 产生的新资源不需要它（D3：前面没有写者的读，取最终版本）。
- `.From` 只用于本帧的访问；`ReadPrevious` 读的总是上一帧的最终版本。

**pass 的执行顺序完全由资源依赖推出**（D4），没有"pass 的逻辑顺序"这个概念，也没有 `.After` / `.Before`。一个 pass 对
不同资源的要求互相矛盾时（例如读 Lights 写出的 SceneColor，又读管线末尾才产生的 TemporalAA），依赖成环，断言并报出
环上的 pass 与资源。

build 回调仍按声明顺序调用：回调只记录，调用顺序只决定不带 `.From` 的访问怎么解析。

**不做命名插入点**（Unity 的 `RenderPassEvent`、UE 的 SceneViewExtension 钩子那种预定义阶段），也不让写者给自己的
输出起版本别名：两者都要求生产者预先为每个可能被插入的位置起名。

### D3　版本由写者链推出，不在访问上写版本号　✅ 已定

最初的想法是写入时指定版本号，以此解决"多个 pass 写同一资源"和"读某个中间版本"。不采用数字版本号，原因：

- 版本号取决于前面有几次写。某个 pass 多写一次，后面所有人的版本号都要改；
- 插入一个**读写** SceneColor 的 pass 时，它后面的写者要改成读它的输出，版本号同样要改；
- 真正要表达的是"谁写出的那一版"，由 D2 的 `.From` 表达。

解析在 `End()` 逐资源进行：先把不带 `.From` 的访问按（pass 的声明顺序，Scope 序号）排好，写者依次组成链；再把带
`.From(P)` 的写接到 P 的最后一次写之后；沿链编号，每次写消费上一版本、产生下一版本；最后给每个读定版本——不带
`.From` 的读取"声明在它之前的写者及接在这些写者后面的写"里链上最靠后的一个，带 `.From(P)` 的读取 P 的版本。同一 pass
的多个 Scope 按 Scope 序号相接（HZB 逐 mip 的读写不变）。

**transient 资源的读，前面一个写者都没有时取链的最终版本。**版本 0 是未定义的内容，没有人想读它；这样
生产者注册在读者之后也能读到，执行顺序由依赖边排好。内置 pass 可以按名字读一个可选输入，由注册在管线之后的模块提供，
不必把那个模块的注册挪进管线中间。imported 资源不适用：它的版本 0 是有效内容，前面没有写者的读就读版本 0。

### D4　依赖边补全，执行顺序由边推出、以声明顺序定先后　✅ 已定

- 每个 (名字, 版本) 建三种边：写者 → 读者（RAW）；读者 → 下一版本的写者（WAR，新增）；写者 → 下一版本的写者（WAW，
  现在已经经由"写入登记读了上一版本"得到）。
- **执行顺序**：Kahn 算法的就绪集合按 pass 的声明序号取最小者，而不是栈顶，结果完全确定。所有边都指向声明靠后的 pass 时
  （现有管线如此），执行顺序就是声明顺序；后声明而带 `.From` 的 pass 排在第一个必须等它的 pass 之前（读 Lights 写出
  的 SceneColor 的 pass，紧排在下一个改写 SceneColor 的 pass 之前）。
- WAR 边因此是必需的：带 `.From` 的读者靠它排到下一个写者之前。
- 成环即断言（D2）。边也是以后 async compute 重排、剔除无用 pass 的依据，现在不做。
- 步骤 1 先在现有 builder 上补 WAR 边、把栈换成按声明序号取最小，与其余步骤无关。

### D5　上一帧读取在解析时完成　✅ 已定

- `ReadPrevious(名字)` 只记录。`End()` 时本帧所有声明都已就绪，按现在的逻辑处理：给本帧的资源加 `ShaderRead`、挂
  `ExtractedImage`，用它的描述符判断上一帧的副本能否用，缺失时分配替身。不需要读者提供描述符。
- **本帧必须有人声明这个名字**（在哪个 pass、先后都行），否则断言：没有描述符就无从判断与分配替身，也说明读者与
  生产者的开关条件不一致。真有需要时再放宽。
- **"上一帧是否缺失"不再能当场查询**：删除 `IsPreviousFrameMissing()`，改为由渲染图在解析时填写的常量：

  ```cpp
  s.ReadPrevious(PostProcess::TemporalAAName()).Bind("g_History").BindValid("g_TemporalAAHistoryValid");
  ```

  解析时写入 1（有上一帧）或 0（缺失）。root constant 与 per-pass 常量都支持，写入检查同 `.BindIndex`。

### D6　校验分两处：只看记录的留在 `CloseScope`，要用资源的移到解析　✅ 已定

build 时能查的都留在 build 时查：断言落在出错的 pass 自己的 build 回调里，调试器停在写错的那一行；解析时的断言只能靠
报错里的 pass 名与 Scope 序号定位。只有非用资源不可的才移到解析。

| 校验 | 位置 |
|---|---|
| Scope 至少一个 attachment、root constant 全部写过、shader 访问有 stage、shader 输入存在且读写方式匹配 | `CloseScope` / 绑定时（不变） |
| 同一 Scope 里对同一子资源的访问不冲突 | `CloseScope`（不变），改为按名字比较：现在按资源比较，而资源要到解析才有 |
| 名字已登记（Create / Import） | 解析时（原在 `AddScopeAttachment`） |
| 深度模板覆盖图像的所有 aspect | 解析时（原在 `CloseScope`）：要用图像的格式 |
| 清除值落到 transient 资源上 | 解析时（原在 `AddScopeAttachment`），记录上先存着 |

按名字比较漏掉一种情况：同一个外部资源用两个名字 Import，又在同一个 Scope 里分别访问。解析时按资源补查一遍。

### D7　新增校验：读到从未写过的 transient 资源　✅ 已定

transient 资源的版本 0 是"刚分配、内容未定义"。解析出来读到版本 0 的读即断言（imported 资源的版本 0 是有效内容，
不受限）。这类错误现在会静默读到垃圾；记录与解析分离后，排错顺序更容易写出这种 bug，这条校验正好兜住。按 D3，前面没有
写者的读取最终版本，所以落到这里的只有"这一帧没有任何 pass 写它"。

### D8　解析逻辑写成纯函数，有单元测试　✅ 已定

- 版本推导、建边、排序写成不依赖 `RHIContext` / `PassContext` 的函数（输入是访问记录：资源、pass 的声明序号、Scope
  序号、读写、可选的 `.From`；输出是每个访问的版本、边与执行顺序），放在 `RenderGraph/RenderGraphResolve.{h,cpp}`，同 `ImageBarrierMerge` 的做法。builder 负责收集记录、
  写回结果。
- 数据用平铺的 vector，顺带落实 `TODO_RenderGraphItemPlan.md`「建图的每帧分配」里的改法（使用记录平铺、排序后线性
  连边、容器做成员只清不释放），不再用每帧大量分配的节点式容器。
- **出错作为结果返回，不在函数里断言**：`.From` 的 pass 不存在 / 没写 / 声明在后、读到没人写过的 transient、依赖成环，
  各是一种错误，带上涉及的访问或 pass。builder 拿到后断言并拼出带名字的报错。仓库的测试里没有 death test，这样出错的
  路径也能用普通用例测到。
- `SparkRenderTest` 新增用例（见步骤 2~4）。

### D9　不做的　✅ 已定

- 在访问上写数字版本号（D3）。
- pass 之间的顺序声明（`.After` / `.Before`）、命名插入点、版本别名（D2）。
- 剔除输出无人读的 pass、为 async compute 重排：边已具备，等有需求再做。

---

## 三、步骤

### 1　WAR 边与确定的执行顺序　✅

- `BuildGraph`：用到 (名字, 版本 v) 的每个 pass 向版本 v+1 的写者连边。v 的生产者到 v+1 的写者那条原来就有，新增的是
  v 的只读者那些。
- `TopoSort`：就绪集合是按声明序号的最小堆，取声明最早的。
- **跑过的**：RenderTest 60 个通过。编辑器打开 `Scene.scene`：debug layer 下跑一次并改窗口大小三次（含奇数尺寸），
  GPU-based validation 下同样跑一次，都无断言、无报错。临时日志打出的执行顺序与 `RenderSystem` 的注册顺序完全一致
  （场景打开前 13 个 pass，打开后多出 SceneDownsample 与 Bloom，共 15 个）。
- **改之前的顺序**（同一场景，临时开关切回旧排序）：`DepthPre → HZB → GBuffer → VelocityResolve → Shadow →
  ShadowProjection → …`，HZB 提到了 GBuffer 之前、Shadow 落到 VelocityResolve 之后，其余与注册顺序相同。这两处都是
  互不依赖的 pass 换位，没有读到被覆盖内容的情况。
- **没有跑到的**：场景里没有 AO 组件，GTAO 的三个 pass 这几次都没有声明任何东西；画面只确认了不报错，没有逐像素对比；
  新增的边在现有管线里不改变任何先后（所有边本来就指向后声明的 pass），它真正起作用要等步骤 4 的用例。

### 2　解析逻辑抽成纯函数

- 把现在的版本推导与建边搬进 `RenderGraphResolve`，语义不变（仍按声明顺序、在声明时调用）。
- 用例：单写多读；读写链（Lights → IndirectDiffuse → Reflections 的 SceneColor）；读中间版本后被改写（WAR）；同一 pass 多个
  Scope 逐 mip 读写（HZB）；自环忽略；成环断言。
- **验证**：新用例全过；画面与 RenderDoc 的 pass 顺序同步骤 1。

### 3　记录与解析分离

- attachment 实体在 build 时不填资源与版本；`End()` 按 D1 的次序解析并写回。
- 上一帧读取改为解析时处理（D5），加 `.BindValid`，删除 `IsPreviousFrameMissing`，TAA 改用新写法。
- 校验按 D6 迁移，加 D7。
- 用例：先读上一帧、后声明本帧；本帧未声明即断言；描述符变化算缺失；访问出现在 Create 之前；读者声明在写者之前时读到
  最终版本、排在写者之后执行；这一帧没人写的 transient 被读即断言。
- **验证**：同步骤 1，另外改窗口尺寸几次，确认 TAA 在尺寸变化时重置历史。

### 4　访问上的 `.From`

- `Attachment` / `ShaderAttachment` 加 `.From(passName)`，记在访问上；解析按 D2、D3 处理。
- 用例：后声明的 pass 读中间版本，排到下一个写者之前；后声明的 pass 原地改写中间版本，后面的写者与读者拿到它的
  输出，而带 `.From` 的读者仍读到改写前的；两个写接在同一个 pass 之后按声明顺序相接；只读的 `.From` 不改变其余
  访问解析到的版本；互相矛盾的要求成环即断言；`.From` 的 pass 不存在、这一帧没写、或声明在自己之后即断言。
- 现有 pass 都不用 `.From`。SSR 也不需要：它按声明顺序排在 Reflections 之前即可，只依赖步骤 3。

---

## 四、改动清单

| 步骤 | 文件 |
|---|---|
| 1 | `RenderGraph/RenderGraphBuilder.cpp`（`BuildGraph`、`TopoSort`） |
| 2 | `RenderGraph/RenderGraphResolve.{h,cpp}`（新）；`RenderGraphBuilder.{h,cpp}`；`Feature/Render/CMakeLists.txt`；`Test/Render/RenderGraphResolveTest.cpp`（新）与其 CMake |
| 3 | `RenderGraphBuilder.{h,cpp}`；`PassScopes.{h,cpp}`；`TemporalAA/TemporalAAPass.cpp`；`Pass/Component/RHIComponents.h`（记录用的组件）；测试 |
| 4 | `PassScopes.{h,cpp}`（`.From`）；`RenderGraphResolve.{h,cpp}`；`RenderGraphBuilder.cpp`；测试 |

---

## 五、未决

- 多视图：解析与视图无关，不受影响；"每个视图读写不同资源"仍是 `TODO_MultiViewPlan.md` 的范围。

---

## 关联文档

- `TODO_ScreenSpacePlan.md` —— §五 SSR 的先决条件
- `TODO_RenderGraphItemPlan.md` —— 现有 builder 的设计；「待优化」的"版本解析依赖注册顺序"与"建图的每帧分配"在这里落实
- `TODO_TemporalPlan.md` —— 上一帧读取机制的原始设计（步骤 7），"名字必须在本帧已声明"的约定由本文 D5 取代
