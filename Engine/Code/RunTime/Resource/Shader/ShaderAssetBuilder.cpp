#include "ShaderAssetBuilder.h"

#include <EASTL/string_view.h>

#include <Log/ILogSystem.h>
#include <Resource/AssetBuildContext.h>

#include "ShaderAsset.h"


namespace Spark::Resource
{
    namespace
    {
        //! TEMPORARY stage "source": detect which stages a shader defines by
        //! probing for the conventional entry-point names in the source text.
        //! This unblocks non-VS+PS shaders (depth-only, compute) without a global
        //! hardcode. A real authoring mechanism (pragma / [shader()] attribute /
        //! sidecar descriptor) replaces this later. Limitation: a plain substring
        //! match can false-positive on the name appearing in a comment.
        eastl::vector<ShaderStageEntry> DetectShaderStages(eastl::string_view src)
        {
            struct Candidate
            {
                RHI::ShaderStage stage;
                const char*      entry;
                const char*      profile;
            };
            static const Candidate kCandidates[] = {
                { RHI::ShaderStage::Vertex,   "VSMain", "vs_6_0" },
                { RHI::ShaderStage::Fragment, "PSMain", "ps_6_0" },
                { RHI::ShaderStage::Geometry, "GSMain", "gs_6_0" },
                { RHI::ShaderStage::Compute,  "CSMain", "cs_6_0" },
            };

            eastl::vector<ShaderStageEntry> stages;
            for (const auto& c : kCandidates)
            {
                if (src.find(c.entry) != eastl::string_view::npos)
                {
                    stages.push_back({ c.stage, c.entry, c.profile });
                }
            }
            return stages;
        }
    }

    HashString ShaderAssetBuilder::GetName() const
    {
        return "ShaderAssetBuilder"_hs;
    }

    void ShaderAssetBuilder::InitInternal()
    {
        AssetBuildBus::Handler::BusConnect(AssetType::Shader);
    }

    void ShaderAssetBuilder::ShutdownInternal()
    {
        AssetBuildBus::Handler::BusDisconnect();
    }

    Ptr<Asset> ShaderAssetBuilder::CreateAsset(const AssetId& id)
    {
        return Ptr<Asset>(new ShaderAsset(id));
    }

    void ShaderAssetBuilder::Load(AssetBuildContext& ctx)
    {
        ASSERT(ctx.type == AssetType::Shader, "[ShaderAssetBuilder] ctx.type mismatch");
        m_loader.SetSearchPaths(ctx.searchPaths);
        ctx.rawData = m_loader.Load(ctx.id);
    }

    void ShaderAssetBuilder::Compile(AssetBuildContext& ctx)
    {
        ASSERT(ctx.type == AssetType::Shader, "[ShaderAssetBuilder] ctx.type mismatch");
        if (!ctx.rawData)
        {
            return;
        }

        // Per-shader stages live on the ShaderDescriptor (compile config), no
        // longer a global VS+PS hardcode. Detect them from the source for now
        // (see DetectShaderStages) and hand them to the compiler.
        const auto& bytes = static_cast<BinaryAssetData&>(*ctx.rawData).GetBytes();
        ShaderDescriptor descriptor;
        descriptor.stages = DetectShaderStages(
            eastl::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));

        if (descriptor.stages.empty())
        {
            LOG_ERROR("[ShaderAssetBuilder] No known shader entry point "
                "(VSMain/PSMain/GSMain/CSMain) found in '{}'.", ctx.id.GetPath().c_str());
            return;
        }

        ctx.compiledData = m_compiler.Compile(
            ctx.id, *ctx.rawData, ctx.searchPaths, descriptor);
    }
}
