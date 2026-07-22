#include "ModelAssetCompiler.h"

#include <meshoptimizer.h>
#include <mikktspace.h>

#include <Log/ILogSystem.h>
#include <Math/MathUtils.h>

#include "ModelAsset.h"

namespace Spark::Resource
{
    namespace
    {
        static constexpr uint32_t StrideFloat3 = 3 * sizeof(float);
        static constexpr uint32_t StrideFloat4 = 4 * sizeof(float);
        static constexpr uint32_t StrideFloat2 = 2 * sizeof(float);

        // ===== MikkTSpace callbacks =====

        struct MikkContext
        {
            const uint32_t* indices;
            size_t          indexCount;
            const uint8_t*  vertexData;
            uint32_t        vertexStride;
            uint32_t        positionOffset;
            uint32_t        normalOffset;
            uint32_t        uvOffset;
            float*          tangents;
        };

        int MikkGetNumFaces(const SMikkTSpaceContext* ctx)
        {
            auto* mc = static_cast<const MikkContext*>(ctx->m_pUserData);
            return static_cast<int>(mc->indexCount / 3);
        }

        int MikkGetNumVerticesOfFace(const SMikkTSpaceContext* /*ctx*/, const int /*iFace*/)
        {
            return 3;
        }

        void MikkGetPosition(const SMikkTSpaceContext* ctx, float out[], const int iFace, const int iVert)
        {
            auto* mc = static_cast<const MikkContext*>(ctx->m_pUserData);
            const uint32_t idx = mc->indices[iFace * 3 + iVert];
            const float* pos = reinterpret_cast<const float*>(
                mc->vertexData + static_cast<size_t>(idx) * mc->vertexStride + mc->positionOffset);
            out[0] = pos[0];
            out[1] = pos[1];
            out[2] = pos[2];
        }

        void MikkGetNormal(const SMikkTSpaceContext* ctx, float out[], const int iFace, const int iVert)
        {
            auto* mc = static_cast<const MikkContext*>(ctx->m_pUserData);
            const uint32_t idx = mc->indices[iFace * 3 + iVert];
            const float* nrm = reinterpret_cast<const float*>(
                mc->vertexData + static_cast<size_t>(idx) * mc->vertexStride + mc->normalOffset);
            out[0] = nrm[0];
            out[1] = nrm[1];
            out[2] = nrm[2];
        }

        void MikkGetTexCoord(const SMikkTSpaceContext* ctx, float out[], const int iFace, const int iVert)
        {
            auto* mc = static_cast<const MikkContext*>(ctx->m_pUserData);
            const uint32_t idx = mc->indices[iFace * 3 + iVert];
            const float* uv = reinterpret_cast<const float*>(
                mc->vertexData + static_cast<size_t>(idx) * mc->vertexStride + mc->uvOffset);
            out[0] = uv[0];
            out[1] = uv[1];
        }

        void MikkSetTSpaceBasic(const SMikkTSpaceContext* ctx, const float tangent[], const float sign,
                                const int iFace, const int iVert)
        {
            auto* mc = static_cast<MikkContext*>(ctx->m_pUserData);
            const size_t outIdx = static_cast<size_t>(iFace) * 3 + static_cast<size_t>(iVert);
            mc->tangents[outIdx * 4 + 0] = tangent[0];
            mc->tangents[outIdx * 4 + 1] = tangent[1];
            mc->tangents[outIdx * 4 + 2] = tangent[2];
            mc->tangents[outIdx * 4 + 3] = sign;
        }

        // ===== Tangent generation =====

        bool GenerateTangents(Primitive& prim)
        {
            const auto* posAttr = prim.layout.FindAttribute(VertexSemantic::Position);
            const auto* nrmAttr = prim.layout.FindAttribute(VertexSemantic::Normal);
            const auto* tanAttr = prim.layout.FindAttribute(VertexSemantic::Tangent);
            const auto* uvAttr  = prim.layout.FindAttribute(VertexSemantic::TexCoord);

            if (!posAttr || !nrmAttr || !uvAttr || tanAttr)
            {
                return false;
            }

            const size_t indexCount = prim.indexBuffer.size() / sizeof(uint32_t);
            if (indexCount < 3)
            {
                return false;
            }

            const uint32_t* indices = reinterpret_cast<const uint32_t*>(prim.indexBuffer.data());
            const uint8_t*  vb      = prim.vertexBuffer.data();
            const uint32_t  stride  = prim.layout.stride;

            // Run MikkTSpace — produces per-face-vertex tangents
            eastl::vector<float> tangents(indexCount * 4);

            MikkContext mc;
            mc.indices        = indices;
            mc.indexCount     = indexCount;
            mc.vertexData     = vb;
            mc.vertexStride   = stride;
            mc.positionOffset = posAttr->byteOffset;
            mc.normalOffset   = nrmAttr->byteOffset;
            mc.uvOffset       = uvAttr->byteOffset;
            mc.tangents       = tangents.data();

            SMikkTSpaceInterface iface{};
            iface.m_getNumFaces          = MikkGetNumFaces;
            iface.m_getNumVerticesOfFace = MikkGetNumVerticesOfFace;
            iface.m_getPosition          = MikkGetPosition;
            iface.m_getNormal            = MikkGetNormal;
            iface.m_getTexCoord          = MikkGetTexCoord;
            iface.m_setTSpaceBasic       = MikkSetTSpaceBasic;

            SMikkTSpaceContext ctx;
            ctx.m_pInterface = &iface;
            ctx.m_pUserData  = &mc;

            if (!genTangSpaceDefault(&ctx))
            {
                LOG_WARN("[ModelAssetCompiler] MikkTSpace tangent generation failed");
                return false;
            }

            // Expand indexed data to per-face-vertex arrays
            eastl::vector<float> expandedPos(indexCount * 3);
            eastl::vector<float> expandedNrm(indexCount * 3);
            eastl::vector<float> expandedUV(indexCount * 2);

            for (size_t i = 0; i < indexCount; ++i)
            {
                const uint32_t idx = indices[i];
                const uint8_t* src = vb + static_cast<size_t>(idx) * stride;

                memcpy(&expandedPos[i * 3], src + posAttr->byteOffset, StrideFloat3);
                memcpy(&expandedNrm[i * 3], src + nrmAttr->byteOffset, StrideFloat3);
                memcpy(&expandedUV[i * 2],  src + uvAttr->byteOffset,  StrideFloat2);
            }

            // Re-index with tangents included (meshopt deduplicates per-face-vertex data)
            meshopt_Stream streams[4];
            streams[0].data   = expandedPos.data();
            streams[0].size   = StrideFloat3;
            streams[0].stride = StrideFloat3;
            streams[1].data   = expandedNrm.data();
            streams[1].size   = StrideFloat3;
            streams[1].stride = StrideFloat3;
            streams[2].data   = tangents.data();
            streams[2].size   = StrideFloat4;
            streams[2].stride = StrideFloat4;
            streams[3].data   = expandedUV.data();
            streams[3].size   = StrideFloat2;
            streams[3].stride = StrideFloat2;

            eastl::vector<unsigned int> remap(indexCount);
            const size_t uniqueVertices = meshopt_generateVertexRemapMulti(
                remap.data(), nullptr, indexCount, indexCount, streams, 4);

            // Remap each attribute stream to unique-vertex buffers
            eastl::vector<float> newPos(uniqueVertices * 3);
            eastl::vector<float> newNrm(uniqueVertices * 3);
            eastl::vector<float> newTan(uniqueVertices * 4);
            eastl::vector<float> newUV(uniqueVertices * 2);

            meshopt_remapVertexBuffer(newPos.data(), expandedPos.data(), indexCount, StrideFloat3, remap.data());
            meshopt_remapVertexBuffer(newNrm.data(), expandedNrm.data(), indexCount, StrideFloat3, remap.data());
            meshopt_remapVertexBuffer(newTan.data(), tangents.data(),       indexCount, StrideFloat4, remap.data());
            meshopt_remapVertexBuffer(newUV.data(),  expandedUV.data(),    indexCount, StrideFloat2, remap.data());

            // Build new index buffer (un-indexed input → identity sequence remapped)
            eastl::vector<uint32_t> newIndices(indexCount);
            meshopt_remapIndexBuffer(newIndices.data(), nullptr, indexCount, remap.data());

            // Interleave into final vertex buffer
            const uint32_t newStride = StrideFloat3 + StrideFloat3 + StrideFloat4 + StrideFloat2;
            eastl::vector<uint8_t> newVB(uniqueVertices * newStride);

            for (size_t i = 0; i < uniqueVertices; ++i)
            {
                uint8_t* dst = newVB.data() + i * newStride;
                uint32_t off = 0;
                memcpy(dst + off, &newPos[i * 3], StrideFloat3); off += StrideFloat3;
                memcpy(dst + off, &newNrm[i * 3], StrideFloat3); off += StrideFloat3;
                memcpy(dst + off, &newTan[i * 4], StrideFloat4); off += StrideFloat4;
                memcpy(dst + off, &newUV[i * 2],  StrideFloat2);
            }

            // Build new VertexLayout
            VertexLayout newLayout;
            VertexAttribute attr;

            attr.semantic      = VertexSemantic::Position;
            attr.semanticIndex = 0;
            attr.format        = RHI::Format::R32G32B32_FLOAT;
            attr.byteOffset    = 0;
            newLayout.attributes.push_back(attr);
            newLayout.stride += StrideFloat3;

            attr.semantic      = VertexSemantic::Normal;
            attr.semanticIndex = 0;
            attr.format        = RHI::Format::R32G32B32_FLOAT;
            attr.byteOffset    = newLayout.stride;
            newLayout.attributes.push_back(attr);
            newLayout.stride += StrideFloat3;

            attr.semantic      = VertexSemantic::Tangent;
            attr.semanticIndex = 0;
            attr.format        = RHI::Format::R32G32B32A32_FLOAT;
            attr.byteOffset    = newLayout.stride;
            newLayout.attributes.push_back(attr);
            newLayout.stride += StrideFloat4;

            attr.semantic      = VertexSemantic::TexCoord;
            attr.semanticIndex = 0;
            attr.format        = RHI::Format::R32G32_FLOAT;
            attr.byteOffset    = newLayout.stride;
            newLayout.attributes.push_back(attr);
            newLayout.stride += StrideFloat2;

            // Commit
            prim.vertexBuffer = eastl::move(newVB);
            prim.indexBuffer.assign(
                reinterpret_cast<const uint8_t*>(newIndices.data()),
                reinterpret_cast<const uint8_t*>(newIndices.data() + newIndices.size()));
            prim.layout = newLayout;

            return true;
        }

        // ===== Mesh optimization pipeline =====

        void OptimizePrimitive(Primitive& prim)
        {
            const size_t indexCount  = prim.indexBuffer.size() / sizeof(uint32_t);
            const size_t vertexCount = prim.vertexBuffer.size() / prim.layout.stride;

            if (indexCount < 3)
            {
                return;
            }

            const uint32_t* srcIndices = reinterpret_cast<const uint32_t*>(prim.indexBuffer.data());
            const uint8_t*  srcVB      = prim.vertexBuffer.data();
            const uint32_t  stride     = prim.layout.stride;

            const auto* posAttr = prim.layout.FindAttribute(VertexSemantic::Position);
            if (!posAttr)
            {
                return;
            }
            const float* positions = reinterpret_cast<const float*>(srcVB + posAttr->byteOffset);

            // Step 1: Vertex cache optimization
            eastl::vector<uint32_t> vcIndices(indexCount);
            meshopt_optimizeVertexCache(vcIndices.data(), srcIndices, indexCount, vertexCount);

            // Step 2: Overdraw optimization (needs VC-optimized input)
            eastl::vector<uint32_t> odIndices(indexCount);
            meshopt_optimizeOverdraw(odIndices.data(), vcIndices.data(), indexCount,
                                     positions, vertexCount, stride, 1.05f);

            // Step 3: Vertex fetch optimization (modifies indices in-place, reorders vertices)
            eastl::vector<uint8_t> newVB(vertexCount * stride);
            const size_t newVertexCount = meshopt_optimizeVertexFetch(
                newVB.data(), odIndices.data(), indexCount,
                srcVB, vertexCount, stride);

            newVB.resize(newVertexCount * stride);

            prim.vertexBuffer = eastl::move(newVB);
            prim.indexBuffer.assign(
                reinterpret_cast<const uint8_t*>(odIndices.data()),
                reinterpret_cast<const uint8_t*>(odIndices.data() + odIndices.size()));
        }
    }

    // ===== Public API =====

    UniquePtr<AssetData> ModelAssetCompiler::Compile(const AssetId& /*id*/, AssetData& rawData)
    {
        auto& raw = static_cast<ModelAssetRawData&>(rawData);

        // Geometry only. Materials + image sub-assets are assembled by ModelAssetBuilder
        // (it owns image dispatch, which the material texture AssetIds depend on).
        auto result = MakeUnique<ModelAssetData>();
        result->m_resolvedPath = eastl::move(raw.m_resolvedPath);
        result->m_meshes       = eastl::move(raw.m_meshes);
        result->m_nodes        = eastl::move(raw.m_nodes);

        size_t totalPrimitives  = 0;
        size_t tangentGenerated = 0;

        for (auto& mesh : result->m_meshes)
        {
            for (auto& prim : mesh.primitives)
            {
                ++totalPrimitives;

                if (GenerateTangents(prim))
                {
                    ++tangentGenerated;
                }

                OptimizePrimitive(prim);
            }
        }

        // Recalculate global bounds
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

        LOG_INFO("[ModelAssetCompiler] Compiled '{}': {} primitives, {} tangents generated",
            result->m_resolvedPath.c_str(), totalPrimitives, tangentGenerated);

        return result;
    }
}
