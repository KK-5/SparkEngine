#pragma once

#include <cstdint>

namespace Spark
{
    //! One frame's clock, computed once by the engine loop and broadcast with OnTick.
    //!
    //! Game time is what the world lives in: it is clamped against hitches and will stop
    //! under pause and stretch under time scale. Real time is the wall clock and never
    //! does either — use it for what must keep responding while the game is paused (editor
    //! camera, UI). Times accumulate in double; narrow to float only at the GPU boundary.
    struct FrameTime
    {
        //! Advances every tick, paused or not.
        uint64_t m_frameNumber = 0;

        float  m_deltaTime    = 0.0f;
        double m_gameTime     = 0.0;
        double m_prevGameTime = 0.0;

        float  m_realDeltaTime = 0.0f;
        double m_realTime      = 0.0;
        double m_prevRealTime  = 0.0;
    };
}
