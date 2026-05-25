# ModelAsset 实施方案

基于已有的 Asset 基础设施（AssetBuildBus / AssetDataBase / AssetBuildContext）+ 三方库（fastgltf / meshoptimizer / MikkTSpace）实现 glTF 模型资产。

---

## 1. 三方库角色分工

| 库 | 阶段 | 用途 |
|---|---|---|
| **fastgltf** | Loader | 解析 `.gltf`/`.glb` 成内存 IR；提供 buffer / accessor / material / texture / node 视图 |
| **meshoptimizer** | Compiler | 顶点去重、cache 优化、overdraw 优化、（后置）量化、LOD、meshlet |
| **MikkTSpace** | Compiler | 原模型缺 tangent 且材质带 normalTexture 时合成 MikkT 标准切线 |

---

## 2. 设计原则与关键决策

### 2.1 v1 范围：方案 A（Mesh/Material 内嵌，Image 独立资产）

```
ModelAsset
  ├─ SubMesh[]      ← 顶点/索引数据内嵌
  ├─ Material[]     ← PBR 标量内嵌
  │     └─ AssetId  → ImageAsset  ← 仅贴图是独立资产
  └─ Node[]
```

理由：贴图天然多模型共享、编译耗时最长（mip + BC），独立资产价值最高；Mesh/Material 和 glTF 文件强绑定，独立的收益小、复杂度高。未来需要时将 Material 独立化只是数据结构调整，AssetId/sub-label 机制已就绪。

### 2.2 节点层级作为"prefab 数据"

ModelAssetData 持有 flat Node 数组（parent 索引 + local transform + mesh 索引）。
**不**在 Load 时直接 spawn ECS 实体——那是上层 `InstantiateModel(world, model)` 的事。
一份 asset 多次实例化的能力天然存在。

### 2.3 贴图槽 → ImageAssetDescriptor 的自动派生

不同 glTF 贴图槽走不同 BC 格式和色彩空间：

| glTF 贴图槽 | colorSpace | compression | 备注 |
|---|---|---|---|
| baseColorTexture | sRGB | BC7_RGBA | 高质量 albedo |
| normalTexture | Linear | BC5_RG | 需 ImageAssetCompiler 加 normal-map 模式 |
| metallicRoughnessTexture | Linear | BC3_RGBA | glTF 规范：B=metallic, G=roughness |
| emissiveTexture | sRGB | BC7_RGBA | 自发光 |
| occlusionTexture | Linear | BC4_R | 单通道 |

→ **依赖**：ImageAssetDescriptor 需要加一个 `usage` enum，ImageAssetCompiler 需要加 normal-map 编码分支（XY 提取 + BC5）。可放在 v1 后置。

---

## 3. 类型骨架

```cpp
// Per-instance 编译配置，进 AssetId hash
class ModelAssetDescriptor : public AssetDescriptor
{
public:
    bool generateTangents     = true;   // 缺 tangent 且材质带 normal map 时用 MikkTSpace 合成
    bool optimizeVertexCache  = true;
    bool optimizeOverdraw     = true;
    bool optimizeVertexFetch  = true;
    // 后续可加：bool quantize / uint32_t lodCount / bool buildMeshlets
    AssetHash Hash() const override;
};

// Loader 产物：fastgltf 解析结果 + 路径环境
class ModelAssetRawData : public AssetData
{
public:
    fastgltf::Asset     gltf;          // 完整解析结果（buffers / accessors / meshes / materials / images）
    eastl::string       resolvedPath;  // .gltf 的绝对路径
    eastl::string       baseDir;       // 父目录，用于拼外部贴图 URI
};

// Compiler 产物：运行时形态
class ModelAssetData : public AssetData
{
public:
    eastl::vector<SubMesh>   submeshes;
    eastl::vector<Material>  materials;
    eastl::vector<Node>      nodes;
    Aabb                     bounds;
};

struct SubMesh
{
    eastl::vector<uint8_t>   vertexBuffer;   // interleaved
    eastl::vector<uint8_t>   indexBuffer;
    VertexLayout             layout;         // attribute 偏移 + 量化 scale/bias
    uint32_t                 materialIndex;
    Aabb                     bounds;
    // 后续：LOD 区段、meshlet 数据
};

struct Material
{
    // PBR 标量因子（内嵌）
    vec4    baseColorFactor   = {1,1,1,1};
    float   metallicFactor    = 1.0f;
    float   roughnessFactor   = 1.0f;
    vec3    emissiveFactor    = {0,0,0};
    float   alphaCutoff       = 0.5f;
    AlphaMode alphaMode       = AlphaMode::Opaque;

    // 贴图槽：仅持 AssetId（弱引用），不持有 Ptr
    AssetId baseColorImageId;
    AssetId normalImageId;
    AssetId metallicRoughnessImageId;
    AssetId emissiveImageId;
    AssetId occlusionImageId;
};

struct Node
{
    int32_t       parent;        // -1 = 根
    mat4          localTransform;
    int32_t       meshIndex;     // -1 = 纯 transform 节点
    eastl::string name;          // debug
};

class ModelAsset : public Asset
{
public:
    using Descriptor = ModelAssetDescriptor;
    static constexpr AssetType GetAssetTypeStatic() { return AssetType::Model; }
    static Ptr<AssetDescriptor> DefaultDescriptor();
    explicit ModelAsset(AssetId id);

    const ModelAssetData* GetModelData() const;
    // 便利 getter：submesh 数量、材质数量、bounds 等
};
```

**Material 的贴图槽只存 AssetId（弱引用）**：避免循环引用、贴图独立 hot-reload、模型 Ready 不必等贴图 Ready。

---

## 4. Loader 阶段（fastgltf）

```cpp
void ModelAssetBuilder::Load(AssetBuildContext& ctx)
{
    ASSERT(ctx.type == AssetType::Model, ...);

    eastl::string path = ctx.ResolvePath(ctx.id.GetPath());
    if (path.empty()) { /* error log */ return; }

    fastgltf::GltfDataBuffer buf;
    buf.loadFromFile(path);

    fastgltf::Parser parser;
    auto fileType = fastgltf::determineGltfFileType(&buf);

    constexpr auto options = fastgltf::Options::LoadExternalBuffers;
    // 注意：不开 LoadExternalImages —— 贴图由我们路由到 ImageAssetBuilder

    auto expected = (fileType == fastgltf::GltfType::GLB)
                  ? parser.loadGltfBinary(&buf, basePath, options)
                  : parser.loadGltfJson(&buf, basePath, options);
    if (expected.error() != fastgltf::Error::None) { /* error */ return; }

    auto raw = MakeUnique<ModelAssetRawData>();
    raw->gltf         = eastl::move(expected.get());
    raw->resolvedPath = path;
    raw->baseDir      = ...;
    ctx.rawData = eastl::move(raw);
}
```

**关键 option**：
- `LoadExternalBuffers` 让 fastgltf 自动读 `.bin`（外部 buffer）
- **不开** `LoadExternalImages` —— 我们自己处理贴图路径，避免双重解析

---

## 5. Compiler 阶段（meshoptimizer + MikkTSpace）

按 `gltf.meshes[].primitives[]` 循环，每个 primitive 一个 SubMesh。

### 5.1 顶点属性提取（fastgltf accessor 视图）

```cpp
eastl::vector<vec3> positions;
fastgltf::iterateAccessor<fastgltf::math::fvec3>(gltf, posAccessor,
    [&](fvec3 p) { positions.emplace_back(p.x, p.y, p.z); });

// 类似拉 normal / tangent / uv0 / color0
// joints/weights → v1 跳过
```

### 5.2 索引提取

```cpp
eastl::vector<uint32_t> indices;
if (primitive.indicesAccessor) {
    fastgltf::iterateAccessor<uint32_t>(gltf, indexAccessor,
        [&](uint32_t i) { indices.push_back(i); });
} else {
    // 无索引：合成单调递增
    indices.resize(positions.size());
    std::iota(indices.begin(), indices.end(), 0);
}
```

### 5.3 切线生成（MikkTSpace）

**触发条件**：material 有 `normalTexture` 且 primitive **没有** TANGENT 属性。

```cpp
struct MikktUserData {
    vec3* positions;
    vec3* normals;
    vec2* uvs;
    uint32_t* indices;
    vec4* outTangents;   // (xyz, sign)
    uint32_t triCount;
};

SMikkTSpaceInterface iface{};
iface.m_getNumFaces           = [](const SMikkTSpaceContext* c) -> int {
    return static_cast<MikktUserData*>(c->m_pUserData)->triCount;
};
iface.m_getNumVerticesOfFace  = [](auto*, int) -> int { return 3; };
iface.m_getPosition           = ...;  // 从 indices[face*3+vert] → positions
iface.m_getNormal             = ...;
iface.m_getTexCoord           = ...;
iface.m_setTSpaceBasic        = [](auto* c, const float fvTangent[3], 
                                    float fSign, int face, int vert) {
    auto* u = static_cast<MikktUserData*>(c->m_pUserData);
    u->outTangents[u->indices[face*3 + vert]] = {fvTangent[0], fvTangent[1], fvTangent[2], fSign};
};

SMikkTSpaceContext mctx{ &iface, &userData };
genTangSpaceDefault(&mctx);
```

### 5.4 meshoptimizer 流水线

```cpp
// a. 顶点去重
eastl::vector<uint32_t> remap(positions.size());
size_t newVertCount = meshopt_generateVertexRemap(
    remap.data(),
    indices.data(), indices.size(),
    interleavedVerts.data(), positions.size(), sizeof(Vertex));

meshopt_remapIndexBuffer(indices.data(), indices.data(), indices.size(), remap.data());
meshopt_remapVertexBuffer(remappedVerts.data(), interleavedVerts.data(), 
                          positions.size(), sizeof(Vertex), remap.data());

// b. post-T&L cache 优化
meshopt_optimizeVertexCache(indices.data(), indices.data(), 
                            indices.size(), newVertCount);

// c. 减少 overdraw（需要 position pointer）
meshopt_optimizeOverdraw(indices.data(), indices.data(), indices.size(),
                         &remappedVerts[0].position.x, newVertCount, 
                         sizeof(Vertex), 1.05f);

// d. 顶点 buffer 局部性
meshopt_optimizeVertexFetch(remappedVerts.data(), indices.data(), indices.size(),
                            remappedVerts.data(), newVertCount, sizeof(Vertex));
```

### 5.5 AABB

```cpp
Aabb bounds;
for (auto& v : remappedVerts) bounds.Expand(v.position);
```

### 5.6 后置：量化 / LOD / meshlet（v1 不实装）

descriptor 上的开关默认 false。接入点：
- 量化：`meshopt_encodeVertexBuffer` / `meshopt_quantizeSnorm` / 自定义 oct 编码
- LOD：`meshopt_simplify`（生成多套 indices）
- Meshlet：`meshopt_buildMeshlets`（绑定 ~64 顶点 / 124 三角形）

---

## 6. 材质 + 贴图（**子资产用例**）

### 6.1 总体流程

```cpp
void ProcessMaterial(ctx, gltfMat, out)
{
    Material mat;
    mat.baseColorFactor   = gltfMat.pbrData.baseColorFactor;
    mat.metallicFactor    = gltfMat.pbrData.metallicFactor;
    mat.roughnessFactor   = gltfMat.pbrData.roughnessFactor;
    mat.emissiveFactor    = gltfMat.emissiveFactor;
    mat.alphaCutoff       = gltfMat.alphaCutoff;
    mat.alphaMode         = MapAlphaMode(gltfMat.alphaMode);

    if (auto& bc = gltfMat.pbrData.baseColorTexture)
        mat.baseColorImageId = ResolveTexture(ctx, bc->textureIndex,
            { .usage = Albedo,    .colorSpace = sRGB,   .compression = BC7_RGBA });

    if (auto& n  = gltfMat.normalTexture)
        mat.normalImageId = ResolveTexture(ctx, n->textureIndex,
            { .usage = NormalMap, .colorSpace = Linear, .compression = BC5_RG });

    if (auto& mr = gltfMat.pbrData.metallicRoughnessTexture)
        mat.metallicRoughnessImageId = ResolveTexture(ctx, mr->textureIndex,
            { .usage = Data,      .colorSpace = Linear, .compression = BC3_RGBA });

    if (auto& em = gltfMat.emissiveTexture)
        mat.emissiveImageId = ResolveTexture(ctx, em->textureIndex,
            { .usage = Albedo,    .colorSpace = sRGB,   .compression = BC7_RGBA });

    if (auto& oc = gltfMat.occlusionTexture)
        mat.occlusionImageId = ResolveTexture(ctx, oc->textureIndex,
            { .usage = Data,      .colorSpace = Linear, .compression = BC4_R });

    out->materials.push_back(eastl::move(mat));
}
```

### 6.2 外部 vs 内嵌：两条路径

```cpp
AssetId ResolveTexture(AssetBuildContext& ctx, size_t texIdx, ImageAssetDescriptor desc)
{
    auto& gltfTex   = raw->gltf.textures[texIdx];
    auto& gltfImage = raw->gltf.images[gltfTex.imageIndex.value()];

    return std::visit(overloaded{
        // 外部文件：URI（"textures/foo.png"）
        [&](fastgltf::sources::URI& u) {
            eastl::string fullPath = JoinPath(raw->baseDir, u.uri.path());
            AssetId id = AssetId::Of<ImageAsset>(fullPath, desc);
            Service<AssetManager>::Get()->RequestAsset(id, AssetType::Image);
            return id;
        },
        // 内嵌：bufferView 切片（.glb 主流情况）
        [&](fastgltf::sources::BufferView& bv) {
            AssetId id = AssetId::OfSub<ImageAsset>(
                ctx.id.GetPath(), MakeImageSubLabel(texIdx), desc);
            EmitEmbeddedImage(ctx, id, ExtractBytes(raw->gltf, bv), gltfImage.mimeType);
            return id;
        },
        // 内嵌：data URI（base64 解码后的 Array）
        [&](fastgltf::sources::Array& arr) {
            AssetId id = AssetId::OfSub<ImageAsset>(
                ctx.id.GetPath(), MakeImageSubLabel(texIdx), desc);
            EmitEmbeddedImage(ctx, id, arr.bytes, gltfImage.mimeType);
            return id;
        },
        // 其它 source（fallback / etc）：标错并返回空 id
        [&](auto&) { return AssetId{}; }
    }, gltfImage.data);
}
```

### 6.3 子资产发射

```cpp
void EmitEmbeddedImage(ctx, imgId, encodedBytes, mimeType)
{
    // KTX2 v1 不支持
    if (mimeType == "image/ktx2") {
        LOG_ERROR("KTX2 embedded texture not supported in v1: {}", imgId.GetSubLabel());
        return;
    }

    // 去重
    if (ctx.db->Find(imgId)) return;

    // 通过 Bus 创建 ImageAsset 对象
    Ptr<Asset> imgAsset;
    AssetBuildBus::EventResult(imgAsset, AssetType::Image,
                               &AssetBuildEvents::CreateAsset, imgId);
    if (!imgAsset) return;

    // 注册到 DB（处理竞争）
    Ptr<Asset> stored = ctx.db->InsertOrGet(imgId, imgAsset);
    if (stored.get() != imgAsset.get()) return;   // 别人先注册了

    // 派生子 ctx 并预置 rawData（跳过 Load）
    AssetBuildContext child = ctx.MakeChild(imgId, AssetType::Image);
    child.rawData = ImageAssetLoader::DecodeFromMemory(encodedBytes);   // ← 见 §7
    if (!child.rawData) {
        stored->SetStatus(AssetStatus::Error);
        return;
    }

    // 同步派发 Compile 事件
    stored->SetStatus(AssetStatus::Compiling);
    AssetBuildBus::Event(AssetType::Image, &AssetBuildEvents::Compile, child);

    if (child.compiledData) {
        stored->SetDataReady(eastl::move(child.compiledData));
    } else {
        stored->SetStatus(AssetStatus::Error);
    }
}
```

---

## 7. ImageAssetLoader 前置改造（关键）

### 7.1 问题

glTF 内嵌的 image 字节是**原始编码文件流**（PNG/JPEG 头还在），不是 decoded pixels。
ImageAssetCompiler 期待 `ImageAssetRawData{ width, height, format, pixels }`，即 decoded 状态。

**直接把 glTF 字节塞进 rawData 会让 Compiler 把 PNG header 当 mip0 来 BC 编码，整体崩溃。**

### 7.2 解决方案：抽出 from-memory decode 路径

stb 已有 `stbi_load_from_memory` / `stbi_loadf_from_memory` / `stbi_is_hdr_from_memory` —— API 对应 file 版。

[ImageAssetLoader.cpp](Engine/Code/RunTime/Resource/Image/ImageAssetLoader.cpp) 当前 ~50 行的 `Load` 里：前 5 行解路径，后 45 行做 stb 调用 + 通道处理。

**重构**：把后 45 行（stb decode + format 推断 + 3→4 通道升级 + 包 ImageAssetRawData）抽出来成共享 helper：

```cpp
class ImageAssetLoader
{
public:
    UniquePtr<AssetData> Load(const AssetId& id);                  // 现有：从磁盘
    
    /// 新增：从已加载到内存的编码字节流 decode。
    /// bytes 可以是 PNG / JPEG / HDR（任何 stb 支持的格式）。
    /// sourceLabel 仅用于 log，可传 "embedded:helmet.glb:image/3"。
    static UniquePtr<AssetData> DecodeFromMemory(
        eastl::span<const uint8_t> bytes,
        eastl::string_view sourceLabel);
};
```

内部实现复用同一段 stb 调用，只换数据源（`stbi_load` ↔ `stbi_load_from_memory`）。

**ImageAssetCompiler 完全不动**——它拿到的还是统一形态的 `ImageAssetRawData`。

### 7.3 顺序

这个改造**独立于** ModelAsset，可以单独一个 commit 先落地。落地之后 ModelAsset 实现里直接调 `DecodeFromMemory` 就行。

---

## 8. Builder 集成

跟现有 ImageAssetBuilder/ShaderAssetBuilder 同构：

```cpp
class ModelAssetBuilder final : public ISystem, public AssetBuildBus::Handler
{
public:
    eastl::vector<HashString> Request() const override { return {}; }
    HashString                GetName() const override;

    Ptr<Asset> CreateAsset(const AssetId& id) override;
    void       Load(AssetBuildContext& ctx) override;
    void       Compile(AssetBuildContext& ctx) override;

private:
    void InitInternal()     override;   // BusConnect(AssetType::Model)
    void ShutdownInternal() override;   // BusDisconnect

    ModelAssetLoader   m_loader;   // 内部委托
    ModelAssetCompiler m_compiler;
};
```

[SparkAssetManager::InitInternal](Engine/Code/RunTime/Resource/AssetManager.cpp) 加一行：

```cpp
m_modelBuilder = CreateSystem<ModelAssetBuilder>();
m_modelBuilder->Init();
```

[AssetManager.h](Engine/Code/RunTime/Resource/AssetManager.h) 加一个成员：

```cpp
SystemUniquePtr<ModelAssetBuilder>  m_modelBuilder;
```

---

## 9. 实施步骤

| # | 内容 | 验收 |
|---|---|---|
| **0** | **`ImageAssetLoader::DecodeFromMemory` 抽出**（§7） | 现有 ImageAsset 测试仍通过 |
| 1 | `AssetType::Model` 入枚举 + `ModelAsset` 骨架 + `ModelAssetDescriptor` | 编译过 |
| 2 | `ModelAssetRawData` / `ModelAssetData` 字段定义（空实现） | 编译过 |
| 3 | `ModelAssetLoader::Load`：fastgltf 解析填 RawData | 拿 `Box.gltf` 跑 Load，dump submesh / material 数量 |
| 4 | `ModelAssetCompiler` stage 1-2（属性 + 索引提取，不优化） | 拿到正确顶点数组 |
| 5 | `ModelAssetBuilder` + 接入 SparkAssetManager | `LoadAsset<ModelAsset>` 走通到 Ready |
| 6 | meshoptimizer 流水线接入（5.4） | 顶点去重后顶点数下降 |
| 7 | MikkTSpace 切线生成（5.3） | 切线和参考实现匹配（验证用一个带 normal map 的 sample） |
| 8 | 写 `DrawModel` sample，把 Box 渲染出来（贴图先用纯白） | 屏幕上看到立方体 |
| 9 | 外部贴图路径：material 走 `RequestAsset` | `DamagedHelmet.gltf` (sidecar 版) 走通 |
| 10 | **内嵌贴图路径**：`EmitEmbeddedImage` 走子资产 | `DamagedHelmet.glb`（单文件 ）走通 |
| 11 | （后置）ImageAssetDescriptor 加 `usage` + normal-map 编码模式 | 法线贴图正确显示 |
| 12 | （后置）量化 / LOD / meshlet 接入点实装 | 视需求 |

---

## 10. 已就绪 vs 仍欠缺

### ✅ 已就绪（Asset 基础设施层，不必再动）

| 设施 | 用途 |
|---|---|
| `AssetId::OfSub<T>(parent, subLabel)` | 子资产 ID 命名（UE-style `:subobject`） |
| `AssetBuildContext::db` | Builder 注册子资产入口 |
| `AssetBuildContext::MakeChild()` | 派生子 ctx（自动继承 searchPaths + db + parentId） |
| `AssetBuildBus::EventResult(out, Image, &CreateAsset, id)` | 跨类型构造 Asset 对象 |
| `AssetBuildBus::Event(Image, &Compile, childCtx)` | 跨类型派发 Compile |
| `Asset::SetDataReady(data)` | 落地子资产（release/acquire 同步） |
| `AssetDataBase::InsertOrGet(id, asset)` | 幂等注册（处理竞争） |

### ⚠️ 需要前置改造（步骤 0）

- `ImageAssetLoader::DecodeFromMemory`：抽出 stb decode 的内存路径，给 ModelAssetBuilder 调

### ⚠️ 后置项（步骤 11，先不挡 v1 主线）

- `ImageAssetDescriptor.usage` 字段 + `ImageAssetCompiler` 的 normal-map 编码分支（BC5 XY）

---

## 11. 设计上故意排除的范围（v1 不做）

| 项 | 推迟理由 |
|---|---|
| 骨骼 + 动画（joints/weights、AnimationClip） | 独立资产类，工程量大；先静态网格走通 |
| Mesh / Material 升级成独立子资产 | 方案 A 已留好切割线（结构本就分开），需要时改即可 |
| 离线缓存（.smodel 序列化） | 跟 image cache 一起做 |
| Meshlet 渲染路径 | 需要 mesh shader RHI 支持 |
| KTX2 内嵌贴图 | 需要在 Image pipeline 加 libktx decode 路径 |
| 多 UV 通道 / 顶点颜色 | 视需求 |
| Hot reload | 通用机制，未来跨资产统一做 |

---

## 12. 关键文件清单（最终落地后新增 / 修改）

**新增**：
- `Engine/Code/RunTime/Resource/Model/ModelAsset.h` + `.cpp`
- `Engine/Code/RunTime/Resource/Model/ModelAssetLoader.h` + `.cpp`
- `Engine/Code/RunTime/Resource/Model/ModelAssetCompiler.h` + `.cpp`
- `Engine/Code/RunTime/Resource/Model/ModelAssetBuilder.h` + `.cpp`
- `SandBox/Program/RHI/DrawModel.cpp`（验证 sample）
- `SandBox/Asset/Model/Box.gltf` 等测试资产

**修改**：
- `Engine/Code/RunTime/Resource/AssetTypes.h`：`AssetType::Model` 入枚举
- `Engine/Code/RunTime/Resource/AssetManager.h/.cpp`：拉起 ModelAssetBuilder
- `Engine/Code/RunTime/Resource/CMakeLists.txt`：链接 fastgltf / meshoptimizer / mikktspace
- `Engine/Code/RunTime/Resource/Image/ImageAssetLoader.h/.cpp`：加 `DecodeFromMemory`（步骤 0）
- （后置）`Engine/Code/RunTime/Resource/Image/ImageAsset.h`：descriptor 加 `usage`
- （后置）`Engine/Code/RunTime/Resource/Image/ImageAssetCompiler.cpp`：normal-map BC5 编码分支
