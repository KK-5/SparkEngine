#pragma once

#include <EASTL/array.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/functional.h>
#include <EASTL/span.h>
#include <EASTL/type_traits.h>
#include <EASTL/vector.h>

#include <RHI/Component/Component.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/Context/RHIEventContext.h>
#include <RHI/HardwareQueue.h>
#include <RHI/RHILimits.h>
#include <RHI/Resource/Buffer/IndexBufferView.h>
#include <RHI/Resource/Sampler/SamplerState.h>
#include <RHI/Resource/ShaderInput/ShaderInputDescriptor.h>
#include <Pass/Pass.h>
#include <Pass/Component/RHIComponents.h>

namespace Spark::RHI
{
    class Fence;
    class PipelineState;
    class ShaderBindings;
}

namespace Spark::Render
{
    struct ExecuteWork;
    class RenderGraphExecuter;

    //! An ordered stretch of a pass: within it items do not depend on each other and each
    //! resource is used one way. A per-frame entity in RHIContext, where all its attachments
    //! and items live.
    struct Scope
    {
        Pass                    m_pass {NullPass};
        uint32_t                m_index {0};   //!< order within m_pass
        RHI::HardwareQueueClass m_queue {RHI::HardwareQueueClass::Graphics};
        uint32_t                m_order {0};   //!< position in the stream, set by SortScopes
    };

    //! On every attachment, naming the Scope it belongs to. Links point from attachment to
    //! Scope; a Scope holds no attachment list. Items get a component of their own: nothing
    //! ever walks attachments and items together.
    struct ScopeAttachment
    {
        RHI::RHIHandle m_scope {RHI::NullHandle};
        //! Only groups one resource's attachments within the Scope; the value means nothing.
        RHI::RHIHandle m_resource {RHI::NullHandle};
        uint32_t       m_scopeOrder {0};   //!< the Scope's m_order, copied by SortScopes
    };

    //! On every Scope: where its attachments sit in the ScopeAttachment storage's packed array,
    //! contiguous since SortScopes, which records it. Within the range only one resource's
    //! attachments being adjacent is guaranteed, not their order.
    struct ScopeAttachmentRange
    {
        uint32_t m_begin = 0;
        uint32_t m_end   = 0;
    };

    //! The Scope's attachments. Valid from SortScopes to the end of the frame.
    inline eastl::span<const RHI::RHIHandle> GetScopeAttachments(RHI::RHIContext& context, RHI::RHIHandle scope)
    {
        const ScopeAttachmentRange& range = context.Get<ScopeAttachmentRange>(scope);
        return { context.GetStorage<ScopeAttachment>().data() + range.m_begin, range.m_end - range.m_begin };
    }

    //! On every item a Scope declared itself (a Draw), naming that Scope. The item entity
    //! lives for the frame, like the Scope.
    struct ScopeItem
    {
        RHI::RHIHandle m_scope {RHI::NullHandle};
        uint32_t       m_scopeOrder {0};   //!< the Scope's m_order, copied by SortScopes
    };

    //! On every Scope: where its items sit in the ScopeItem storage's packed array, contiguous
    //! since SortScopes, which records it.
    struct ScopeItemRange
    {
        uint32_t m_begin = 0;
        uint32_t m_end   = 0;
    };

    //! The items the Scope declared. Valid from SortScopes to the end of the frame.
    inline eastl::span<const RHI::RHIHandle> GetScopeItems(RHI::RHIContext& context, RHI::RHIHandle scope)
    {
        const ScopeItemRange& range = context.Get<ScopeItemRange>(scope);
        return { context.GetStorage<ScopeItem>().data() + range.m_begin, range.m_end - range.m_begin };
    }

    //! On an item that reads its arguments from a buffer of the graph (a DispatchIndirect, a
    //! DrawIndirect): the access of its Scope that reads that buffer. The buffer has no backing
    //! when the item is declared, so lowering puts it into the item
    //! (CompileItemIndirectArguments).
    struct ItemIndirectArguments
    {
        RHI::RHIHandle m_attachment {RHI::NullHandle};
        //! A draw's access of the buffer that says how many records to execute. None for a
        //! draw whose groups say exactly how many.
        RHI::RHIHandle m_countAttachment {RHI::NullHandle};
    };

    //! On a draw of a Scope that reads its vertices, and its indices, from buffers of the graph:
    //! the Scope's InputAssembly accesses of them, the vertex ones by input slot. The buffers have
    //! no backing when the draw is declared, so lowering binds them (CompileItemInputAssembly),
    //! whole, with the stride the pass's input layout gives each slot.
    struct ItemInputAssembly
    {
        eastl::fixed_vector<RHI::RHIHandle, RHI::Limits::Pipeline::StreamCountMax> m_vertexAttachments;
        RHI::RHIHandle   m_indexAttachment {RHI::NullHandle};
        RHI::IndexFormat m_indexFormat {RHI::IndexFormat::Unknown};
    };

    //! On an item lowering made for one view out of an item of a Scope whose indirect
    //! arguments are held per view (IndirectArgumentsOfView): it reads that view's. It lives
    //! for the frame.
    struct PerViewItemTag {};

    //! On such an item drawing one DrawGroup: the group's PSO, which the executer switches to for it.
    //! RenderGraphCompiler's DrawPipelineCache owns it, and outlives the frame.
    struct ItemPipelineState
    {
        const RHI::PipelineState* m_pso = nullptr;
    };

    //! On a Scope that selects sets of scene items (.Accepts): per set, the query that appends
    //! its members under `view`. The items are persistent and carry no link to the Scope.
    struct ScopeSelections
    {
        using Collect = void (*)(RHI::RHIContext&, RHI::RHIHandle view, eastl::vector<RHI::RHIHandle>& submitList);

        eastl::fixed_vector<Collect, 4> m_collects;
    };

    //! An upload awaiting its acquire whose target carries a tag a Scope uses, with the
    //! access that tag says its resources are read with.
    struct UploadTake
    {
        RHI::RHIEvent    m_request {RHI::NullEvent};
        RHI::AccessFlags m_access  {RHI::AccessFlags::None};
    };

    using UploadTakeList = eastl::fixed_vector<UploadTake, 8, true>;

    //! On a Scope that reads resources from outside the graph by tag (.Uses): per tag, what
    //! picks the uploads into resources carrying it that await their acquire, whichever
    //! Scope acquires them. CompileScopeUploads decides which this Scope does.
    struct ScopeUses
    {
        using Take = void (*)(RHI::RHIContext&, RHI::RHIEventContext&, UploadTakeList& out);

        eastl::fixed_vector<Take, 4> m_takes;
    };

    //! On an upload awaiting its acquire (an RHIEventContext entity), naming the Scope that
    //! records the acquire: the transfer the request carries, its dst side set by then.
    struct ScopeUpload
    {
        RHI::RHIHandle m_scope {RHI::NullHandle};
        uint32_t       m_scopeOrder {0};   //!< the Scope's m_order
    };

    //! On every Scope: where the uploads it acquires sit in the ScopeUpload storage's packed
    //! array, contiguous since CompileScopeBarriers, which records it.
    struct ScopeUploadRange
    {
        uint32_t m_begin = 0;
        uint32_t m_end   = 0;
    };

    //! The uploads the Scope acquires. Valid from CompileScopeBarriers to the end of the frame.
    inline eastl::span<const RHI::RHIEvent> GetScopeUploads(
        RHI::RHIContext& context, RHI::RHIEventContext& events, RHI::RHIHandle scope)
    {
        const ScopeUploadRange& range = context.Get<ScopeUploadRange>(scope);
        return { events.GetStorage<ScopeUpload>().data() + range.m_begin, range.m_end - range.m_begin };
    }

    //! Declare one more tag on `scope`. One it declares already, from another source, is not
    //! added again.
    inline void AddScopeUse(RHI::RHIContext& context, RHI::RHIHandle scope, ScopeUses::Take take)
    {
        auto* component = context.TryGet<ScopeUses>(scope);
        auto& takes     = (component != nullptr ? *component : context.Add<ScopeUses>(scope)).m_takes;
        for (ScopeUses::Take existing : takes)
        {
            if (existing == take)
            {
                return;
            }
        }
        takes.push_back(take);
    }

    //! On a Scope whose access to some resource follows one on another queue: per source queue,
    //! the latest producer Scope it must wait for, and the fence and value that works out to.
    //! CompileScopeBarriers fills m_producer, CompileScopeSync the syncs in stream order.
    //! A null fence means no wait on that queue — none needed, or an earlier wait covers it.
    static_assert(RHI::HardwareQueueClassCount == 3, "ScopeWait's initializer lists one producer per queue.");

    struct ScopeWait
    {
        RHI::RHIHandle   m_producer[RHI::HardwareQueueClassCount] { RHI::NullHandle, RHI::NullHandle, RHI::NullHandle };
        PendingSync m_sync[RHI::HardwareQueueClassCount] {};
    };

    //! On a Scope another queue waits for: its queue's cross-queue fence and next value, assigned
    //! by CompileScopeSync in stream order so each queue signals monotonically.
    struct ScopeSignal
    {
        RHI::Fence* m_fence = nullptr;
        uint64_t    m_value = 0;
    };

    //! Samplers a Scope declared (.Sampler), for its pass's per-pass bindings.
    struct ScopeSampler
    {
        RHI::InputName    m_input;
        RHI::SamplerState m_state;
    };

    struct ScopeSamplers
    {
        eastl::fixed_vector<ScopeSampler, 4> m_samplers;
    };

    //! Constants a Scope declared (.Constant), as the bytes the caller passed.
    struct ScopeConstant
    {
        static constexpr uint32_t ByteCountMax = 64;

        RHI::InputName                     m_input;
        uint32_t                           m_byteCount = 0;
        eastl::array<uint8_t, ByteCountMax> m_bytes {};
    };

    struct ScopeConstants
    {
        eastl::fixed_vector<ScopeConstant, 8> m_constants;
    };

    //! On every Scope of a pass whose shaders declare root constants: the whole block, zeroed
    //! at OpenScope and set whole when the Scope is recorded, so no Scope inherits another's.
    //! .Constant fills it at declaration, .BindIndex at lowering.
    struct ScopeRootConstants
    {
        static constexpr uint32_t NoViewIndex = ~0u;

        eastl::array<uint8_t, RHI::Limits::Pipeline::RootConstantByteCountMax> m_bytes {};
        uint32_t m_byteCount     = 0;
        uint32_t m_writtenDwords = 0;   //!< bit i: DWORD i is set, so no field is set twice

        //! Byte offset of `viewIndex` in a pass that renders views: the executer rewrites just
        //! that field at each view handle. NoViewIndex when the shader reads no view.
        uint32_t m_viewIndexOffset = NoViewIndex;

        static_assert(RHI::Limits::Pipeline::RootConstantByteCountMax / 4 <= 32, "m_writtenDwords has a bit per DWORD.");
    };

    //! The part of a Scope's submit state its pass decides: the PSO and the bindings bound once
    //! for the whole Scope (the pass's own space2 and those it declared via .Binds). Resolved by
    //! lowering. Viewport and viewIndex come from the view handles in the Scope's submit range.
    struct ScopeState
    {
        const RHI::PipelineState*  m_pso = nullptr;
        const RHI::ShaderBindings* m_bindings[RHI::Limits::Pipeline::ShaderInputGroupCountMax] {};
        uint8_t                    m_bindingCount = 0;
    };

    //! The Scope's stretch of the executer's submit list: per ready view, the view's handle and
    //! then the items submitted under it; a viewless Scope has items only. A view gets its
    //! handle even when no item follows — nothing assumes the CPU knows how many there are.
    struct ScopeSubmitRange
    {
        uint32_t m_begin = 0;
        uint32_t m_end   = 0;
    };

    //! On a Scope whose work is opaque: the executer hands its submit range to this hook once
    //! per view segment (empty ones included), or once if the range is empty, instead of
    //! submitting the items itself. Points into the pass's PassFunctions.
    struct ScopeExecute
    {
        const eastl::function<void(ExecuteWork&, RenderGraphExecuter&)>* m_execute = nullptr;
    };

    //! On a Scope that starts a new CommandList. Nothing adds it yet.
    struct WorkStartTag {};

    // Lowering sorts these storages, which entt refuses on an in_place_delete storage holding
    // tombstones — a trivially movable type keeps the default swap-and-pop policy.
    static_assert(eastl::is_trivially_copyable_v<Scope>);
    static_assert(eastl::is_trivially_copyable_v<ScopeAttachment>);
    static_assert(eastl::is_trivially_copyable_v<ScopeItem>);
    static_assert(eastl::is_trivially_copyable_v<ScopeUpload>);
}
