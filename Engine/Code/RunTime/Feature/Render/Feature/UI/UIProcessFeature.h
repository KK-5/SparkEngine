#pragma once

namespace Spark::Render
{
    //! Registers icon textures with ImGui once their RHI resources have materialised.
    class UIProcessFeature final
    {
    public:
        void Process();
    };
}
