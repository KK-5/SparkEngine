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
用户代码                              RHI 层                          DX12 后端
───────                              ──────                          ────────
ShaderBindings (per-HLSL-space)      RHI::PipelineLayout            DX12::PipelineLayout
  Descriptor:                          ├─ m_spaceGroups[]             ├─ m_spaceRootParams[]
    layout, spaceId                    │   (按 space 分组)             │   (per-space RootParameterBinding)
  Init: 按 SpaceGroup 创建             │                               │
    ├─ vector<ShaderInputBuffer>    ShaderInputCompiler              ├─ BuildRootSignature
    ├─ vector<ShaderInputImage>     (per-ShaderBindings 编译)        │   按 SpaceGroup 生成
    ├─ vector<ShaderInputSampler>       │                            │   descriptor table range
    └─ vector<ShaderInputConstant>      │                            └─ m_spaceRootParams[i]
  用户:                                 │                               对应 m_spaceGroups[i]
    FindBufferInput(name)->SetView(..)  │
    FindConstantInput(name)->SetData()  │
                                        ▼
                              DX12::ShaderBindings (派生)
                                ├─ m_viewsTable    (一次性分配, ring-buffered)
                                ├─ m_samplersTable
                                ├─ m_constantMemory
                                ├─ m_entry (per-space, 至多 3 个 root param)
                                │    { gpuDescriptorTable, gpuConstantAddress, ... }
                                └─ m_dirty (≤3 bit: views/samplers/constants)

CommandList::BindShaderBindings(ShaderBindings&, DX12::PipelineLayout&)
  └─ 取 DX12::PipelineLayout::FindSpaceBindingBySpaceId(bindings.GetSpaceId()) →
       RootParameterBinding{ m_constantBuffer, m_resourceTable, m_samplerTable }
     按 ShaderBindings 的 dirty 位绑定:
       SetGraphicsRootDescriptorTable(m_resourceTable, ...)
       SetGraphicsRootConstantBufferView(m_constantBuffer, ...)
       SetGraphicsRootDescriptorTable(m_samplerTable, ...)
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

### 2. ShaderInputGroup — RHI 层按 space 分组

```cpp
// PipelineLayoutDescriptor / RHI::PipelineLayout 持有
// 引用 PipelineLayoutDescriptor 各类型 descriptor 数组中的下标，避免裸指针稳定性问题
struct ShaderInputHandle {
    ShaderInputType m_type  = ShaderInputType::Count;
    uint32_t        m_index = 0; // 在 m_bufferDescs / m_imageDescs / m_samplerDescs / m_constantDescs 中的位置
};

struct ShaderInputGroup {
    uint32_t        m_spaceId   = 0;
    ShaderStageMask m_stageMask = ShaderStageMask::None; // AddShaderInputDescriptors 时做并集更新
    eastl::fixed_vector<ShaderInputHandle, 8> m_shaderInputs; // 纯 layout 引用，无数据
};
eastl::fixed_vector<ShaderInputGroup, Limits::Pipeline::ShaderInputGroupCountMax> m_spaceGroups;
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

构建 root signature 时，遍历 SpaceGroup 内每个 `ShaderInputRef`，通过 `(type, index)` 查 PipelineLayoutDescriptor 的 descriptor 数组，取 `(registerId, count)` → 生成 `D3D12_DESCRIPTOR_RANGE`，全组共享一个 descriptor table。

Vulkan 后端：SpaceGroup → VkDescriptorSet（set = m_spaceId），组内每个 ref 对应的 descriptor → VkDescriptorSetLayoutBinding（binding = registerId）。

### 4. ShaderBindings — per-space 自持对象

ShaderBindings 取代旧的 `ShaderResource`，但**粒度从 per-SRG 改为 per-HLSL-space**。一个 ShaderBindings 实例对应 `PipelineLayoutDescriptor::m_spaceGroups[i]`，自持该 space 的所有 ShaderInput 数据 + 后端 compile 产物。

```cpp
// RHI 层：抽象基类，对外提供 ShaderInput 数据访问
class ShaderBindings : public DeviceObject
{
public:
    struct Descriptor {
        ConstPtr<PipelineLayoutDescriptor> m_layout;
        uint32_t                           m_spaceId = 0;  // HLSL space number
    };

    ResultCode Init(Device& device, const Descriptor& descriptor);
    void       Shutdown() override;

    // 按 name 找单条 ShaderInput，用户对其 SetView / SetData / SetState
    ShaderInputBuffer*   FindBufferInput(const InputName& name);
    ShaderInputImage*    FindImageInput(const InputName& name);
    ShaderInputSampler*  FindSamplerInput(const InputName& name);
    ShaderInputConstant* FindConstantInput(const InputName& name);

    // 编译用迭代
    eastl::span<const ShaderInputBuffer>   GetBufferInputs()   const;
    eastl::span<const ShaderInputImage>    GetImageInputs()    const;
    eastl::span<const ShaderInputSampler>  GetSamplerInputs()  const;
    eastl::span<const ShaderInputConstant> GetConstantInputs() const;

    uint32_t                              GetSpaceId()          const { return m_spaceId; }
    const PipelineLayoutDescriptor&       GetLayoutDescriptor() const { return *m_layoutDescriptor; }

protected:
    virtual ResultCode InitInternal(Device& device, const Descriptor& descriptor) = 0;
    virtual void       ShutdownInternal() = 0;

private:
    void BuildInputsFromLayout(const Descriptor& descriptor);  // 按 SpaceGroup 创建 ShaderInput 对象

    ConstPtr<PipelineLayoutDescriptor> m_layoutDescriptor;
    uint32_t                           m_spaceId = 0;
    eastl::vector<ShaderInputBuffer>   m_buffers;
    eastl::vector<ShaderInputImage>    m_images;
    eastl::vector<ShaderInputSampler>  m_samplers;
    eastl::vector<ShaderInputConstant> m_constants;
};
```

**典型用法**：

```cpp
ShaderBindings::Descriptor desc{ pipelineLayoutDesc, /*spaceId*/0 };
auto bindings = factory.CreateShaderBindings(desc);

bindings->FindBufferInput("PerView")->SetView(0, viewBuf);
bindings->FindImageInput("AlbedoTex")->SetView(0, albedoView);
bindings->FindConstantInput("Material")->SetData(&matCb, sizeof(matCb));

// 每帧（或脏时）
compiler.Compile(*bindings);

// 绑定
commandList.BindShaderBindings(*bindings, pipelineLayout);
```

和旧 `DX12::ShaderResource::m_compiledData` 的区别：
- 旧：3 个 GPU handle 绑定在一个 SRG 上，SRG 是 layout + data 平铺数组的间接对象
- 新：ShaderBindings 自持 ShaderInput 数据（per-name SetView/SetData，无平铺数组与 index 查表），并在后端派生类中持有 compile 产物
- 一个 Pass / Material 持有 N 个 ShaderBindings（每个用到的 space 一个），不再有跨 space 的全局 root param 表

## 编译与绑定流程

### Compiler

```
输入:
  - ShaderBindings& (自持 ShaderInput 数据 + spaceId + layout)

1. 取 bindings.GetLayoutDescriptor() / GetSpaceId() → SpaceGroup（layout.FindSpaceGroupBySpaceId）
2. 首次编译（DX12 ShaderBindings::m_layoutHash == 0）:
   a. 计算 resource table 大小: 遍历 SpaceGroup::m_shaderInputs 累加 m_count
   b. 从 DescriptorContext 分配 resource table（CBV+SRV+UAV, ring-buffered × FrameCountMax）
   c. 同样分配 sampler table（仅当存在 sampler）
   d. 同样分配 constant buffer 内存（仅当存在 constant）
   e. 记录 m_layoutHash = layout.GetHash()
3. 推进 ring index
4. 写 resource descriptors（顺序与 SpaceGroup::m_shaderInputs 一致，累加 offset）:
   Buffer(Constant)  → CBV @ offset
   Buffer(Read)      → SRV @ offset
   Buffer(ReadWrite) → UAV @ offset
   Image(Read)       → SRV @ offset
   Image(ReadWrite)  → UAV @ offset
   offset += GetDescription().m_count
5. 写 sampler descriptors（独立 table）
6. 常量 memcpy 到 constant buffer
7. 设置 dirty 位（views / samplers / constants）
```

### CommandList 绑定

```
输入: ShaderBindings + DX12::PipelineLayout
RootParameterBinding rp = pipelineLayout.FindSpaceBindingBySpaceId(bindings.GetSpaceId())

if (dirty & DirtyResources)
    SetGraphicsRootDescriptorTable(rp.m_resourceTable, m_viewsTable.GetGpuHandle())
if (dirty & DirtyConstants)
    SetGraphicsRootConstantBufferView(rp.m_constantBuffer, m_constantMemory.GetGpuAddress())
if (dirty & DirtySamplers)
    SetGraphicsRootDescriptorTable(rp.m_samplerTable, m_samplersTable.GetGpuHandle())
dirty = 0
```

不再读 SRG → 不再有 slotToIndex / dedup cache / per-SRG compiled data。多个 ShaderBindings（Pass 持有的多个 space）各自管理 dirty，CommandList 按顺序绑即可。

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
| `ShaderResourceDescriptor.h` | 拆为 `ShaderInputDescriptor.h`（纯 layout）+ `ShaderInput.h`（data，由 Pass/Material 持有） |
| `DX12/Pipeline/PipelineLayout.cpp` | 不再每 slot 一个 CBV；按 SpaceGroup 映射到 root params |
| `DX12/Command/CommandList.cpp` | SetShaderResourceForDraw → BindShaderBindings(per-space ShaderBindings) |
| `RHI/Pipeline/PipelineLayoutDescriptor.h/.cpp` | 只存 `ShaderInputXXXDescriptor` + `ShaderInputSpaceGroup`；不持有任何 ShaderInput 数据对象 |
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

### 所有权边界（核心规则）

```
PipelineLayoutDescriptor   →  只持有 ShaderInputXXXDescriptor（纯 layout）
                               用于构建 SpaceGroup 和 root signature
                               不持有任何 views / states / bytes
                               字段: vector<ShaderInputBufferDescriptor>
                                     vector<ShaderInputImageDescriptor>
                                     vector<ShaderInputSamplerDescriptor>
                                     vector<ShaderInputConstantDescriptor>

ShaderBindings (per-HLSL-space)
                            →  由 Pass / Material 实例自己持有（每个 space 一个 ShaderBindings）
                               Init(layout, spaceId) 时按 SpaceGroup 自动建出
                                 所有 ShaderInputBuffer/Image/Sampler/Constant
                               用户 FindXxxInput(name)->SetView/SetData
                               后端派生类持有 compile 产物（descriptor table、CBV memory）
```

**为什么不能让 PipelineLayoutDescriptor 持有含数据的 ShaderInput：** Pipeline 是跨 draw 共享的静态对象；ShaderInput 的 data 是 per-draw/per-material 的动态状态。如果二者混在一起，同一帧内两个使用相同 pipeline 但绑定不同资源的 drawcall 就无法独立持有各自的数据。

**为什么 ShaderBindings 的粒度是 space 而不是 Pass：**
- 一个 HLSL space 对应 Vulkan 一个 `VkDescriptorSet`、DX12 一组 root params（resource table + sampler table + 可选 CBV），是天然的绑定单元
- per-space 粒度让 ShaderBindings 自身的 dirty 标记退化为最多 3 个布尔位，CommandList 绑定循环只看自己这一组 root param
- 跨 pass 共享的 binding 集（例如 PerView）只需一个 ShaderBindings 对象，多个 pass 引用即可——粒度细 = 复用更精确

### ShaderInput 的 dirty flag

ShaderInput 数据现在归 ShaderBindings 持有。dirty 状态由 ShaderBindings 自身（具体为后端派生类）跟踪：
- ShaderInput 本体不带 dirty 成员（保持值语义纯洁）
- 后端 ShaderBindings 在 Compile 时基于 "本帧用户是否动过对应输入" 决定写哪些 descriptor，置位对应 dirty bit
- CommandList 按 dirty bit 决定绑哪些 root param，绑完清零

最简实现：每次 Compile 都全量重写自己的 descriptor table，dirty bit 仅用于跳过未使用的 root param 类别（例如没有 sampler 时不绑 sampler table）。

### ShaderBindings 的生命周期

由 Pass / Material 直接持有，每用到一个 HLSL space 就一个实例。Init 时绑定 layout + spaceId 不变（layout 变化 = 重新 Init）。跨 pass 共享的数据（如全局 ViewBuffer 对应的 space）单独建一个 ShaderBindings，通过引用被多个 pass 共享。

### 常量处理

PipelineLayout 构建时（从 `ShaderInputConstantDescriptor`）自动判断：
- `constantByteCount ≤ 32` 且有可用 slot → root constants / push constants
- 其余 → CBV descriptor table（始终走 descriptor table/set，保持 Vulkan 兼容）
- 多个小常量合并到一个 root constant range / VkPushConstantRange

### 为什么不用 BindPointMapping（per-input 映射表）

`register` 本身就可以充当 descriptor table offset。按 space 分组（SpaceGroup）后，`table[input.registerId]` 直接写，不需要维护额外的 `descriptorOffset` 字段。register 号紧凑是 HLSL 的常态，稀疏情况极少见，且可降级为多个小 SpaceGroup。

## 实施进展

### 2026-05-28 — Step 1 完成: ShaderInputDescriptor + ShaderInput 类型

**新建文件** `RHI/Resource/ShaderInput/`:
- `ShaderInputDescriptor.h/.cpp` — 从 `ShaderResourceDescriptor` 抄出纯描述类型，去掉 UnboundedArray
- `ShaderInput.h` — 自持类型（描述 + 数据），全部 inline 实现

**四个 ShaderInput 类型**:

| 类型 | 数据存储 | 主要方法 |
|---|---|---|
| `ShaderInputBuffer` | `fixed_vector<ConstPtr<BufferView>, 16>` | `SetView(index, view)`, `GetView(index)`, `SetViews(span)`, `GetViews()` |
| `ShaderInputImage` | `fixed_vector<ConstPtr<ImageView>, 16>` | 同上 |
| `ShaderInputSampler` | `fixed_vector<SamplerState, 8>` | `SetState(index, state)`, `GetState(index)`, `SetStates(span)`, `GetStates()` |
| `ShaderInputConstant` | `vector<uint8_t>` | `SetData(bytes, count)`, `SetData(bytes, offset, count)`, `GetData()` |

**设计决策**（实施中调整）：
- 每个 ShaderInput 包含 `m_descriptor`（组合，不扁平化），通过 `GetDescription()` 访问
- 构造时从 descriptor 的 `m_count` / `m_constantByteCount` 确定数据容器大小
- 无 UnboundedArray（后续以普通类型 + flag 方式处理）
- 无 dirty flag（资源管理交上层）
- 不继承 Object，纯值语义
- `ShaderInputConstant` 和 `ShaderInputBuffer` 保留区分——前者支持 root constants 优化合并，后者是完整的 GPU buffer binding

**ShaderInputGroup 设计**（已实现）：

```cpp
// 引用 PipelineLayoutDescriptor 各类型 descriptor 数组中的下标，不持有数据
struct ShaderInputHandle {
    ShaderInputType m_type  = ShaderInputType::Count;
    uint32_t        m_index = 0;
};

struct ShaderInputGroup {
    uint32_t        m_spaceId   = 0;
    ShaderStageMask m_stageMask = ShaderStageMask::None;
    eastl::fixed_vector<ShaderInputHandle, 8> m_shaderInputs;
};
```

- Key = `spaceId`，分组用 `fixed_vector<ShaderInputGroup, ShaderInputGroupCountMax>` + 线性查找
- `stageMask` 存在 group 上，由多次 `AddShaderInputDescriptors` 做并集更新
- 用户不感知 group——由 `InsertShaderInput()` 内部自动分组
- DX12 后端通过 `m_stageMask` 设置 `D3D12_SHADER_VISIBILITY`
- 用下标而非裸指针，避免 descriptor vector 扩容后指针失效

**ShaderInputList**（反射中间容器）:

```cpp
// 纯 descriptor 容器，由 ShaderAsset 反射产出，消费后销毁
struct ShaderInputList {
    eastl::vector<ShaderInputBufferDescriptor>   m_buffers;
    eastl::vector<ShaderInputImageDescriptor>    m_images;
    eastl::vector<ShaderInputSamplerDescriptor>  m_samplers;
    eastl::vector<ShaderInputConstantDescriptor> m_constants;
};
```

从 ShaderAsset 反射产出纯 layout 的临时集合，通过 `PipelineLayoutDescriptor::AddShaderInputDescriptors(list, stageMask)` 消费后销毁。

**数据流**:
```
ShaderAsset 反射 → ShaderInputList (临时, 纯 descriptor)
                        │
                        ▼ AddShaderInputDescriptors(list, stageMask)
                   PipelineLayoutDescriptor (layout 唯一持有者)
                     ├─ vector<ShaderInputBufferDescriptor>   m_bufferDescs
                     ├─ vector<ShaderInputImageDescriptor>    m_imageDescs
                     ├─ vector<ShaderInputSamplerDescriptor>  m_samplerDescs
                     ├─ vector<ShaderInputConstantDescriptor> m_constantDescs
                     ├─ fixed_vector<ShaderInputSpaceGroup, 8> m_spaceGroups  (引用上面数组的下标)
                     └─ 查询入口: FindBufferDescriptor(name) / FindImageDescriptor(name) / ...

Pass / Material (持有 N 个 ShaderBindings，N = 用到的 space 数)
  └─ ShaderBindings (per-space, 自持 ShaderInput 数据)
        ├─ vector<ShaderInputBuffer>   m_buffers   { desc, views[] }
        ├─ vector<ShaderInputImage>    m_images    { desc, views[] }
        ├─ vector<ShaderInputSampler>  m_samplers  { desc, states[] }
        └─ vector<ShaderInputConstant> m_constants { desc, bytes[] }
       │
       ▼ ShaderInputCompiler.Compile(bindings)
  DX12::ShaderBindings (后端派生, 持有 descriptor table + GPU handles + dirty 位)
```

> **注**：旧设计草案曾让 Pass / Material 直接持有零散的 ShaderInput*，由 Compiler 通过 span 收集。Step 4 调整为 per-space ShaderBindings 自持 ShaderInput 数据，这是当前生效的设计。

### 2026-05-29 — Step 2 完成：PipelineLayoutDescriptor 新路径 API

**改动文件**：
- `RHI/Pipeline/PipelineLayoutDescriptor.h` — 新增 `ShaderInputHandle`、`ShaderInputGroup`、`ShaderInputList` 结构体；新增完整新路径 API；`ValidateShaderInputOverlapInternal` 虚函数
- `RHI/Pipeline/PipelineLayoutDescriptor.cpp` — 实现 `AddShaderInputDescriptors`、`InsertShaderInput`、所有新路径访问器
- `RHI/RHILimits.h` — 新增 `Limits::VulkanBindingShift`（CBV=0, SRV=1000, UAV=2000, Sampler=3000）

**新增 API**（新路径，旧 SRG 路径不动）：

| 方法 | 用途 |
|------|------|
| `AddShaderInputDescriptors(list, stageMask)` | 批量消费 ShaderInputList，自动按 spaceId 分组 |
| `AddStaticSamplerDescriptor(desc, stageMask)` | 静态采样器（DX12 static sampler，不占 root param） |
| `UsesShaderInputPath()` | 判断走新路径还是旧 SRG 路径 |
| `GetSpaceGroup(index)` / `GetSpaceGroupCount()` | DX12 build loop 入口 |
| `GetBufferDescriptor(i)` / `GetImageDescriptor(i)` 等 | SpaceGroup loop 内按下标取 descriptor |
| `FindBufferDescriptor(name)` / `FindImageDescriptor(name)` 等 | 按名字查找（调试/校验用，线性扫描） |
| `GetStaticSamplerDescriptor(i)` / `GetStaticSamplerStageMask(i)` | 静态采样器访问 |

**Binding 冲突检查设计**（讨论确定）：

DX12 和 Vulkan 的寄存器命名空间模型根本不同：
- **DX12**：同一 space 内 `b`/`t`/`u`/`s` 是 4 个独立命名空间，`b0` 和 `t0` 不冲突
- **Vulkan**：同一 descriptor set 内所有 binding 是平坦命名空间，需要 shift 区分

采用 **WickedEngine 风格** 的 shift 方案：
```
Limits::VulkanBindingShift::CBV     =    0   // b registers
Limits::VulkanBindingShift::SRV     = 1000   // t registers
Limits::VulkanBindingShift::UAV     = 2000   // u registers
Limits::VulkanBindingShift::Sampler = 3000   // s registers
```

这三处必须使用同一套常量保持一致：
1. DXC 编译 HLSL→SPIR-V 时的 `-fvk-X-shift` 参数（ShaderAsset 负责）
2. Vulkan backend 运行时写 descriptor 时加的偏移（Vulkan backend 负责）
3. `ValidateShaderInputOverlapInternal` 里的冲突检查（backend 实现）

**`ValidateShaderInputOverlapInternal` 虚函数**：

```cpp
// 默认：no-op。各 backend override 实现 API-specific 冲突检查。
// DX12 backend：按 (HlslRegNs, registerId) 范围检查 + Vulkan flat binding 检查
// Vulkan backend：按 (VulkanBindingShift[ns] + registerId) 范围检查
virtual void ValidateShaderInputOverlapInternal(
    const ShaderInputHandle& newHandle,
    const ShaderInputHandle& existingHandle,
    uint32_t spaceId) const;
```

abstract RHI 只做调用，完全不出现 CBV/SRV/UAV/Sampler 字样。CBV/SRV 等 DX12 register namespace 概念属于 backend，`HlslRegNs` enum 和 `kVkBindingShift` 查表逻辑在 DX12 backend 的 override 里实现。

**新增 CLAUDE.md 原则**：
> 禁止出现"等 Vulkan 实装后再处理"的设计决策。RHI 抽象层的数据结构和 API 必须在设计时就对所有目标后端正确，不能留跨后端兼容债。

### 2026-05-29 — Step 3 完成：DX12 PipelineLayout 构建 root signature from SpaceGroups

**改动文件**：
- `RHI/Backend/DX12/Pipeline/PipelineLayout.h` — 新增 `GetSpaceBinding`、`GetSpaceGroupCount`；新增 4 个 BuildSpaceGroup* 方法声明；新增 `m_spaceRootParams` 成员
- `RHI/Backend/DX12/Pipeline/PipelineLayout.cpp` — 实现 `BuildSpaceGroupConstants`、`BuildSpaceGroupResources`、`BuildSpaceGroupSamplers`、`BuildSpaceGroupStaticSamplers`；`Init()` 内 `UsesShaderInputPath()` 分支

**BuildSpaceGroupConstants**：遍历 SpaceGroup，找到 Constant 类型 handle → 查 descriptor 取 `registerId`/`spaceId` → 生产 `D3D12_ROOT_PARAMETER_TYPE_CBV`。一个 SpaceGroup 最多一个 CBV root param。

**BuildSpaceGroupResources**：对每个 SpaceGroup，收集内部 Buffer/Image handle 对应的 `D3D12_DESCRIPTOR_RANGE`（Buffer Constant→CBV, Buffer Read→SRV, Buffer ReadWrite→UAV, Image Read→SRV, Image ReadWrite→UAV），汇总为一个 `D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE`。

**BuildSpaceGroupSamplers**：同上，只收集 Sampler 类型 → 一个 sampler descriptor table。

**BuildSpaceGroupStaticSamplers**：遍历 `m_staticSamplerDescs`，调用 `ConvertStaticSampler` 生成 `D3D12_STATIC_SAMPLER_DESC`。

**Root parameter 顺序（每个 SpaceGroup）**：
1. CBV root param（如有 Constant）
2. Resource descriptor table（CBV+SRV+UAV，如有多条 range）
3. Sampler descriptor table（如有 Sampler）

`m_spaceRootParams[spaceIndex]` 并行于 `PipelineLayoutDescriptor::m_spaceGroups`，存每组对应的 `RootParameterBinding{m_constantBuffer, m_resourceTable, m_samplerTable}`。

### RHI 层剩余待完成

以下 5 项是 RHI 层尚未完成的工作（按优先级排序）：

#### 1. `BuildRootCanstants` 对新路径仍执行（正确性 bug）

`PipelineLayout.cpp::Init()` L454 在 `UsesShaderInputPath()` 分支**之前**无条件调用 `BuildRootCanstants(dx12Descriptor, parameters)`。旧路径的 root constants 排在 root signature 最前面，但新路径的常量通过 `BuildSpaceGroupConstants` 以 CBV 形式处理。结果：新路径 root signature 多一个不该存在的 32BIT_CONSTANTS root parameter。

**修复**：把 `BuildRootCanstants` 移入 `else` 分支（旧路径专用）。

#### 2. 旧路径初始化代码也在分支前执行（浪费）

`PipelineLayout.cpp::Init()` L436-484 计算 `groupLayoutCount`、填充 `m_slotToIndexTable`/`m_indexToSlotTable`/`m_indexToRootParameterBindingTable`、构建 `indexesSortedByFrequency`——新路径不需要这些。对新路径不会崩溃但浪费，应移入 `else` 分支。

#### 3. DX12 `ValidateShaderInputOverlapInternal` 未 override

虚函数在 `RHI::PipelineLayoutDescriptor` 已定义（默认 no-op），`InsertShaderInput` 在 `Validation::isEnabled` 下调用，但 DX12 的 `PipelineLayoutDescriptor` 没有 override。Binding 重叠检查被静默跳过。

需要：在 DX12 `PipelineLayoutDescriptor` override `ValidateShaderInputOverlapInternal`，按 `(HlslRegNs namespace, registerId, count)` 做范围重叠检查。同时考虑 `VulkanBindingShift`（CBV=0, SRV=1000, UAV=2000, Sampler=3000）做 flat binding namespace 检查，确保同一 space 内不同 HLSL register namespace 不映射到相同 flat binding。

#### 4. ShaderInputCompiler + ShaderBindings（新文件）

参见下方 [ShaderInputCompiler 设计](#ShaderInputCompiler-设计)。

#### 5. 旧 ShaderResource 路径删除（远期）

`ShaderResource`/`ShaderResourceLayout`/`ShaderResourcePool`/`ConstantsLayout`/`ConstantsData` 等 ~16 个文件。等新路径跑通 demo 后一次性删除。

### 2026-05-30 — Step 4 完成：RHI 层 ShaderBindings 骨架

**设计方向调整**：原计划中 ShaderInput 数据由 Pass / Material 各自持有，Compiler 接收 `span<const ShaderInput*>`。实施时改为 **ShaderBindings 自持 ShaderInput 数据**：

- ShaderBindings 粒度 = HLSL space（1 对 1 对应 `PipelineLayoutDescriptor::m_spaceGroups[i]`）
- Pass / Material 持有 N 个 ShaderBindings（用到的每个 space 一个）
- ShaderBindings::Init(layout, spaceIndex) 时按 SpaceGroup 自动构造该 space 内所有 ShaderInputBuffer/Image/Sampler/Constant
- 用户通过 `FindBufferInput(name)->SetView(...)` 直接设值，不再有平铺数组 + index 查表的中间层
- Compiler 改签名为 `Compile(ShaderBindings& bindings)`，所有上下文（layout、spaceIndex、ShaderInput 数据）都从 bindings 取
- 后端派生 ShaderBindings 持有 compile 产物（descriptor table、CBV memory、GPU handles、dirty 位）

**为什么改方向**：
- per-space ShaderBindings 自然 1:1 对应 Vulkan `VkDescriptorSet` 和 DX12 一组 root params（resource table + sampler table + 可选 CBV）
- dirty 位退化为最多 3 位（views / samplers / constants），不再需要 64 位 mask + BitScan
- 跨 pass 共享 binding 集（如 PerView）只需一个 ShaderBindings 对象被多个 pass 引用，复用粒度更精确
- 用户 API 大幅简化：no span 收集、no SRG layout/data 双对象

**新建文件** `RHI/Resource/ShaderInput/`:
- `ShaderBindings.h` — 抽象基类，per-space 自持 ShaderInput 数据 + Find*Input/Get*Inputs 查询接口 + InitInternal/ShutdownInternal 虚函数
- `ShaderBindings.cpp` — Init 时 `BuildInputsFromLayout` 按 SpaceGroup handle 类型创建 ShaderInput 实例

### 下一步

1. 修复 `BuildRootCanstants` 和旧路径 init 代码的 placement（移入 `else` 分支）
2. DX12 `ValidateShaderInputOverlapInternal` override
3. DX12 `ShaderBindings` 派生类（descriptor table 字段 + dirty 位 + InitInternal/ShutdownInternal）
4. ShaderInputCompiler + DX12 后端实现
5. ShaderBuilder::BuildShaderInputList()（DXC 反射填 `ShaderInputList`）

---

## ShaderInputCompiler 设计

### 概述

ShaderInputCompiler 取代 `ShaderResourceCompiler`。输入是一个 ShaderBindings（自带 layout、spaceIndex、以及 ShaderInput 数据），输出写回到这个 ShaderBindings 的后端字段。首次 Compile 时惰性分配 descriptor table，后续复用。

### API

```cpp
// RHI 层
class ShaderInputCompiler : public DeviceObject
{
public:
    ResultCode Init(Device& device);
    void Shutdown() override;

    // 输入即输出：ShaderBindings 自带 layout + spaceIndex + ShaderInput 数据
    // 后端派生的 ShaderBindings 接收 compile 产物（descriptor table / GPU handles / dirty bits）
    ResultCode Compile(ShaderBindings& bindings);

private:
    virtual ResultCode CompileInternal(ShaderBindings& bindings) = 0;
};
```

### ShaderBindings 后端派生

**RHI 层**（[ShaderBindings.h](Engine/Code/RunTime/Feature/RHI/Resource/ShaderInput/ShaderBindings.h)）：自持 ShaderInput 数据 + 查询接口；Init 时按 SpaceGroup 自动构造 ShaderInput；compile 产物留给派生类。

**DX12 层**：per-space 编译产物。由于 ShaderBindings 已经是 per-space 的，无需再嵌套 SpaceGroupData 数组。

```cpp
class ShaderBindings final : public RHI::ShaderBindings
{
private:
    ResultCode InitInternal(Device& device, const Descriptor& descriptor) override;
    void       ShutdownInternal() override;

    // 一次性分配（首次 Compile），ring-buffered × FrameCountMax
    DescriptorTable m_viewsTable;       // CBV+SRV+UAV
    DescriptorTable m_samplersTable;    // sampler
    MemoryView      m_constantMemory;   // constant buffer
    uint32_t        m_ringIndex = 0;

    // 本帧产出的 GPU handles
    struct Frame {
        GpuDescriptorHandle m_gpuViewsHandle;
        GpuDescriptorHandle m_gpuSamplersHandle;
        GpuVirtualAddress   m_gpuConstantAddress;
        CpuVirtualAddress   m_cpuConstantAddress;
    } m_frame;

    // dirty 位 — 至多 3 个：views / samplers / constants
    enum DirtyBits : uint8_t {
        DirtyResources = 1 << 0,
        DirtySamplers  = 1 << 1,
        DirtyConstants = 1 << 2,
    };
    uint8_t m_dirty = 0;

    size_t m_layoutHash = 0;   // 首次 Compile 记下，后续校验

    friend class ShaderInputCompiler;
    friend class CommandList;
};
```

**首次 Compile 惰性分配**（类似 Vulkan `vkAllocateDescriptorSets`）：

```cpp
// CompileInternal:
auto& b = static_cast<ShaderBindings&>(bindings);
const auto& layout = b.GetLayoutDescriptor();
const auto& group  = layout.GetSpaceGroup(b.GetSpaceIndex());

if (b.m_layoutHash == 0)
{
    b.m_layoutHash = layout.GetHash();

    // 累加 SpaceGroup 内所有 ShaderInputHandle 对应 descriptor 的 m_count
    uint32_t viewsCount    = ComputeViewsCount(layout, group);
    uint32_t samplersCount = ComputeSamplersCount(layout, group);
    uint32_t constantBytes = ComputeConstantBytes(layout, group);

    if (viewsCount)    b.m_viewsTable    = descriptorContext.Allocate(viewsCount    * FrameCountMax, ...);
    if (samplersCount) b.m_samplersTable = descriptorContext.Allocate(samplersCount * FrameCountMax, ...);
    if (constantBytes) b.m_constantMemory = constantContext.Allocate(constantBytes  * FrameCountMax);
}
else
{
    ASSERT(b.m_layoutHash == layout.GetHash(),
           "[ShaderInputCompiler] ShaderBindings / PipelineLayoutDescriptor mismatch.");
}
```

不预设全局上限，per-ShaderBindings 按实际 layout 规模分配。

**绑定**：CommandList 通过 `DX12::PipelineLayout::GetSpaceRootParam(spaceIndex)` 拿到 `RootParameterBinding{ m_constantBuffer, m_resourceTable, m_samplerTable }`，按 ShaderBindings 自身的 dirty 位绑定（最多 3 次 Set*Root* 调用）。

**dirty 位机制**

per-ShaderBindings 的 dirty 位非常小（≤3 个 bool）。Compile 写完 descriptor 后置位：

```cpp
// Compiler
if (viewsCount)    b.m_dirty |= DirtyResources;
if (samplersCount) b.m_dirty |= DirtySamplers;
if (constantBytes) b.m_dirty |= DirtyConstants;

// CommandList — 见上方"CommandList 绑定"伪代码
```

dirty 位在 CommandList 绑定后清零。不存在跨 SpaceGroup 的全局 dirty 位图——每个 ShaderBindings 各自管自己的 3 位即可。

**layout 匹配校验**

首次 Compile 记录 `PipelineLayoutDescriptor` 的 hash，后续 Compile 校验。理论上 ShaderBindings 的 layout 在 Init 之后不变，hash 校验主要捕获"用户在 Init 后改了 PipelineLayoutDescriptor"这类误用。

### 后端 CompileInternal 流程（DX12）

```
输入: ShaderBindings& (自带 layout + spaceIndex + ShaderInput 数据)

1. 取 SpaceGroup = layout.GetSpaceGroup(bindings.GetSpaceIndex())

2. 首次编译: 见上方"首次 Compile 惰性分配"

3. 推进 ring index (m_ringIndex = (m_ringIndex + 1) % FrameCountMax)
   计算本帧 views/samplers/constant 在 ring buffer 内的起始位置

4. 写资源 descriptor（按 SpaceGroup::m_shaderInputs 顺序，累加 offset）:
   uint32_t offset = 0;
   for handle in SpaceGroup::m_shaderInputs:
       if Constant: 跳过（独立 CBV）
       else 取对应 ShaderInputBuffer/Image → ConvertView → 写到 m_viewsTable[ringStart + offset]
       offset += desc.m_count
   记录 m_frame.m_gpuViewsHandle, 置位 DirtyResources

5. 写 sampler descriptor（同上，sampler table 独立）:
   遍历 Sampler handle → sampler descriptor @ offset, offset += count
   记录 m_frame.m_gpuSamplersHandle, 置位 DirtySamplers

6. 常量处理（暂不做 root constant split，全部走 CBV）:
   memcpy 常量数据到 m_constantMemory[ringStart]
   记录 m_frame.m_gpuConstantAddress / m_cpuConstantAddress, 置位 DirtyConstants

7. 静态采样器不占 descriptor table，跳过
```

### Descriptor table offset 计算

**key: BuildSpaceGroupResources 和 Compiler 两边遍历同一个 `SpaceGroup::m_shaderInputs`，用同一个顺序，累加同一个 `m_count`。**

```
SpaceGroup::m_shaderInputs (space=0):
  [0] Buffer(b0, Constant, count=1)  → offset = 0
  [1] Buffer(b3, Read,     count=2)  → offset = 0 + 1 = 1
  [2] Image (t0, Read,     count=1)  → offset = 0 + 1 + 2 = 3

DescriptorTable 布局:
  table[0]   = CBV(b0)       ← handle[0]
  table[1]   = SRV(b3 view0) ← handle[1]
  table[2]   = SRV(b3 view1)
  table[3]   = SRV(t0)       ← handle[2]
```

Build 侧和 Compile 侧的伪代码：

```cpp
// BuildSpaceGroupResources — 生成 D3D12_DESCRIPTOR_RANGE
uint32_t offset = 0;
for (auto& handle : spaceGroup.m_shaderInputs) {
    uint32_t count = GetCountFromDescriptor(desc, handle);
    D3D12_DESCRIPTOR_RANGE& range = ranges.push_back();
    range.BaseShaderRegister  = GetRegisterId(desc, handle);
    range.OffsetInDescriptorsFromTableStart = offset;   // GPU 从这里读
    range.NumDescriptors      = count;
    offset += count;
}

// Compiler — 写 descriptor
uint32_t offset = 0;
for (auto& handle : spaceGroup.m_shaderInputs) {
    uint32_t count = GetCountFromDescriptor(desc, handle);  // 同一个 desc
    WriteDescriptors(&table[offset], shaderInput, count);   // 写到 GPU 读的位置
    offset += count;
}
```

不存预计算 offset：一个 SpaceGroup 最多 8 个 handle（`fixed_vector<ShaderInputHandle, 8>`），累加 count 就是 8 次查 descriptor + 8 次加法。全局最多 8 个 SpaceGroup，不值得存。

### 与旧 Compiler 对应关系

| 旧 (ShaderResourceCompiler) | 新 (ShaderInputCompiler) |
|---|---|
| `span<ShaderResource*>` | `ShaderBindings&`（自带 layout + spaceIndex + ShaderInput 数据） |
| `AllocateShaderResource` | `ShaderBindings` 首次 Compile 惰性分配 |
| `UpdateShaderResource` | Compile 每帧 descriptor 写入 |
| `SRG.m_compiledData[N]` | `DX12::ShaderBindings::m_frame`（per-space） |
| `GetGroupInterval` 计算偏移 | 遍历 SpaceGroup `m_shaderInputs` 累加 `m_count` |
| `m_indexToRootParameterBindingTable[slot]` | `m_spaceRootParams[spaceIndex]` |
| 所有常量 → SRG constant buffer | 所有常量 → CBV（暂不做 root constant split） |
| per-SRG 平铺数组 + index 查表 | per-name `FindXxxInput(name)->SetView/SetData` |

### VulkanBindingShift 职责限定

`VulkanBindingShift`（CBV=0, SRV=1000, UAV=2000, Sampler=3000）**只用于 Vulkan 路径**，和 DX12 descriptor table offset 无关：

1. DXC 编译 HLSL→SPIR-V 时的 `-fvk-X-shift` 参数（shader 侧 register remapping）
2. Vulkan backend 写 `VkWriteDescriptorSet::dstBinding` 时的偏移（`shift + registerId`）
3. `ValidateShaderInputOverlapInternal` binding 冲突检查——把不同 HLSL register namespace 映射到 Vulkan 平坦 binding 空间做重叠检测

## 验证

1. 编译通过：`cmake --build build --config Debug`
2. 运行现有 demo：DrawShape、TrianglePassFeature、MSAAPassFeature
3. 关键验证点：常量绑定、纹理/采样器绑定、多 pass 自动绑定、root signature 结构与改动前一致
