#include "DrawGroupSystem.h"

#include <EASTL/algorithm.h>

#include <Log/ILogSystem.h>
#include <Math/Bit.h>

#include <RHI/Component/Component.h>
#include <RHI/Device/Device.h>
#include <RHI/RHILimits.h>
#include <RHI/Resource/Buffer/Buffer.h>

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
    }

    bool DrawGroupSystem::Init(RHI::Device& device)
    {
        // DX12 also creates a bindless RAW SRV, whose byte offset must be 16-byte aligned.
        // Other backends may require a larger storage-buffer offset alignment.
        m_tableAlignment = eastl::max<uint32_t>(sizeof(IndirectLayoutEntry), eastl::max(
            RHI::Alignment::Buffer, device.GetLimits().m_minStorageBufferOffsetAlignment));
        StagingBuffer::Descriptor descriptor;
        descriptor.m_name      = ObjectName("IndirectLayouts");
        descriptor.m_bindFlags = RHI::BufferBindFlags::ShaderRead;
        return m_tables.Init(device, descriptor);
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
        WriteIndirectLayout(passCtx, pass);

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

    void DrawGroupSystem::WriteIndirectLayout(PassContext& passCtx, Pass pass)
    {
        PassIndirectLayout& layout = passCtx.Get<PassIndirectLayout>(pass);
        const StagingBuffer::Allocation allocation = m_tables.Allocate(
            uint64_t(layout.m_tableLength) * sizeof(IndirectLayoutEntry), m_tableAlignment);
        auto* table = static_cast<IndirectLayoutEntry*>(allocation.m_data);
        eastl::fill_n(table, layout.m_tableLength, IndirectLayoutEntry{});

        uint32_t drawCount = 0;
        for (const DrawEntry& entry : m_entries)
        {
            table[entry.m_keyIndex] = IndirectLayoutEntry{ entry.m_groupIndex, m_groups[entry.m_groupIndex].m_firstDraw };
            drawCount += entry.m_count;
        }
        layout.m_table           = m_tables.GetResource();
        layout.m_tableByteOffset = allocation.m_offset;
        layout.m_countsPerView    = AlignUp(eastl::max(static_cast<uint32_t>(m_groups.size()), 1u), CountsPerViewStep);
        layout.m_recordsPerView   = AlignUp(eastl::max(drawCount, 1u), RecordsPerViewStep);
    }

    bool DrawGroupSystem::PrepareIndirectLayoutTables(PassContext& passCtx, DrawContext& drawCtx)
    {
        // Size the whole frame before writing: growing the buffer later would discard earlier tables.
        uint64_t byteCount = 0;
        for (auto [pass, drawList] : passCtx.GetView<PassDrawList>().each())
        {
            if (drawList.m_culling != DrawCulling::Gpu)
            {
                continue;
            }

            uint32_t tableLength = 1;
            for (auto [entity, key] : drawCtx.GetView<DrawListKey>().each())
            {
                if (key.m_drawMaskBitIndex == drawList.m_drawMaskBitIndex)
                {
                    tableLength = eastl::max(tableLength, key.m_keyIndex.m_value + 1);
                }
            }
            ASSERT(tableLength <= TableCapacity, "[DrawGroupSystem] Pass '{}' has a key index past its table.",
                passCtx.Get<PassName>(pass).m_name.GetCStr());
            passCtx.AddOrReplace<PassIndirectLayout>(pass).m_tableLength = tableLength;
            // Match Allocate's padding between tables when reserving the whole frame.
            byteCount = AlignUp(byteCount, m_tableAlignment) + uint64_t(tableLength) * sizeof(IndirectLayoutEntry);
        }
        m_tables.Begin(byteCount);
        if (byteCount > 0 && (!m_tables.GetBuffer() || m_tables.GetBuffer()->GetDescriptor().m_byteCount < byteCount))
        {
            LOG_ERROR("[DrawGroupSystem] No staging buffer for {} bytes of indirect layouts.", byteCount);
            return false;
        }
        return true;
    }

    void DrawGroupSystem::Update(RHI::RHIContext& rhiCtx, PassContext& passCtx, DrawContext& drawCtx)
    {
        if (!PrepareIndirectLayoutTables(passCtx, drawCtx))
        {
            return;
        }

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

    void DrawGroupSystem::Shutdown(PassContext& passCtx)
    {
        eastl::vector<Pass> passes;
        for (auto [pass, layout] : passCtx.GetView<PassIndirectLayout>().each())
        {
            passes.push_back(pass);
        }
        for (Pass pass : passes)
        {
            passCtx.Remove<PassIndirectLayout>(pass);
        }

        m_tables.Shutdown();
    }
}
