# NRD 直射光双向样本置信度接入方案

Spark 当前给 RELAX 的 diffuse 和 specular 绑定同一张值为 `1` 的置信度纹理。接入按像素变化的置信度，让灯光开启、关闭、移动或遮挡变化时，RELAX 能在受影响的区域更快降低旧光照的权重。

方案沿用 RTXDI 的双向样本复测思路。对齐两帧中同一个接收表面，在小块内从两帧已缓存的样本中选一个贡献较强的样本，再到另一帧评估同一个光源采样点。每个有效探针只做一次跨帧评估。 [RTXDI 置信度方案](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/Confidence.md#temporal-gradients-in-rtxdi)

## 1. 两帧光追场景

由光追系统管理当前和上一帧的 TLAS，提供各自的资源句柄和有效状态。需要复测的渲染 pass 自行导入两份场景并声明读取依赖。每帧构建一次 TLAS，构建结果保留到下一帧使用。TLAS 存储的复用和旧 BLAS 的释放都要等到相关 GPU 读取结束，尤其要覆盖网格删除、替换或形变。静态 BLAS 可以共享。 [现有光追场景方案](TODO_RayTracingScenePlan.md)

## 2. 灯光持久化和跨帧身份

沿用实例和材质的持久槽位思路，为灯光维护稳定身份，并保留当前和上一帧的灯光参数。通过稳定身份定位各帧的灯光记录，明确区分同一盏灯、真正新增的灯和已删除的灯。场景灯遍历、光源权重树和矩形灯求交目前依赖紧凑索引。迁移时保留活动灯光的紧凑索引语义，持久槽与活动索引之间的布局在实现时决定。对自发光网格，同样需要跨帧辨认发光实例，并保留复测所需的几何、变换和发光状态。 [灯光绑定](../Engine/Code/RunTime/Feature/Render/Binding/Scene/SceneBindingSystem.cpp) · [光源采样](../Engine/Asset/Shaders/Lib/Sample/LightSampling.hlsli)

## 3. 表面配对和光源样本历史

复用 `ResolvedVelocity`、上一帧相机数据和历史深度，在两帧中定位同一个接收表面点，并验证对应关系。复测使用目标帧的表面位置，可以沿用被选中样本一侧的着色属性，以减少像素中心和 jitter 差异引入的误报。置信度 pass 自己声明历史深度的读取依赖，关闭 TAA 时也能使用这条路径。 [时域历史验证](TODO_TemporalHistoryValidationPlan.md) · [RTXDI 表面重建](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Samples/FullSample/Shaders/DenoisingPasses/ComputeGradients.hlsl#L101-L127)

在直射光 pass 中增加光源样本记录，包含稳定光源 ID、灯面采样坐标和该样本自身的 diffuse/specular 贡献。网格自发光样本还需要三角形索引和重心坐标。在合并 GGX 分支之前记录光源分支，本帧写入的记录在下一帧供读取。可复用现有的历史资源机制。 [双样本直射光方案](TODO_DirectLightingMISPlan.md) · [直射光估计器](../Engine/Asset/Shaders/Lib/RayTracing/RayTracingDirectLighting.hlsli)

## 4. 双向复测和时域梯度

在直射光之后生成低分辨率梯度。对通过表面验证的候选，比较两帧缓存的贡献，选择较强的一侧。若选中旧样本，使用本帧灯光与 TLAS 复测。若选中本帧样本，使用上一帧灯光与 TLAS 复测。光源确实只存在于其中一帧时，缺失侧的贡献按零处理。

缓存与复测使用一致的贡献定义，避免光源选择概率或 MIS 权重的变化形成虚假的光照梯度。分别记录 diffuse 和 specular 的贡献差与参考亮度，供后续滤波。 [RTXDI 梯度实现](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Samples/FullSample/Shaders/DenoisingPasses/ComputeGradients.hlsl)

## 5. 置信度滤波和 RELAX 输入

对梯度和参考亮度分别做空间滤波，再将相对变化映射到 `[0, 1]`。生成独立的 diffuse 和 specular 置信度，替换 RELAX 当前的共享常量输入。RELAX 的时域累积按上一帧的 UV 取置信度，输出坐标应符合这个约定。首帧、视图切换、尺寸变化或历史资源缺失时，沿用 RELAX 的重启或中性置信度路径。 [RTXDI 置信度转换](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/Confidence.md#converting-the-gradients-into-confidence) · [NRD 输入契约](https://github.com/NVIDIA-RTX/NRD/blob/v4.18.0/Include/NRDDescs.h#L46-L55)

## 实施时确定的细节

TLAS 轮转与同步方式、灯光数据布局、样本记录格式、采样密度、贡献计算公式和滤波参数，在接入时结合现有接口和 GPU capture 确定。

## 验收

- 静止相机下分别开灯、关灯和移动灯。对比常量置信度，检查 RELAX 输出的拖尾持续帧数。
- 固定灯光并移动遮挡物，分别验证遮挡出现和消失时的响应，覆盖两个复测方向。
- 在静态场景和仅相机移动的场景中检查误触发。验证关闭 TAA、首帧和分辨率切换时的行为。
- 用同一段场景记录复测和滤波新增的 GPU 时间，同时对比稳定区域的噪声。
