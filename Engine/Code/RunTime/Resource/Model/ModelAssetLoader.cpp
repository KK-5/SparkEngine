#include "ModelAssetLoader.h"

#include <filesystem>

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/util.hpp>

#include <Log/ILogSystem.h>
#include <Math/MathUtils.h>

namespace Spark::Resource
{
    namespace
    {
        static constexpr uint32_t StrideFloat3 = 3 * sizeof(float);
        static constexpr uint32_t StrideFloat4 = 4 * sizeof(float);
        static constexpr uint32_t StrideFloat2 = 2 * sizeof(float);

        // Split a GLTF semantic like "TEXCOORD_0" into name="TEXCOORD" and index=0.
        // Names without a trailing _N (e.g. "POSITION") return index=0 and the
        // original name unchanged.
        struct ParsedSemantic
        {
            eastl::string name;
            uint32_t      index = 0;
        };
        ParsedSemantic ParseSemantic(const char* gltfAttributeName)
        {
            eastl::string_view sv(gltfAttributeName);
            const size_t len = sv.size();
            if (len > 2 && sv[len - 1] >= '0' && sv[len - 1] <= '9')
            {
                size_t pos = len - 1;
                while (pos > 0 && sv[pos - 1] >= '0' && sv[pos - 1] <= '9')
                {
                    --pos;
                }
                if (pos > 0 && sv[pos - 1] == '_')
                {
                    uint32_t idx = 0;
                    for (size_t i = pos; i < len; ++i)
                    {
                        idx = idx * 10 + static_cast<uint32_t>(sv[i] - '0');
                    }
                    return { eastl::string(sv.data(), sv.data() + (pos - 1)), idx };
                }
            }
            return { eastl::string(sv.data(), sv.size()), 0 };
        }

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

        /// meshRemap[gltfMeshIdx] = new mesh index in m_meshes, or -1 if mesh was dropped
        void FlattenNodes(const fastgltf::Asset& gltf, size_t nodeIdx, int32_t parentFlat,
                          const eastl::vector<int32_t>& meshRemap,
                          eastl::vector<Node>& out)
        {
            const auto& src = gltf.nodes[nodeIdx];

            Node dst;
            dst.name         = src.name.empty() ? eastl::string() : eastl::string(src.name.c_str());
            dst.parent       = parentFlat;
            dst.meshIndex    = -1;
            if (src.meshIndex.has_value())
            {
                const size_t gltfMeshIdx = src.meshIndex.value();
                if (gltfMeshIdx < meshRemap.size())
                {
                    dst.meshIndex = meshRemap[gltfMeshIdx];
                }
            }

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
                FlattenNodes(gltf, child, myIndex, meshRemap, out);
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

        // ---- node transform baking ----

        // Bake a world-space transform into the primitive's vertex data
        // (positions, normals, tangents) in place; recompute the AABB; and,
        // if the transform's 3x3 part has negative determinant (mirror), undo
        // the loader's earlier winding swap so screen-space winding remains CCW.
        void BakeNodeTransformIntoPrimitive(Primitive& prim, const Math::Matrix4X4& world)
        {
            if (world == Math::Matrix4X4Const::IDENTITY)
            {
                return;
            }

            const uint32_t stride      = prim.layout.stride;
            const size_t   vertexCount = stride > 0 ? prim.vertexBuffer.size() / stride : 0;
            if (vertexCount == 0)
            {
                return;
            }

            const VertexAttribute* posAttr = prim.layout.FindAttribute(VertexSemantic::Position);
            const VertexAttribute* nrmAttr = prim.layout.FindAttribute(VertexSemantic::Normal);
            const VertexAttribute* tanAttr = prim.layout.FindAttribute(VertexSemantic::Tangent);
            const int32_t posOffset = posAttr ? static_cast<int32_t>(posAttr->byteOffset) : -1;
            const int32_t nrmOffset = nrmAttr ? static_cast<int32_t>(nrmAttr->byteOffset) : -1;
            const int32_t tanOffset = tanAttr ? static_cast<int32_t>(tanAttr->byteOffset) : -1;

            const Math::Matrix3X3 rot3         = Math::ToMatrix3X3(world);
            const Math::Matrix3X3 normalMatrix = Math::Transpose(Math::Inverse(rot3));
            const float           det          = Math::Determinant(rot3);
            const bool            mirrors      = det < 0.0f;

            uint8_t* vb = prim.vertexBuffer.data();
            for (size_t v = 0; v < vertexCount; ++v)
            {
                uint8_t* vert = vb + v * stride;

                if (posOffset >= 0)
                {
                    float* p = reinterpret_cast<float*>(vert + posOffset);
                    const Math::Vector4 t = world * Math::Vector4(p[0], p[1], p[2], 1.0f);
                    p[0] = t.x; p[1] = t.y; p[2] = t.z;
                }
                if (nrmOffset >= 0)
                {
                    float* n = reinterpret_cast<float*>(vert + nrmOffset);
                    Math::Vector3 t = normalMatrix * Math::Vector3(n[0], n[1], n[2]);
                    t = Math::Normalize(t);
                    n[0] = t.x; n[1] = t.y; n[2] = t.z;
                }
                if (tanOffset >= 0)
                {
                    float* t = reinterpret_cast<float*>(vert + tanOffset);
                    Math::Vector3 r = rot3 * Math::Vector3(t[0], t[1], t[2]);
                    r = Math::Normalize(r);
                    t[0] = r.x; t[1] = r.y; t[2] = r.z;
                    if (mirrors)
                    {
                        t[3] = -t[3];  // tangent-space handedness flips under mirror
                    }
                }
            }

            // Mirrored transforms flip triangle winding in object space; combined
            // with the loader's preemptive RH→LH winding swap this would double-flip,
            // so undo the swap here.
            if (mirrors)
            {
                uint32_t* idx = reinterpret_cast<uint32_t*>(prim.indexBuffer.data());
                const size_t indexCount = prim.indexBuffer.size() / sizeof(uint32_t);
                for (size_t i = 0; i + 2 < indexCount; i += 3)
                {
                    const uint32_t tmp = idx[i + 1];
                    idx[i + 1] = idx[i + 2];
                    idx[i + 2] = tmp;
                }
            }

            // AABB is no longer the original glTF-local one; recompute from the
            // transformed positions.
            if (posOffset >= 0)
            {
                prim.bounds.min = Math::Vector3(eastl::numeric_limits<float>::max());
                prim.bounds.max = Math::Vector3(eastl::numeric_limits<float>::lowest());
                for (size_t v = 0; v < vertexCount; ++v)
                {
                    const float* p = reinterpret_cast<const float*>(vb + v * stride + posOffset);
                    prim.bounds.Expand(Math::Vector3(p[0], p[1], p[2]));
                }
            }
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

        // Track glTF mesh index -> m_meshes index (or -1 if dropped) so node.meshIndex
        // stays valid when meshes without usable primitives are filtered out.
        eastl::vector<int32_t> meshRemap(gltf.meshes.size(), -1);

        for (size_t gltfMeshIdx = 0; gltfMeshIdx < gltf.meshes.size(); ++gltfMeshIdx)
        {
            const auto& gltfMesh = gltf.meshes[gltfMeshIdx];
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

                // ---- glTF (CCW front) → engine LH screen winding ----
                // Diagnostic: only reverse triangle winding; leave positions/normals/
                // tangents untouched to see if the model's face layout matches engine
                // Y-up convention without any per-axis flip.
                for (size_t i = 0; i + 2 < indices.size(); i += 3)
                {
                    const uint32_t tmp = indices[i + 1];
                    indices[i + 1] = indices[i + 2];
                    indices[i + 2] = tmp;
                }

                // Build VertexLayout & interleave
                VertexLayout layout;
                VertexAttribute attr;

                attr.semantic      = VertexSemantic::Position;
                attr.semanticIndex = 0;
                attr.format        = RHI::Format::R32G32B32_FLOAT;
                attr.byteOffset    = 0;
                layout.attributes.push_back(attr);
                layout.stride += StrideFloat3;

                const uint32_t nrmOffset = layout.stride;
                if (hasNormals)
                {
                    attr.semantic      = VertexSemantic::Normal;
                    attr.semanticIndex = 0;
                    attr.format        = RHI::Format::R32G32B32_FLOAT;
                    attr.byteOffset    = layout.stride;
                    layout.attributes.push_back(attr);
                    layout.stride += StrideFloat3;
                }

                const uint32_t tanOffset = layout.stride;
                if (hasTangents)
                {
                    attr.semantic      = VertexSemantic::Tangent;
                    attr.semanticIndex = 0;
                    attr.format        = RHI::Format::R32G32B32A32_FLOAT;
                    attr.byteOffset    = layout.stride;
                    layout.attributes.push_back(attr);
                    layout.stride += StrideFloat4;
                }

                const uint32_t uvOffset = layout.stride;
                if (hasUV0)
                {
                    attr.semantic      = VertexSemantic::TexCoord;
                    attr.semanticIndex = 0;
                    attr.format        = RHI::Format::R32G32_FLOAT;
                    attr.byteOffset    = layout.stride;
                    layout.attributes.push_back(attr);
                    layout.stride += StrideFloat2;
                }

                Primitive prim;
                prim.layout        = layout;
                prim.vertexBuffer.resize(vertexCount * layout.stride);
                prim.indexBuffer.resize(indices.size() * sizeof(uint32_t));
                prim.materialIndex = gltfPrim.materialIndex.has_value()
                    ? static_cast<uint32_t>(gltfPrim.materialIndex.value())
                    : Primitive::kInvalidMaterialIndex;

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
                meshRemap[gltfMeshIdx] = static_cast<int32_t>(result->m_meshes.size());
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
                FlattenNodes(gltf, rootIdx, -1, meshRemap, result->m_nodes);
            }
        }

        // ---- Bake node world transforms into mesh vertex data ----
        //
        // glTF often stores geometry in an authoring tool's local frame (e.g.
        // Blender Z-up) and relies on the node tree to orient it into glTF
        // Y-up at render time. Bake those transforms so the vertex buffer is
        // already in world space relative to the model root, and feature code
        // doesn't need to walk the node tree. After baking, node localTransforms
        // are reset to identity to prevent double-application downstream.
        //
        // Limitation: if multiple nodes reference the same mesh (glTF allows
        // this as a primitive form of instancing), only the first reference's
        // transform is baked; subsequent references are warned and skipped.
        // Real instancing support requires duplicating mesh data per reference.
        {
            // FlattenNodes guarantees children appear after their parent in
            // m_nodes, so a single forward pass can compose world transforms.
            eastl::vector<Math::Matrix4X4> worldTransforms(
                result->m_nodes.size(), Math::Matrix4X4Const::IDENTITY);
            for (size_t i = 0; i < result->m_nodes.size(); ++i)
            {
                const Node& node = result->m_nodes[i];
                if (node.parent >= 0 &&
                    static_cast<size_t>(node.parent) < i)
                {
                    worldTransforms[i] =
                        worldTransforms[node.parent] * node.localTransform;
                }
                else
                {
                    worldTransforms[i] = node.localTransform;
                }
            }

            eastl::vector<bool> meshBaked(result->m_meshes.size(), false);
            for (size_t nodeIdx = 0; nodeIdx < result->m_nodes.size(); ++nodeIdx)
            {
                Node& node = result->m_nodes[nodeIdx];
                if (node.meshIndex < 0 ||
                    static_cast<size_t>(node.meshIndex) >= result->m_meshes.size())
                {
                    continue;
                }
                if (meshBaked[node.meshIndex])
                {
                    LOG_WARN("[ModelAssetLoader] mesh '{}' is referenced by multiple nodes; "
                             "only the first reference's transform is baked",
                             result->m_meshes[node.meshIndex].name.c_str());
                    continue;
                }

                Mesh& mesh = result->m_meshes[node.meshIndex];
                for (Primitive& prim : mesh.primitives)
                {
                    BakeNodeTransformIntoPrimitive(prim, worldTransforms[nodeIdx]);
                }
                meshBaked[node.meshIndex] = true;
            }

            for (Node& node : result->m_nodes)
            {
                node.localTransform = Math::Matrix4X4Const::IDENTITY;
            }
        }

        // ---- Images (copy bytes / URI; fastgltf objects die when this fn returns) ----
        //
        // 严格按 gltf.images[] 顺序 push，不可跳过：m_rawImages[i] ↔ gltf.images[i]
        // 是材质 by-index 引用的基础。不支持/解析失败的源仍 push 一个空 entry，
        // 由 Builder 决定如何在 m_imageAssetIds 上占位。

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
                    // generic_string() 强制正斜杠，保证 AssetId 跨平台稳定
                    auto uri = src.uri.fspath().generic_string();
                    entry.externalUri.assign(uri.c_str(), uri.size());
                },
                [&](const auto&) {
                    LOG_WARN("[ModelAssetLoader] image '{}' has unsupported source variant; "
                             "leaving slot empty (index alignment preserved)",
                        entry.name.empty() ? "<unnamed>" : entry.name.c_str());
                }
            }, image.data);

            result->m_rawImages.push_back(eastl::move(entry));
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

        LOG_INFO("[ModelAssetLoader] Loaded '{}': {} meshes, {} primitives, {} nodes, {} images",
            result->m_resolvedPath.c_str(),
            result->GetMeshCount(),
            result->GetPrimitiveCount(),
            result->GetNodeCount(),
            result->GetRawImageCount());

        return result;
    }
}
