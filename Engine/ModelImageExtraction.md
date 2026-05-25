# Step 5: 模型内嵌/外部纹理提取

## 背景

模型资产管线已完成几何数据的加载和优化，但 glTF 中的图片（内嵌在 GLB 中的字节，或外部 URI 引用）未被处理。Step 5 在 Loader 阶段把所有图片的源字节**拷贝**到 `RawImageEntry` 中，在 Builder 阶段通过 `AssetBuildBus` 派发 Image 子资产，复用现有的 Image Load → Compile → KTX2 管线。材质数据不在 v1 范围。

## 核心约束

1. **`ModelAssetData` 无模型格式依赖**：不出现 fastgltf 类型，也不持有任何来源对象
2. **Loader 无状态**：每次 `Load` 自包含，源数据在函数返回前全部拷贝完成
3. **Builder 不依赖 `SparkAssetManager`**：子资产派发通过已有的 `AssetBuildBus` + `ctx.db` 横向完成

> **关于"为什么不零拷贝"**：理论上可以让 `RawImageEntry` 指向 `fastgltf::Asset` 内部字节，但这要求字节持有者在跨 `Load → Compile` 调用期间一直存活，落到哪里都会引入隐含约定或污染通用类型；而下游每张图都要走 stbi 解码 + 全 mip + BC + KTX，那条 memcpy 在总成本里远小于 1%，不值得换。
>
> **关于"为什么不绕 SparkAssetManager"**：`SparkAssetManager` 和各个 `Builder` 都是 `AssetBuildBus` 的对等消费者；Bus 本就是为"任一消费者触发另一消费者"设计的。Builder 横走 Bus 派发子资产是 Bus 的正常用法，不需要、也不应该反向依赖 Manager。`ProcessAsset` 里的编排序列只有约 10 行直白代码，与其用一个新概念把它抽到 Manager 上、再让 Builder 反向调用，不如就地写一遍 —— 编排协议固化在 `AssetBuildBus`/`AssetBus`/`AssetStatus` 这几个公开接口上，"漂移"风险由接口本身托住。

## 数据流

```
Loader::Load(id)
  ├─ GltfDataBuffer::FromPath / parser.loadGltf 解析
  ├─ 提取 meshes / nodes（已有）
  ├─ 新增: 遍历 gltf.images[]:
  │       内嵌 → 把字节 copy 到 RawImageEntry::data (eastl::vector<uint8_t>)
  │       外部 → 把 URI 字符串 copy 到 RawImageEntry::externalUri
  └─ return ModelAssetData                   ← gltf/buf 在此函数 return 时析构
                                                  RawImageEntry 已自带数据，不受影响
         │
         ▼
Builder::Compile(ctx)
  ├─ ctx.compiledData = m_compiler.Compile(ctx.id, *ctx.rawData)  ← 签名不变
  ├─ 新增: 遍历 rawData.m_rawImages →
  │        每张图调内部 DispatchImageSubAsset(ctx, ...)：
  │            Find dedup → CreateAsset(Bus) → InsertOrGet(db) →
  │            SetStatus(Loading) → Load(Bus) → SetStatus(Compiling) →
  │            Compile(Bus) → SetDataReady → OnAssetReady(Bus)
  └─ 新增: raw.m_rawImages.clear() 立即释放内嵌图字节
```

## 文件改动

| 文件 | 改动 |
|------|------|
| `ModelAsset.h` | 新增 `RawImageEntry` 结构体；`ModelAssetData` 新增 `m_rawImages` 成员与公开访问器；新增 `friend class ModelAssetBuilder` |
| `ModelAssetLoader.h` | 不动 |
| `ModelAssetLoader.cpp` | `LoadFromBuffer` 末尾新增图片提取循环（按 source 类型 copy 字节或 URI） |
| `ModelAssetBuilder.h` | 不动 |
| `ModelAssetBuilder.cpp` | `Compile` 末尾派发 Image 子资产；新增 `DispatchImageSubAsset` 自由函数（匿名命名空间） |
| `ModelAssetCompiler.*` | 不动 |
| `AssetBuildContext.*` | 不动 |
| `AssetManager.*` | 不动 |

## 新增类型

### `ModelAsset.h` — 新增 `RawImageEntry`

```cpp
namespace Spark::Resource
{
    struct RawImageEntry
    {
        eastl::vector<uint8_t> data;          // 内嵌图片字节（已 copy 自 GLB）；空 = 外部
        eastl::string          externalUri;   // 外部图片相对路径（相对 glTF 所在目录）；空 = 内嵌
        eastl::string          name;          // glTF image.name，用作子资产 subLabel；为空时回落到索引
    };
    // 不变量：data 与 externalUri 至少一个非空
}
```

### `ModelAsset.h` — `ModelAssetData` 新增成员与访问器

```cpp
class ModelAssetData : public AssetData
{
public:
    // ... 已有访问器 ...
    size_t                GetRawImageCount() const { return m_rawImages.size(); }
    const RawImageEntry*  GetRawImage(size_t i) const
    {
        return i < m_rawImages.size() ? &m_rawImages[i] : nullptr;
    }

private:
    friend class ModelAssetLoader;
    friend class ModelAssetCompiler;
    friend class ModelAssetBuilder;        // 派发完允许 clear

    eastl::vector<RawImageEntry> m_rawImages;
    // ... 已有: m_meshes, m_materials, m_nodes, m_bounds, m_resolvedPath ...
};
```

> **注意**：`Compiler` 不把 `m_rawImages` 拷到编译输出，所以运行时 `asset->GetData<ModelAssetData>()->GetRawImageCount() == 0`。该访问器主要服务于 Builder 内部派发以及（如有需要）单测断言；运行时不应依赖。

## Loader 改动

### `LoadFromBuffer` 末尾新增图片提取循环

```cpp
// ---- Images ----
result->m_rawImages.reserve(gltf.images.size());
for (const auto& image : gltf.images)
{
    RawImageEntry entry;
    entry.name = image.name.empty()
        ? eastl::string()
        : eastl::string(image.name.c_str());

    std::visit(fastgltf::visitor{
        [&](const fastgltf::sources::Array& src) {
            const auto* p = reinterpret_cast<const uint8_t*>(src.bytes.data());
            entry.data.assign(p, p + src.bytes.size_bytes());
        },
        [&](const fastgltf::sources::Vector& src) {
            const auto* p = reinterpret_cast<const uint8_t*>(src.bytes.data());
            entry.data.assign(p, p + src.bytes.size());
        },
        [&](const fastgltf::sources::ByteView& src) {
            const auto* p = reinterpret_cast<const uint8_t*>(src.bytes.data());
            entry.data.assign(p, p + src.bytes.size());
        },
        [&](const fastgltf::sources::BufferView& src) {
            fastgltf::DefaultBufferDataAdapter adapter;
            auto span = adapter(gltf, src.bufferViewIndex);
            const auto* p = reinterpret_cast<const uint8_t*>(span.data());
            entry.data.assign(p, p + span.size());
        },
        [&](const fastgltf::sources::URI& src) {
            auto uri = src.uri.fspath().string();
            entry.externalUri.assign(uri.c_str(), uri.size());
        },
        [&](const auto&) {
            LOG_WARN("[ModelAssetLoader] image '{}' has unsupported source variant; skipping",
                entry.name.empty() ? "<unnamed>" : entry.name.c_str());
        }
    }, image.data);

    if (entry.data.empty() && entry.externalUri.empty())
    {
        continue;  // 跳过解析失败 / 不支持的图
    }
    result->m_rawImages.push_back(eastl::move(entry));
}
```

Loader 头/类签名零改动；`buf` 和 `gltf` 在 `Load` / `LoadFromBuffer` return 时自然析构，`RawImageEntry::data` 已经是独立拷贝，安全。

## Builder 改动

### `ModelAssetBuilder.cpp` — 新增子资产派发自由函数

放在匿名命名空间，依赖范围严格限制在 `AssetBuildBus`/`AssetBus`/`ctx.db`/`ImageAsset`，**完全不 include `AssetManager.h`**：

```cpp
#include <filesystem>
#include <Log/SpdLogSystem.h>
#include <Resource/AssetBuildContext.h>
#include <Resource/AssetDataBase.h>
#include <Resource/EBus/AssetBuildBus.h>
#include <Resource/EBus/AssetBus.h>
#include <Resource/Image/ImageAsset.h>

namespace {

eastl::string MakeImageSubLabel(const eastl::string& name, size_t index)
{
    if (!name.empty())
    {
        return eastl::string("image/") + name;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "image/%zu", index);
    return eastl::string(buf);
}

void DispatchImageSubAsset(AssetBuildContext& parentCtx,
                           AssetId subId,
                           const uint8_t* sourceData,
                           size_t sourceSize,
                           eastl::vector<eastl::string> extraSearchPaths)
{
    ASSERT(parentCtx.db != nullptr,
        "[ModelAssetBuilder] parent ctx.db not set; cannot dispatch sub-asset");

    // 1. dedup —— 外部图被多个模型/调用方共享时直接命中现有
    if (parentCtx.db->Find(subId))
    {
        return;
    }

    // 2. create + 抢注（竞争失败说明别人先派发了，直接返回）
    Ptr<Asset> created;
    AssetBuildBus::EventResult(created, AssetType::Image,
                               &AssetBuildEvents::CreateAsset, subId);
    if (!created)
    {
        LOG_WARN("[ModelAssetBuilder] CreateAsset failed for image '{}'",
            subId.GetPath().c_str());
        return;
    }
    Ptr<Asset> stored = parentCtx.db->InsertOrGet(subId, created);
    if (stored.get() != created.get())
    {
        return;
    }

    // 3. child ctx
    AssetBuildContext child = parentCtx.MakeChild(subId, AssetType::Image);
    child.sourceData = sourceData;
    child.sourceSize = sourceSize;
    for (auto& p : extraSearchPaths)
    {
        child.searchPaths.insert(child.searchPaths.begin(), eastl::move(p));
    }

    // 4. Load
    stored->SetStatus(AssetStatus::Loading);
    AssetBuildBus::Event(AssetType::Image, &AssetBuildEvents::Load, child);
    if (!child.rawData)
    {
        stored->SetStatus(AssetStatus::Error);
        AssetBus::Event(AssetType::Image, &AssetBus::Events::OnAssetError, *stored);
        LOG_WARN("[ModelAssetBuilder] sub-asset Load failed: {}",
            subId.GetPath().c_str());
        return;
    }

    // 5. Compile
    stored->SetStatus(AssetStatus::Compiling);
    AssetBuildBus::Event(AssetType::Image, &AssetBuildEvents::Compile, child);
    if (!child.compiledData)
    {
        stored->SetStatus(AssetStatus::Error);
        AssetBus::Event(AssetType::Image, &AssetBus::Events::OnAssetError, *stored);
        LOG_WARN("[ModelAssetBuilder] sub-asset Compile failed: {}",
            subId.GetPath().c_str());
        return;
    }

    // 6. Ready —— SetDataReady 内部置 status 为 Ready/Error
    stored->SetDataReady(eastl::move(child.compiledData));
    AssetBus::Event(AssetType::Image, &AssetBus::Events::OnAssetReady, *stored);
}

}  // anonymous namespace
```

### `ModelAssetBuilder::Compile` —— 末尾派发图片

```cpp
void ModelAssetBuilder::Compile(AssetBuildContext& ctx)
{
    ASSERT(ctx.type == AssetType::Model, "[ModelAssetBuilder] ctx.type mismatch");
    if (!ctx.rawData) { return; }

    // 1. 几何编译（不变）
    ctx.compiledData = m_compiler.Compile(ctx.id, *ctx.rawData);

    // 2. 图片子资产派发
    auto& raw = static_cast<ModelAssetData&>(*ctx.rawData);

    // glTF 父目录：外部图相对路径的解析基准
    eastl::string modelDir;
    {
        auto parent = std::filesystem::path(raw.GetResolvedPath().c_str())
            .parent_path().string();
        modelDir.assign(parent.c_str(), parent.size());
    }

    for (size_t i = 0; i < raw.m_rawImages.size(); ++i)
    {
        auto& entry = raw.m_rawImages[i];
        AssetId subId;
        const uint8_t* src = nullptr;
        size_t srcSize = 0;
        eastl::vector<eastl::string> extra;

        if (!entry.data.empty())
        {
            // 内嵌：subId = (parentPath, "image/N|name")
            eastl::string subLabel = MakeImageSubLabel(entry.name, i);
            subId = AssetId::OfSub<ImageAsset>(
                eastl::string_view(ctx.id.GetPath().c_str(), ctx.id.GetPath().size()),
                eastl::string_view(subLabel.c_str(), subLabel.size()));
            src     = entry.data.data();
            srcSize = entry.data.size();
        }
        else
        {
            // 外部：subId = relative URI（保持可移植），把 modelDir 加进搜索路径前头
            subId = AssetId::Of<ImageAsset>(
                eastl::string_view(entry.externalUri.c_str(), entry.externalUri.size()));
            if (!modelDir.empty())
            {
                extra.push_back(modelDir);
            }
        }

        DispatchImageSubAsset(ctx, eastl::move(subId), src, srcSize, eastl::move(extra));
    }

    // 3. 主动清空 raw 端，立即回收内嵌图字节
    raw.m_rawImages.clear();
}
```

## 设计要点

| 关注点 | 实现 |
|--------|------|
| Builder 不知道 `SparkAssetManager` | `ModelAssetBuilder.cpp` 不 include `AssetManager.h`；派发只用 `AssetBuildBus`/`AssetBus`/`ctx.db` |
| 编排"漂移"风险 | 编排协议（`AssetBuildBus::Load/Compile/CreateAsset` + `AssetBus::OnAssetReady/Error` + `AssetStatus` 枚举）是 Bus 公开接口，任何修改都会牵动接口本身 |
| dedup | `DispatchImageSubAsset` 进入处 `ctx.db->Find` 一次，外部图被多次引用 / 热重载直接命中 |
| 状态机完整 | `SetStatus(Loading/Compiling)` + `SetDataReady`（内置 Ready/Error）+ `OnAssetReady/OnAssetError` 全齐 |
| 内嵌图 subId | `OfSub<ImageAsset>(parentPath, "image/N")` 或 `"image/<glTF image.name>"`（UE 风格，跟 CLAUDE.md 约定一致） |
| 外部图 subId | `Of<ImageAsset>(relativeUri)` —— 跟用户直接 `AssetId::Of<ImageAsset>(uri)` 加载等价，AssetId 跨机器可移植 |
| 外部图搜索路径 | 把 glTF 父目录追加到 `child.searchPaths` 前头，避免使用绝对路径 |
| 内嵌图字节生命周期 | 进 RawImageEntry 时一次 copy，进 `ImageAssetLoader::DecodeFromMemory` 借用一次，派发完 `raw.m_rawImages.clear()` 立即释放 |
| MimeType | v1 不传；`stbi` 嗅探处理 PNG/JPG/HDR。日后若要支持嵌入式 KTX，在 `RawImageEntry` 加 `mimeType` 字段并通过 child ctx 透传 |

## 验证

1. **构建**：`cmake --build build --config Debug --target SparkAssetTest`
2. **单测**：
   - `ModelAssetLoaderTest.LoadCubeGLB_HasEmbeddedImages` —— 加载带内嵌贴图的 GLB，断言 `GetRawImageCount() > 0`，每个 entry 的 `data` 非空且大小合理
   - `ModelAssetLoaderTest.LoadGLTF_HasExternalImages` —— 加载带外部贴图的 .gltf，断言 entry 的 `externalUri` 非空、`data` 为空
   - `ModelAssetBuilderTest.DispatchEmbeddedImageAsSubAsset` —— 通过 AssetManager 加载模型，用 `AssetId::OfSub<ImageAsset>(modelPath, "image/0")` 在 db 中能 `FindAsset` 到对应 ImageAsset 且 `IsReady()`
   - `ModelAssetBuilderTest.DispatchExternalImageAsSubAsset` —— 同上，外部图通过 `AssetId::Of<ImageAsset>(relativeUri)` 查到
   - `ModelAssetBuilderTest.SameExternalImageReferencedTwice_IsDedup` —— 两次加载同一外部贴图，`FindAsset` 返回的 `Ptr<Asset>` 指针一致
   - `ModelAssetBuilderTest.CompileClearsRawImages` —— Builder 跑完后 `asset->GetData<ModelAssetData>()->GetRawImageCount() == 0`
3. **全量回归**：`./bin/Debug/SparkAssetTest.exe`

## 不在 v1 范围

- 材质属性提取（baseColor/metallic/roughness/normal 等）—— 单独的 Step 6
- 材质到 ImageAsset 的引用绑定（Material 现在还是空结构）
- 嵌入式 KTX2 直通（需要 MimeType 路径 + ImageAssetLoader 支持）
- 异步子资产派发（v1 同步阻塞父 Compile，跟 Image/Shader 现有的同步语义对齐）
