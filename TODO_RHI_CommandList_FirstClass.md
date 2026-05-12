# RHI CommandList 一等公民化 — 实施方案

## 背景

当前 `RHI::CommandList` 不是标准 RHI 资源——它没继承 `DeviceObject`、没有公开的 `Init/Shutdown/Reset` 模板，所有生命周期都被 thread-local `CommandListAllocator` 池子隐藏。这造成两个直接问题：

1. **帧循环外不可用**：`AsyncUploadSystem` 这种背景线程系统想录制 copy command 时，只能调 `factory->CreateCommandList(...)`，结果返回的是池子分配的、跨线程不安全、不会被自动回收的 raw 指针——这是 [TODO_DataDrivenRHI.md](TODO_DataDrivenRHI.md) 之外审查时发现的 #2 问题（"CommandList 内存泄漏"）的根因，本质不是泄漏，而是**池子的线程模型不匹配**：
   - `CommandListAllocator` 用 `thread_local ThreadSubAllocator`，每个调用 `Allocate` 的线程注册自己的 sub-alloc 到全局 `m_registeredSubAllocators`；
   - 主线程在帧边界统一调 `Collect()` 时，会跨线程 `Reset` 别人的 sub-alloc——data race；
   - `Reset` 内部还会调 `ID3D12CommandAllocator::Reset()`，而上传线程那边可能正在录制或者 GPU 还在用，DX12 UB。

2. **API 与其他 RHI 资源不一致**：Buffer/Image/Fence 都是 `factory->CreateX() → Ptr<>` → `x->Init(device, ...)` 这套模板，唯独 CommandList 走特殊路径，缺失对偶概念 `CommandAllocator`。Vulkan backend 进来后 `VkCommandPool` 也无处对应。

本方案把 CommandList 提到一等公民，补齐缺失的 `CommandAllocator`，让任何调用方（帧循环内或外）都能用标准模板使用它；池子保留为"per-frame 借用便利层"。

---

## 设计概览

### 分层

```
┌─────────────────────────────────────────────────────────────────┐
│ Layer 1 — RHI 一等公民（对齐 Buffer/Image/Fence 模板）            │
│                                                                  │
│   RHI::CommandAllocator : DeviceObject                          │
│     Init(Device&, HardwareQueueClass) / Shutdown / Reset        │
│                                                                  │
│   RHI::CommandList : DeviceObject                                │
│     Init(Device&, CommandAllocator&) / Shutdown / Reset         │
│     Open / Close / Submit / ...  (既有)                          │
└─────────────────────────────────────────────────────────────────┘
            ▲                                          ▲
            │                                          │
┌───────────┴──────────────────────────────────────────┴──────────┐
│ Layer 2 — Factory                                                │
│                                                                  │
│   Ptr<CommandAllocator> CreateCommandAllocator()                │
│   Ptr<CommandList>      CreateCommandList()       (无参重载)     │
│   CommandList*          AcquirePooledCommandList(D&, QC)         │
└─────────────────────────────────────────────────────────────────┘
            ▲                                          ▲
            │                                          │
            │ 一等公民路径                              │ 池子便利层（保留）
            │                                          │
┌───────────┴─────────────┐                ┌──────────┴────────────┐
│ AsyncUploadSystem        │                │ CommandListAllocator   │
│   FramePacket 持         │                │  (thread-local pool)   │
│   Ptr<CommandAllocator>  │                │  内部继续用 raw        │
│   Ptr<CommandList>       │                │  ID3D12CommandAllocator│
└─────────────────────────┘                └────────────────────────┘
```

### 核心契约

**CommandAllocator**
- `Init` 后处于可用状态，可以绑定多个 CL。
- `Reset()` 会让**所有**绑定到此 allocator 的 CL 失效——调用方必须保证 GPU 已完成所有这些 CL 的执行。
- 1:1 是典型用法（一个 packet / 一个独立录制器），N:1 也支持。

**CommandList**
- `Init(device, allocator)` 之后处于 **closed/idle** 状态（与现有的"Init 后即 recording"不同——为了让每次使用周期都对称：`Reset → record → Close`）。
- `Reset()` 把 CL 回到 recording 状态，复用绑定的 allocator。调用方必须保证：
  - 该 CL 上次 `Close()` 之后 GPU 已经消费完（或从未提交）；
  - 绑定的 allocator 已经被 `Reset()`（如果其他 CL 之前在它上面录过）；或者从该 allocator 上次 Reset 至今没有任何 CL 被提交过。
- `Open()` 保持现有的"近 no-op + 可选 PIX marker"语义；**不**承担 reset 责任。

**生命周期**
- `Ptr<>` 持有，标准 intrusive_ptr 引用计数。
- 调用方在 Shutdown 时显式释放 Ptr（或让作用域结束自然释放）。

---

## 文件清单

### 新增

| 文件 | 内容 |
|---|---|
| `Engine/Code/RunTime/Feature/RHI/Command/CommandAllocator.h` | `RHI::CommandAllocator` 接口（Init/Shutdown/Reset/GetHardwareQueueClass + InitInternal/ShutdownInternal/ResetInternal 纯虚） |
| `Engine/Code/RunTime/Feature/RHI/Command/CommandAllocator.cpp` | RHI-base 包装：参数校验 + 转调 *Internal |
| `Engine/Code/RunTime/Feature/RHI/Backend/DX12/Command/CommandAllocator.h` | `DX12::CommandAllocator` 封装 `ComPtr<ID3D12CommandAllocator>` |
| `Engine/Code/RunTime/Feature/RHI/Backend/DX12/Command/CommandAllocator.cpp` | DX12 实现 |

### 修改

| 文件 | 改动要点 |
|---|---|
| `Engine/Code/RunTime/Feature/RHI/Command/CommandList.h` | 继承 `DeviceObject`；加 `Init(Device&, CommandAllocator&) / Shutdown / Reset()` 公开 API |
| `Engine/Code/RunTime/Feature/RHI/Command/CommandList.cpp`（新建或扩充） | RHI-base 实现：参数校验 + 调 backend InitInternal |
| `Engine/Code/RunTime/Feature/RHI/Backend/DX12/Command/CommandListBase.h` | **移除** `: public RHI::DeviceObject`；变成纯助手类。删除 `GetDevice()` 依赖（如果有的话），改由派生类提供或参数传入 |
| `Engine/Code/RunTime/Feature/RHI/Backend/DX12/Command/CommandListBase.cpp` | 同步调整：`Init` 不再调 `DeviceObject::Init`；如有 `GetDevice()` 调用，改为参数化 |
| `Engine/Code/RunTime/Feature/RHI/Backend/DX12/Command/CommandList.h` | 增加 public override：`Init(Device&, RHI::CommandAllocator&)` 和 `Reset()`（无参）。私有成员 `RHI::CommandAllocator* m_allocator = nullptr;`（一等公民路径下记录绑定，池路径下保持 null） |
| `Engine/Code/RunTime/Feature/RHI/Backend/DX12/Command/CommandList.cpp` | 新 Init 内部 `static_cast<DX12::CommandAllocator&>` 后调既有 `CommandListBase::Init(device, qc, ID3D12CommandAllocator*)`，并 `CommandListBase::Close()` 让 CL 落到 idle 状态。Reset() 内部从 `m_allocator` 拿 ID3D12CommandAllocator* 后转调既有 `Reset(ID3D12CommandAllocator*)` |
| `Engine/Code/RunTime/Feature/RHI/Factory.h` | 新增 `Ptr<CommandAllocator> CreateCommandAllocator()` 和 `Ptr<CommandList> CreateCommandList()` 纯虚；**重命名**原有 `CreateCommandList(Device&, HardwareQueueClass)` 为 `AcquirePooledCommandList`，注释强调"借的、当帧内有效" |
| `Engine/Code/RunTime/Feature/RHI/Backend/DX12/ID3D12Factory.h/.cpp` | 实现新方法 + 重命名旧方法 |
| `Engine/Code/RunTime/Feature/Render/RenderGraph/RenderGraphExecuter.cpp:186` | `CreateCommandList` → `AcquirePooledCommandList` |
| `SandBox/Program/RHI/HelloTriangle.cpp:433,444` | 同上 |
| `SandBox/Program/RHI/DrawShape.cpp:919,935` | 同上 |
| `Engine/Code/RunTime/Feature/RHI/Upload/AsyncUploadSystem.h` | `FramePacket` 加 `Ptr<CommandAllocator> m_commandAllocator;` 和 `Ptr<CommandList> m_commandList;` |
| `Engine/Code/RunTime/Feature/RHI/Upload/AsyncUploadSystem.cpp` | Init 时每个 packet 创建 allocator + cl 并 Init；Shutdown 释放；ProcessBatch 不再 `factory->CreateCommandList(...)`，改用 packet 持有的 CL，每次进入 packet 前 `alloc->Reset(); cl->Reset();` |

### 不动

- `Engine/Code/RunTime/Feature/RHI/Backend/DX12/Command/CommandListPool.h/.cpp` — 池子内部仍用 raw `ID3D12CommandAllocator` 和 DX12-内部的 `CommandList::Init(Device&, qc, raw*)` 签名（这是后端的合法实现细节，不暴露到 RHI 层）。

---

## 分阶段实施路径

每一步结束都要保证 build green，便于回滚和定位。

### Step 1 — DeviceObject 继承迁移（结构调整，单步原子）

把 `RHI::CommandList` 升为 `DeviceObject`，同步把 `CommandListBase` 从 `DeviceObject` 摘下来。这一步必须一次性完成，否则中间态会断。

**子任务**：
1. `RHI::CommandList` 加 `: public DeviceObject`。
2. 在 RHI 层加 `Init(Device&, CommandAllocator&)` 的声明（先用 `// TODO: implement` stub，因为 CommandAllocator 还没建）——或者把这步并到 Step 2。
3. `CommandListBase` 去掉 `: public RHI::DeviceObject`；改成普通基类，构造/Init 不再触碰 DeviceObject。
4. `CommandListBase::Init` 原本调 `DeviceObject::Init(device)` 的那行删掉。`GetDevice()` 如果有调用方，改成派生类 `RHI::CommandList::GetDevice()`（一样可用，因为 DX12::CommandList 通过 RHI::CommandList 拿到 DeviceObject）。
5. `DX12::CommandList` 不显式变化，但要验证 `GetDevice()`、引用计数等都通过 RHI::CommandList → DeviceObject 路径走通。

**风险点**：`CommandListBase::Init` 当前调 `DeviceObject::Init(device)`。摘掉后，`DX12::CommandList::Init` 需要自己负责调 `DeviceObject::Init`（通过 RHI::CommandList 路径）——确保 `m_device` 被设置。看下 [CommandListBase.cpp:23-38](Engine/Code/RunTime/Feature/RHI/Backend/DX12/Command/CommandListBase.cpp#L23-L38)。

**验证**：编译 + 跑现有 HelloTriangle / DrawShape 烟雾测试，确认池路径未回归。

### Step 2 — 新增 CommandAllocator 一等公民

无现有调用方，单纯加新东西，build 必然 green。

**子任务**：
1. 新建 RHI 层 `CommandAllocator.h/.cpp`，模板：`Init/Shutdown/Reset/InitInternal/ShutdownInternal/ResetInternal`。
2. 新建 DX12 `CommandAllocator.h/.cpp`，包 `ComPtr<ID3D12CommandAllocator>`，`InitInternal` 调 `ID3D12Device::CreateCommandAllocator`，`ResetInternal` 调 `ID3D12CommandAllocator::Reset`。
3. Factory 加 `CreateCommandAllocator()` 纯虚 + DX12 实现：`new DX12::CommandAllocator()` 包成 `Ptr<>` 返回。
4. 把 Step 1 里 `RHI::CommandList::Init(Device&, CommandAllocator&)` 的 TODO 补上：
   - 调 `DeviceObject::Init(device)`；
   - 调 backend 纯虚 `InitInternal(allocator)`（DX12 实现里调既有 `CommandListBase::Init(device, qc, dx12Alloc.Get())` 后 `CommandListBase::Close()`）；
   - 记下 `m_allocator = &allocator`。
5. `RHI::CommandList::Reset()` 纯虚；DX12 实现：`ASSERT(m_allocator)` → `CommandListBase::Reset(static_cast<DX12::CommandAllocator*>(m_allocator)->Get())` + 必要的 descriptor heap 重新绑定（参考既有 [CommandList.cpp:55-68](Engine/Code/RunTime/Feature/RHI/Backend/DX12/Command/CommandList.cpp#L55-L68)）。
6. `RHI::CommandList::Shutdown()` 释放 m_allocator 弱引用，调 `DeviceObject::Shutdown`。

**验证**：写一个最小单元测试或在 sandbox 里手动 `Create + Init + Reset + Close + Reset + Shutdown` 走一遍，没崩、没 D3D12 validation error 即可。

### Step 3 — Factory 重命名 + 调用点迁移

**子任务**：
1. `Factory::CreateCommandList(Device&, HardwareQueueClass)` 重命名为 `AcquirePooledCommandList`。注释加：
   ```
   //! Acquire a pool-managed command list valid only for the current frame.
   //! The pointer is borrowed: DO NOT store across frames; the pool reclaims it.
   //! For standalone (non-pool) recording, use CreateCommandList() + Init().
   ```
2. DX12 实现同步重命名。
3. 加新签名 `Ptr<CommandList> CreateCommandList()` 纯虚 + DX12 实现：`new DX12::CommandList()` 包 Ptr<>，未初始化返回。
4. 4 个调用点改名：
   - `RenderGraphExecuter.cpp:186`
   - `HelloTriangle.cpp:433, 444`
   - `DrawShape.cpp:919, 935`

**验证**：完整 build + sandbox 烟雾测试。

### Step 4 — AsyncUploadSystem 迁移

**子任务**：
1. `FramePacket` 加：
   ```cpp
   Ptr<CommandAllocator> m_commandAllocator;
   Ptr<CommandList>      m_commandList;
   ```
2. `AsyncUploadSystem::InitInternal`：每个 packet
   ```cpp
   packet.m_commandAllocator = factory->CreateCommandAllocator();
   packet.m_commandAllocator->Init(*device, HardwareQueueClass::Copy);
   packet.m_commandList = factory->CreateCommandList();
   packet.m_commandList->Init(*device, *packet.m_commandAllocator);
   ```
3. `ShutdownInternal`：循环 reset `m_commandList`/`m_commandAllocator`（Ptr 自动析构）。
4. `ProcessBatch`：删掉 `factory->CreateCommandList(...)` 和局部 `cmdList` 变量；改成从当前 packet 取 CL。
5. 每次"进入一个 packet"前（包括 ProcessBatch 起点、SubmitFramePacket 轮转后）：
   ```cpp
   packet->m_commandAllocator->Reset();
   packet->m_commandList->Reset();
   ```
   （Reset 顺序：allocator 先；CL 后。allocator 的 Reset 安全前提是 `m_packetFence->WaitOnCpu()` 已经过——在轮转路径里已经做了；初次进入则该 packet 从未提交过，安全。）
6. `SubmitFramePacket`（轮转）：用 packet 的 CL Close+Execute+Signal，不再 new CL。
7. Batch 结尾：同上，最后一段 CL Close+Execute+Signal m_uploadFence/m_packetFence。

**验证**：
- D3D12 validation layer 开启，跑 TrianglePass 或任意 upload 路径
- 多帧 + 大数据（>packet size）触发 packet 轮转，确认无 validation error
- 进程退出干净（无引用计数泄漏告警）

---

## 关键代码片段示意

### RHI::CommandAllocator（接口大致样貌）

```cpp
namespace Spark::RHI
{
    class CommandAllocator : public DeviceObject
    {
    public:
        ResultCode Init(Device& device, HardwareQueueClass hwClass);
        void       Shutdown() override final;
        ResultCode Reset();

        HardwareQueueClass GetHardwareQueueClass() const { return m_hwClass; }

    private:
        virtual ResultCode InitInternal(Device&, HardwareQueueClass) = 0;
        virtual void       ShutdownInternal() = 0;
        virtual void       ResetInternal() = 0;

        HardwareQueueClass m_hwClass = HardwareQueueClass::Graphics;
    };
}
```

### RHI::CommandList 新增部分

```cpp
namespace Spark::RHI
{
    class CommandList : public DeviceObject
    {
    public:
        // 新增 — 一等公民模板
        ResultCode Init(Device& device, CommandAllocator& allocator);
        void       Shutdown() override final;
        virtual void Reset() = 0;

        // 既有 — 录制 API
        virtual void Open()  = 0;
        virtual void Close() = 0;
        virtual void Submit(const CopyItem&, uint32_t = 0) = 0;
        // ... 其他既有方法

    protected:
        virtual ResultCode InitInternal(CommandAllocator&) = 0;
        virtual void       ShutdownInternal() = 0;

        CommandAllocator* m_allocator = nullptr;   // 弱引用，Init 时设置
    };
}
```

### DX12::CommandList 新增 override

```cpp
namespace Spark::RHI::DX12
{
    class CommandList : public RHI::CommandList, public CommandListBase  // CommandListBase 现已不继承 DeviceObject
    {
    public:
        // RHI 一等公民路径
        void Reset() override;

    private:
        ResultCode InitInternal(RHI::CommandAllocator& allocator) override;
        void       ShutdownInternal() override;

        // 既有 — 池路径仍用这套（friend class CommandListFactory 访问）
        void Init(Device& device, RHI::HardwareQueueClass hwClass, ID3D12CommandAllocator* allocator);
        void Reset(ID3D12CommandAllocator* allocator);  // 既有 CommandListBase override
    };
}
```

### AsyncUploadSystem ProcessBatch 改造后骨架

```cpp
void AsyncUploadSystem::ProcessBatch(Batch& batch)
{
    auto* packet = &m_packets[m_currentPacketIndex];

    // 第一次进入 packet：allocator 之前没人用过（或上一轮已经 fence-wait 过），Reset 安全
    packet->m_commandAllocator->Reset();
    packet->m_commandList->Reset();
    CommandList* cmdList = packet->m_commandList.get();

    auto SubmitFramePacket = [&]()
    {
        cmdList->Close();
        m_copyQueue->ExecuteCommands({ &cmdList, 1 });
        packet->m_fenceValue = m_packetFence->Increment();
        m_copyQueue->Signal(*m_packetFence);

        m_currentPacketIndex = (m_currentPacketIndex + 1) % m_packets.size();
        packet = &m_packets[m_currentPacketIndex];

        if (packet->m_fenceValue > m_packetFence->GetCompletedValue())
        {
            m_packetFence->WaitOnCpu();
        }
        packet->m_offset = 0;

        packet->m_commandAllocator->Reset();
        packet->m_commandList->Reset();
        cmdList = packet->m_commandList.get();
    };

    // ... buffer/image upload 循环不变 ...

    cmdList->Close();
    m_copyQueue->ExecuteCommands({ &cmdList, 1 });
    m_copyQueue->Signal(*m_uploadFence, batch.m_fenceValue);

    packet->m_fenceValue = m_packetFence->Increment();
    m_copyQueue->Signal(*m_packetFence);
}
```

---

## 风险与边界

### 必须验证的细节

1. **`CommandListBase` 摘掉 DeviceObject 后**，原本通过 `GetDevice()` 拿 device 的地方（[CommandList.cpp:61](Engine/Code/RunTime/Feature/RHI/Backend/DX12/Command/CommandList.cpp#L61) Reset 路径里有用）需要改走 `RHI::CommandList::GetDevice()`。注意虚函数解析路径。
2. **池子 ResetObject 调既有 `Reset(ID3D12CommandAllocator*)`** 不变。新签名 `Reset()`（无参）只用于一等公民路径，池子永远不会调到它。两个路径互不交叉。
3. **`DX12::CommandList::Init` 重载**：一等公民路径调 `InitInternal(CommandAllocator&)`（RHI-style），池路径调既有 `Init(Device&, qc, ID3D12CommandAllocator*)`。两个路径分别设置 `m_allocator`（前者非空，后者保持 null）——`Reset()` 无参靠 `ASSERT(m_allocator)` 区分。
4. **`Init` 后状态**：一等公民路径 Init 完后 CL **落到 closed/idle**（多一步 `CommandListBase::Close()`），保证使用周期对称。池路径保持现状（Init 后即 recording，Open 是 no-op）——两个路径的"刚 Init 完"状态不同，但池路径调用方从来不调 `Reset()` 无参，不会混淆。

### 已知不动的部分

- `CommandListAllocator` thread-local pool 整体不动，性能特性维持。
- 主渲染路径（RenderGraphExecuter 等）只是改个名字，行为不变。
- Fence/CommandQueue/Buffer/Image 等其他 RHI 资源不动。

### Vulkan backend 后续接入时

`RHI::CommandAllocator` 直接对应 `VkCommandPool`，`RHI::CommandList::Reset()` 对应 `vkResetCommandBuffer` 或在共享 pool 时由调用方先 `vkResetCommandPool`。这套抽象正好对得上 Vulkan 的强制性 pool 概念，免去 backend 内部再造一层。

---

## 验证清单

- [ ] Step 1 后：`cmake --build build --config Debug` 干净；HelloTriangle / DrawShape 启动渲染正常。
- [ ] Step 2 后：手写小测试 `CreateCommandAllocator + CreateCommandList + 各种 Init/Reset/Close 循环`，无 D3D12 debug layer 报错。
- [ ] Step 3 后：完整 build；sandbox 与 RenderGraph 烟雾测试通过。
- [ ] Step 4 后：AsyncUploadSystem 跑多帧大数据 upload，触发 packet 轮转；D3D12 validation layer 干净；进程退出无引用计数告警。
- [ ] 与 [TODO_DataDrivenRHI.md](TODO_DataDrivenRHI.md) 的 T7 TrianglePass 端到端测试一起跑：VB 上传通过新路径走通，三角形正常显示。

---

## 与其他 TODO 的关系

- 修复 [TODO_DataDrivenRHI.md](TODO_DataDrivenRHI.md) 审查报告里的 **#2 问题**（CommandList "泄漏" / 跨线程不安全）。
- 不依赖也不阻塞 T1/T2/T5/T6/T7。可以独立排期。
- 完成后，AsyncUploadSystem 这套 system 的实现复杂度明显下降，后续接 streaming / 多 channel batching 都更顺畅。
