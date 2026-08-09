#pragma once

#include <EASTL/array.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/optional.h>
#include <EASTL/span.h>
#include <EASTL/vector.h>

#include <RHI/HardwareQueue.h>
#include <RHI/RHILimits.h>

#include <Drawable/DrawList.h>
#include <Pass/PassContext.h>
#include <Pass/Component/PassComponents.h>
#include <RenderGraph/RenderGraphCompiler.h>


namespace Spark::RHI
{
    class CommandList;
    class Device;
    class Factory;
    struct DrawItem;
}


namespace Spark::Render
{
    struct DrawRange
    {
        uint32_t m_startIndex = 0;
        uint32_t m_endIndex   = 0;
    };

    //! Item::m_listIndex for a pass that has no DrawList (custom-pipeline, compute, copy,
    //! or a pass that never declared .RendersView<>). Its single item exists only to drive
    //! BeginRenderPass / EndRenderPass and its own execute hook.
    inline constexpr uint16_t kNoDrawList = 0xFFFF;

    //! An entry whose DrawItem was gone at submit time. Collected during recording and
    //! applied after the frame — removing it mid-record would shift the batch ranges the
    //! items were built from. Nothing mutates the lists while recording, so the index
    //! stays valid until then.
    struct DeadDrawEntry
    {
        Pass     m_pass;
        uint16_t m_listIndex  = kNoDrawList;
        uint32_t m_entryIndex = 0;
    };

    //! A live DrawItem plus the entry index it must be submitted with — the index is what
    //! CommandList::ValidateSubmitIndex checks against the slice's SetSubmitRange.
    struct ResolvedDraw
    {
        const RHI::DrawItem* m_item        = nullptr;
        uint32_t             m_submitIndex = 0;
    };

    //! 一个 CommandList 的录制内容:若干 (pass, DrawList 的一个 batch 内的一段) 按序录制。
    //!
    //! An Item is pre-resolved down to a single batch, so recording needs no range
    //! arithmetic: the list index changing means re-establish list state, the batch index
    //! changing means re-bind the PSO, and m_draws goes straight to the pass's execute
    //! hook. A split that crosses a batch boundary is simply two items.
    struct ExecuteWork
    {
        struct Item
        {
            Pass      m_pass;
            uint16_t  m_listIndex  = kNoDrawList;
            uint16_t  m_batchIndex = 0;
            DrawRange m_draws;
            uint32_t  m_itemIndex  = 0;   //!< which slice of its pass this is
            uint32_t  m_itemCount  = 1;
        };
        eastl::vector<Item>    m_items;
        RHI::CommandList*      m_commandList = nullptr;

        //! Recording cursor — what the execute hook is currently being asked to submit.
        //! Null list / batch means the pass has no DrawLists (see kNoDrawList).
        //! m_drawItems is the slice already resolved and validated by the executer, so a
        //! hook never touches RHIContext nor deals with entries that died mid-frame.
        Pass                        m_pass       {NullPass};
        const DrawList*             m_drawList   = nullptr;
        const DrawBatch*            m_drawBatch  = nullptr;
        DrawRange                   m_draws;
        uint16_t                    m_listIndex  = kNoDrawList;
        eastl::vector<ResolvedDraw> m_drawItems;

        //! Executer bookkeeping, not part of the hook contract.
        eastl::vector<DeadDrawEntry> m_deadEntries;
    };

    class RenderGraphExecuter;

    //! The execute hook RenderPassBuilder installs when a pass declares none — an ordinary
    //! ExecuteFunction, not a framework fallback.
    void SubmitDrawBatch(ExecuteWork& work, RenderGraphExecuter&);

    //! 一组 Work 并行录制,GPU 按数组顺序执行,对应一次 ExecuteCommandLists。
    //! m_passes 由 BuildExecuteGroups 填充,m_works 由 BuildExecuteWorks 填充。
    struct ExecuteGroup
    {
        eastl::vector<Pass>        m_passes;
        eastl::vector<ExecuteWork> m_works;
    };

    //! 一次跨队列同步区间:waits 之后、signal 之前,包含若干次 GPU submission
    //! (每个 ExecuteGroup 一次 ExecuteCommandLists)。
    //! m_passes 由 BuildSegments 填充,m_groups 由 BuildExecuteGroups 填充。
    struct QueueSegment
    {
        eastl::vector<SyncOperation>             m_waits;
        eastl::vector<RHI::PendingSync>          m_externalWaits;
        eastl::vector<Pass>                      m_passes;
        eastl::vector<ExecuteGroup>              m_groups;
        eastl::optional<SyncOperation>           m_signal;
    };

    using QueueSegments = eastl::array<eastl::vector<QueueSegment>, RHI::HardwareQueueClassCount>;


    class RenderGraphExecuter
    {
    public:
        uint32_t GetFrameIndex() const { return m_frameIndex; }

    private:
        friend class RenderGraph;

        void Begin(uint32_t frameIndex);

        void End();

        void BuildExecuteTable(const QueueBasedPasses& queueBasedPasses, const PassContext& passContext);

        QueueSegments& GetQueueSegments() { return m_queueSegments; }

        void BuildSegments(uint32_t queueIndex, eastl::span<const Pass> queuePasses, const PassContext& passContext);

        void BuildExecuteGroups(QueueSegment& segment, const PassContext& passContext) const;

        void BuildExecuteWorks(ExecuteGroup& group, const PassContext& passContext) const;

        void ExecuteBindPSO(RHI::CommandList* commandList, Pass pass, PassContext& passContext);

        void ExecuteBindShared(RHI::CommandList* commandList, Pass pass, PassContext& passContext);

        void ExecutePassViewportState(RHI::CommandList* commandList, Pass pass, PassContext& passContext);

        //! Per-DrawList state: the view's viewport / scissor and its per-view bindings.
        void ExecuteDrawListState(RHI::CommandList* commandList, const DrawList& list);

        template<typename Fn>
        void ForEachWork(Fn&& fn)
        {
            for (auto& queue : m_queueSegments)
            {
                for (auto& segment : queue)
                {
                    for (auto& group : segment.m_groups)
                    {
                        for (auto& work : group.m_works)
                        {
                            fn(work);
                        }
                    }
                }
            }
        }

        //! Apply the removals recorded during this frame's recording.
        void ReapDeadDrawEntries(PassContext& passContext);

        void ExecutePreBarriers(RHI::CommandList* commandList, Pass pass, PassContext& passContext);

        void ExecutePostBarriers(RHI::CommandList* commandList, Pass pass, PassContext& passContext);

        void ExecuteBeginRenderPass(RHI::CommandList* commandList, Pass pass, PassContext& passContext);

        void ExecuteEndRenderPass(RHI::CommandList* commandList, Pass pass, PassContext& passContext);

        //! 创建 CommandList 并录制 ExecuteWork,完成后存入 work.m_commandList。
        void Execute(ExecuteWork& work, RHI::Factory& factory, RHI::Device& device, RHI::HardwareQueueClass queueClass, PassContext& passContext);

        void ExecuteFinalBarriers(RHI::CommandList* commandList);

        void ExecuteStaticPreBarriers(RHI::CommandList* commandList, uint32_t queueIndex);

        void SetStaticPreBarriers(StaticPreBarrierTable&& table) { m_staticPreBarriers = eastl::move(table); }

        QueueSegments m_queueSegments;
        StaticPreBarrierTable m_staticPreBarriers;

        //! This frame's dead entries, flattened out of the works so they can be ordered
        //! across works before any removal runs.
        eastl::vector<DeadDrawEntry> m_deadDrawEntries;

        uint32_t m_frameIndex { 0 };
    };
}