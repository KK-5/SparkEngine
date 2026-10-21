#include "DrawGroupSystem.h"

#include <EASTL/algorithm.h>
#include <EASTL/string.h>

#include <CoreComponents/Tags.h>
#include <Log/ILogSystem.h>
#include <Math/Bit.h>

#include <RHI/Component/Component.h>
#include <RHI/ResourceBuilder.h>

#include <Pass/Component/PassComponents.h>
#include <PipelineState/PipelineVariantInterpret.h>

#include "DrawList.h"

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

    void DrawGroupSystem::UpdateGpuCulledPass(RHI::RHIContext& rhiCtx, PassContext& passCtx, DrawContext& drawCtx, Pass pass)
    {
        const PassDrawList& drawList = passCtx.Get<PassDrawList>(pass);
        const auto* pipelineState = passCtx.TryGet<PassPipelineState>(pass);
        ASSERT(pipelineState, "[DrawGroupSystem] Pass '{}' draws a list but has no pipeline.",
            passCtx.Get<PassName>(pass).m_name.GetCStr());

        // No shader template declares its features until shader permutations exist.
        m_entries.clear();
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
        }

        GroupDrawEntries(m_entries, drawList.m_order, m_groups);
        WriteIndirectLayout(rhiCtx, passCtx, pass);

        // Only the GPU knows which instances a view sees, so the groups are the whole list's,
        // the same under every view, each view's in its own part of the arguments.
        CollectPassViews(rhiCtx, passCtx, pass);
        for (RHI::RHIHandle view : m_views)
        {
            for (DrawGroup group : m_groups)
            {
                group.m_pass = pass;
                group.m_view = view;
                drawCtx.Add<DrawGroup>(drawCtx.CreateEntity(), group);
            }
        }
    }

    void DrawGroupSystem::UpdateCpuCulledPass(RHI::RHIContext& rhiCtx, PassContext& passCtx, DrawContext& drawCtx, Pass pass)
    {
        const PassDrawList& drawList = passCtx.Get<PassDrawList>(pass);
        const auto* pipelineState = passCtx.TryGet<PassPipelineState>(pass);
        ASSERT(pipelineState, "[DrawGroupSystem] Pass '{}' draws a list but has no pipeline.",
            passCtx.Get<PassName>(pass).m_name.GetCStr());
        // The list's order is its draws' order, which the pass only cuts into groups.
        ASSERT(drawList.m_order == DrawOrder::BackToFront,
            "[DrawGroupSystem] Pass '{}' draws a list the CPU culls in any order; such a list is culled on the GPU.",
            passCtx.Get<PassName>(pass).m_name.GetCStr());

        CollectPassViews(rhiCtx, passCtx, pass);
        for (RHI::RHIHandle view : m_views)
        {
            const VisibleInstanceRange visible = VisibleInstancesOf(drawCtx, view, drawList.m_drawMaskBitIndex);

            m_entries.clear();
            for (const VisibleInstance& instance : visible)
            {
                DrawEntry entry;
                entry.m_variant = InterpretPipelineVariant(instance.m_key, pipelineState->m_renderStates, ShaderTemplateFeatures{});
                m_entries.push_back(entry);
            }
            GroupDrawEntries(m_entries, drawList.m_order, m_groups);

            // Cut out of the view's draws, where DrawListCullingSystem wrote them.
            const uint32_t firstDraw = FirstDrawOffset(drawCtx, visible);
            for (DrawGroup group : m_groups)
            {
                group.m_pass       = pass;
                group.m_view       = view;
                group.m_firstDraw += firstDraw;
                drawCtx.Add<DrawGroup>(drawCtx.CreateEntity(), group);
            }
        }
    }

    void DrawGroupSystem::CollectPassViews(RHI::RHIContext& rhiCtx, PassContext& passCtx, Pass pass)
    {
        // Those the render graph draws the pass under, by the same call in the same frame; one it
        // skips leaves its groups unread.
        const auto* capabilities = passCtx.TryGet<PassCapabilities>(pass);
        ASSERT(capabilities && capabilities->m_collectViews,
            "[DrawGroupSystem] Pass '{}' draws a list but declares no .RendersView<>().",
            passCtx.Get<PassName>(pass).m_name.GetCStr());
        m_views.clear();
        if (capabilities && capabilities->m_collectViews)
        {
            capabilities->m_collectViews(rhiCtx, m_views);
        }
    }

    void DrawGroupSystem::WriteIndirectLayout(RHI::RHIContext& rhiCtx, PassContext& passCtx, Pass pass)
    {
        // What a binning thread can look up: every instance it sees is counted in some DrawListKey.
        uint32_t tableLength = 1;
        uint32_t drawCount   = 0;
        for (const DrawEntry& entry : m_entries)
        {
            tableLength = eastl::max(tableLength, entry.m_keyIndex + 1);
            drawCount  += entry.m_count;
        }
        ASSERT(tableLength <= TableCapacity, "[DrawGroupSystem] Pass '{}' has a key index past its table.",
            passCtx.Get<PassName>(pass).m_name.GetCStr());

        eastl::vector<IndirectLayoutEntry>& image = m_tables[pass];
        image.assign(tableLength, IndirectLayoutEntry{});
        for (const DrawEntry& entry : m_entries)
        {
            image[entry.m_keyIndex] = IndirectLayoutEntry{ entry.m_groupIndex, m_groups[entry.m_groupIndex].m_firstDraw };
        }

        if (!passCtx.Has<PassIndirectLayout>(pass))
        {
            passCtx.Add<PassIndirectLayout>(pass).m_table = CreateTable(rhiCtx, passCtx.Get<PassName>(pass));
        }
        PassIndirectLayout& layout = passCtx.Get<PassIndirectLayout>(pass);
        layout.m_tableLength    = tableLength;
        layout.m_countsPerView  = AlignUp(eastl::max(static_cast<uint32_t>(m_groups.size()), 1u), CountsPerViewStep);
        layout.m_recordsPerView = AlignUp(eastl::max(drawCount, 1u), RecordsPerViewStep);

        // Also on the frame the table is requested: the frame's start makes it, then copies
        // into it, so a binning pass never reads a table not written.
        rhiCtx.AddOrReplace<RHI::PendingBufferMap>(
            layout.m_table, RHI::PendingBufferMap{ image.data(), 0, image.size() * sizeof(IndirectLayoutEntry) });
    }

    void DrawGroupSystem::Update(RHI::RHIContext& rhiCtx, PassContext& passCtx, DrawContext& drawCtx)
    {
        for (auto [pass, drawList] : passCtx.GetView<PassDrawList>().each())
        {
            if (drawList.m_culling == DrawCulling::Gpu)
            {
                UpdateGpuCulledPass(rhiCtx, passCtx, drawCtx, pass);
            }
            else
            {
                UpdateCpuCulledPass(rhiCtx, passCtx, drawCtx, pass);
            }
        }

        SortDrawGroups(drawCtx);
    }

    void DrawGroupSystem::Shutdown(RHI::RHIContext& rhiCtx, PassContext& passCtx)
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
