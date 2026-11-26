# NRD 直射光双向样本置信度接入方案

Spark 当前给 RELAX 的 diffuse 和 specular 绑定同一张值为 `1` 的置信度纹理。接入按像素变化的置信度，让灯光开启、关闭、移动或遮挡变化时，RELAX 能在受影响的区域更快降低旧光照的权重。

方案沿用 RTXDI 的双向样本复测思路。对齐两帧中同一个接收表面，在小块内从两帧已缓存的样本中选一个贡献较强的样本，再到另一帧评估同一个光源采样点。每个有效探针只做一次跨帧评估。 [RTXDI 置信度方案](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/Confidence.md#temporal-gradients-in-rtxdi)

## 1. 两帧光追场景

### 实现进展（2026-11-26）

- `RayTracingSceneSystem` 在一块 512 MiB 的 AS buffer 中预留两个独立的 TLAS 区间，其余空间继续分配给 BLAS。每帧在 Graphics 队列构建一次当前 TLAS，上一帧的结果留在原区间。实例输入和 scratch 各复用一份，构建与查询沿用 RenderGraph 的资源依赖和整块 buffer 屏障。 [场景系统](../Engine/Code/RunTime/Feature/Render/RayTracing/RayTracingSceneSystem.cpp) · [原光追场景方案](TODO_RayTracingScenePlan.md)
- `RayTracingSceneTag` 发布当前帧的 `g_RayTracingScene`，现有 pass 继续通过 `ImportShared` 读取。`GetCurrentTopLevel()` 和 `GetPreviousTopLevel()` 分别提供两帧的 AS 对象和区间。首帧或紧邻上一帧未安排 TLAS 构建时，`GetPreviousTopLevel()` 返回空。置信度 pass 普通导入 AS buffer 一次，在同一个 scope 声明两次 `ReadAccelerationStructure`，分别显式绑定对应的 AS 和区间。 [场景接口](../Engine/Code/RunTime/Feature/Render/RayTracing/RayTracingScene.h)
- 两帧可共享静态 BLAS。网格删除或替换时，旧 BLAS 对象和存储区间沿用设备延迟回收，覆盖上一帧 TLAS 在本帧的读取。后续接入顶点形变时，需要用构建输入版本识别变化，并为仍被历史引用的 BLAS 保留旧地址。 [BLAS 生命周期](../Engine/Code/RunTime/Feature/Render/RayTracing/RayTracingBottomLevel.cpp)

第一步代码已实现，编译和 GPU 验证待完成。

## 2. 灯光持久化和跨帧身份

### 场景灯光已实现（2026-11-26）

- 方向光、点光、聚光和矩形灯的实体通过 `LightSlot` 持有稳定槽位。设备端 `g_Lights` 是持久的 `DeviceArray`，每槽的 `LightRecord` 保存当前与上一帧的 `LightData`。`GetLightBySlot(slot, previousFrame)` 按同一个槽号读取两帧。GPU 类型值 `0` 表示 `Invalid`，有效灯光类型从 `1` 开始。 [灯光组件](../Engine/Code/RunTime/Feature/Render/Binding/Scene/SceneBinding.h) · [GPU 记录](../Engine/Asset/Shaders/LightData.hlsli) · [读取接口](../Engine/Asset/Shaders/SceneBindings.hlsli)
- 新灯的上一帧记录从 `Invalid` 开始。删灯当帧，`RemovedLightSlot` 暂存槽号和最后一份参数，灯光更新将该槽的当前记录写为 `Invalid`，上一帧保留最后一份有效参数。移除 `LightSlot` 后，槽位从当帧进入延迟回收，临时组件在同一次更新结束时清理。相邻两帧的样本可以直接记录槽号，回收延迟覆盖这一引用窗口。 [槽位更新](../Engine/Code/RunTime/Feature/Render/Binding/Scene/SceneBindingSystem.cpp)
- `g_LightSlotCount` 是当前最高活动槽号加一，无活动灯时为零。权重树的场景灯叶子直接对应槽号，空槽权重为零，自发光实例叶子排在场景灯槽区间之后。矩形灯求交直接扫描场景灯槽，只处理矩形灯。 [权重树](../Engine/Asset/Shaders/Lighting/LightWeightTree.hlsl) · [矩形灯求交](../Engine/Asset/Shaders/Lib/Sample/LightIntersection.hlsli)
- `DeviceArray::GetInitializationRanges()` 提供本次需要初始化的合并字节区间。`SceneLightPageClear` 在 Graphics 队列用 RenderGraph 的 `FillBuffer` 清零这些区间，随后 `SceneLightUpdate` 用 `ElementUpdate.hlsl` 写入变更记录。清零和更新通过同一个 buffer 的写入依赖排序。 [区间接口](../Engine/Code/RunTime/Feature/Render/Binding/DeviceArray.h) · [灯光更新 pass](../Engine/Code/RunTime/Feature/Render/Binding/Scene/SceneBindingSystem.cpp) · [FillBuffer](../Engine/Code/RunTime/Feature/Render/RenderGraph/PassScopes.h)

### 自发光网格统一到光源列表（设计草案）

当前帧仍通过 `g_EmissiveInstanceIDs` 枚举发光实例。拟在 `g_Lights` 增加 `EmissiveMesh` 类型，使场景灯和发光网格共用 `LightSlot`。权重树统一按光源槽建叶子，采样和历史样本统一记录槽号。发光网格样本另外记录三角形索引和重心坐标。迁移完成后，光源枚举可移除 `g_EmissiveInstanceIDs`。 [当前光源采样](../Engine/Asset/Shaders/Lib/Sample/LightSampling.hlsli) · [权重树](../Engine/Asset/Shaders/Lighting/LightWeightTree.hlsl)

- `InstanceBindingSystem` 标记本帧可参与采样的发光实例。`SceneBindingSystem` 统一分配光源槽、收集两帧数据并处理关闭和删除。新增与失效沿用场景灯的槽位生命周期。 [发光实例判定](../Engine/Code/RunTime/Feature/Render/Binding/Instance/InstanceBindingSystem.cpp) · [灯光槽位](../Engine/Code/RunTime/Feature/Render/Binding/Scene/SceneBinding.h)
- 发光网格记录携带 `InstanceID`，用于访问本帧实例。GGX 命中给出 `InstanceID`，MIS 反过来需要光源槽号。候选做法是用 `InstanceData` 的 padding 字段记录本帧光源槽。更新顺序为标记发光实例、分配光源槽、编码实例记录，保证新发光实例当帧就能双向查找。 [实例数据](../Engine/Code/RunTime/Feature/Render/Binding/Instance/InstanceData.h) · [GGX 命中后的光源 PDF](../Engine/Asset/Shaders/Lib/RayTracing/RayTracingLighting.hlsli)
- 历史记录保留复测所需的变换、几何引用和发光参数。两帧使用同一张发光贴图时，共用该贴图并按帧读取发光颜色和强度。发光贴图变化或几何拓扑无法对应时，跳过该样本的复测。确认目标帧的发光实例已关闭或删除时，目标帧贡献按零处理。 [发光求值](../Engine/Asset/Shaders/Material/MaterialTemplate.hlsli) · [材质贴图生命周期](../Engine/Code/RunTime/Feature/Render/Material/MaterialTextureSystem.cpp)

## 3. 表面配对和光源样本历史

复用 `ResolvedVelocity`、上一帧相机数据和历史深度，在两帧中定位同一个接收表面点，并验证对应关系。复测使用目标帧的表面位置，可以沿用被选中样本一侧的着色属性，以减少像素中心和 jitter 差异引入的误报。置信度 pass 自己声明历史深度的读取依赖，关闭 TAA 时也能使用这条路径。 [时域历史验证](TODO_TemporalHistoryValidationPlan.md) · [RTXDI 表面重建](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Samples/FullSample/Shaders/DenoisingPasses/ComputeGradients.hlsl#L101-L127)

在直射光 pass 中增加光源样本记录，包含稳定光源 ID、灯面采样坐标和该样本自身的 diffuse/specular 贡献。场景灯的 ID 直接记录持久槽号。网格自发光样本还需要三角形索引和重心坐标。在合并 GGX 分支之前记录光源分支，本帧写入的记录在下一帧供读取。可复用现有的历史资源机制。 [双样本直射光方案](TODO_DirectLightingMISPlan.md) · [直射光估计器](../Engine/Asset/Shaders/Lib/RayTracing/RayTracingDirectLighting.hlsli)

## 4. 双向复测和时域梯度

在直射光之后生成低分辨率梯度。对通过表面验证的候选，比较两帧缓存的贡献，选择较强的一侧。若选中旧样本，使用本帧灯光与 TLAS 复测。若选中本帧样本，使用上一帧灯光与 TLAS 复测。光源确实只存在于其中一帧时，缺失侧的贡献按零处理。

缓存与复测使用一致的贡献定义，避免光源选择概率或 MIS 权重的变化形成虚假的光照梯度。分别记录 diffuse 和 specular 的贡献差与参考亮度，供后续滤波。 [RTXDI 梯度实现](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Samples/FullSample/Shaders/DenoisingPasses/ComputeGradients.hlsl)

## 5. 置信度滤波和 RELAX 输入

对梯度和参考亮度分别做空间滤波，再将相对变化映射到 `[0, 1]`。生成独立的 diffuse 和 specular 置信度，替换 RELAX 当前的共享常量输入。RELAX 的时域累积按上一帧的 UV 取置信度，输出坐标应符合这个约定。首帧、视图切换、尺寸变化或历史资源缺失时，沿用 RELAX 的重启或中性置信度路径。 [RTXDI 置信度转换](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/Confidence.md#converting-the-gradients-into-confidence) · [NRD 输入契约](https://github.com/NVIDIA-RTX/NRD/blob/v4.18.0/Include/NRDDescs.h#L46-L55)

## 实施时确定的细节

自发光网格的历史数据布局、样本记录格式、采样密度、贡献计算公式和滤波参数，在接入时结合现有接口和 GPU capture 确定。

## 验收

- 静止相机下分别开灯、关灯和移动灯。对比常量置信度，检查 RELAX 输出的拖尾持续帧数。
- 固定灯光并移动遮挡物，分别验证遮挡出现和消失时的响应，覆盖两个复测方向。
- 在静态场景和仅相机移动的场景中检查误触发。验证关闭 TAA、首帧和分辨率切换时的行为。
- 用同一段场景记录复测和滤波新增的 GPU 时间，同时对比稳定区域的噪声。
