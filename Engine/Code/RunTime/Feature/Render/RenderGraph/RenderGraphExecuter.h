#pragma once

#include <EASTL/array.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/optional.h>
#include <EASTL/span.h>
#include <EASTL/vector.h>

#include <RHI/HardwareQueue.h>
#include <RHI/RHILimits.h>
#include <RHI/Viewport/Viewport.h>
#include <RHI/Scissor/Scissor.h>
#include <RHI/Context/RHIHandle.h>

#include <Pass/PassContext.h>
#include <Pass/Component/PassComponents.h>
#include <RenderGraph/RenderGraphCompiler.h>


namespace Spark::RHI
{
    class CommandList;
    class Device;
    class Factory;
    class PipelineState;
    class ShaderBindings;
    struct DrawItem;
}


namespace Spark::Render
{
    //! DrawItem carries no variant id yet, so every DrawList holds exactly one batch.
    inline constexpr uint16_t kSingleVariantId = 0;

    //! One replay: what a view establishes on the command list before its draws. Every
    //! DrawList of a pass covers the same draws — the view is the only difference.
    struct DrawList
    {
        RHI::Viewport              m_viewport;
        RHI::Scissor               m_scissor;
        const RHI::ShaderBindings* m_viewShaderBindings = nullptr;
    };

    //! One PSO variant's run within a DrawList. ExecuteIndirect cannot switch PSO within a
    //! call, so this grouping outlives the CPU submit path.
    //!
    //! m_submit* are positions in the pass's submit order — the same draws are replayed
    //! once per DrawList, so that order is (draw count x list count) long while m_draws
    //! holds only draw count entries. The two are related by
    //!     drawIndex = m_drawBegin + (submitIndex - m_submitBegin)
    struct DrawBatch
    {
        uint32_t                  m_submitBegin = 0;
        uint32_t                  m_submitEnd   = 0;
        uint32_t                  m_drawBegin   = 0;   //!< where the run starts in m_draws
        uint32_t                  m_listIndex   = 0;   //!< absolute index into m_drawLists
        uint16_t                  m_variantId   = kSingleVariantId;   //!< identity
        const RHI::PipelineState* m_pso         = nullptr;            //!< resolved from it
    };

    //! Pass component: that pass's span of m_drawBatches plus the length of its submit order.
    //! Written by BuildDrawTables, read by BuildExecuteGroups / BuildExecuteWorks. Declared
    //! here rather than in PassComponents.h because its indices mean nothing without the
    //! executer's m_drawBatches.
    //!
    //! Frame-scoped, and Execute::End MUST clear it: the arenas it indexes are rebuilt every
    //! frame, so a table left over from a frame in which the pass had draws would point into
    //! another pass's batches on a frame in which it has none.
    struct PassDrawTable
    {
        uint32_t m_batchBegin  = 0;
        uint32_t m_batchEnd    = 0;
        uint32_t m_submitCount = 0;   //!< also the load figure the splitters key on
    };

    //! How many draws an ExecuteGroup should carry before the next pass starts a new group —
    //! and so how much gets recorded between one submission and the next. Too small and
    //! CommandList creation plus submission dominates; too large and the GPU waits on the CPU
    //! to finish recording. Not measured yet — this is the knob to turn when it is.
    inline constexpr uint32_t kMaxSubmitsPerCommandList = 512;

    //! 一个 CommandList 的录制内容:若干 (pass, 该 pass 提交序上的一段) 按序录制。
    //!
    //! An Item is a LOAD slice, not a logical unit: its range may span several batches and
    //! several DrawLists. Recording walks the batches it covers and re-establishes state at
    //! each boundary, which is why the batches live in the executer's frame table rather
    //! than on the item.
    struct ExecuteWork
    {
        struct Item
        {
            Pass     m_pass       {NullPass};
            uint32_t m_submitBegin = 0;   //!< this slice, in the pass's submit order
            uint32_t m_submitEnd   = 0;
            //! The pass's span of m_drawBatches. Submit order restarts at 0 per pass, so
            //! the walk cannot be bounded by m_submitEnd alone — it would run on into the
            //! next pass's batches, whose m_submitBegin starts over at 0.
            uint32_t m_firstBatch  = 0;
            uint32_t m_batchEnd    = 0;
            uint32_t m_itemIndex   = 0;   //!< which slice of its pass this is
            uint32_t m_itemCount   = 1;
        };
        eastl::vector<Item> m_items;
        RHI::CommandList*   m_commandList = nullptr;

        //! Recording cursor — the state-homogeneous run the execute hook is currently being
        //! asked to submit. m_drawHandles is already sliced to that run, and m_submitBase is
        //! the submit index its first entry must be submitted with (what
        //! CommandList::ValidateSubmitIndex checks against SetSubmitRange).
        Pass                              m_pass       {NullPass};
        eastl::span<const RHI::RHIHandle> m_drawHandles;
        uint32_t                          m_submitBase = 0;
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

        void BuildExecuteTable(const QueueBasedPasses& queueBasedPasses, PassContext& passContext);

        QueueSegments& GetQueueSegments() { return m_queueSegments; }

        //! Graphics queue only: the Compute and Copy command list types have no draw at
        //! all, so their passes get a single empty item from BuildExecuteWorks instead.
        void BuildDrawTables(const QueueBasedPasses& queueBasedPasses, PassContext& passContext);

        //! One pass: collect its draws, then one DrawList per ready view. Stamps the pass
        //! with a PassDrawTable if any batch came out of it.
        void BuildPassDrawTable(Pass pass, PassContext& passContext, RHIContext& rhiContext);

        //! Split one DrawList's draws into DrawBatches by PSO variant. Returns the new end
        //! of the pass's submit order.
        uint32_t BuildDrawBatches(
            Pass pass, const PassContext& passContext,
            uint32_t listIndex, uint32_t drawBegin, uint32_t drawCount, uint32_t submitBegin);

        void BuildSegments(uint32_t queueIndex, eastl::span<const Pass> queuePasses, const PassContext& passContext);

        void BuildExecuteGroups(QueueSegment& segment, const PassContext& passContext) const;

        void BuildExecuteWorks(ExecuteGroup& group, const PassContext& passContext) const;

        const RHI::PipelineState* ExecuteBindPSO(RHI::CommandList* commandList, Pass pass, PassContext& passContext);

        void ExecuteBindShared(RHI::CommandList* commandList, Pass pass, PassContext& passContext);

        //! Per-DrawList state: the view's viewport / scissor and its per-view bindings.
        void ExecuteDrawListState(RHI::CommandList* commandList, const DrawList& list);

        void ExecutePreBarriers(RHI::CommandList* commandList, Pass pass, PassContext& passContext);

        void ExecutePostBarriers(RHI::CommandList* commandList, Pass pass, PassContext& passContext);

        void ExecuteBeginRenderPass(RHI::CommandList* commandList, Pass pass, PassContext& passContext);

        void ExecuteEndRenderPass(RHI::CommandList* commandList, Pass pass, PassContext& passContext);

        void Execute(ExecuteWork& work, RHI::Factory& factory, RHI::Device& device, RHI::HardwareQueueClass queueClass, PassContext& passContext);

        void ExecuteFinalBarriers(RHI::CommandList* commandList);

        void ExecuteStaticPreBarriers(RHI::CommandList* commandList, uint32_t queueIndex);

        void SetStaticPreBarriers(StaticPreBarrierTable&& table) { m_staticPreBarriers = eastl::move(table); }

        QueueSegments m_queueSegments;
        StaticPreBarrierTable m_staticPreBarriers;

        eastl::vector<RHI::RHIHandle> m_draws;
        eastl::vector<DrawList>       m_drawLists;
        eastl::vector<DrawBatch>      m_drawBatches;

        uint32_t m_frameIndex { 0 };
    };
}