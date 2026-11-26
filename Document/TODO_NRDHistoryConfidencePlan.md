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

### 自发光网格与解析灯共用光源表（设计）

`g_Lights` 的同一个分配器为解析灯和自发光网格实例分配稳定槽位。每个槽继续使用 `LightRecord { current, previous }`，每帧的 `LightData` 独立保存类型。新增 `EmissiveMesh` 类型后，网格源所需的变换、几何快照、版本和发光纹理引用都放在当帧的 `LightData` 中。 [现有光源记录](../Engine/Asset/Shaders/LightData.hlsli) · [灯光槽位](../Engine/Code/RunTime/Feature/Render/Binding/Scene/SceneBinding.h)

#### GPU 布局

`LightData` 使用带类型标签的固定布局，公共字段直接访问，其余字段按 `type` 解码。下表中的分量顺序为 `x`、`y`、`z`、`w`。

| 字段 | 字节偏移 | 解析灯 | 自发光网格 |
| --- | ---: | --- | --- |
| `float3 color`、`uint type` | 0、12 | 光色、类型 | 发光颜色、`EmissiveMesh` |
| `uint4 payload[0]` | 16 | `direction.xyz`、`directionalConeHeight` | 物体到世界仿射变换的第 0 行 |
| `uint4 payload[1]` | 32 | `position.xyz`、`invRange` | 变换的第 1 行 |
| `uint4 payload[2]` | 48 | `cosInner`、`cosOuter`、`shadowIndex`、`shadowMapCount` | 变换的第 2 行 |
| `uint4 payload[3]` | 64 | `right.xyz`、`shadowMaskIndex` | `firstIndex`、`vertexOffset`、`indexByteStride`、`triangleCount` |
| `uint3 extra` | 80 | `halfWidth`、`halfHeight`、`castShadow` | 几何版本、发光纹理索引、`localSurfaceArea` |
| `float intensity` | 92 | 光强 | 发光强度 |

C++ 按固定偏移打包，初始化所有上传字节，并用 `sizeof` 和 `offsetof` 校验布局。HLSL 通过 `DecodeAnalyticLight()` 和 `DecodeEmissiveLight()` 提供具名字段，在解码时用 `asfloat()` 和 `asint()` 解释对应位模式。采样和求交代码使用解码后的字段。网格光源沿用当前总是投射阴影的策略，法线变换从模型变换推导。 [当前采样策略](../Engine/Asset/Shaders/Lib/Sample/LightSampling.hlsli) · [几何读取](../Engine/Asset/Shaders/Lib/Geometry/MeshGeometry.hlsli) · [网格权重](../Engine/Asset/Shaders/Lighting/LightWeightTree.hlsl)

用仓库自带的 DXC 编译了上述固定布局。DXIL 和 Vulkan SPIR-V 的 `LightData` 都为 96 B，`LightRecord` 步长都为 192 B。这个验证只覆盖布局，接入后的 shader 和渲染仍待验证。

#### 槽位更新、采样与复测

- 材质和几何准备好后，`InstanceBindingSystem` 识别可采样的发光实例，`SceneBindingSystem` 为它分配或沿用光源槽。实例上传前，用 `InstanceData` 的一个 padding 字段存本帧 `LightSlotId`，以 `UINT32_MAX` 表示实例没有光源槽。GGX 命中后用 `InstanceID` 读取该槽号，再按槽查权重树叶子。 [发光实例判定](../Engine/Code/RunTime/Feature/Render/Binding/Instance/InstanceBindingSystem.cpp) · [实例数据](../Engine/Code/RunTime/Feature/Render/Binding/Instance/InstanceData.h) · [GGX 命中后的光源 PDF](../Engine/Asset/Shaders/Lib/RayTracing/RayTracingLighting.hlsli)
- 活动槽先执行 `previous = current`，再写本帧状态。新光源的 `previous.type` 为 `Invalid`。关灯或删除当帧将 `current.type` 写为 `Invalid`，`previous` 保留上一帧的有效值。槽位和历史引用的几何分配、纹理及描述符留到相邻帧的 GPU 读取完成后再回收。历史样本只用于相邻的有效帧，查找时允许访问刚删除且已经超出本帧 `g_LightSlotCount` 的槽。 [槽位更新](../Engine/Code/RunTime/Feature/Render/Binding/Scene/SceneBindingSystem.cpp) · [历史读取接口](../Engine/Asset/Shaders/SceneBindings.hlsli)
- 权重树按 `g_Lights` 槽号建叶子，只用 `current` 估算权重，`Invalid` 槽权重为零。迁移完成后移除 `g_EmissiveInstanceIDs`。第一版的矩形灯求交继续遍历当前槽范围并筛选矩形灯。 [权重树](../Engine/Asset/Shaders/Lighting/LightWeightTree.hlsl) · [矩形灯求交](../Engine/Asset/Shaders/Lib/Sample/LightIntersection.hlsli)
- 网格样本保存光源槽号、三角形索引和重心坐标。复测从目标帧的 `LightData` 取几何快照和变换，重建采样点和 UV，再用该帧的发光颜色、强度和纹理求值。两帧可共享静态顶点、索引和发光纹理。第一版在发光纹理版本变化、几何拓扑无法对应或历史资源不可读时跳过该探针。确认目标帧光源已关闭或删除时，目标帧贡献按零处理。 [当前网格采样](../Engine/Asset/Shaders/Lib/Sample/LightSampling.hlsli) · [发光求值](../Engine/Asset/Shaders/Material/MaterialTemplate.hlsli) · [材质贴图生命周期](../Engine/Code/RunTime/Feature/Render/Material/MaterialTextureSystem.cpp)

## 3. 表面配对和光源样本历史

复用 `ResolvedVelocity`、上一帧相机数据和历史深度，在两帧中定位同一个接收表面点，并验证对应关系。复测使用目标帧的表面位置，可以沿用被选中样本一侧的着色属性，以减少像素中心和 jitter 差异引入的误报。置信度 pass 自己声明历史深度的读取依赖，关闭 TAA 时也能使用这条路径。 [时域历史验证](TODO_TemporalHistoryValidationPlan.md) · [RTXDI 表面重建](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Samples/FullSample/Shaders/DenoisingPasses/ComputeGradients.hlsl#L101-L127)

在直射光 pass 中增加光源样本记录，包含 `g_Lights` 的稳定槽号、灯面采样坐标和该样本自身的 diffuse/specular 贡献。网格样本用三角形索引和重心坐标指定采样点。在合并 GGX 分支之前记录光源分支，本帧写入的记录在下一帧供读取。可复用现有的历史资源机制。 [双样本直射光方案](TODO_DirectLightingMISPlan.md) · [直射光估计器](../Engine/Asset/Shaders/Lib/RayTracing/RayTracingDirectLighting.hlsli)

## 4. 双向复测和时域梯度

在直射光之后生成低分辨率梯度。对通过表面验证的候选，比较两帧缓存的贡献，选择较强的一侧。若选中旧样本，使用本帧灯光与 TLAS 复测。若选中本帧样本，使用上一帧灯光与 TLAS 复测。光源确实只存在于其中一帧时，缺失侧的贡献按零处理。

缓存与复测使用一致的贡献定义，避免光源选择概率或 MIS 权重的变化形成虚假的光照梯度。分别记录 diffuse 和 specular 的贡献差与参考亮度，供后续滤波。 [RTXDI 梯度实现](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Samples/FullSample/Shaders/DenoisingPasses/ComputeGradients.hlsl)

## 5. 置信度滤波和 RELAX 输入

对梯度和参考亮度分别做空间滤波，再将相对变化映射到 `[0, 1]`。生成独立的 diffuse 和 specular 置信度，替换 RELAX 当前的共享常量输入。RELAX 的时域累积按上一帧的 UV 取置信度，输出坐标应符合这个约定。首帧、视图切换、尺寸变化或历史资源缺失时，沿用 RELAX 的重启或中性置信度路径。 [RTXDI 置信度转换](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/Confidence.md#converting-the-gradients-into-confidence) · [NRD 输入契约](https://github.com/NVIDIA-RTX/NRD/blob/v4.18.0/Include/NRDDescs.h#L46-L55)

## 实施时确定的细节

样本记录格式、采样密度、贡献计算公式和滤波参数，在接入时结合现有接口和 GPU capture 确定。

## 验收

- 静止相机下分别开灯、关灯和移动灯。对比常量置信度，检查 RELAX 输出的拖尾持续帧数。
- 固定网格拓扑，依次移动发光网格、改变发光颜色和强度、关灯或删除。检查双向复测读取各帧独立的光源参数，并验证删除最高活动槽时上一帧样本仍可读取。
- 固定灯光并移动遮挡物，分别验证遮挡出现和消失时的响应，覆盖两个复测方向。
- 在静态场景和仅相机移动的场景中检查误触发。验证关闭 TAA、首帧和分辨率切换时的行为。
- 用同一段场景记录复测和滤波新增的 GPU 时间，同时对比稳定区域的噪声。
