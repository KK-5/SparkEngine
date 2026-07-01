#pragma once

#include <Math/Vector2.h>
#include <RHI/Context/RHIHandle.h>

namespace Spark::Render
{
    //! Feeds the SkyboxPass a single full-screen DrawItem (Path B: no Drawable /
    //! DrawRequest). Each frame it finds the ready environment cube (published by
    //! SkyboxSystem as Skybox::SkyboxGPUComponent on a world entity — read directly, the
    //! same way InstanceBindingSystem reads Mesh::MeshGPUComponent), binds its cube SRV +
    //! sampler into this pass's space1 bindings, and rebuilds the DrawItem (DrawLinear(3),
    //! view space0 + cube space1, viewport sized to the frame) on a dedicated
    //! SkyboxPassTag entity. No cube ready -> no DrawItem -> the clear color shows.
    class SkyboxProcessor final
    {
    public:
        void Init();
        void Shutdown();
        void Process(const Math::Vector2Int& renderSize);

    private:
        RHI::RHIHandle m_cubeBindings = RHI::NullHandle; //!< space1 SRG (g_SkyCube + g_SkySampler)
        RHI::RHIHandle m_drawEntity   = RHI::NullHandle; //!< holds SkyboxPassTag + the full-screen DrawItem
    };
}
