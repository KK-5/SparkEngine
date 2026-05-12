# RHI Resource Pool 切分方案

## 背景

[TODO_AsyncUpload_RemainingIssues.md](TODO_AsyncUpload_RemainingIssues.md) 的 #7 指出 `RHIResourceSystem::SelectBufferPool` 用 bind flags 推断驻留位置和多帧倍率，把三个正交的轴搅在一起。修这个问题分两步：

1. **先把 pool 怎么切想清楚**（本文档）
2. 再定 select 机制（select 怎么从 descriptor 推到具体池，**留给下一轮**）

本文档只解决第 1 步。

---

## 切分轴

### 真正应该当 pool 轴的（硬件/分配器层面互斥）

**Axis 1 — 存储位置 `HeapKind`**

不同 heap 在硬件上是物理分离的内存，一个 heap 一个内存特性，不能混：

| HeapKind | 含义 | D3D12 | Vulkan |
|---|---|---|---|
| `DeviceLocal` | VRAM，CPU 不可见 | `D3D12_HEAP_TYPE_DEFAULT` | `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT` |
| `HostUpload` | write-combined，CPU 写 + GPU 读 | `D3D12_HEAP_TYPE_UPLOAD` | `HOST_VISIBLE \| HOST_COHERENT` |
| `HostReadback` | cached，GPU 写 + CPU 读 | `D3D12_HEAP_TYPE_READBACK` | `HOST_VISIBLE \| HOST_CACHED` |

未来扩展：`DeviceCoherent`（RBAR / 集显，device-local AND CPU-visible），暂时不开。

**Axis 2 — 分配策略 `AllocationStrategy`**

不同策略需要完全不同的内部数据结构和回收逻辑，不可共享一个池实例：

| Strategy | 含义 | 适用场景 |
|---|---|---|
| `Placed` | 从共享 heap suballocate（D3D12 placed resource / Vulkan VkBuffer 共享 VkDeviceMemory）。内存利用率最高，支持 aliasing | **默认主力**：长寿资源 |
| `Linear` | bump-pointer 环形分配器，按帧节奏回收 | 高频小分配的 staging / transient buffer |
| `Committed` | 一个 resource 一份独立 allocation（D3D12 committed resource） | 巨型 / 特殊对齐资源的逃生口 |

### 不该当 pool 轴的

下面这些虽然看起来"也分得开"，但放到 pool 维度会切得过细，或者根本就不是 pool 应该关心的事：

| 候选 | 为什么不当轴 |
|---|---|
| **多帧倍率** Single / PerFrame | 是"从池里要几份 buffer"的问题，不是 heap 或 allocator 的问题。同一个池既能给 Single 也能给 PerFrame。倍率属于 buffer 的创建参数，由 materializer 解释 |
| **绑定用途** bind flags | DX12 heap-tier 2 下所有 buffer 类型共享一个 heap；Vulkan VkDeviceMemory 只看 memoryTypeBits，跟 usage 正交 |
| **队列归属** shared / exclusive | DX12 没这个概念；Vulkan `VK_SHARING_MODE` 是每个 buffer 自己的事 |
| **更新频率** static / streaming / per-frame | 这是消费者意图，不是内存物性。Static 和 streaming buffer 都从同一个 `DeviceLocal+Placed` 池出，差别只在调用方更新它的频率 |

---

## Buffer Pool 矩阵

`HeapKind × AllocationStrategy = 3 × 3 = 9` 个理论组合，实际有意义的不多：

| HeapKind \ Strategy | Placed | Linear | Committed |
|---|---|---|---|
| **DeviceLocal**  | ★ 主力：VB/IB/CBV/SRV/UAV 长寿资源 | — 罕见（GPU 不能 map，linear 没意义） | ◯ 巨型单 resource 逃生口 |
| **HostUpload**   | ★ per-frame 动态 UBO/SSBO（host map 直写） | ★ staging ring（AsyncUploadSystem 当前用法） | — 极罕见 |
| **HostReadback** | ◯ 普通 readback buffer | — 罕见 | ◯ 巨型 readback |

- `★` v1 立即需要
- `◯` 等首个用户出现再开
- `—` 不开

### v1 实际要实例化的 buffer 池

3 个起步：

| 池名 | HeapKind | Strategy | 用途 |
|---|---|---|---|
| `m_deviceBufferPool` | DeviceLocal | Placed | 主力：VB/IB/static CBV/SRV/UAV、device-side per-frame buffer |
| `m_hostUploadBufferPool` | HostUpload | Placed | per-frame 动态 CBV（host map 写）、一次性 host buffer |
| `m_stagingBufferPool` | HostUpload | Linear | 上传 staging ring |

**后续按需添加**：`m_hostReadbackBufferPool`（Placed）、`m_deviceCommittedBufferPool`（Committed）。

### 与当前代码的映射关系

| 当前 | v1 新方案 | 备注 |
|---|---|---|
| `m_deviceBufferPool` (RHIResourceSystem) | `m_deviceBufferPool` | 配置基本对，但要去掉 `BufferBindFlags::Constant` 之类不应在 device 池声明的位（参见 [RHIResourceSystem.cpp:23-29](Engine/Code/RunTime/Feature/RHI/Resource/RHIResourceSystem.cpp#L23-L29)）— 实际上 `Constant` 在 device 也合理（static CBV），需要重新审视池的 bind flag 声明含义 |
| `m_hostBufferPool` (RHIResourceSystem) | `m_hostUploadBufferPool` | **关键变化**：不再强制 PerFrame 倍率。`CreateBuffers` 里 host 池分支的"必 PerFrame"逻辑要拆掉，倍率改由 descriptor 字段驱动 |
| `m_stagingPool` (AsyncUploadSystem 私有) | `m_stagingBufferPool`（升为 RHIResourceSystem 管） | AsyncUploadSystem 不再自己持有；从中心池借用。预算可控、可监控 |

---

## Image Pool

Image 几乎只用 `DeviceLocal + Placed`，**image 池不需要按 HeapKind 切**。1 个池就够：

| 池名 | HeapKind | Strategy | 用途 |
|---|---|---|---|
| `m_deviceImagePool` | DeviceLocal | Placed | 所有 GPU 端纹理（采样 / RT / DS / UAV） |

> Image 上传走的是 buffer staging，不是 image-to-image。所以 image 不需要 `HostUpload` 池。

当前代码已经是这个形态，**image 池本轮不动**。

---

## 设计要点

### 多帧倍率从池里抽出来

旧设计：`host pool → 强制 PerFrame`。新设计：池只管"从哪个 heap 出、怎么 sub-allocate"，倍率由 materializer 读 descriptor 上的字段（具体怎么表达留给 select 那一轮）。

这一拆开之后，下面这些以前表达不出来的组合都自然支持：

| 场景 | 期望组合 | 旧方案 | 新方案 |
|---|---|---|---|
| 静态 device CBV，一次上传 | DeviceLocal + Placed + Single | ✗ 被路由到 host + PerFrame | ✓ |
| per-frame device cbuffer（staging 上传） | DeviceLocal + Placed + PerFrame | ✗ device 必 Single | ✓ |
| 一次性 host upload buffer | HostUpload + Placed + Single | ✗ host 必 PerFrame | ✓ |
| per-frame 动态 cbuffer host 直写 | HostUpload + Placed + PerFrame | ✓ 唯一对的格子 | ✓ |
| Staging 缓冲（AsyncUpload） | HostUpload + Linear + N/A | 由 AsyncUpload 自己管 | ✓ 收编到中心池 |

### 池实例差异化实现

不同 `AllocationStrategy` 的池需要不同的 allocator：

- **Placed**：继续走 D3D12MA（当前 `BufferPool` 已经是这个实现）
- **Linear**：环形分配器 + persistent map（AsyncUploadSystem 现有的 `FramePacket` 模式抽出来）
- **Committed**：直接 `CreateCommittedResource`，没有 sub-alloc

对应 RHI 层可能要做的事：
- 把现有 `BufferPool` 接口拆出 `BufferPlacedPool` / `BufferLinearPool` / `BufferCommittedPool` 三个子类，或者保留 `BufferPool` 一个接口、内部按 strategy 分派
- 选哪种取决于现有 `BufferPool` 的接口宽度——下一轮 select 设计时一起定

### Vulkan backend 零摩擦

切分轴跟 Vulkan 的概念是 1:1 的：

- `HeapKind` → 选 `VkPhysicalDeviceMemoryProperties` 里 memoryType 的 propertyFlags
- `AllocationStrategy` → VkDeviceMemory 是 dedicated 还是 suballocate（VMA 处理这个）

不需要任何"backend 内部再造一层"的工作。

---

## 不在本文档范围

明确划界，避免下次接手时混淆：

- **Select 机制（descriptor → pool 的映射）** — 这是 #7 的核心，本文档只定义了**有哪些池**，没定义**怎么挑**。三种候选方向（descriptor 加 hint / 单独 residency 组件 / 沿用 BufferPoolDescriptor 概念）留到下一轮。
- **Descriptor 加什么字段** — 取决于 select 方案。
- **PerFrame 倍率字段的形态** — 同上。
- **池内 allocator 的具体实现** — 现在的 `BufferPool` 单类是否要拆成 3 个子类、还是保留一个接口内部分派，取决于现有 `BufferPool::Init/Allocate/Shutdown` 接口与 Linear/Committed 策略的契合度，需要看代码后定。
- **Readback 池 / Committed 池** — 等首个用户出现再开，留矩阵格子但不实例化。
- **DeviceCoherent (RBAR) 支持** — 未来扩展，不在 v1。

---

## 关键文件

接手实施时主要看这几处：

- [RHIResourceSystem.{h,cpp}](Engine/Code/RunTime/Feature/RHI/Resource/RHIResourceSystem.cpp) — 池实例化、`SelectBufferPool` / `SelectImagePool`、`CreateBuffers` 里的 host/device 分支
- [RHI/Resource/Buffer/BufferPool.h](Engine/Code/RunTime/Feature/RHI/Resource/Buffer/BufferPool.h) — `BufferPool` 接口（评估是否要分子类）
- [RHI/Resource/Buffer/BufferPoolDescriptor.h](Engine/Code/RunTime/Feature/RHI/Resource/Buffer/BufferPoolDescriptor.h) — 池子的描述符
- [AsyncUploadSystem.{h,cpp}](Engine/Code/RunTime/Feature/RHI/Upload/AsyncUploadSystem.cpp) — 私有 `m_stagingPool` 收编时这里要改

## 与其他 TODO 的关系

- 阻塞 [TODO_AsyncUpload_RemainingIssues.md](TODO_AsyncUpload_RemainingIssues.md) 的 #7、#9（Descriptor 入口的设计要等 select 方案）
- 与 [TODO_RHI_CommandList_FirstClass.md](TODO_RHI_CommandList_FirstClass.md) 正交，可并行推进
- 实施 #7 时建议把 #9 一起做（都要触及 `BufferDescriptor` schema）
