# Per-Draw PSO Variant 支持

## 约束

- **同一 pass 内所有 PSO 共享 `PipelineLayoutDescriptor`**。这是引擎的架构契约，不是底层 API 的限制。因为 `PassShaderResources` 声明了 slot→layout 映射，`BuildPipelineLayoutDescriptor` 据此生成一份确定的 layout。如果 per-draw 切一个不同 layout 的 PSO，SRG 绑定会错位。
- `PipelineLayoutDescriptor` 由 compiler 自动生成，外部不应直接创建。

## 方案 A：PassBuilder ShaderVariant（少量变体）

适合已知变体数量有限（2-5 个）的场景，如 Forward pass 的 `Default` / `Skinning` / `Wireframe`。

### PassBuilder API

```cpp
SPARK_RENDER_PASS(passCtx, "Main")
    .InputLayout(defaultLayout)
    .RenderTargetLayout(rtLayout)
    .VertexShader(defaultVS)
    .FragmentShader(defaultFS)
    .ShaderVariant("Skinning",     /* 替换 VS  */ skinningVS,  nullptr)
    .ShaderVariant("Wireframe",    /* 替换 FS+RS*/ nullptr,     wireframeFS)
    .ShaderResource(0, m_viewSRGEntity)
    .ShaderResource(1, m_sceneSRGEntity)
    .ShaderResource(2, m_materialLayout)
    .Execute([this](ExecuteWork& work, RenderGraphExecuter&)
    {
        auto& cmdList = *work.m_commandList;
        // pass-begin: default PSO + PerPass SRG 已自动绑好

        for (auto& draw : draws)
        {
            if (draw.m_variantIndex != m_lastVariant)
            {
                cmdList.SetPipelineState(*work.m_variants[draw.m_variantIndex]);
                m_lastVariant = draw.m_variantIndex;
            }
            cmdList.SetShaderResourceForDraw(*draw.m_srg);
            cmdList.Submit(draw.m_item);
        }
    });
```

`ShaderVariant(name, ...)` 记录与 default PSO 的**差异**（哪个 shader stage 替换、render state 覆盖），compiler 创建 PSO 时复制 default 的其余配置，只改差异部分。

### 组件

```cpp
// 替代 PassCompiledPSO，放在 Pass entity 上
struct PassCompiledPSOVariants
{
    // 主 PSO（variantIndex == 0 留给 default）
    Ptr<RHI::PipelineState> m_defaultPso;
    // 变体 PSO 列表，index 按 ShaderVariant 注册顺序（1-based）
    eastl::vector<Ptr<RHI::PipelineState>> m_variantPsos;
};
```

### Compiler

1. `ComputePSOForPass` 接受 `PassPipelineState + PassShaders + ShaderVariant` 差异
2. `BuildPipelineLayoutDescriptor` 返回的 layout 在所有 variant 间复用
3. 校验：每个 variant PSO 的 `m_pipelineLayoutDescriptor` == default PSO 的（指针或 hash）
4. 写入 `PassCompiledPSOVariants`

### Executer

- `ExecuteBindPSO` 设置 `m_defaultPso`
- `ExecuteWork` 加字段：`eastl::vector<const RHI::PipelineState*> m_variants`（裸指针）
- lambda 自己管理 variant 切换
- per-draw PSO 切换在 `m_shaderResources` 绑定之前（同 layout 下 `SetPipelineState` 不清 SRG 状态缓存）

### 数据流

```
Build:
  PassBuilder.ShaderVariant("Skinning", skinningVS) → 存入 PassShaderVariants 组件

Compile:
  对每个 variant: 复制 default descriptor → 覆盖差异 stage → Init(device, desc, library)
  → 校验 layout == default layout → 存入 PassCompiledPSOVariants

Execute:
  executer: SetPipelineState(defaultPso) + BindPerPassSRGs
  lambda:   for each draw → switch PSO if needed → bind SRGs → Submit
```

---

## 方案 B：PipelineStateCache Service（大量变体 / 数据驱动）

当变体数量不受控（如 shader permutation 爆炸、材质系统驱动 PSO 创建），方案 A 的 builder 枚举方式不够用。此时引入独立的 `PipelineStateCache` Service。

### 架构

```
PipelineStateCache (Service, 独立于 RenderGraph)
  ├─ Acquire(key) → Ptr<PipelineState>         // 查缓存或创建新 PSO
  ├─ key = { PipelineLayoutDescriptor*, ShaderStageFunction*, RenderStates, InputStreamLayout, RenderTargetLayout }
  └─ 内部: hash map dedup + LRU eviction（可选）
```

PassBuilder 只声明 layout（通过 `PassShaderResources`），不直接持有任何 PSO。Compiler 负责调 `Cache::Acquire` 创建 default PSO。DrawItem 提取管线按需调 `Cache::Acquire` 创建 variant PSO。

### 关键点

- **Cache key 不含 `PipelineLayoutDescriptor*` 的 hash 比较**——同一 pass 内所有 PSO 共享同一 layout，key 的 layout 部分可直接用指针相等
- **`PipelineStateCache` 不耦合 RenderGraph**——它是独立的 Service，任何层都可以 `Service<PipelineStateCache>::Get()->Acquire(key)`
- **Pass 的 default PSO 仍然是 compiler 创建并写入 `PassCompiledPSO`**——executer auto-bind 需要 default PSO
- **Per-draw variant PSO 由提取管线从 Cache 获取**——DrawItem entity 上存储 `Ptr<PipelineState>`（variant PSO 或 nullptr = 用 default）

### DrawItem 上的 PSO 引用

### 关键设计：PipelineStateCache 是 compiler 内部工具，非全局 Service

`PipelineLayoutDescriptor` 是 compiler 的产物，不能暴露到 render graph 外部。因此 `PipelineStateCache` 不是全局 Service，而是 **compiler 内部用的工具类**。`PipelineLayoutDescriptor` 自始至终不出 compiler 的栈。

### 两阶段数据流

DrawItem 在 Build 阶段只提供**变体的输入条件**（shader key），**变体的结果索引**等 compiler 创建完 PSO 之后回填：

```
Build 阶段（提取管线写入）:          Compile 阶段（compiler 写入）:

  ┌──────────────────────┐             ┌──────────────────────┐
  │ DrawItemData          │             │ PassCompiledPSOVariants│
  │   m_args, m_vb, m_ib  │             │   m_defaultPso        │
  │   m_materialSrgs      │             │   m_variantPsos[]      │
  │   m_variantShaderKey ←┼── 输入      │   m_variantIndex ←────┼── 结果回填
  └──────────────────────┘             └──────────────────────┘
```

**提取管线**有材质信息，产出 `m_variantShaderKey`（如 `{ PixelShader: "PbrStandard", Flags: NORMALMAP | EMISSIVE }`），这是描述性的标识，跟 PSO 是否创建无关。

**Compiler** 扫该 pass 下所有 DrawItem，收集唯一 shader key → 调 `PipelineStateCache::Acquire` 创建 PSO → 按创建顺序分配 index → 回写到 `DrawItemData::m_variantIndex`。

### Compiler 逻辑

```cpp
// Compile 阶段
eastl::hash_map<ShaderVariantKey, uint32_t> keyToIndex;
keyToIndex[defaultKey] = 0;  // 0 留给 default
eastl::vector<Ptr<PipelineState>> variants = { defaultPso };

auto drawItems = ctx.GetView<MainPassTag, DrawItemData>();
drawItems.each([&](DrawItemData& item)
{
    if (item.m_variantShaderKey == defaultKey) return;
    
    auto [it, inserted] = keyToIndex.try_emplace(item.m_variantShaderKey, (uint32_t)variants.size());
    if (inserted)
    {
        auto pso = pipelineStateCache.Acquire(layout, item.m_variantShaderKey, renderStates);
        variants.push_back(pso);
    }
    item.m_variantIndex = it->second;  // ← 回填索引
});

ctx.Add<PassCompiledPSOVariants>(passEntity, { defaultPso, eastl::move(variants) });
```

### DrawItemData

```cpp
struct DrawItemData
{
    DrawArguments    m_args;

    // Geometry
    VertexInputView m_vb;
    IndexBufferView m_ib;

    // Per-draw SRGs
    eastl::fixed_vector<Ptr<RHI::ShaderResource>, 2> m_materialSrgs;
    Ptr<RHI::ShaderResource> m_perDrawSrg;

    // PSO variant — Build 阶段写入 shader key，Compile 阶段回填 index
    ShaderVariantKey m_variantShaderKey;  // 提取管线写入（输入条件）
    uint32_t         m_variantIndex = 0;  // compiler 回填（结果索引），0 = default
};
```

### Execute 示例

Lambda 直接用 `m_variantIndex`，这个值在 Compile 阶段已经填好了：

```cpp
SPARK_RENDER_PASS(passCtx, "Main")
    .ShaderResource(0, m_viewSRGEntity)
    .ShaderResource(1, m_sceneSRGEntity)
    .ShaderResource(2, m_materialLayout)
    .Execute([this](ExecuteWork& work, RenderGraphExecuter&)
    {
        auto& cmdList = *work.m_commandList;
        // default PSO + PerPass SRG 已自动绑好

        const RHI::PipelineState* currentPso = work.m_defaultPso;

        auto view = ctx.GetView<MainPassTag, DrawItemData>();
        view.each([&](const DrawItemData& item)
        {
            const auto* pso = work.m_variants[item.m_variantIndex];
            if (pso != currentPso)
            {
                cmdList.SetPipelineState(*pso);
                currentPso = pso;
            }

            for (auto& srg : item.m_materialSrgs)
                cmdList.SetShaderResourceForDraw(*srg);
            cmdList.SetVertexBuffer(item.m_vb);
            cmdList.SetIndexBuffer(item.m_ib);
            cmdList.DrawIndexed(item.m_args);
        });
    });
```

### 方案对比

| | 方案 A: ShaderVariant | 方案 B: PipelineStateCache |
|---|---|---|
| 适用场景 | 少量（2-5）已知变体 | 大量 / 数据驱动变体 |
| 变体声明 | PassBuilder 枚举 | DrawItem 携带 shader key |
| PipelineStateCache 角色 | N/A | compiler 内部工具类 |
| PSO 生命周期 | Pass entity 持有 | compiler 持有（栈内） |
| PipelineLayoutDescriptor | 不出 compiler | 不出 compiler |
| Dedup | N/A（手动枚举） | hash map 自动 |
| 复杂度 | 低 | 中 |

### 当前决策

当前先落实方案 A（ShaderVariant），满足手工 pass 的少量变体需求。方案 B 的 `PipelineStateCache` 在材质系统 / shader permutation 需求明确后再启动，但其核心约束已确定：**它是 compiler 内部工具类，不做全局 Service，PipelineLayoutDescriptor 不离开 compiler 栈**。方案 A 的 `PassCompiledPSOVariants` 不阻塞未来迁移到方案 B。
