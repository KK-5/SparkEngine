#pragma once

#include <Base.h>
#include <Math/Vector2.h>
#include <RHI/Context/RHIContext.h>

namespace Spark::RHI
{
    class PipelineLayoutDescriptor;
}

namespace Spark::Render
{
    //! Reconciles view entities (View + its own space1 ShaderBindings entity) against the
    //! world's cameras every frame: find-or-create per camera, refresh, reap the orphans.
    //!
    //! Not an ISystem: a plain helper owned by RenderSystem and driven from
    //! RenderSystem::OnTick, sequenced BEFORE DrawItem derivation / pass execution so the
    //! bindings are materialized and up to date before any draw references them.
    class ViewBindingSystem
    {
    public:
        void Init();
        void Update(const Math::Vector2Int& renderSize);
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        //! NullHandle if the bindings could not be created.
        RHI::RHIHandle CreateView(RHI::RHIContext& rhiCtx);
        void DestroyView(RHI::RHIContext& rhiCtx, RHI::RHIHandle view);

        Ptr<RHI::PipelineLayoutDescriptor> m_viewLayout;
    };
}
