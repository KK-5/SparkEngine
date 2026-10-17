#include "DrawBinSystem.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <EASTL/string.h>
#include <EASTL/tuple.h>
#include <EASTL/vector.h>

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
        constexpr uint32_t BinCountStep   = 4;
        constexpr uint32_t ViewStrideStep = 1024;

        //! The order a pass's bins draw in: by priority, the rest only to make it total.
        bool DrawsBefore(const PipelineVariant& a, const PipelineVariant& b)
        {
            auto order = [](const PipelineVariant& v)
            {
                return eastl::make_tuple(v.m_priority, static_cast<uint32_t>(v.m_cullMode),
                                         static_cast<uint32_t>(v.m_featureMacros), v.m_materialShader);
            };
            return order(a) < order(b);
        }

        RHI::RHIHandle CreateBinTableBuffer(RHI::RHIContext& rhiCtx, const PassName& passName)
        {
            RHI::BufferDescriptor desc;
            desc.m_byteCount = sizeof(PassBinTable::m_entries);
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

        //! Finds or makes the bin of pass that key's instances draw in, and records it.
        void AssignKey(RHI::RHIContext& rhiCtx, PassContext& passCtx, Pass pass, RHI::RHIHandle key)
        {
            const auto* pipelineState = passCtx.TryGet<PassPipelineState>(pass);
            ASSERT(pipelineState, "[DrawBinSystem] Pass '{}' draws a list but has no pipeline.",
                passCtx.Get<PassName>(pass).m_name.GetCStr());

            // No shader template declares its features until shader permutations exist.
            const PipelineVariant variant = InterpretPipelineVariant(
                rhiCtx.Get<PipelineKey>(key), pipelineState->m_renderStates, ShaderTemplateFeatures{});

            DrawBinHandle bin = NullDrawBin;
            for (auto [entity, of, binVariant] : passCtx.GetView<DrawBinOf, PipelineVariant>().each())
            {
                if (of.m_pass == pass && binVariant == variant)
                {
                    bin = entity;
                    break;
                }
            }
            if (bin == NullDrawBin)
            {
                bin = passCtx.CreateEntity();
                passCtx.Add<DrawBinOf>(bin, DrawBinOf{ pass });
                passCtx.Add<PipelineVariant>(bin, variant);
                passCtx.Add<DrawBinRange>(bin);
            }

            const DrawBinAssignmentHandle assignment = passCtx.CreateEntity();
            passCtx.Add<DrawBinAssignment>(assignment, DrawBinAssignment{ pass, key, bin });
        }

        //! A new pass against the keys already assigned, then the new keys against every pass:
        //! a key and a pass that are both new meet once, in the second.
        void Assign(RHI::RHIContext& rhiCtx, PassContext& passCtx)
        {
            eastl::vector<RHI::RHIHandle> binnedKeys;
            for (auto [key, value] : rhiCtx.GetView<PipelineKey, KeyBinned>().each())
            {
                binnedKeys.push_back(key);
            }

            eastl::vector<Pass> newPasses;
            for (auto [pass, drawList] : passCtx.GetView<PassDrawList>(Exclude<PassBinned>).each())
            {
                newPasses.push_back(pass);
            }
            for (Pass pass : newPasses)
            {
                for (RHI::RHIHandle key : binnedKeys)
                {
                    AssignKey(rhiCtx, passCtx, pass, key);
                }
                PassBinTable table;
                table.m_buffer = CreateBinTableBuffer(rhiCtx, passCtx.Get<PassName>(pass));
                passCtx.Add<PassBinTable>(pass, table);
                passCtx.Add<PassBinLayout>(pass);
                passCtx.Add<PassBinned>(pass);
            }

            eastl::vector<RHI::RHIHandle> newKeys;
            for (auto [key, value] : rhiCtx.GetView<PipelineKey>(Exclude<KeyBinned>).each())
            {
                newKeys.push_back(key);
            }
            eastl::vector<Pass> binnedPasses;
            for (auto [pass, drawList] : passCtx.GetView<PassDrawList, PassBinned>().each())
            {
                binnedPasses.push_back(pass);
            }
            for (RHI::RHIHandle key : newKeys)
            {
                for (Pass pass : binnedPasses)
                {
                    AssignKey(rhiCtx, passCtx, pass, key);
                }
                rhiCtx.Add<KeyBinned>(key);
            }
        }

        void Layout(RHI::RHIContext& rhiCtx, PassContext& passCtx)
        {
            for (auto [bin, range] : passCtx.GetView<DrawBinRange>().each())
            {
                range.m_capacity = 0;
            }
            for (auto [entity, assignment] : passCtx.GetView<DrawBinAssignment>().each())
            {
                const uint32_t bit = passCtx.Get<PassDrawList>(assignment.m_pass).m_drawMaskBitIndex;
                passCtx.Get<DrawBinRange>(assignment.m_bin).m_capacity +=
                    rhiCtx.Get<KeyInstanceCount>(assignment.m_key).m_counts[bit];
            }

            // Ordered by what they are, not by where their entities happen to be stored.
            eastl::vector<DrawBinHandle> bins;
            for (auto [pass, drawList, layout] : passCtx.GetView<PassDrawList, PassBinLayout>().each())
            {
                bins.clear();
                for (auto [bin, of] : passCtx.GetView<DrawBinOf>().each())
                {
                    if (of.m_pass == pass)
                    {
                        bins.push_back(bin);
                    }
                }
                eastl::sort(bins.begin(), bins.end(), [&](DrawBinHandle a, DrawBinHandle b)
                {
                    return DrawsBefore(passCtx.Get<PipelineVariant>(a), passCtx.Get<PipelineVariant>(b));
                });

                uint32_t first = 0;
                for (uint32_t index = 0; index < bins.size(); ++index)
                {
                    auto& range   = passCtx.Get<DrawBinRange>(bins[index]);
                    range.m_index = index;
                    range.m_first = first;
                    first += range.m_capacity;
                }

                layout.m_binCount   = AlignUp(eastl::max(static_cast<uint32_t>(bins.size()), 1u), BinCountStep);
                layout.m_viewStride = AlignUp(eastl::max(first, 1u), ViewStrideStep);
            }
        }

        void WriteBinTables(RHI::RHIContext& rhiCtx, PassContext& passCtx)
        {
            for (auto [pass, table] : passCtx.GetView<PassBinTable>().each())
            {
                table.m_entries.fill(DrawBinTableEntry{});
            }
            for (auto [entity, assignment] : passCtx.GetView<DrawBinAssignment>().each())
            {
                const auto& range = passCtx.Get<DrawBinRange>(assignment.m_bin);
                const uint32_t key = rhiCtx.Get<PipelineKey>(assignment.m_key).m_value;
                passCtx.Get<PassBinTable>(assignment.m_pass).m_entries[key] = DrawBinTableEntry{ range.m_index, range.m_first };
            }

            // Also on the frame the buffer is requested: the frame's start makes it, then copies
            // into it, so a binning pass never reads a table not written.
            for (auto [pass, table] : passCtx.GetView<PassBinTable>().each())
            {
                rhiCtx.AddOrReplace<RHI::PendingBufferMap>(
                    table.m_buffer, RHI::PendingBufferMap{ table.m_entries.data(), 0, sizeof(table.m_entries) });
            }
        }
    }

    void CountKeyInstance(KeyInstanceCounts& counts, PipelineKey key, DrawMask drawMask)
    {
        ASSERT(key.m_value < PipelineKeyCount, "[DrawBinSystem] Key {} is outside the key space.", key.m_value);
        for (uint32_t bit = 0; bit < DrawMaskBitCount; ++bit)
        {
            if (CheckBit(drawMask, bit))
            {
                ++counts[key.m_value].m_counts[bit];
            }
        }
    }

    void PublishKeyInstanceCounts(RHI::RHIContext& rhiCtx, const KeyInstanceCounts& counts)
    {
        eastl::array<RHI::RHIHandle, PipelineKeyCount> entityOfKey;
        entityOfKey.fill(RHI::NullHandle);
        for (auto [entity, key] : rhiCtx.GetView<PipelineKey>().each())
        {
            entityOfKey[key.m_value] = entity;
        }

        for (uint32_t value = 0; value < PipelineKeyCount; ++value)
        {
            RHI::RHIHandle entity = entityOfKey[value];
            if (entity == RHI::NullHandle)
            {
                const auto& perBit = counts[value].m_counts;
                if (eastl::all_of(perBit.begin(), perBit.end(), [](uint32_t count) { return count == 0; }))
                {
                    continue;
                }
                entity = rhiCtx.CreateEntity();
                rhiCtx.Add<PipelineKey>(entity, PipelineKey{ value });
            }
            rhiCtx.AddOrReplace<KeyInstanceCount>(entity, counts[value]);
        }
    }

    void CollectBinRanges(const PassContext& passCtx, Pass pass, eastl::vector<DrawBinRange>& out)
    {
        const size_t begin = out.size();
        for (auto [bin, of, range] : passCtx.GetView<DrawBinOf, DrawBinRange>().each())
        {
            if (of.m_pass == pass && range.m_capacity > 0)
            {
                out.push_back(range);
            }
        }
        eastl::sort(out.begin() + begin, out.end(),
            [](const DrawBinRange& a, const DrawBinRange& b) { return a.m_index < b.m_index; });
    }

    void DrawBinSystem::Update(RHI::RHIContext& rhiCtx, PassContext& passCtx)
    {
        Assign(rhiCtx, passCtx);
        Layout(rhiCtx, passCtx);
        WriteBinTables(rhiCtx, passCtx);
    }

    void DrawBinSystem::Shutdown(RHI::RHIContext& rhiCtx, PassContext& passCtx)
    {
        eastl::vector<Pass> passes;
        for (auto [pass, table] : passCtx.GetView<PassBinTable>().each())
        {
            rhiCtx.Add<DeadTag>(table.m_buffer);
            passes.push_back(pass);
        }
        for (Pass pass : passes)
        {
            passCtx.Remove<PassBinTable>(pass);
            passCtx.Remove<PassBinLayout>(pass);
            passCtx.Remove<PassBinned>(pass);
        }

        eastl::vector<DrawBinHandle> bins;
        for (auto [bin, of] : passCtx.GetView<DrawBinOf>().each())
        {
            bins.push_back(bin);
        }
        passCtx.DestoryEntity(bins.begin(), bins.end());

        eastl::vector<DrawBinAssignmentHandle> assignments;
        for (auto [entity, assignment] : passCtx.GetView<DrawBinAssignment>().each())
        {
            assignments.push_back(entity);
        }
        passCtx.DestoryEntity(assignments.begin(), assignments.end());

        for (auto [key, value] : rhiCtx.GetView<PipelineKey>().each())
        {
            rhiCtx.Add<DeadTag>(key);
        }
    }
}
