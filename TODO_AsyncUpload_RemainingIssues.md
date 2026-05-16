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

**搁置原因**：目前只有一个使用者，YAGNI。第二个调用方（很可能是 RenderSystem）出现时再搬迁，那时"谁负责 advance"的语义问题会自然有答案。

**相关代码**：[RHIResourceSystem.cpp:117](Engine/Code/RunTime/Feature/RHI/System/RHIResourceSystem.cpp#L117)

---

## 与其他 TODO 的关系

- 主推进路径在 [TODO_DataDrivenRHI.md](TODO_DataDrivenRHI.md)。
- CommandList 一等化方案在 [TODO_RHI_CommandList_FirstClass.md](TODO_RHI_CommandList_FirstClass.md)。
