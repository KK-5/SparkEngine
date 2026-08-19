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

`RHIResourceSystem::m_frameIndex` 每帧在 `OnFrameBegin` 里 `(idx + 1) % frameCountMax`，目前只用于 per-frame buffer 的 `PendingBufferMap` 当前 slot 写入。

但"当前帧的 in-flight slot"本质是全局概念，将来 RenderSystem 写 per-frame SRG、swap-chain 关联 back buffer 等都需要同一份索引。应该挪到 `Device`（或 `RHIInterface`），由它统一推进，提供 `GetCurrentFrameIndex()`。

**第二调用方已到（2026-06-24）**：`InstanceBindingSystem` 的 `g_Instances` 已改 per-frame（`PerFrameTag`，N 份 buffer 解 G2 帧间 race）。它每帧用 **swap-chain 的 `GetCurrentImageIndex()`**（由 `RenderSystem::OnTick` 传入 `Update(frameIndex)`）选当前帧的 buffer 来绑 per-frame SRV；而 `ProcessBufferMaps` 写的是 `RHIResourceSystem::m_frameIndex` 那一份。**两者必须是同一 slot**，目前靠"都从 device 的 `frameCountMax` 取模、都从 0 起、每帧各 +1 一次"的隐式锁步成立——脆弱点：任一计数器漏跳/相位偏移就会让"写的 buffer"和"读的 buffer"错位（轻则读到陈旧帧，重则与正在写的那份撞上、race 复现）。

**加固**：把 frame index 收口到 `Device::GetCurrentFrameIndex()`，让 `ProcessBufferMaps`、`InstanceBindingSystem` 的 per-frame SRV 绑定、render 三方读同一权威 index，消掉两计数器锁步假设。**待 InstanceBindingSystem 这套先成功跑通一遍后再做**（已和作者确认这个顺序）。

**相关代码**：[RHIResourceSystem.cpp:117](Engine/Code/RunTime/Feature/RHI/System/RHIResourceSystem.cpp#L117)、`InstanceBindingSystem::BindFrameInstances` / `Update(frameIndex)`、`RenderSystem::OnTick`。

---

## 与其他 TODO 的关系

- 主推进路径在 [TODO_DataDrivenRHI.md](TODO_DataDrivenRHI.md)。
- CommandList 一等化方案在 [TODO_RHI_CommandList_FirstClass.md](TODO_RHI_CommandList_FirstClass.md)。
