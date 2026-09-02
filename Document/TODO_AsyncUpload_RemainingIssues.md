# AsyncUploadSystem / RHIResourceSystem 审查残余问题

剩余的 TODO，已完成项已清理。

---

## 待处理

### Image upload 只支持 2D 单 subresource

`AsyncUploadSystem::ProcessBatch` 的 image 上传循环只按 `m_size.m_height` 走行 memcpy，`m_size.m_depth` 完全没用到；`PendingImageUpload` 也只挂一个 `ImageSubresource`。3D / 体积纹理 / cubemap 的多 slice 当前单次只能传一片。

**搁置原因**：纹理格式与上传 API 后续会统一重新拟定，等那次重构一并处理。当前如果有调用方传 `m_size.m_depth > 1`，会被无声吞掉——重构前可考虑加个 assert 把契约钉死。

**相关代码**：[AsyncUploadSystem.cpp:375-418](Engine/Code/RunTime/Feature/RHI/System/AsyncUploadSystem.cpp#L375-L418)

---

### Frame index 应归属 Device

`RHIResourceSystem::m_frameIndex` 每帧在 `OnFrameBegin` 里 `(idx + 1) % frameCountMax`，用于 per-frame buffer 的 `PendingBufferMap` 当前 slot 写入。

但"当前帧的 in-flight slot"本质是全局概念，`StagedArrayBuffer::BindFrame`、`ImageViewCachePerFrame` / `BufferViewCachePerFrame`、DX12 `ShaderBindings::m_compiledData[]` 都要用同一份索引。应该挪到 `Device`，由它统一推进。

**第二调用方已到（2026-06-24）**：`InstanceBindingSystem` 的 `g_Instances` 已改 per-frame（`PerFrameTag`，N 份 buffer 解 G2 帧间 race）。它每帧用 **swap-chain 的 `GetCurrentImageIndex()`**（由 `RenderSystem::OnTick` 传入 `Update(frameIndex)`）选当前帧的 buffer 来绑 per-frame SRV；而 `ProcessBufferMaps` 写的是 `RHIResourceSystem::m_frameIndex` 那一份。**两者必须是同一 slot**，目前靠"都从 device 的 `frameCountMax` 取模、都从 0 起、每帧各 +1 一次"的隐式锁步成立。

**锁步已被打破（2026-09-03）**：swap-chain resize 落地后，`SwapChain::Resize` → `InitImages()` 把 `m_currentImageIndex` 归零（[SwapChain.cpp:180](Engine/Code/RunTime/Feature/RHI/SwapChain/SwapChain.cpp#L180)），而 `RHIResourceSystem::m_frameIndex` 照常递增。一次 resize 之后两者**永久错开一格**，实测：

```
resize 前   graph frameIndex 0 / 1 / 2   ←→   upload slot 0 / 1 / 2
resize 后   graph frameIndex 0 / 1 / 2   ←→   upload slot 2 / 0 / 1
```

后果是 SRV 绑 slot N、数据写进 slot N-1，着色器恒定读到陈旧副本。表现为方向光阴影在相机移动时抖动——`ShadowViewData::m_worldToShadowUV` 每帧跟着相机视锥球变，读到旧的就与几何体错位；相机静止时新旧相同，看不出问题。

**方案**：分两步，顺序不能反。

*第一步 —— 先立唯一入口，不动来源。* `Device` 持有 frame index，提供 `GetFrameIndex()`；`RenderGraph::ExecutePipeline` 在广播 `OnFrameBegin` **之前**调 `Device::BeginFrame(frameIndex)` 写入；`RHIResourceSystem` 删掉私有计数器改读 `Device`。来源暂时仍是 swap-chain 的 image index。这一步即修复上述 bug，风险接近零。

推进点必须是显式调用，不能挂在 `RHIInterface::OnFrameBegin` 上——同一条 bus 的 handler 没有顺序保证，`RHIResourceSystem::OnFrameBegin` 可能先跑而读到上一帧的值。

*第二步 —— 再换来源。* `Device::BeginFrame()` 自己推进，`RenderSystem` 不再从 swap-chain 取帧号。连带项：全局帧号与 back buffer 号从此不再相等，swap-chain 的 `BackingImage` 必须取自后者，`GetOrCreateImageViewPerFrame` 的 `&slot->GetImage() == &image` 断言会立刻炸。解法是给每个 per-frame 资源实体挂上自己的环位置：

```cpp
struct PerFrameIndex { uint32_t m_index = 0; };   // 由 RefreshPerFrameBackings 写
```

`ImagePerFrame` / `BufferPerFrame` 写全局帧号，`SwapChainImages` 写 `swapChain.GetCurrentImageIndex()`；view helper 从实体读而不是当参数传。这样 "swap chain 是特例" 本身就消失了，多 swap-chain 也顺带成立。代价是 per-frame view / `BindFrame` 的 16 处调用点和 `GetFrameIndex()` 的 5 处要改签名。

**不要并进来的两个计数器**：

- `SwapChain::m_currentImageIndex` —— DXGI back buffer 号，`ResizeBuffers` 后归零是正确行为，只服务 swap chain 自己。
- `AsyncUploadSystem::m_currentPacketIndex` —— 名字像帧号，实为环形 staging 分配器的游标，一帧内 packet 空间不够时会多次推进（[AsyncUploadSystem.cpp:507](Engine/Code/RunTime/Feature/RHI/System/AsyncUploadSystem.cpp#L507)）。并进来会破坏上传。

`CommandQueueContext::m_currentFrameIndex` 只索引自己的 fence 数组，自洽，不修也不会错；形状相同，第二步可顺手对齐。

**相关代码**：[RHIResourceSystem.cpp:133](Engine/Code/RunTime/Feature/RHI/System/RHIResourceSystem.cpp#L133)、[StagedArrayBuffer.h:88](Engine/Code/RunTime/Feature/Render/Binding/StagedArrayBuffer.h#L88)、`RenderSystem::OnTick`、`RenderGraph::ExecutePipeline`。

---

## 与其他 TODO 的关系

- 主推进路径在 [TODO_DataDrivenRHI.md](TODO_DataDrivenRHI.md)。
- CommandList 一等化方案在 [TODO_RHI_CommandList_FirstClass.md](TODO_RHI_CommandList_FirstClass.md)。
