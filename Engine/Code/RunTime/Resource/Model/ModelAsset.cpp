#include "ModelAsset.h"

#include <cstring>

#include <EASTL/numeric_limits.h>
#include <EASTLEX/hash.h>
#include <HashString/HashString.h>
#include <Math/MathUtils.h>

namespace Spark::Resource
{
    // === ModelAssetDescriptor ===

    AssetHash ModelAssetDescriptor::Hash() const
    {
        // Seeded with the descriptor's own type: without it a one-field hash collides with
        // any other type's one-field hash (ModelAssetType::GLTF and ShaderBackend::SPIRV are
        // both 1), which would fold two different descriptors into the same AssetId hash.
        size_t h = static_cast<size_t>(HashString("ModelAssetDescriptor").value());
        eastl::hash_combine(h, static_cast<size_t>(type));
        return static_cast<AssetHash>(h);
    }

    void Primitive::RecalculateLocalSurfaceArea()
    {
        localSurfaceArea = 0.0f;
        if (!IsStandardVertexLayout(layout))
        {
            return;
        }

        const size_t vertexCount = vertexBuffer.size() / layout.stride;
        const bool indexed = !indexBuffer.empty();
        const size_t indexStride = RHI::GetIndexFormatSize(indexFormat);
        if (indexed && (indexStride == 0 || indexCount > indexBuffer.size() / indexStride))
        {
            return;
        }

        auto vertexIndex = [&](size_t element) -> size_t
        {
            if (!indexed)
            {
                return element;
            }

            const uint8_t* src = indexBuffer.data() + element * indexStride;
            if (indexFormat == RHI::IndexFormat::UINT16)
            {
                uint16_t index;
                std::memcpy(&index, src, sizeof(index));
                return index;
            }

            uint32_t index;
            std::memcpy(&index, src, sizeof(index));
            return index;
        };

        auto position = [&](size_t index)
        {
            float xyz[3];
            std::memcpy(xyz, vertexBuffer.data() + index * layout.stride + StandardVertex::PositionOffset, sizeof(xyz));
            return Math::Vector3(xyz[0], xyz[1], xyz[2]);
        };

        const size_t triangleCount = (indexed ? indexCount : vertexCount) / 3;
        double twiceArea = 0.0;
        for (size_t triangle = 0; triangle < triangleCount; ++triangle)
        {
            const size_t i0 = vertexIndex(triangle * 3);
            const size_t i1 = vertexIndex(triangle * 3 + 1);
            const size_t i2 = vertexIndex(triangle * 3 + 2);
            if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
            {
                return;
            }

            const Math::Vector3 p0 = position(i0);
            twiceArea += Math::Length(Math::Cross(position(i1) - p0, position(i2) - p0));
        }

        const double area = 0.5 * twiceArea;
        if (area >= 0.0 && area <= eastl::numeric_limits<float>::max())
        {
            localSurfaceArea = static_cast<float>(area);
        }
    }

    // === ModelAssetData ===

    size_t ModelAssetData::GetPrimitiveCount() const
    {
        size_t count = 0;
        for (const auto& mesh : m_meshes)
        {
            count += mesh.primitives.size();
        }
        return count;
    }

    // === ModelAsset ===

    Ptr<AssetDescriptor> ModelAsset::DefaultDescriptor()
    {
        static Ptr<AssetDescriptor> instance(new ModelAssetDescriptor{});
        return instance;
    }

    ModelAsset::ModelAsset(AssetId id)
        : Asset(eastl::move(id))
    {}

    const ModelAssetData* ModelAsset::GetModelData() const
    {
        return GetData<ModelAssetData>();
    }
}
