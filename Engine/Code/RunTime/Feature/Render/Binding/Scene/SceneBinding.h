#pragma once

#include <RHI/Resource/AccessFlags.h>
#include <Scene/SceneTextureComponents.h>

#include <Pass/TagList.h>

namespace Spark::Render
{
    //! This frame's g_Lights / g_ShadowViews / g_ViewShadowMaps buffer, on the staging slot it
    //! was written into. The slots rotate, so each tag follows the one the frame writes.
    struct LightBufferTag
    {
    };

    //! CPU mirror of g_LightCount for passes that size their dispatch by scene lights.
    struct SceneLightCount
    {
        uint32_t m_count = 0;
    };


    struct ShadowViewBufferTag
    {
    };

    struct ViewShadowMapBufferTag
    {
    };
}
