# ShaderInput 设计：消除 ShaderResource，直接绑定

## 动机

当前 SRG 模型有两个人造的复杂度：

1. **Schema/Data 分离**：`ShaderResourceLayout`（描述有哪些输入）+ `ShaderResource`（平铺数组存数据）两个对象，用户需要先创建 layout、再创建 SRG 实例、再通过 index 间接访问数据
2. **强制单 CB 约束**：`PipelineLayout::BuildShaderResourceConstants` 每个 binding slot 只创建一个 CBV root parameter，所有常量被迫塞进一个 buffer，底层 D3D12/Vulkan 无此限制

目标：消除 `ShaderResource` 和 `ShaderResourceLayout`，`ShaderInput` 成为自持描述+数据的一等对象。

## 核心思路：register + space 即跨后端标识

HLSL 的 `register(t0, space1)` 在 Vulkan 对应 `layout(set=1, binding=0)`。ShaderInput 自身的 `(m_registerId, m_spaceId)` 就是跨后端的 bind point 标识，无需 RHI 层再引入额外映射。

```
DX12                              Vulkan
────                              ──────
register(t0, space1)              layout(set=1, binding=3)

Root Signature 查：                  Pipeline Layout 查：
  (SRV, t=0, space=1)                  set=1 → VkDescriptorSetLayout
  → 命中 root parameter[2]             binding=0 → descriptor type + offset

绑定:                                绑定:
  SetGraphicsRootDescriptorTable       vkCmdBindDescriptorSets
  (paramIdx=2, heap_start + offset)    (set=1, VkDescriptorSet)
```

### 后端绑定对照

| RHI | DX12 | Vulkan |
|-----|------|--------|
| `register(t0, space0)` | root param descriptor table + offset | VkDescriptorSet(set=0, binding=0) |
| `register(b1, space0)` | 同上 table，offset 不同 | 同上 set，binding=1 |
| `register(s0, space1)` | 独立 sampler table | VkDescriptorSet(set=1) |
| 小常量 (≤256 bytes) | Root Constants (`SetGraphicsRoot32BitConstants`) | Push Constants (`vkCmdPushConstants`) |
| 大常量 / UBO | CBV descriptor table | VkDescriptorSetLayoutBinding(UNIFORM_BUFFER) |
| 结构化 Buffer Read | SRV descriptor table | VkDescriptorSetLayoutBinding(STORAGE_BUFFER) |
| 结构化 Buffer ReadWrite | UAV descriptor table | VkDescriptorSetLayoutBinding(STORAGE_BUFFER) |

**Vulkan 没有 Root Descriptor**，除 Push Constants 外所有绑定都走 VkDescriptorSet。取公共分母：始终走 descriptor table/set，DX12 后端可在单 descriptor 时优化为 root descriptor（对 RHI 层透明）。

## 当前 vs 目标 数据流

### 当前（SRG 模式）

```
用户代码                         RHI 层                            DX12 后端
───────                         ──────                            ────────
ShaderInput*Descriptor  ──►  ShaderResourceLayout           PipelineLayoutDescriptor
  (纯描述, 无数据)              ├─ 聚合成列表                       ├─ AddShaderResourceLayoutInfo()
                               ├─ 计算 GroupInterval              │   Ptr<ShaderResourceLayout>
                               ├─ name→index map                  │   + ShaderResourceBindingInfo
                               └─ Finalize()                      └─ Finalize()
                                    │                                      │
                                    ▼                                      ▼
                             ShaderResource                        PipelineLayout::Init()
                               ├─ m_imageViews[] 平铺数组          ├─ BuildRootCanstants
                               ├─ m_bufferViews[]                  ├─ BuildShaderResourceConstants (1 CBV/SRG)
                               ├─ m_samplers[]                     ├─ BuildShaderResourceBuffersAndImages
                               ├─ m_constantsData                  ├─ BuildShaderResourceSamplers
                               └─ m_bindingSlot                   └─ Serialize + CreateRootSignature
                                    │
                                    ▼
                          DX12::ShaderResource
                            ├─ m_compiledData[FrameCountMax]
                            │   ├─ m_gpuViewsDescriptorHandle
                            │   ├─ m_gpuSamplersDescriptorHandle
                            │   └─ m_gpuConstantAddress
                            ├─ m_viewsDescriptorTable   (从 DescriptorContext 分配)
                            ├─ m_samplersDescriptorTable
                            └─ m_constantMemoryView

ShaderResourceCompiler
  ├─ AllocateShaderResource: 从 DescriptorContext 分配 per-SRG descriptor table
  ├─ UpdateShaderResource:   遍历 layout 的 ShaderInput 列表 → 读 SRG 平铺数组 → 写 descriptor
  └─ Compiler:               收集所有 dirty SRG，逐个编译

CommandList::SetShaderResourceForDraw(srg)
  └─ GetBindingSlot → slotToIndex → RootParameterBinding
     ├─ SetGraphicsRootDescriptorTable(m_resourceTable, gpuViewsHandle)
     ├─ SetGraphicsRootConstantBufferView(m_constantBuffer, gpuConstantAddress)
     └─ SetGraphicsRootDescriptorTable(m_samplerTable, gpuSamplersHandle)
```

### 目标（ShaderInput 模式）

```
用户代码                         RHI 层                            DX12 后端
───────                         ──────                            ────────
ShaderInput  (自持描述+数据)     RHI::PipelineLayout              DX12::PipelineLayout
  ├─ m_name, m_registerId         ├─ m_spaceGroups[]               ├─ m_spaceBindings[]
  ├─ m_spaceId, m_type, ...         (按 space 分组，                 (DX12SpaceBinding:
  └─ 数据:                           register 即组内 offset)          rootParamIdx)
      ├─ m_imageViews[]                                              │
      ├─ m_bufferViews[]          ShaderInputCompiler              ├─ BuildRootSignature()
      ├─ m_samplers[]             (重命名自 SRG Compiler)          │   按 spaceGroup 生成
      └─ m_constantData               │                            │   descriptor table range
                                      │                            └─ 生成 m_spaceBindings
                                    ShaderBindings      │
                                    ├─ 新建对象           │
                                    ├─ Compiler 每帧产出   │
                                    └─ m_entries[]       │
                                       (按 rootParamIdx   │
                                        索引的 GPU handles)│

CommandList::BindShaderInputs(ShaderBindings&, DX12::PipelineLayout&)
  └─ 遍历 ShaderBindings::m_entries:
     ├─ SetGraphicsRootDescriptorTable(paramIdx, entry.m_gpuDescriptorTable)
     ├─ SetGraphicsRootConstantBufferView(paramIdx, entry.m_gpuConstantAddress)
     └─ SetGraphicsRoot32BitConstants(paramIdx, ...)
```

## 核心数据结构

### 1. ShaderInput — 自持对象

保持与现有 descriptor 平行的类型层次，每个增加 bound data 成员：

```cpp
class ShaderInputConstant {
    InputName m_name;
    uint32_t m_registerId, m_spaceId;
    uint32_t m_byteSize;
    eastl::vector<uint8_t> m_data;
};

class ShaderInputImage {
    InputName m_name;
    ShaderInputImageType m_type;
    ShaderInputImageAccess m_access;
    uint32_t m_count;
    uint32_t m_registerId, m_spaceId;
    eastl::vector<ConstPtr<ImageView>> m_views;
};

class ShaderInputBuffer {
    InputName m_name;
    ShaderInputBufferType m_type;
    ShaderInputBufferAccess m_access;  // Constant → CBV/UBO, Read → SRV/SSBO, ReadWrite → UAV/SSBO
    uint32_t m_count;
    uint32_t m_strideSize;
    uint32_t m_registerId, m_spaceId;
    eastl::vector<ConstPtr<BufferView>> m_views;
};

class ShaderInputSampler {
    InputName m_name;
    uint32_t m_count;
    uint32_t m_registerId, m_spaceId;
    eastl::vector<SamplerState> m_states;
};
```

### 2. SpaceGroup — RHI 层按 space 分组

```cpp
static constexpr uint32_t MaxShaderInputsPerPipeline = 64;
static constexpr uint32_t MaxSpaceGroups = 8;

// RHI::PipelineLayout 持有
struct ShaderInputSpaceGroup {
    uint32_t m_spaceId;
    eastl::small_vector<const ShaderInput*, 8> m_inputs;
    // register 就是组内索引，不需要额外 offset 字段
};
eastl::fixed_vector<ShaderInputSpaceGroup, MaxSpaceGroups> m_spaceGroups;
```

### 3. DX12SpaceBinding — DX12 后端映射

```cpp
// DX12::PipelineLayout 持有 — 紧贴 SpaceGroup 的并行数组
struct DX12SpaceBinding {
    RootParameterIndex m_rootParamIndex;
};
eastl::fixed_vector<DX12SpaceBinding, MaxSpaceGroups> m_spaceBindings;
// m_spaceBindings[i] 对应 m_spaceGroups[i]
```

构建 root signature 时，遍历 SpaceGroup 内 ShaderInput 的 `(type, register, count)` → 生成 `D3D12_DESCRIPTOR_RANGE`，全组共享一个 descriptor table。

Vulkan 后端：SpaceGroup → VkDescriptorSet（set = m_spaceId），组内每个 ShaderInput → VkDescriptorSetLayoutBinding（binding = registerId）。

### 4. ShaderBindings — GPU 资源载体

当前 `DX12::ShaderResource::m_compiledData[FrameCountMax]` 存 GPU handles。去掉 SRG 后，引入新对象，由 Compiler 每帧产出：

```cpp
class ShaderBindings {
public:
    static constexpr uint32_t MaxRootParams = 64;

    struct Entry {
        GpuDescriptorHandle m_gpuDescriptorTable;  // descriptor table 类型
        GpuVirtualAddress  m_gpuConstantAddress;   // CBV 类型
        CpuVirtualAddress  m_cpuConstantAddress;   // 常量更新用
    };

    // 按 root param index 索引，和 root signature 顺序一致
    eastl::fixed_vector<Entry, MaxRootParams> m_entries;
};
```

和当前 `ShaderResourceCompiledData` 的区别：
- 当前：3 个 GPU handle 绑定在一个 SRG 上，每个 SRG 独立分配 descriptor table
- 新：Entry 按 root param 粒度组织，每个 Entry 对应 root signature 中的一个 root parameter
- 多个 ShaderInput 指向同一个 SpaceGroup 时共享一个 descriptor table（Compiler 分配一次，填多条）

## 编译与绑定流程

### Compiler

```
输入: ShaderInput 列表 + PipelineLayout (SpaceGroup + DX12SpaceBinding)
1. 按 SpaceGroup 分组 ShaderInput
2. 为每个 SpaceGroup:
   a. 按 register 最大值确定 descriptor table 大小
   b. 从 DescriptorContext 分配 descriptor table
   c. 遍历组内 ShaderInput → 读 bound data → 按 register 位置写入:
      table[input.registerId] = ConvertDescriptor(input.boundData)
   d. 记录 GPU handle 到 ShaderBindings::Entry
3. 常量处理: 小常量合并为 root constants（≤256 bytes）；大常量走 CBV
4. 输出: ShaderBindings
```

### CommandList 绑定

```
输入: ShaderBindings + DX12::PipelineLayout
for each non-null Entry in ShaderBindings.m_entries:
    SetGraphicsRootDescriptorTable(rootParamIdx, entry.m_gpuDescriptorTable)
    // 或 SetGraphicsRootConstantBufferView(rootParamIdx, entry.m_gpuConstantAddress)
    // 或 SetGraphicsRoot32BitConstants(rootParamIdx, ...)
```

不再读 SRG → 不再有 slotToIndex / dedup cache / per-SRG compiled data。

## 改动范围

### 删除（~16 个文件）

| 文件 | 原因 |
|------|------|
| `RHI/.../ShaderResource.h/.cpp` | 被 ShaderInput 替代 |
| `RHI/.../ShaderResourceLayout.h/.cpp` | 职责并入 PipelineLayout + SpaceGroup |
| `RHI/.../ShaderResourceLayoutCommon.h` | `ShaderInputIndex` 不再需要 |
| `RHI/.../ShaderResourcePool.h/.cpp` | DescriptorContext 已管理 heap |
| `RHI/.../ShaderResourcePoolDescriptor.h` | 配套 |
| `RHI/.../ShaderResourceCompiler.h/.cpp` | 重写为 ShaderInputCompiler |
| `RHI/.../ConstantsLayout.h/.cpp` | 每个 ShaderInput 自己管理常量 |
| `RHI/.../ConstantsData.h/.cpp` | 常量数据下沉到 ShaderInput |
| `DX12/.../ShaderResource/ShaderResource.h/.cpp` | GPU compiled data 移到 ShaderBindings |
| `DX12/.../ShaderResource/ShaderResourcePool.h/.cpp` | 配套 |
| `DX12/.../ShaderResource/ShaderResourceCompiler.h/.cpp` | 重写 |

### 重写（核心）

| 文件 | 改动 |
|------|------|
| `ShaderResourceDescriptor.h` | 重写为 ShaderInput.h，每个 descriptor 加 bound data |
| `DX12/Pipeline/PipelineLayout.cpp` | 不再每 slot 一个 CBV；按 SpaceGroup 映射到 root params |
| `DX12/Command/CommandList.cpp` | SetShaderResourceForDraw → BindShaderInputs |
| `RHI/Pipeline/PipelineLayoutDescriptor.h/.cpp` | 不再存 ShaderResourceLayout，改为 SpaceGroup 列表 |
| `RHI/Component/Component.h` | Components::ShaderResource → ShaderBindings |

### 适配

| 文件 | 改动量 |
|------|--------|
| `RHI/Factory.h/.cpp`, `DX12/ID3D12Factory.h/.cpp` | 中等 |
| `Render/RenderGraph/RenderGraphCompiler.cpp` | 中等 |
| `Render/RenderGraph/RenderGraphExecuter.cpp` | 中等 |
| `Render/Pass/PassBuilder.h`, `PassComponents.h` | 中等 |
| `SandBox/Program/**/DrawShape.cpp, *PassFeature.cpp` | 小 |
| `DX12/Conversions.h`, `RHIComponents.h`, `DX12/Pipeline/PipelineLayoutDescriptor` | 小 |

总计改动：约 **25-28 个文件**

## 设计决策

### ShaderInput 持有 dirty flag

当前 ECS entity + `ShaderResourceUpdateTag` 的 dirty 标记机制替换为 ShaderInput 自身的 dirty flag。Compiler 收集所有 dirty 的 ShaderInput → 按 pass/PipelineLayout 分组编译 → 产出 ShaderBindings。

### ShaderBindings 的生命周期

和当前 `PassCompiledPSO` 类似，作为 pass 编译产出存在 PassContext 上。跨 pass 共享的 ShaderInput 组（如 ViewSRG）可独立编译并放自己的 ECS entity 上，多个 pass 引用同一个 entity。

### 常量处理

PipelineLayout 构建时自动判断每个 ShaderInputConstant：
- `byteSize ≤ 256` 且有可用 slot → 合并为 root constants / push constants
- 大常量 → CBV descriptor（始终走 descriptor table/set，保持 Vulkan 兼容）
- 多个小常量合并到一个 root constant range / VkPushConstantRange

### 为什么不用 BindPointMapping（per-input 映射表）

`register` 本身就可以充当 descriptor table offset。按 space 分组（SpaceGroup）后，`table[input.registerId]` 直接写，不需要维护额外的 `descriptorOffset` 字段。register 号紧凑是 HLSL 的常态，稀疏情况极少见，且可降级为多个小 SpaceGroup。

## 验证

1. 编译通过：`cmake --build build --config Debug`
2. 运行现有 demo：DrawShape、TrianglePassFeature、MSAAPassFeature
3. 关键验证点：常量绑定、纹理/采样器绑定、多 pass 自动绑定、root signature 结构与改动前一致
