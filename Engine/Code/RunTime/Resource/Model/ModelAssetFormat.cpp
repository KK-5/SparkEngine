#include "ModelAssetFormat.h"

#include <cstring>
#include <initializer_list>
#include <string>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <Log/ILogSystem.h>
#include <Serialization/Json.h>

#include <Resource/AssetJsonSerializer.h>

#include "ModelAsset.h"

namespace Spark::Resource
{
    namespace
    {
        constexpr uint32_t kMagic = 0x4C444D53;   // "SMDL"

        class Writer
        {
        public:
            template <typename T>
            void Pod(const T& value)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                Bytes(&value, sizeof(T));
            }

            void Bytes(const void* data, size_t size)
            {
                const auto* p = static_cast<const uint8_t*>(data);
                m_out.insert(m_out.end(), p, p + size);
            }

            void Blob(const eastl::vector<uint8_t>& bytes)
            {
                Pod(static_cast<uint64_t>(bytes.size()));
                Bytes(bytes.data(), bytes.size());
            }

            void String(eastl::string_view text)
            {
                Pod(static_cast<uint32_t>(text.size()));
                Bytes(text.data(), text.size());
            }

            //! By its JSON text, the one spelling of an id that survives a restart. An unset
            //! id is an empty string, keeping the slot so index alignment holds.
            bool Id(const AssetId& id)
            {
                if (!id.IsValid())
                {
                    String({});
                    return true;
                }

                JsonValue json;
                if (!AssetIdToJson(id, json))
                {
                    return false;
                }
                const std::string text = json.dump();
                String(eastl::string_view(text.data(), text.size()));
                return true;
            }

            eastl::vector<uint8_t> Take() { return eastl::move(m_out); }

        private:
            eastl::vector<uint8_t> m_out;
        };

        class Reader
        {
        public:
            Reader(const uint8_t* bytes, size_t size) : m_cur(bytes), m_end(bytes + size) {}

            template <typename T>
            bool Pod(T& value)
            {
                return Bytes(&value, sizeof(T));
            }

            bool Bytes(void* out, size_t size)
            {
                if (static_cast<size_t>(m_end - m_cur) < size)
                {
                    return false;
                }
                memcpy(out, m_cur, size);
                m_cur += size;
                return true;
            }

            bool Blob(eastl::vector<uint8_t>& out)
            {
                uint64_t size = 0;
                if (!Pod(size) || static_cast<uint64_t>(m_end - m_cur) < size)
                {
                    return false;
                }
                out.assign(m_cur, m_cur + size);
                m_cur += size;
                return true;
            }

            bool String(eastl::string& out)
            {
                uint32_t size = 0;
                if (!Pod(size) || static_cast<size_t>(m_end - m_cur) < size)
                {
                    return false;
                }
                out.assign(reinterpret_cast<const char*>(m_cur), size);
                m_cur += size;
                return true;
            }

            bool Id(AssetId& out)
            {
                eastl::string text;
                if (!String(text))
                {
                    return false;
                }
                if (text.empty())
                {
                    out = AssetId{};
                    return true;
                }

                const JsonValue json = JsonValue::parse(text.begin(), text.end(), nullptr, false);
                if (json.is_discarded())
                {
                    return false;
                }
                out = AssetIdFromJson(json);
                return out.IsValid();
            }

            //! Guards a count before it sizes a vector: every element takes at least a byte.
            bool Count(uint32_t& out)
            {
                return Pod(out) && out <= static_cast<size_t>(m_end - m_cur);
            }

            bool AtEnd() const { return m_cur == m_end; }

        private:
            const uint8_t* m_cur;
            const uint8_t* m_end;
        };

        void WriteBounds(Writer& w, const Math::AABB& bounds)
        {
            w.Bytes(&bounds.min[0], sizeof(float) * 3);
            w.Bytes(&bounds.max[0], sizeof(float) * 3);
        }

        bool ReadBounds(Reader& r, Math::AABB& bounds)
        {
            return r.Bytes(&bounds.min[0], sizeof(float) * 3)
                && r.Bytes(&bounds.max[0], sizeof(float) * 3);
        }

        void WritePrimitive(Writer& w, const Primitive& prim)
        {
            w.Blob(prim.vertexBuffer);
            w.Blob(prim.indexBuffer);
            w.Pod(prim.indexCount);
            w.Pod(prim.indexFormat);
            w.Pod(prim.materialIndex);
            WriteBounds(w, prim.bounds);

            w.Pod(prim.layout.stride);
            w.Pod(static_cast<uint32_t>(prim.layout.attributes.size()));
            for (const VertexAttribute& attr : prim.layout.attributes)
            {
                w.String(eastl::string_view(attr.semantic.c_str(), attr.semantic.size()));
                w.Pod(attr.semanticIndex);
                w.Pod(attr.format);
                w.Pod(attr.byteOffset);
            }
        }

        bool ReadPrimitive(Reader& r, Primitive& prim)
        {
            if (!r.Blob(prim.vertexBuffer) || !r.Blob(prim.indexBuffer)
                || !r.Pod(prim.indexCount) || !r.Pod(prim.indexFormat)
                || !r.Pod(prim.materialIndex) || !ReadBounds(r, prim.bounds)
                || !r.Pod(prim.layout.stride))
            {
                return false;
            }

            uint32_t count = 0;
            if (!r.Count(count))
            {
                return false;
            }
            prim.layout.attributes.resize(count);
            for (VertexAttribute& attr : prim.layout.attributes)
            {
                if (!r.String(attr.semantic) || !r.Pod(attr.semanticIndex)
                    || !r.Pod(attr.format) || !r.Pod(attr.byteOffset))
                {
                    return false;
                }
            }
            return true;
        }

        bool ReadIds(Reader& r, eastl::vector<AssetId>& ids)
        {
            uint32_t count = 0;
            if (!r.Count(count))
            {
                return false;
            }
            ids.resize(count);
            for (AssetId& id : ids)
            {
                if (!r.Id(id))
                {
                    return false;
                }
            }
            return true;
        }
    }

    eastl::vector<uint8_t> ModelAssetFormat::Write(const ModelAssetData& data,
                                                   eastl::string_view identity)
    {
        Writer w;
        w.Pod(kMagic);
        w.String(identity);
        w.String(eastl::string_view(data.m_resolvedPath.c_str(), data.m_resolvedPath.size()));
        WriteBounds(w, data.m_bounds);

        w.Pod(static_cast<uint32_t>(data.m_meshes.size()));
        for (const Mesh& mesh : data.m_meshes)
        {
            w.String(eastl::string_view(mesh.name.c_str(), mesh.name.size()));
            w.Pod(static_cast<uint32_t>(mesh.primitives.size()));
            for (const Primitive& prim : mesh.primitives)
            {
                WritePrimitive(w, prim);
            }
        }

        w.Pod(static_cast<uint32_t>(data.m_nodes.size()));
        for (const Node& node : data.m_nodes)
        {
            w.Pod(node.parent);
            w.Bytes(&node.localTransform[0][0], sizeof(float) * 16);
            w.Pod(node.meshIndex);
            w.String(eastl::string_view(node.name.c_str(), node.name.size()));
        }

        for (const eastl::vector<AssetId>* ids : {&data.m_imageAssetIds, &data.m_materialAssetIds})
        {
            w.Pod(static_cast<uint32_t>(ids->size()));
            for (const AssetId& id : *ids)
            {
                if (!w.Id(id))
                {
                    LOG_ERROR("[ModelAssetFormat] '{}' references an id that cannot be written.",
                        data.m_resolvedPath.c_str());
                    return {};
                }
            }
        }

        return w.Take();
    }

    UniquePtr<AssetData> ModelAssetFormat::Read(const uint8_t* bytes, size_t size,
                                                eastl::string_view identity)
    {
        Reader r(bytes, size);

        uint32_t magic = 0;
        eastl::string storedIdentity;
        if (!r.Pod(magic) || magic != kMagic || !r.String(storedIdentity))
        {
            LOG_WARN("[ModelAssetFormat] Not a model cache entry.");
            return nullptr;
        }
        if (eastl::string_view(storedIdentity.c_str(), storedIdentity.size()) != identity)
        {
            LOG_WARN("[ModelAssetFormat] Cache entry belongs to another asset: {}",
                storedIdentity.c_str());
            return nullptr;
        }

        auto data = MakeUnique<ModelAssetData>();
        bool ok = r.String(data->m_resolvedPath) && ReadBounds(r, data->m_bounds);

        uint32_t meshCount = 0;
        ok = ok && r.Count(meshCount);
        if (ok)
        {
            data->m_meshes.resize(meshCount);
            for (Mesh& mesh : data->m_meshes)
            {
                uint32_t primCount = 0;
                ok = r.String(mesh.name) && r.Count(primCount);
                if (!ok)
                {
                    break;
                }
                mesh.primitives.resize(primCount);
                for (Primitive& prim : mesh.primitives)
                {
                    ok = ReadPrimitive(r, prim);
                    if (!ok)
                    {
                        break;
                    }
                }
                if (!ok)
                {
                    break;
                }
            }
        }

        uint32_t nodeCount = 0;
        ok = ok && r.Count(nodeCount);
        if (ok)
        {
            data->m_nodes.resize(nodeCount);
            for (Node& node : data->m_nodes)
            {
                ok = r.Pod(node.parent)
                    && r.Bytes(&node.localTransform[0][0], sizeof(float) * 16)
                    && r.Pod(node.meshIndex)
                    && r.String(node.name);
                if (!ok)
                {
                    break;
                }
            }
        }

        ok = ok && ReadIds(r, data->m_imageAssetIds) && ReadIds(r, data->m_materialAssetIds)
                && r.AtEnd();
        if (!ok)
        {
            LOG_WARN("[ModelAssetFormat] Malformed model cache entry.");
            return nullptr;
        }

        return data;
    }
}
