#pragma once

namespace Spark::Render
{
    class PassContext;
    class RenderUI;

    //! Draws the UI over the tonemapped swap chain. It sets no shader: its .Execute records
    //! the whole draw through RenderUI, state included.
    struct UIPass
    {
        static void SetUp(PassContext& ctx, RenderUI& renderUI);
    };
}
