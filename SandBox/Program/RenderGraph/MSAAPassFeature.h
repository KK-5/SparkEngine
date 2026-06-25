#pragma once

#include <Base.h>
#include <Tick/TickBus.h>
#include <Pass/Pass.h>
#include <RHI/Context/RHIHandle.h>
#include <RHI/Viewport/Viewport.h>
#include <RHI/Scissor/Scissor.h>

namespace Spark::RHI
{
    class ShaderBindings;
    class Fence;
}

namespace Spark::Resource
{
    class ShaderAsset;
}

namespace Spark::SandBox
{
    class MSAAPassFeature : public TickBus::Handler
    {
    public:
        MSAAPassFeature();
        ~MSAAPassFeature();

        bool Init();
        void Shutdown();

        // TickBus
        void OnTick(float deltaTime) override;
        unsigned int GetTickOrder() const override
        {
            return static_cast<unsigned int>(Spark::RenderSystemTickOrder) - 1;
        }

    private:
        void CreateViewBindings();
        void CreateVertexBuffer();
        void CreatePasses();
        void UpdateViewBindings();
        void BuildDrawRequest();

        // Per-pass ShaderBindings entity. Lives in RHIContext (CompileShaderInputs
        // discovers it, the entity owns the binding's Ptr); data is staged through
        // the entity via Render::SetShaderXxx, no local Ptr<> needed.
        Spark::RHI::RHIHandle m_viewBindingsEntity = Spark::RHI::NullHandle;

        // Vertex buffer (in RHIContext)
        Spark::RHI::RHIHandle m_vbEntity     = Spark::RHI::NullHandle;

        Spark::RHI::RHIHandle m_drawItemEntity = Spark::RHI::NullHandle;
        Spark::RHI::RHIHandle m_drawableEntity = Spark::RHI::NullHandle;

        // Shader assets
        Ptr<Spark::Resource::ShaderAsset> m_shader;

        Spark::RHI::Viewport m_viewport;
        Spark::RHI::Scissor  m_scissor;

        // Transform
        float m_rotationAngle = 0.f;
        float m_colorPhase = 0.f;
    };
}
