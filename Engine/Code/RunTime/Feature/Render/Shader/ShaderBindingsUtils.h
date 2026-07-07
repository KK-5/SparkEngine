#pragma once

#include <Log/ILogSystem.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>
#include <RHI/Resource/Sampler/SamplerState.h>

#include <Request/DrawRequest.h>

namespace Spark::Render
{
    //! Declarative data-injection helpers for ShaderBindings ENTITIES. Each takes a
    //! binding entity (RHIHandle), an InputName, and the data; it resolves the
    //! RHI::ShaderBindings from the entity in the current RHIContext, stages the
    //! data into the named input, and marks the binding dirty so the next
    //! CompileShaderInputs recompiles its descriptor. Callers never touch the RHI
    //! ShaderBindings object directly.

    namespace Detail
    {
        inline RHI::ShaderBindings* ResolveShaderBindings(RHI::RHIContext& ctx, RHI::RHIHandle bindings)
        {
            auto* comp = ctx.TryGet<RHI::Components::ShaderBindings>(bindings);
            if (!comp || !comp->m_bindings)
            {
                LOG_ERROR("[ShaderBindingsUtils] Entity has no Components::ShaderBindings (or it is null).");
                return nullptr;
            }
            return comp->m_bindings.get();
        }

        inline void MarkShaderBindingsDirty(RHI::RHIContext& ctx, RHI::RHIHandle bindings)
        {
            if (!ctx.Has<RHI::ShaderBindingsUpdateTag>(bindings))
            {
                ctx.Add<RHI::ShaderBindingsUpdateTag>(bindings);
            }
        }
    }

    //! Stage a constant value — any trivially-copyable POD (matrix / vector /
    //! scalar) — into the named constant input, then mark the binding dirty.
    template<typename T>
    void SetShaderConstant(RHI::RHIHandle bindings, RHI::InputName input, const T& value)
    {
        auto& ctx = *RHI::RHIExecuteContext::Current();
        auto* sb = Detail::ResolveShaderBindings(ctx, bindings);
        if (!sb)
        {
            return;
        }
        auto* constant = sb->FindConstantInput(input);
        if (!constant)
        {
            LOG_ERROR("[ShaderBindingsUtils] Constant input '{}' not found.", input.GetCStr());
            return;
        }
        if (!constant->SetData(&value, static_cast<uint32_t>(sizeof(T))))
        {
            LOG_ERROR("[ShaderBindingsUtils] SetData size mismatch for constant '{}'.", input.GetCStr());
            return;
        }
        Detail::MarkShaderBindingsDirty(ctx, bindings);
    }

    //! Bind an image view into the named image input, then mark the binding dirty.
    inline void SetShaderImage(
        RHI::RHIHandle bindings, RHI::InputName input, const RHI::ImageView* view, uint32_t arrayIndex = 0)
    {
        auto& ctx = *RHI::RHIExecuteContext::Current();
        auto* sb = Detail::ResolveShaderBindings(ctx, bindings);
        if (!sb)
        {
            return;
        }
        auto* image = sb->FindImageInput(input);
        if (!image)
        {
            LOG_ERROR("[ShaderBindingsUtils] Image input '{}' not found.", input.GetCStr());
            return;
        }
        // Change-detection: an unchanged view must not re-dirty the SRG, which would
        // force a redundant descriptor recompile every frame. First bind (null → view)
        // and genuine swaps still fall through and mark dirty.
        if (image->GetView(arrayIndex).get() == view)
        {
            return;
        }
        image->SetView(arrayIndex, view);
        Detail::MarkShaderBindingsDirty(ctx, bindings);
    }

    //! Bind a buffer view into the named buffer input, then mark the binding dirty.
    inline void SetShaderBuffer(
        RHI::RHIHandle bindings, RHI::InputName input, const RHI::BufferView* view, uint32_t arrayIndex = 0)
    {
        auto& ctx = *RHI::RHIExecuteContext::Current();
        auto* sb = Detail::ResolveShaderBindings(ctx, bindings);
        if (!sb)
        {
            return;
        }
        auto* buffer = sb->FindBufferInput(input);
        if (!buffer)
        {
            LOG_ERROR("[ShaderBindingsUtils] Buffer input '{}' not found.", input.GetCStr());
            return;
        }
        buffer->SetView(arrayIndex, view);
        Detail::MarkShaderBindingsDirty(ctx, bindings);
    }

    //! Bind a sampler state into the named sampler input, then mark the binding dirty.
    inline void SetShaderSampler(
        RHI::RHIHandle bindings, RHI::InputName input, const RHI::SamplerState& state, uint32_t arrayIndex = 0)
    {
        auto& ctx = *RHI::RHIExecuteContext::Current();
        auto* sb = Detail::ResolveShaderBindings(ctx, bindings);
        if (!sb)
        {
            return;
        }
        auto* sampler = sb->FindSamplerInput(input);
        if (!sampler)
        {
            LOG_ERROR("[ShaderBindingsUtils] Sampler input '{}' not found.", input.GetCStr());
            return;
        }
        sampler->SetState(arrayIndex, state);
        Detail::MarkShaderBindingsDirty(ctx, bindings);
    }

    //! Append every ShaderBindings ENTITY tagged BindingTag into req.m_shaderBindings,
    //! returning how many were added. BindingTag is the binding's frequency / ownership
    //! tag — MainViewTag for the shared per-view SRG, a PassTag for a pass's own
    //! sampling SRG(s). All matches are appended (a pass may own several per-space
    //! SRGs); bindings self-describe their HLSL space (GetSpaceId), so append order is
    //! irrelevant. Per-object bindings are NOT handled here — they ride on the
    //! Drawable's m_instanceData and are resolved by CompileDrawRequests.
    //!
    //! The return count lets callers gate on readiness (skip the draw until the view
    //! SRG exists) without a separate lookup. Caller clears req.m_shaderBindings once
    //! before the first append of the frame.
    template<typename BindingTag>
    size_t AddShaderBindings(DrawRequest& req, RHI::RHIContext& ctx)
    {
        size_t added = 0;
        for (auto [entity, comp] : ctx.GetView<BindingTag, RHI::Components::ShaderBindings>().each())
        {
            req.m_shaderBindings.push_back(entity);
            ++added;
        }
        return added;
    }
}
