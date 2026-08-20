#pragma once

#include <Base.h>
#include <Math/Vector3.h>
#include <Math/Matrix4x4.h>
#include <Tick/TickBus.h>
#include <Pass/Pass.h>
#include <RHI/Context/RHIHandle.h>

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
        void CreateVertexBuffer();
        void CreateView();
        void CreatePasses();
        void Update();
        void BuildGeometry();

        // Vertex buffer (in RHIContext)
        Spark::RHI::RHIHandle m_vertexBuffer     = Spark::RHI::NullHandle;

        Spark::RHI::RHIHandle m_drawable = Spark::RHI::NullHandle;

        Spark::RHI::RHIHandle m_view = Spark::RHI::NullHandle;

        // Shader assets
        Ptr<Spark::Resource::ShaderAsset> m_shader;

        // Per-frame CPU data computed in Update(), consumed by the pass Compile hook.
        Spark::Math::Matrix4X4 m_matrix;
        Spark::Math::Vector3   m_colors[3];

        // Transform
        float m_rotationAngle = 0.f;
        float m_colorPhase = 0.f;
    };
}
