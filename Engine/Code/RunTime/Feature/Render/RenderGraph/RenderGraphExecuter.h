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
    //! What the command list must have established before a batch's items are submitted.
    //! Pass-level (PSO, space0 / 2 / 4) and view-level (space1, viewport) merged into one
    //! dimension, on the strength of the one property they share: idempotence. That is what
    //! lets a Work boundary fall anywhere — a fresh CommandList re-applies and is caught up.
    //! Barriers and BeginRenderPass are not idempotent (a transition; a loadOp clear), so
    //! they stay pass-level, and splitting a render pass needs suspend / resume instead.
    //!
    //! Empty means "don't care", never "clear" — so a copy pass's empty state can sit between
    //! two populated ones. The convention's failure mode is a redundant call, never a missed one.
    struct SubmitState
    {
        const RHI::PipelineState* m_pso = nullptr;

        //! Bounded by the register-space count, so inline. Raw array, not fixed_vector:
        //! filled in one go, never push_back'd.
        const RHI::ShaderBindings* m_bindings[RHI::Limits::Pipeline::ShaderInputGroupCountMax] {};
        uint8_t                    m_bindingCount = 0;

        RHI::Viewport m_viewport;               //!< graphics only
        RHI::Scissor  m_scissor;
        bool          m_hasViewport = false;
    };

    //! One state-homogeneous run of the arena. ExecuteIndirect cannot switch PSO within a
    //! call, so this grouping outlives the CPU submit path.
    //!
    //! m_submit* index m_submitItems directly — the arena IS the submit order, each view's
    //! replay occupying its own stretch. That is also what makes per-view culling and ordering
    //! expressible: they give each view a different item set, which a shared stretch cannot hold.
    //!
    //! The state is inlined rather than indexed: with pass- and view-level merged, no two
    //! batches can share one, so the mapping is strictly 1:1.
    struct SubmitBatch
    {
        uint32_t    m_submitBegin = 0;
        uint32_t    m_submitEnd   = 0;
        SubmitState m_state;
    };

    //! Pass component: where that pass's batches start, plus its stretch of the arena.
    //! Declared here rather than in PassComponents.h because its indices mean nothing
    //! without the executer's arenas.
    //!
    //! Frame-scoped, and Execute::End MUST clear it: the arenas it indexes are rebuilt every
    //! frame, so a stale table would point into another pass's batches.
    struct PassSubmitTable
    {
        uint32_t m_batchBegin  = 0;   //!< the pass's span of m_submitBatches
        uint32_t m_batchEnd    = 0;
        uint32_t m_submitBegin = 0;   //!< the pass's stretch of m_submitItems;
        uint32_t m_submitEnd   = 0;   //!< its length is the load figure the splitters key on
    };

    //! How many draws an ExecuteGroup should carry before the next pass starts a new group —
    //! and so how much gets recorded between one submission and the next. Too small and
    //! CommandList creation plus submission dominates; too large and the GPU waits on the CPU
    //! to finish recording. Not measured yet — this is the knob to turn when it is.
    inline constexpr uint32_t kMaxSubmitsPerCommandList = 512;

    //! 一个 pass 在一个 CommandList 里的录制内容。work 引用的是它而不是「一段 submit item」,
    //! 因为 GPU 工作是按 pass 组织的:零 item 的 pass 照样要清屏、照样要转换资源。
    //!
    //! A LOAD division: its range may span several batches, and recording re-establishes
    //! state at each boundary it crosses.
    struct ExecuteWorkItem
    {
        Pass     m_pass       {NullPass};
        uint32_t m_submitBegin = 0;   //!< this portion's stretch of m_submitItems
        uint32_t m_submitEnd   = 0;
        uint32_t m_firstBatch  = 0;   //!< the batches it covers
        uint32_t m_batchEnd    = 0;
        //! Which ExecuteWorkItem of its pass this is — NOT an index into m_submitItems.
        //! Fixed at 0 / 1 until RenderPassBeginInfo carries suspend / resume.
        uint32_t m_itemIndex   = 0;
        uint32_t m_itemCount   = 1;
    };

    //! 一个 CommandList 的录制内容:若干 ExecuteWorkItem 按序录制。
    struct ExecuteWork
    {
        eastl::vector<ExecuteWorkItem> m_items;
        RHI::CommandList*              m_commandList = nullptr;

        //! Recording cursor — the state-homogeneous run the execute hook is currently being
        //! asked to submit. m_itemHandles is already sliced to that run, and m_submitBase is
        //! the submit index its first entry must be submitted with (what
        //! CommandList::ValidateSubmitIndex checks against SetSubmitRange).
        eastl::span<const RHI::RHIHandle> m_itemHandles;
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

        void BuildSubmitTables(const QueueBasedPasses& queueBasedPasses, PassContext& passContext);

        //! One pass: per ready view — or once, for a viewless copy / compute pass — that
        //! stretch of the arena plus the state it is submitted under. Stamps the pass with a
        //! PassSubmitTable if any batch came out.
        void BuildPassSubmitTable(Pass pass, PassContext& passContext, RHIContext& rhiContext);

        //! Split one view's stretch of the arena into SubmitBatches by PSO variant.
        void BuildSubmitBatches(const SubmitState& state, uint32_t submitBegin, uint32_t submitEnd);

        void BuildSegments(uint32_t queueIndex, eastl::span<const Pass> queuePasses, const PassContext& passContext);

        void BuildExecuteGroups(QueueSegment& segment, const PassContext& passContext) const;

        void BuildExecuteWorks(ExecuteGroup& group, const PassContext& passContext) const;

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

        eastl::vector<RHI::RHIHandle> m_submitItems;
        eastl::vector<SubmitBatch>    m_submitBatches;

        uint32_t m_frameIndex { 0 };
    };
}