#pragma once

namespace Spark::Render
{
    class RenderUIInterface
    {
    public:
        virtual ~RenderUIInterface() = 0;

        virtual void NewFrame() = 0;  // Called when frame begin
    };
}