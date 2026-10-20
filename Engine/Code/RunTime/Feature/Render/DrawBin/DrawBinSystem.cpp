#include "DrawBinSystem.h"

#include <EASTL/algorithm.h>
#include <EASTL/string.h>
#include <EASTL/tuple.h>

#include <CoreComponents/Tags.h>
#include <Log/ILogSystem.h>
#include <Math/Bit.h>

#include <RHI/Component/Component.h>
#include <RHI/ResourceBuilder.h>

#include <Pass/Component/PassComponents.h>
#include <PipelineState/PipelineVariantInterpret.h>

namespace Spark::Render
{
    namespace
    {
        constexpr uint32_t CountsPerViewStep  = 4;
        constexpr uint32_t RecordsPerViewStep = 1024;
        //! While a key's index is its value, no index reaches the key space.
        constexpr uint32_t TableCapacity      = PipelineKeyCount;

        RHI::RHIHandle CreateTable(RHI::RHIContext& rhiCtx, const PassName& passName)
        {
            RHI::BufferDescriptor desc;
            desc.m_byteCount = static_cast<uint64_t>(TableCapacity) * sizeof(IndirectLayoutEntry);
            desc.m_bindFlags = RHI::BufferBindFlags::ShaderRead | RHI::BufferBindFlags::CopyRead;

            RHI::PendingBufferInit init;
            init.m_descriptor       = desc;
            init.m_heapMemoryLevel  = RHI::HeapMemoryLevel::Host;
            init.m_hostMemoryAccess = RHI::HostMemoryAccess::Write;

            const eastl::string name = eastl::string(passName.m_name.GetCStr()) + "IndirectLayout";
            const RHI::RHIHandle buffer = rhiCtx.CreateEntity();
            rhiCtx.Add<RHI::PendingBufferInit>(buffer, init);
            rhiCtx.Add<RHI::PerFrameTag>(buffer);
            rhiCtx.Add<RHI::ResourceName>(buffer, RHI::ResourceName{ ObjectName(name) });
            return buffer;
        }
    }

    void CollectDrawBins(const DrawContext& drawCtx, Pass pass, RHI::RHIHandle view, DrawBinList& out)
    {
        out.clear();
        for (auto [entity, bin] : drawCtx.GetView<DrawBin>().each())
        {
            if (bin.m_pass == pass && bin.m_view == view)
            {
                out.push_back(&bin);
            }
        }
    }

    void DrawBinSystem::UpdatePass(RHI::RHIContext& rhiCtx, PassContext& passCtx, DrawContext& drawCtx, Pass pass)
    {
        const PassDrawList& drawList = passCtx.Get<PassDrawList>(pass);
        const auto* pipelineState = passCtx.TryGet<PassPipelineState>(pass);
        ASSERT(pipelineState, "[DrawBinSystem] Pass '{}' draws a list but has no pipeline.",
            passCtx.Get<PassName>(pass).m_name.GetCStr());
        ASSERT(drawList.m_culling == DrawCulling::Gpu,
            "[DrawBinSystem] Pass '{}' draws a list culled on the CPU, which is not drawn yet.",
            passCtx.Get<PassName>(pass).m_name.GetCStr());

        // No shader template declares its features until shader permutations exist.
        m_entries.clear();
        // What a binning thread can look up: every instance it sees is counted in some DrawListKey.
        uint32_t tableLength = 0;
        for (auto [entity, listKey] : drawCtx.GetView<DrawListKey>().each())
        {
            if (listKey.m_drawMaskBitIndex != drawList.m_drawMaskBitIndex)
            {
                continue;
            }
            DrawEntry entry;
            entry.m_variant  = InterpretPipelineVariant(listKey.m_key, pipelineState->m_renderStates, ShaderTemplateFeatures{});
            entry.m_count    = listKey.m_instanceCount;
            entry.m_keyIndex = listKey.m_keyIndex.m_value;
            m_entries.push_back(entry);
            tableLength = eastl::max(tableLength, entry.m_keyIndex + 1);
        }

        GroupDrawEntries(m_entries, drawList.m_order, m_groups);

        eastl::vector<IndirectLayoutEntry>& image = m_tables[pass];
        image.assign(eastl::max(tableLength, 1u), IndirectLayoutEntry{});
        ASSERT(image.size() <= TableCapacity, "[DrawBinSystem] Pass '{}' has a key index past its table.",
            passCtx.Get<PassName>(pass).m_name.GetCStr());

        uint32_t drawCount = 0;
        for (uint32_t index = 0; index < static_cast<uint32_t>(m_groups.size()); ++index)
        {
            const DrawGroup& group = m_groups[index];
            for (uint32_t e = group.m_firstEntry; e < group.m_firstEntry + group.m_entryCount; ++e)
            {
                image[m_entries[e].m_keyIndex] = IndirectLayoutEntry{ index, group.m_firstDraw };
            }
            drawCount += group.m_maxDraws;
        }

        if (!passCtx.Has<PassIndirectLayout>(pass))
        {
            passCtx.Add<PassIndirectLayout>(pass).m_table = CreateTable(rhiCtx, passCtx.Get<PassName>(pass));
        }
        PassIndirectLayout& layout = passCtx.Get<PassIndirectLayout>(pass);
        layout.m_tableLength    = static_cast<uint32_t>(image.size());
        layout.m_countsPerView  = AlignUp(eastl::max(static_cast<uint32_t>(m_groups.size()), 1u), CountsPerViewStep);
        layout.m_recordsPerView = AlignUp(eastl::max(drawCount, 1u), RecordsPerViewStep);

        // Also on the frame the table is requested: the frame's start makes it, then copies
        // into it, so a binning pass never reads a table not written.
        rhiCtx.AddOrReplace<RHI::PendingBufferMap>(
            layout.m_table, RHI::PendingBufferMap{ image.data(), 0, image.size() * sizeof(IndirectLayoutEntry) });

        // Only the GPU knows which instances a view sees, so the groups are the whole list's,
        // the same under every view, each view's in its own part of the arguments. The views
        // are those the render graph draws the pass under, by the same call in the same frame;
        // one it skips leaves its bins unread.
        const auto* capabilities = passCtx.TryGet<PassCapabilities>(pass);
        ASSERT(capabilities && capabilities->m_collectViews,
            "[DrawBinSystem] Pass '{}' draws a list but declares no .RendersView<>().",
            passCtx.Get<PassName>(pass).m_name.GetCStr());
        m_views.clear();
        if (capabilities && capabilities->m_collectViews)
        {
            capabilities->m_collectViews(rhiCtx, m_views);
        }
        for (RHI::RHIHandle view : m_views)
        {
            for (uint32_t index = 0; index < static_cast<uint32_t>(m_groups.size()); ++index)
            {
                const DrawGroup& group = m_groups[index];

                DrawBin bin;
                bin.m_pass      = pass;
                bin.m_view      = view;
                bin.m_index     = index;
                bin.m_variant   = group.m_variant;
                bin.m_firstDraw = group.m_firstDraw;
                bin.m_maxDraws  = group.m_maxDraws;
                drawCtx.Add<DrawBin>(drawCtx.CreateEntity(), bin);
            }
        }
    }

    void DrawBinSystem::Update(RHI::RHIContext& rhiCtx, PassContext& passCtx, DrawContext& drawCtx)
    {
        for (auto [pass, drawList] : passCtx.GetView<PassDrawList>().each())
        {
            UpdatePass(rhiCtx, passCtx, drawCtx, pass);
        }

        drawCtx.Sort<DrawBin>([](const DrawBin& a, const DrawBin& b)
        {
            return eastl::make_tuple(static_cast<uint32_t>(a.m_pass), static_cast<uint32_t>(a.m_view), a.m_index)
                 < eastl::make_tuple(static_cast<uint32_t>(b.m_pass), static_cast<uint32_t>(b.m_view), b.m_index);
        });
    }

    void DrawBinSystem::Shutdown(RHI::RHIContext& rhiCtx, PassContext& passCtx)
    {
        eastl::vector<Pass> passes;
        for (auto [pass, layout] : passCtx.GetView<PassIndirectLayout>().each())
        {
            if (layout.m_table != RHI::NullHandle)
            {
                rhiCtx.Add<DeadTag>(layout.m_table);
            }
            passes.push_back(pass);
        }
        for (Pass pass : passes)
        {
            passCtx.Remove<PassIndirectLayout>(pass);
        }

        m_tables.clear();
    }
}
