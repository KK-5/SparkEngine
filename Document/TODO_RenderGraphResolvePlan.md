# 渲染图的记录与解析分离　实现方案

出处：`TODO_ScreenSpacePlan.md` §五。SSR 要读上一帧的 `TemporalAA`，而这一帧的 TemporalAA 在 SSR 之后才声明，现在的
`ReadPreviousImage` 要求先声明。顺着这个问题往下看，限制的根源是 build 回调边声明边解析：名字、版本、资源都在声明的那一刻
确定。这也是 `TODO_RenderGraphItemPlan.md`「待优化」里"版本解析依赖 pass 的注册顺序"那一条。

目标：build 回调只记录，全部 pass 记录完之后统一解析。做成之后：

- 读上一帧不再受声明先后限制；
- 后注册的 pass 可以插到管线中间，读写中间版本的资源（例如插一个处理 SceneColor 的 pass）；
- 依赖边补全，执行顺序确定。

决策 D1~D9 均已确认，四个步骤全部完成。

---

## 状态

| 步骤 | 内容 | 状态 |
|---|---|---|
| 1 | 补读后写（WAR）边；执行顺序确定：就绪的 pass 里取声明最早的（D4） | ✅ 完成 |
| 2 | 解析逻辑独立成 `ResolveGraph`，补单元测试（D8） | ✅ 完成 |
| 3 | 记录与解析分离；校验迁到解析；上一帧读取在解析时完成（D1、D5、D6、D7） | ✅ 完成 |
| 4 | 访问上的 `.From`：读写指定 pass 写出的版本（D2） | ✅ 完成 |

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

- 访问在 build 时照常建 attachment 实体，**版本留空**。名字已经声明过的，资源当场填上；还没声明的，资源留空并挂
  `UnlinkedAttachmentTag`，由 `End()` 链接（D6）。`.Format` / `.View` / `.Stage` / `.Bind` / `.BindIndex` 都只改 attachment
  上的记录，不受影响。
- `CreateImage` / `CreateBuffer` / `Import` 照常登记资源。同一名字只能登记一次的断言不变；登记出现在哪个 pass、在访问之前
  还是之后，都不再重要。
- **访问自己说明是 image 还是 buffer**：shader 对 image 的访问是 `ReadImage` / `ReadWriteImage` / `WriteImage`，对 buffer 的是
  `ReadBuffer` / `ReadWriteBuffer` / `WriteBuffer`；render target、深度本来就只能是 image。原来是不分种类的 `Read` /
  `ReadWrite` / `Write`，由"名字找到的资源"决定建哪一种 attachment，而资源现在可以声明在访问之后，那时还查不到。声明与访问的
  种类对不上即断言（资源已声明的在访问时，否则在 `End()` 链接时）。读上一帧的 `ReadPrevious` 随之改名为
  `ReadPreviousImage`。
- `End()` 依次：链接声明在资源之前的 attachment（D6）→ 链接上一帧读取（D5）→ `ResolveGraph` 定版本、建边、排出执行
  顺序（D2~D4、D8）→ 把它带回的错误转成断言（D7）。建图与排序本来就在 `End()` 里，解析放在它们前面。
- **编译阶段不变**：`End()` 之后 attachment 与资源实体上的组件和现在完全一样，`RenderGraphCompiler` 不用改。

### D2　访问可以指明"谁写出的版本"；pass 之间不声明顺序　✅ 已定

pass 只关心资源，不关心别的 pass。要读写中间版本是避不开的特例，它的声明放在**访问**上，说的是"我要 LightsPass 写出
的 SceneColor"，不是"我排在 LightsPass 之后"：

```cpp
s.ReadImage("SceneColor").From("LightsPass").Bind("g_SceneColor");     // 读 Lights 写出的那一版
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
- 接在一个插入者之后（X 接在 Lights 后，Z 又 `.From(X)`）：Z 紧跟 X，排在其它接在 Lights 后面的写之前，即使那些写
  声明得比 Z 早。Z 要的是 X 的输出，这样它拿到的正好是。
- P 用 `PassName` 引用（字符串：外部模块拿不到别的 pass 的句柄）。pass 都在 `SetUp` 时注册，所以 `.From` 被调用时当场
  按名字找到 pass，访问上存的是 pass 句柄（`FromPass` 组件）。P 不存在，或这一帧没有写这个资源，即断言：读者与
  生产者的开关条件应当一致。
- **`.From` 只为中途插入而设，不能让别的 pass 跟着改**：
  - 带 `.From` 的写产生的就是同名资源的下一版本，后面的 pass 不用改就拿到它。
  - 带 `.From` 的只读不改变写者链，后面的 pass 读写到的内容与没有它时完全相同；它只多出一条"先于下一个写者执行"的
    边。所以只读不要求这个 pass 再写出同名资源（调试视图、截取中间结果都是只读）。
  - **P 必须声明在使用 `.From` 的 pass 之前**，否则断言。`.From` 只能往回够一个中间版本。读一个声明在自己
    之后的 pass 产生的新资源不需要它（D3：前面没有写者的读，取最终版本）。
- `.From` 只用于本帧的访问；`ReadPreviousImage` 读的总是上一帧的最终版本。
- **不支持：插入的 pass 在后面的 Scope 里读回自己写出的版本。**不带 `.From` 的读按声明顺序解析，插入的 pass 注册在
  后面，读到的是最终版本，结果成环报错。`.From` 也不能指向自己这个 pass。目前没有这样的 pass，等有了再定（候选：
  允许 `.From` 指向自己，含义是"我自己之前写出的版本"）。

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

- `ReadPreviousImage(名字)` 只记录。`End()` 时本帧所有声明都已就绪，按现在的逻辑处理：给本帧的资源加 `ShaderRead`、挂
  `ExtractedImage`，用它的描述符判断上一帧的副本能否用，缺失时分配替身。不需要读者提供描述符。
- **本帧必须有人声明这个名字**（在哪个 pass、先后都行），否则断言：没有描述符就无从判断与分配替身，也说明读者与
  生产者的开关条件不一致。真有需要时再放宽。
- **"上一帧是否缺失"不再能当场查询**：删除 `IsPreviousFrameMissing()`，改为由渲染图在解析时填写的常量：

  ```cpp
  s.ReadPreviousImage(PostProcess::TemporalAAName()).Bind("g_History").BindValid("g_TemporalAAHistoryValid");
  ```

  `.BindValid` 与 `.BindIndex` 是同一类绑定：build 时检查常量（两者共用 `ReserveUintConstant`）并在 attachment 上挂
  `PreviousFrameValidBinding`；`End()` 链接上一帧读取时，缺失的挂 `PreviousFrameMissingTag`；lowering
  （`CompileScopeBindings`）写入 1（有上一帧）或 0（缺失），与 bindless 下标走同一段写入，root constant 与 per-pass 常量
  都支持。"是否缺失"以标记的形式从 builder 带到 lowering，pass 的 build 回调接触不到它。

### D6　校验分两处：只看记录的留在 `CloseScope`，要用资源的移到解析　✅ 已定

build 时能查的都留在 build 时查：断言落在出错的 pass 自己的 build 回调里，调试器停在写错的那一行；解析时的断言只能靠
报错里的 pass 名与 Scope 序号定位。只有非用资源不可的才移到解析。

| 校验 | 位置 |
|---|---|
| Scope 至少一个 attachment、root constant 全部写过、shader 访问有 stage、shader 输入存在且读写方式匹配 | `CloseScope` / 绑定时（不变） |
| 同一 Scope 里对同一子资源的访问不冲突 | `CloseScope`（不变）。两个 attachment 都已链接到资源时按资源比较，否则按名字比较 |
| 名字已登记（Create / Import） | `End()` 链接时（原在 `AddScopeAttachment`） |
| 深度模板覆盖图像的所有 aspect、清除值落到 transient 资源上 | 声明时资源已存在的，照旧在 `CloseScope` / `AddScopeAttachment`；资源还没声明的，`End()` 链接时补做 |

**声明时能链接的当场链接。** `AddScopeAttachment` 找得到资源就直接填上，与原来一样；找不到的才给 attachment 挂
`UnlinkedAttachmentTag`，`End()` 只遍历带这个标记的 attachment，链接后清掉标记。现有 pass 都是先声明后访问，这条路径
目前没有 attachment 会走到。

没有覆盖的一种情况：同一个外部资源用两个名字 Import、在同一个 Scope 里分别访问、且访问出现在 Import 之前。这时只能按
名字比较，查不出两者是同一个资源。

### D7　新增校验：读到从未写过的 transient 资源　✅ 已定

transient 资源的版本 0 是"刚分配、内容未定义"。解析出来读到版本 0 的读即断言（imported 资源的版本 0 是有效内容，
不受限）。这类错误现在会静默读到垃圾；记录与解析分离后，排错顺序更容易写出这种 bug，这条校验正好兜住。按 D3，前面没有
写者的读取最终版本，所以落到这里的只有"这一帧没有任何 pass 写它"。

### D8　解析逻辑独立成函数，不依赖设备，有单元测试　✅ 已定

- 版本推导、建边、排序放在 `RenderGraph/RenderGraphResolve.{h,cpp}` 的 `ResolveGraph` 里。它接收 `RHIContext&` /
  `PassContext&` 与这一帧的 attachment 列表（按声明顺序），直接读 attachment 与 pass 上的组件，把版本写回 attachment 的
  `AttachmentId`、把执行顺序写成 pass 上的 `PassGlobalTimeline`。
- **不脱离 ECS**：最初规定它不碰两个 context（照搬 `ImageBarrierMerge`），结果要把组件里的数据抄成一份记录、给名字和
  pass 编号、解析完再抄回去，builder 里多出一批只为喂它而存在的结构。单元测试需要的是不依赖设备，context 只是
  registry，测试里直接构造（同 `MaterialOverrideTest`）。解析过程中的状态都是函数内的局部变量。
- builder 只留"这一帧的 attachment，按声明顺序"一个列表；`m_resources` 只是名字到资源实体的映射。
- **出错作为结果返回，不在函数里断言**：`.From` 的 pass 这一帧没写这个资源、读到没人写过的 transient、依赖成环，
  各是一种错误，带上涉及的 attachment 或 pass。builder 拿到后断言并拼出带名字的报错。仓库的测试里没有 death test，
  这样出错的路径也能用普通用例测到。`.From` 的 pass 不存在或声明在后，在 `.From` 被调用时就能确定，当场断言，不进
  解析。
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

### 2　解析逻辑独立成 `ResolveGraph`　✅

- 版本推导、建边、排序从 builder 搬进 `RenderGraphResolve.{h,cpp}`（D8）。builder 里的 `m_attachmentUses`、`m_graph`、
  `BuildGraph`、`TopoSort` 随之删除，只留按声明顺序的 attachment 列表。
- `ResolveGraph` 内部分四步，各一个函数：`CollectAccesses`（读 attachment，按资源归到一起）、`AssignVersions`、
  `AddEdges`（都是一个资源一段）、`PlacePasses`（拓扑排序，写 `PassGlobalTimeline`）。
- 用例（`RenderGraphResolveTest.cpp`，16 个，在自己构造的两个 context 里建 pass、资源、attachment）：单写多读；读写链；
  只写不读的写仍排在上一版本之后；读者排在下一版本的写者之前；读 imported 资源的版本 0；同一 pass 多个 Scope 逐级读写不连
  自环；一个 Scope 里多次写；重复访问只连一条边；边不约束的 pass 按声明顺序；没有 attachment 的 pass 不排；步骤 3 的四个
  （见下）；空帧。

### 3　记录与解析分离　✅

- 访问的名字已声明时当场链接资源，否则挂 `UnlinkedAttachmentTag`，`End()` 的 `LinkAttachments` 补上（D1、D6）。版本一律
  由 `ResolveGraph` 给。
- 上一帧读取在 `End()` 的 `LinkPreviousFrameReads` 里链接；`.BindValid` 取代 `IsPreviousFrameMissing`（D5），TAA 改用它。
- transient 资源前面没有写者的读取最终版本（D3）；读到没人写的 transient、成环，由 `ResolveGraph` 带回、builder 断言（D7）。
- 访问自己说明种类（D1）：`Read` / `ReadWrite` / `Write` / `ReadPrevious` 改名 `ReadImage` / `ReadWriteImage` / `WriteImage` /
  `ReadPreviousImage`，新增 buffer 的三个；12 个 pass 与 SandBox 的三个示例的调用处随之改名。
- 用例（在步骤 2 的 16 个里）：读者声明在写者之前读到最终版本、排在写者之后；夹在两次写之间的读取前一次；没人写的
  transient 被读报错；读在自己 pass 的第一次写之前报错；互相需要对方结果的 pass 排不上。
- **跑过的**：RenderTest 76 个通过。在 `Scene.scene` 里跑过，无断言、无报错（最终版本由用户跑；我跑的是中间版本，
  见下）。
- **只在中间版本上跑过的**（之后解析函数改为接收 context、链接改为当场链接、`.BindValid` 改由 lowering 写入，这几项没有
  重跑）：debug layer 下改窗口大小，`g_TemporalAAHistoryValid` 在第一帧与每次尺寸变化时为 0、下一帧回到 1；把 TAA 的
  `RenderTarget` 与上一帧读取临时挪到它的 `CreateImage` 之前，照常运行。
- **没有跑到的**：没有 pass 声明在它的生产者之前，`UnlinkedAttachmentTag` 这条路径与"前面没有写者的读取最终版本"在真实
  管线里没有执行过（只有上面那次临时实验与单元测试）；没有 buffer 走渲染图，三个 buffer 访问没有被调用过；GPU-based
  validation 没有重跑。

### 4　访问上的 `.From`　✅

- `Attachment` / `ShaderAttachment` 加 `.From(passName)`：当场按名字找到声明在自己之前的 pass，在访问上挂 `FromPass`
  （pass 句柄）。找不到（不存在，或声明在后）、用在 `ReadPreviousImage` 上，当场断言。
- 解析只改定版本这一步，连边与排序不动。`AssignVersions` 先由 `ChainWrites` 排出写者链（不带 `.From` 的写按声明顺序；
  带 `.From(P)` 的写插到 P 最后一次写及已经接在它后面的写之后），沿链编号；再给读定版本：带 `.From(P)` 的读取 P
  最后一次写的版本，不带的取"声明在它之后的下一个不带 `.From` 的写"所覆盖的那个版本（没有这样的写则取最终版本）。
- 新增一种错误 `FromPassWritesNothing`：P 在这个访问之前没有写这个资源。出错的访问按没有 `.From` 处理。
- 用例（`RenderGraphResolveTest.cpp` 新增 10 个，共 26 个）：后声明的 pass 读中间版本，排到下一个写者之前，其余访问的
  版本不变；后声明的 pass 原地改写中间版本，后面的写者与读者拿到它的输出，带 `.From` 的读者仍读到改写前的、排在它
  之前；声明在 P 之后、不带 `.From` 的读者读到插入者的输出；两个写接在同一个 pass 之后按声明顺序相接；接在插入者
  之后的写紧跟它；P 没写这个资源（读、写各一个）与 P 声明在后报错；要求互相矛盾的 pass 排不上；插入的 pass 读回
  自己写出的版本排不上（D2 记的限制）。
- **跑过的**：RenderTest 86 个通过。临时把 `ReflectionsPass` 的 SceneColor 改成 `.From("LightsPass")`，debug layer 下
  打开 `Scene.scene` 并改窗口大小三次：执行顺序变为 `… LightsPass → ReflectionsPass → IndirectDiffusePass → …`，
  无断言、无报错。临时改成 `.From("HZBPass")`：第一帧断言，报出 pass、Scope、资源与 `.From` 的 pass 名。两处临时
  改动都已撤掉。
- **没有跑到的**：现有 pass 都不用 `.From`，提交的代码里没有调用处；`.From` 的当场断言（名字不存在、声明在后、用在
  上一帧读取上、重复 `.From`）没有触发过；带 `.From` 的读、buffer 访问上的 `.From` 只有单元测试；上面那次实验只确认
  了顺序与不报错，没有比对画面；GPU-based validation 没有跑。
- SSR 不需要 `.From`：它按声明顺序排在 Reflections 之前即可，只依赖步骤 3。

---

## 四、改动清单

| 步骤 | 文件 |
|---|---|
| 1 | `RenderGraph/RenderGraphBuilder.cpp`（`BuildGraph`、`TopoSort`） |
| 2 | `RenderGraph/RenderGraphResolve.{h,cpp}`（新）；`RenderGraphBuilder.{h,cpp}`；`Feature/Render/CMakeLists.txt`；`Test/Render/RenderGraphResolveTest.cpp`（新）与其 CMake |
| 3 | `RenderGraphBuilder.{h,cpp}`；`PassScopes.{h,cpp}`；`RenderGraphCompiler.{h,cpp}`（`.BindValid` 的写入）；`Pass/Component/RHIComponents.h`、`PassComponents.h`；各 pass 与 SandBox 示例的调用处（改名） |
| 4 | `PassScopes.{h,cpp}`（`.From`）；`Pass/Component/RHIComponents.h`（`FromPass`）；`RenderGraphResolve.{h,cpp}`；`RenderGraphBuilder.cpp`（报错）；`Test/Render/RenderGraphResolveTest.cpp` |

---

## 五、未决

- 多视图：解析与视图无关，不受影响；"每个视图读写不同资源"仍是 `TODO_MultiViewPlan.md` 的范围。

---

## 关联文档

- `TODO_ScreenSpacePlan.md` —— §五 SSR 的先决条件
- `TODO_RenderGraphItemPlan.md` —— 现有 builder 的设计；「待优化」的"版本解析依赖注册顺序"与"建图的每帧分配"在这里落实
- `TODO_TemporalPlan.md` —— 上一帧读取机制的原始设计（步骤 7），"名字必须在本帧已声明"的约定由本文 D5 取代
