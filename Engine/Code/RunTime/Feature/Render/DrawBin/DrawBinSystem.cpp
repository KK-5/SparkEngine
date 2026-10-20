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
#include <PipelineState/PipelineStateBuild.h>
#include <PipelineState/PipelineVariantInterpret.h>

namespace Spark::Render
{
    namespace
    {
        constexpr uint32_t BinCountStep      = 4;
        constexpr uint32_t ViewStrideStep    = 1024;
        constexpr uint32_t BinTableEntryStep = 16;

        RHI::RHIHandle CreateBinTableBuffer(RHI::RHIContext& rhiCtx, const PassName& passName, uint32_t capacity)
        {
            RHI::BufferDescriptor desc;
            desc.m_byteCount = static_cast<uint64_t>(capacity) * sizeof(DrawBinTableEntry);
            desc.m_bindFlags = RHI::BufferBindFlags::ShaderRead | RHI::BufferBindFlags::CopyRead;

            RHI::PendingBufferInit init;
            init.m_descriptor       = desc;
            init.m_heapMemoryLevel  = RHI::HeapMemoryLevel::Host;
            init.m_hostMemoryAccess = RHI::HostMemoryAccess::Write;

            const eastl::string name = eastl::string(passName.m_name.GetCStr()) + "BinTable";
            const RHI::RHIHandle buffer = rhiCtx.CreateEntity();
            rhiCtx.Add<RHI::PendingBufferInit>(buffer, init);
            rhiCtx.Add<RHI::PerFrameTag>(buffer);
            rhiCtx.Add<RHI::ResourceName>(buffer, RHI::ResourceName{ ObjectName(name) });
            return buffer;
        }

        //! The pass's table holds entryCount this frame: a new buffer when the old one holds fewer.
        PassBinTable& FitBinTable(RHI::RHIContext& rhiCtx, PassContext& passCtx, Pass pass, uint32_t entryCount)
        {
            if (!passCtx.Has<PassBinTable>(pass))
            {
                passCtx.Add<PassBinTable>(pass);
            }
            PassBinTable& table = passCtx.Get<PassBinTable>(pass);
            if (table.m_capacity < entryCount)
            {
                if (table.m_buffer != RHI::NullHandle)
                {
                    rhiCtx.Add<DeadTag>(table.m_buffer);
                }
                table.m_capacity = AlignUp(entryCount, BinTableEntryStep);
                table.m_buffer   = CreateBinTableBuffer(rhiCtx, passCtx.Get<PassName>(pass), table.m_capacity);
            }
            table.m_entryCount = entryCount;
            return table;
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

        eastl::vector<DrawBinTableEntry>& image = m_binTables[pass];
        image.assign(eastl::max(tableLength, 1u), DrawBinTableEntry{});

        uint32_t drawCount = 0;
        for (uint32_t index = 0; index < static_cast<uint32_t>(m_groups.size()); ++index)
        {
            const DrawGroup& group = m_groups[index];

            DrawBin bin;
            bin.m_pass      = pass;
            bin.m_index     = index;
            bin.m_variant   = group.m_variant;
            bin.m_firstDraw = group.m_firstDraw;
            bin.m_maxDraws  = group.m_maxDraws;
            drawCtx.Add<DrawBin>(drawCtx.CreateEntity(), bin);

            for (uint32_t e = group.m_firstEntry; e < group.m_firstEntry + group.m_entryCount; ++e)
            {
                image[m_entries[e].m_keyIndex] = DrawBinTableEntry{ index, group.m_firstDraw };
            }
            drawCount += group.m_maxDraws;
        }

        PassBinLayout layout;
        layout.m_binCount   = AlignUp(eastl::max(static_cast<uint32_t>(m_groups.size()), 1u), BinCountStep);
        layout.m_viewStride = AlignUp(eastl::max(drawCount, 1u), ViewStrideStep);
        passCtx.AddOrReplace<PassBinLayout>(pass, layout);

        // Also on the frame the buffer is requested: the frame's start makes it, then copies
        // into it, so a binning pass never reads a table not written.
        const PassBinTable& table = FitBinTable(rhiCtx, passCtx, pass, static_cast<uint32_t>(image.size()));
        rhiCtx.AddOrReplace<RHI::PendingBufferMap>(
            table.m_buffer, RHI::PendingBufferMap{ image.data(), 0, image.size() * sizeof(DrawBinTableEntry) });
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

    void DrawBinSystem::AcquirePipelines(PassContext& passCtx, DrawContext& drawCtx, RHI::Factory& factory, RHI::Device& device)
    {
        for (auto [entity, bin] : drawCtx.GetView<DrawBin>().each())
        {
            const char* passName = passCtx.Get<PassName>(bin.m_pass).m_name.GetCStr();
            const RHI::PipelineStateDescriptorForDraw descriptor = BuildDrawPipelineDescriptor(
                passCtx.Get<PassStageFunctions>(bin.m_pass), passCtx.Get<PassPipelineState>(bin.m_pass),
                passCtx.Get<PassPipelineLayout>(bin.m_pass), bin.m_variant, passName);
            bin.m_pso = m_pipelineCache.Acquire(descriptor, factory, device, passName);
        }
    }

    void DrawBinSystem::Shutdown(RHI::RHIContext& rhiCtx, PassContext& passCtx)
    {
        eastl::vector<Pass> passes;
        for (auto [pass, table] : passCtx.GetView<PassBinTable>().each())
        {
            if (table.m_buffer != RHI::NullHandle)
            {
                rhiCtx.Add<DeadTag>(table.m_buffer);
            }
            passes.push_back(pass);
        }
        for (Pass pass : passes)
        {
            passCtx.Remove<PassBinTable>(pass);
            passCtx.Remove<PassBinLayout>(pass);
        }

        m_binTables.clear();
        m_pipelineCache.Shutdown();
    }
}
