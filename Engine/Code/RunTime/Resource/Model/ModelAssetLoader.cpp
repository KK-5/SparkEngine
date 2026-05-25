#include "ModelAssetLoader.h"

#include <filesystem>

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

#include <Log/SpdLogSystem.h>
#include <Math/MathUtils.h>

namespace Spark::Resource
{
    namespace
    {
        static constexpr uint32_t StrideFloat3 = 3 * sizeof(float);
        static constexpr uint32_t StrideFloat4 = 4 * sizeof(float);
        static constexpr uint32_t StrideFloat2 = 2 * sizeof(float);

        // ---- vertex attribute extraction helpers ----

        template<typename VecT>
        eastl::vector<float> ExtractFloats(const fastgltf::Asset& gltf, const fastgltf::Accessor& acc)
        {
            eastl::vector<float> out;
            const auto count = acc.count;
            constexpr int comps = [] {
                if constexpr (eastl::is_same_v<VecT, fastgltf::math::fvec2>) { return 2; }
                if constexpr (eastl::is_same_v<VecT, fastgltf::math::fvec3>) { return 3; }
                if constexpr (eastl::is_same_v<VecT, fastgltf::math::fvec4>) { return 4; }
                return 0;
            }();
            out.resize(count * comps);
            float* dst = out.data();
            fastgltf::iterateAccessor<VecT>(gltf, acc, [&](VecT v) {
                for (int c = 0; c < comps; ++c) { *dst++ = v[c]; }
            });
            return out;
        }

        eastl::vector<uint32_t> ExtractIndices(const fastgltf::Asset& gltf, const fastgltf::Accessor& acc)
        {
            eastl::vector<uint32_t> out(acc.count);
            fastgltf::copyFromAccessor<uint32_t>(gltf, acc, out.data());
            return out;
        }

        // ---- vertex interleave ----

        void InterleaveVertexBuffer(
            uint8_t* dstStart,
            size_t vertexCount, uint32_t stride,
            const eastl::vector<float>& positions, uint32_t posOffset,
            const eastl::vector<float>& normals,   uint32_t nrmOffset,
            const eastl::vector<float>& tangents,  uint32_t tanOffset,
            const eastl::vector<float>& uvs0,       uint32_t uvOffset,
            bool hasNormals, bool hasTangents, bool hasUV0)
        {
            for (size_t v = 0; v < vertexCount; ++v)
            {
                uint8_t* dst = dstStart + v * stride;

                memcpy(dst + posOffset, &positions[v * 3], StrideFloat3);

                if (hasNormals)
                {
                    memcpy(dst + nrmOffset, &normals[v * 3], StrideFloat3);
                }
                if (hasTangents)
                {
                    memcpy(dst + tanOffset, &tangents[v * 4], StrideFloat4);
                }
                if (hasUV0)
                {
                    memcpy(dst + uvOffset, &uvs0[v * 2], StrideFloat2);
                }
            }
        }

        // ---- node flattening ----

        void FlattenNodes(const fastgltf::Asset& gltf, size_t nodeIdx, int32_t parentFlat,
                          eastl::vector<Node>& out)
        {
            const auto& src = gltf.nodes[nodeIdx];

            Node dst;
            dst.name         = src.name.empty() ? eastl::string() : eastl::string(src.name.c_str());
            dst.parent       = parentFlat;
            dst.meshIndex    = src.meshIndex.has_value() ? static_cast<int32_t>(src.meshIndex.value()) : -1;

            if (const auto* mat = std::get_if<fastgltf::math::fmat4x4>(&src.transform))
            {
                // fastgltf::math::fmat4x4 is column-major, columns are vec<float,4>
                const auto& m = *mat;
                dst.localTransform = Math::Matrix4X4(
                    m[0][0], m[0][1], m[0][2], m[0][3],
                    m[1][0], m[1][1], m[1][2], m[1][3],
                    m[2][0], m[2][1], m[2][2], m[2][3],
                    m[3][0], m[3][1], m[3][2], m[3][3]
                );
            }
            else if (const auto* trs = std::get_if<fastgltf::TRS>(&src.transform))
            {
                const auto& t = trs->translation;
                const auto& r = trs->rotation;
                const auto& s = trs->scale;

                Math::Matrix4X4 mat(1.0f);
                mat = Math::Translate(mat, Math::Vector3(t[0], t[1], t[2]));
                mat = Math::Rotate(mat, Math::Quaternion(r[3], r[0], r[1], r[2]));
                mat = Math::Scale(mat, Math::Vector3(s[0], s[1], s[2]));
                dst.localTransform = mat;
            }

            const int32_t myIndex = static_cast<int32_t>(out.size());
            out.push_back(eastl::move(dst));

            for (auto child : src.children)
            {
                FlattenNodes(gltf, child, myIndex, out);
            }
        }

        // ---- bounds ----

        Math::AABB ComputePrimitiveAABB(const eastl::vector<float>& positions)
        {
            Math::AABB b;
            b.min = Math::Vector3(eastl::numeric_limits<float>::max());
            b.max = Math::Vector3(eastl::numeric_limits<float>::lowest());
            for (size_t i = 0; i < positions.size(); i += 3)
            {
                b.Expand(Math::Vector3(positions[i], positions[i + 1], positions[i + 2]));
            }
            return b;
        }
    }

    // ===== Public API =====

    void ModelAssetLoader::SetSearchPaths(const eastl::vector<eastl::string>& searchPaths)
    {
        m_searchPaths = searchPaths;
    }

    eastl::string ModelAssetLoader::ResolvePath(const AssetId& id) const
    {
        const eastl::string& assetPath = id.GetPath();
        for (const auto& sp : m_searchPaths)
        {
            std::filesystem::path full = std::filesystem::path(sp.c_str()) / assetPath.c_str();
            if (std::filesystem::exists(full))
            {
                auto str = full.string();
                return eastl::string(str.c_str(), str.size());
            }
        }
        return {};
    }

    UniquePtr<AssetData> ModelAssetLoader::Load(const AssetId& id)
    {
        eastl::string resolved = ResolvePath(id);
        if (resolved.empty())
        {
            LOG_ERROR("[ModelAssetLoader] Model file not found: {}", id.GetPath().c_str());
            return nullptr;
        }

        std::string baseDir = std::filesystem::path(resolved.c_str()).parent_path().string();

        auto bufResult = fastgltf::GltfDataBuffer::FromPath(resolved.c_str());
        if (bufResult.error() != fastgltf::Error::None)
        {
            LOG_ERROR("[ModelAssetLoader] Failed to open GLTF file '{}': {}",
                resolved.c_str(), fastgltf::getErrorMessage(bufResult.error()));
            return nullptr;
        }

        return LoadFromBuffer(bufResult.get(), eastl::move(resolved), baseDir);
    }

    UniquePtr<AssetData> ModelAssetLoader::LoadFromBuffer(
        fastgltf::GltfDataBuffer& buf,
        eastl::string resolvedPath,
        const std::string& baseDir)
    {
        fastgltf::Parser parser;
        constexpr auto options = fastgltf::Options::LoadExternalBuffers;

        auto expected = parser.loadGltf(buf, baseDir, options);
        if (expected.error() != fastgltf::Error::None)
        {
            LOG_ERROR("[ModelAssetLoader] GLTF parse error: {}",
                fastgltf::getErrorMessage(expected.error()));
            return nullptr;
        }

        fastgltf::Asset gltf = eastl::move(expected.get());

        auto result = MakeUnique<ModelAssetData>();
        result->m_resolvedPath = eastl::move(resolvedPath);

        // ---- Meshes & Primitives ----

        for (const auto& gltfMesh : gltf.meshes)
        {
            Mesh mesh;
            mesh.name = gltfMesh.name.empty()
                ? eastl::string()
                : eastl::string(gltfMesh.name.c_str());

            for (const auto& gltfPrim : gltfMesh.primitives)
            {
                auto posIt = gltfPrim.findAttribute("POSITION");
                if (posIt == gltfPrim.attributes.end())
                {
                    LOG_WARN("[ModelAssetLoader] Mesh '{}' primitive {} has no POSITION, skipping",
                        mesh.name.c_str(), mesh.primitives.size());
                    continue;
                }

                const auto& posAcc = gltf.accessors[posIt->accessorIndex];
                const size_t vertexCount = posAcc.count;
                eastl::vector<float> positions = ExtractFloats<fastgltf::math::fvec3>(gltf, posAcc);

                bool hasNormals = false, hasTangents = false, hasUV0 = false;
                eastl::vector<float> normals, tangents, uvs0;

                auto nrmIt = gltfPrim.findAttribute("NORMAL");
                if (nrmIt != gltfPrim.attributes.end())
                {
                    normals = ExtractFloats<fastgltf::math::fvec3>(gltf,
                        gltf.accessors[nrmIt->accessorIndex]);
                    hasNormals = true;
                }

                auto tanIt = gltfPrim.findAttribute("TANGENT");
                if (tanIt != gltfPrim.attributes.end())
                {
                    tangents = ExtractFloats<fastgltf::math::fvec4>(gltf,
                        gltf.accessors[tanIt->accessorIndex]);
                    hasTangents = true;
                }

                auto uvIt = gltfPrim.findAttribute("TEXCOORD_0");
                if (uvIt != gltfPrim.attributes.end())
                {
                    uvs0 = ExtractFloats<fastgltf::math::fvec2>(gltf,
                        gltf.accessors[uvIt->accessorIndex]);
                    hasUV0 = true;
                }

                // Indices
                eastl::vector<uint32_t> indices;
                if (gltfPrim.indicesAccessor.has_value())
                {
                    indices = ExtractIndices(gltf,
                        gltf.accessors[gltfPrim.indicesAccessor.value()]);
                }
                else
                {
                    indices.resize(vertexCount);
                    for (uint32_t i = 0; i < vertexCount; ++i) { indices[i] = i; }
                }

                // Build VertexLayout & interleave
                VertexLayout layout;
                VertexAttribute attr;

                attr.semantic   = "POSITION";
                attr.format     = RHI::Format::R32G32B32_FLOAT;
                attr.byteOffset = 0;
                layout.attributes.push_back(attr);
                layout.stride += StrideFloat3;

                const uint32_t nrmOffset = layout.stride;
                if (hasNormals)
                {
                    attr.semantic   = "NORMAL";
                    attr.format     = RHI::Format::R32G32B32_FLOAT;
                    attr.byteOffset = layout.stride;
                    layout.attributes.push_back(attr);
                    layout.stride += StrideFloat3;
                }

                const uint32_t tanOffset = layout.stride;
                if (hasTangents)
                {
                    attr.semantic   = "TANGENT";
                    attr.format     = RHI::Format::R32G32B32A32_FLOAT;
                    attr.byteOffset = layout.stride;
                    layout.attributes.push_back(attr);
                    layout.stride += StrideFloat4;
                }

                const uint32_t uvOffset = layout.stride;
                if (hasUV0)
                {
                    attr.semantic   = "TEXCOORD_0";
                    attr.format     = RHI::Format::R32G32_FLOAT;
                    attr.byteOffset = layout.stride;
                    layout.attributes.push_back(attr);
                    layout.stride += StrideFloat2;
                }

                Primitive prim;
                prim.layout        = layout;
                prim.vertexBuffer.resize(vertexCount * layout.stride);
                prim.indexBuffer.resize(indices.size() * sizeof(uint32_t));
                prim.materialIndex = static_cast<uint32_t>(gltfPrim.materialIndex.value_or(0));

                InterleaveVertexBuffer(
                    prim.vertexBuffer.data(), vertexCount, layout.stride,
                    positions, 0,
                    normals,   nrmOffset,
                    tangents,  tanOffset,
                    uvs0,      uvOffset,
                    hasNormals, hasTangents, hasUV0);

                memcpy(prim.indexBuffer.data(), indices.data(), indices.size() * sizeof(uint32_t));
                prim.bounds = ComputePrimitiveAABB(positions);

                mesh.primitives.push_back(eastl::move(prim));
            }

            if (!mesh.primitives.empty())
            {
                result->m_meshes.push_back(eastl::move(mesh));
            }
        }

        // ---- Nodes ----

        if (gltf.defaultScene.has_value()
            && gltf.defaultScene.value() < gltf.scenes.size())
        {
            const auto& scene = gltf.scenes[gltf.defaultScene.value()];
            for (auto rootIdx : scene.nodeIndices)
            {
                FlattenNodes(gltf, rootIdx, -1, result->m_nodes);
            }
        }

        // ---- Global bounds ----

        result->m_bounds.min = Math::Vector3(eastl::numeric_limits<float>::max());
        result->m_bounds.max = Math::Vector3(eastl::numeric_limits<float>::lowest());
        for (auto& mesh : result->m_meshes)
        {
            for (auto& prim : mesh.primitives)
            {
                result->m_bounds.Expand(prim.bounds.min);
                result->m_bounds.Expand(prim.bounds.max);
            }
        }

        LOG_INFO("[ModelAssetLoader] Loaded '{}': {} meshes, {} primitives, {} nodes",
            result->m_resolvedPath.c_str(),
            result->GetMeshCount(),
            result->GetPrimitiveCount(),
            result->GetNodeCount());

        return result;
    }
}
