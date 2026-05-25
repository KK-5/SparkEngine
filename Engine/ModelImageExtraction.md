# Step 5: 模型内嵌/外部纹理提取

## 背景

模型资产管线已完成几何数据的加载和优化，但 glTF 中的图片（内嵌在 GLB 中的字节，或外部 URI 引用）未被处理。Step 5 在 Loader 阶段记录图片数据指针，在 Builder 阶段派发 Image 子资产走完整的 Image Load → Compile → KTX2 管线。材质数据不在 v1 范围。

## 核心约束

1. **零拷贝**：不拷贝图片数据，`RawImageEntry` 用指针+大小引用源数据
2. **`ModelAssetData` 无模型格式依赖**：不出现 glTF/fastgltf 类型
3. **`ModelAssetLoader.h` 无 glTF 类型暴露**：通过前向声明 + PIMPL 隐藏

## 数据流

```
Loader::Load(id)
  ├─ GltfDataBuffer::FromPath() 读取文件
  ├─ parser.loadGltf() 解析
  ├─ 提取 meshes/nodes（已有）
  ├─ 新增: 遍历 gltf.images[] → RawImageEntry{data, size} 或 RawImageEntry{uri}
  │      指针指向 buf/gltf 内部数据
  ├─ 新增: 将 buf + gltf move 进 m_session（Loader 成员，保持数据存活）
  └─ return ModelAssetData（含 RawImageEntry 数组）
         │
         ▼
Builder::Compile(ctx)
  ├─ ctx.compiledData = m_compiler.Compile(ctx.id, *ctx.rawData)  ← 签名不变
  └─ 新增: 遍历 rawData.m_rawImages →
        内嵌(data非空): AssetBuildBus 派发 Image Load(内存解码) + Compile
        外部(uri非空):  AssetBuildBus 派发 Image Load(磁盘搜索) + Compile
        注册到 ctx.db
```

**关键点**：
- 源数据（`GltfDataBuffer` + `Asset`）保存在 `ModelAssetLoader::m_session` 中
- `ModelAssetLoader` 是 `ModelAssetBuilder` 的成员，Builder 存活期间 Loader 一直存在
- `Load()` → `Compile()` 在同一线程同步调用，m_session 在 Compile 期间有效
- `RawImageEntry::data` 指向 m_session 内的数据，零拷贝
- `ModelAssetData` 中只有纯数据 `RawImageEntry`，零格式依赖

## 文件改动

| 文件 | 改动 |
|------|------|
| `ModelAsset.h` | 新增 `RawImageEntry` 结构体；新增 `m_rawImages` 成员；新增访问器；新增 `friend class ModelAssetBuilder` |
| `ModelAssetLoader.h` | 新增前向声明 `struct LoadSession`；新增 `std::unique_ptr<LoadSession> m_session` 成员 |
| `ModelAssetLoader.cpp` | 定义 `LoadSession`（含 `GltfDataBuffer` + `Asset`）；`Load()` 中 move 源数据到 m_session；`LoadFromBuffer()` 中遍历 images 填充指针 |
| `ModelAssetBuilder.cpp` | `Compile` 内在 compiler.Compile() 之后遍历 rawImages，派发 Image 子资产 |
| `ModelAssetCompiler.h` | 不变 |
| `ModelAssetCompiler.cpp` | 不变 |

## 新增/修改类型

### ModelAsset.h — 新增（零格式依赖）

```cpp
struct RawImageEntry
{
    const uint8_t* data = nullptr;  // 内嵌图片指针（非拥有），指向 Loader 内部源数据
    size_t         size = 0;        // 字节数
    eastl::string  externalUri;     // 外部图片路径（相对 glTF 目录）
};
// data 非空 = 内嵌图片；externalUri 非空 = 外部引用
```

### ModelAsset.h — ModelAssetData 新增成员

```cpp
class ModelAssetData : public AssetData
{
public:
    // ... 已有访问器 ...
    size_t                GetRawImageCount() const;
    const RawImageEntry*  GetRawImage(size_t i)   const;

private:
    friend class ModelAssetLoader;
    friend class ModelAssetCompiler;
    friend class ModelAssetBuilder;  // 新增

    eastl::vector<RawImageEntry> m_rawImages;

    // ... 已有: m_meshes, m_materials, m_nodes, m_bounds, m_resolvedPath ...
};
```

### ModelAssetLoader.h — 新增成员

```cpp
class ModelAssetLoader
{
public:
    void SetSearchPaths(const eastl::vector<eastl::string>& searchPaths);
    UniquePtr<AssetData> Load(const AssetId& id);
private:
    eastl::string ResolvePath(const AssetId& id) const;
    UniquePtr<AssetData> LoadFromBuffer(class fastgltf::GltfDataBuffer& buf,
                                        eastl::string resolvedPath,
                                        const std::string& baseDir);
    eastl::vector<eastl::string> m_searchPaths;

    struct LoadSession;  // 前向声明，定义在 .cpp
    std::unique_ptr<LoadSession> m_session;  // 保持当前加载的源数据存活
};
```

### ModelAssetLoader.cpp — LoadSession 定义

```cpp
struct ModelAssetLoader::LoadSession
{
    fastgltf::GltfDataBuffer gltfBuffer;
    fastgltf::Asset          gltfAsset;
};
```

## Loader 改动

### Load() — 将源数据移入 m_session

```cpp
UniquePtr<AssetData> ModelAssetLoader::Load(const AssetId& id)
{
    eastl::string resolved = ResolvePath(id);
    if (resolved.empty()) { LOG_ERROR(...); return nullptr; }

    std::string baseDir = std::filesystem::path(resolved.c_str()).parent_path().string();

    auto bufResult = fastgltf::GltfDataBuffer::FromPath(resolved.c_str());
    if (bufResult.error() != fastgltf::Error::None) { LOG_ERROR(...); return nullptr; }

    auto buf = eastl::move(bufResult.get());
    auto result = LoadFromBuffer(buf, eastl::move(resolved), baseDir);

    // 保持源数据存活：GltfDataBuffer 移入 m_session
    // RawImageEntry 的指针指向这些数据
    m_session = std::make_unique<LoadSession>();
    m_session->gltfBuffer = eastl::move(buf);

    return result;
}
```

### LoadFromBuffer() — 末尾新增图片提取 + 保存 Asset

```cpp
// ---- Images ----
result->m_rawImages.reserve(gltf.images.size());
for (const auto& image : gltf.images)
{
    RawImageEntry entry;
    std::visit(fastgltf::visitor{
        [&](const fastgltf::sources::Array& array) {
            entry.data = reinterpret_cast<const uint8_t*>(array.bytes.data());
            entry.size = array.bytes.size_bytes();
        },
        [&](const fastgltf::sources::Vector& vec) {
            entry.data = reinterpret_cast<const uint8_t*>(vec.bytes.data());
            entry.size = vec.bytes.size();
        },
        [&](const fastgltf::sources::ByteView& bv) {
            entry.data = reinterpret_cast<const uint8_t*>(bv.bytes.data());
            entry.size = bv.bytes.size();
        },
        [&](const fastgltf::sources::BufferView& bv) {
            fastgltf::DefaultBufferDataAdapter adapter;
            auto span = adapter(gltf, bv.bufferViewIndex);
            entry.data = reinterpret_cast<const uint8_t*>(span.data());
            entry.size = span.size();
        },
        [&](const fastgltf::sources::URI& uriSrc) {
            auto pathStr = uriSrc.uri.fspath().string();
            entry.externalUri.assign(pathStr.c_str(), pathStr.size());
        },
        [](auto&) {}
    }, image.data);
    result->m_rawImages.push_back(eastl::move(entry));
}

// 保存 Asset（Array/Vector/ByteView 数据依赖它）
m_session->gltfAsset = eastl::move(gltf);

return result;
```

**指针有效性保证**：
- `sources::BufferView` → `DefaultBufferDataAdapter` 解析后指向 GLB 二进制块（在 `m_session->gltfBuffer` 内）✓
- `sources::ByteView` → fastgltf 构造时指向 `gltfBuffer` 或 `gltfAsset` 内 ✓
- `sources::Array`/`sources::Vector` → 数据由 `gltfAsset.images[]` 持有 ✓
- `buf` 和 `gltf` 的 move 不改变内部 vector 的 buffer 地址 ✓

## Builder 改动

```cpp
void ModelAssetBuilder::Compile(AssetBuildContext& ctx)
{
    ASSERT(ctx.type == AssetType::Model, "[ModelAssetBuilder] ctx.type mismatch");
    if (!ctx.rawData) { return; }

    // Step 1: 几何优化（现有逻辑，签名不变）
    ctx.compiledData = m_compiler.Compile(ctx.id, *ctx.rawData);

    // Step 2: 图片子资产派发（新增）
    auto& raw = static_cast<ModelAssetData&>(*ctx.rawData);
    const size_t imageCount = raw.GetRawImageCount();

    for (size_t i = 0; i < imageCount; ++i)
    {
        const auto* entry = raw.GetRawImage(i);
        if (!entry) { continue; }

        AssetId subId;
        const uint8_t* srcData = nullptr;
        size_t srcSize = 0;

        if (entry->data != nullptr && entry->size > 0)
        {
            char label[64];
            snprintf(label, sizeof(label), "img:%zu", i);
            subId = AssetId::OfSub<ImageAsset>(
                eastl::string_view(ctx.id.GetPath().c_str()),
                eastl::string_view(label));
            srcData = entry->data;
            srcSize = entry->size;
        }
        else if (!entry->externalUri.empty())
        {
            std::filesystem::path modelPath(ctx.id.GetPath().c_str());
            std::filesystem::path combined = modelPath.parent_path()
                / entry->externalUri.c_str();
            eastl::string texPath(combined.string().c_str(), combined.string().size());
            subId = AssetId::Of<ImageAsset>(
                eastl::string_view(texPath.c_str(), texPath.size()));
        }
        else { continue; }

        AssetBuildContext child = ctx.MakeChild(subId, AssetType::Image);
        child.sourceData = srcData;
        child.sourceSize = srcSize;

        AssetBuildBus::Event(AssetType::Image, &AssetBuildEvents::Load, child);
        if (!child.rawData) { LOG_WARN(...); continue; }

        AssetBuildBus::Event(AssetType::Image, &AssetBuildEvents::Compile, child);
        if (!child.compiledData) { LOG_WARN(...); continue; }

        Ptr<Asset> imgAsset;
        AssetBuildBus::EventResult(imgAsset, AssetType::Image,
                                   &AssetBuildEvents::CreateAsset, subId);
        if (!imgAsset) { continue; }
        imgAsset->SetDataReady(eastl::move(child.compiledData));
        ctx.db->InsertOrGet(subId, imgAsset);
    }
}
```

### Builder 需要的新 include

```cpp
#include <Resource/Image/ImageAsset.h>    // AssetId::OfSub<ImageAsset>
#include <filesystem>                     // 路径拼接
```

## ModelAsset.h 改动总结

1. 新增 `RawImageEntry` 结构体（`const uint8_t* data` + `size_t size` + `eastl::string externalUri`）
2. 新增私有成员 `eastl::vector<RawImageEntry> m_rawImages`
3. 新增 `friend class ModelAssetBuilder;`
4. 新增公开访问器：`GetRawImageCount()`、`GetRawImage()`

## 不修改的文件

- `ModelAssetCompiler.h` / `ModelAssetCompiler.cpp` — 签名不变，逻辑不变

## 验证

1. 编译: `cmake --build build --config Debug --target SparkAssetManager`
2. 测试: `./bin/Debug/SparkAssetTest.exe --gtest_filter="ModelAsset*"`
3. 全量回归: `./bin/Debug/SparkAssetTest.exe`
