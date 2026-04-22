#pragma once

namespace Spark::Render
{
    class RenderUIInterface
    {
    public:
        virtual ~RenderUIInterface() = default;

        virtual void NewFrame() = 0;  // Called when frame begin
    };
}