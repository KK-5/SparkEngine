#pragma once

#include <Math/Vector2.h>
#include <RHI/Context/RHIContext.h>

namespace Spark::Render
{
    //! Per-view frequency system. Builds ONE shared ViewBindings (space1) entity —
    //! its layout reflected straight from ViewBindings.hlsl — tags it MainViewTag,
    //! and refreshes g_ViewProjection from the camera each frame. Consumers fetch
    //! the single shared binding via GetView<MainViewTag, Components::ShaderBindings>,
    //! so every pass's draws can reference one binding instead of each building its own.
    //!
    //! Not an ISystem: a plain helper owned by RenderSystem and driven from
    //! RenderSystem::OnTick, sequenced BEFORE DrawItem derivation / pass execution so the
    //! binding is materialized and up to date before any draw references it.
    //!
    //! Holds no Ptr to the bindings/layout: the binding entity owns the
    //! ShaderBindings (which in turn owns its layout), so m_viewEntity is the only
    //! handle we need. Data is pushed/read through the entity via ShaderBindingsUtils.
    class ViewBindingSystem
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        void Update(const Math::Vector2Int& renderSize);
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        RHI::RHIHandle m_viewEntity = RHI::NullHandle;
    };
}
