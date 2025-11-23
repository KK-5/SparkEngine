#pragma once

namespace Spark
{
    // from O3DE
    enum class TickOrder 
    {
        TICK_FIRST          = 0,       ///< First position in the tick handler order.

        TICK_PLACEMENT      = 50,      ///< Suggested tick handler position for components that need to be early in the tick order.

        TICK_INPUT          = 75,      ///< Suggested tick handler position for input components.

        TICK_GAME           = 80,      ///< Suggested tick handler for game-related components.

        TICK_ANIMATION      = 100,     ///< Suggested tick handler position for animation components.

        TICK_PHYSICS_SYSTEM = 200,     ///< Suggested tick handler position for physics systems. Note: This should only be used for the Physics System.

        TICK_PHYSICS        = TICK_PHYSICS_SYSTEM + 1,  ///< Suggested tick handler position for physics components

        TICK_ATTACHMENT     = 500,     ///< Suggested tick handler position for attachment components.

        TICK_PRE_RENDER     = 750,     ///< Suggested tick handler position to update render-related data.

        TICK_DEFAULT        = 1000,    ///< Default tick handler position when the handler is constructed.

        TICK_UI             = 2000,    ///< Suggested tick handler position for UI components.

        TICK_LAST           = 100000,  ///< Last position in the tick handler order.
    };

    inline constexpr TickOrder operator+(TickOrder e, unsigned int n) 
    {
        return static_cast<TickOrder>(static_cast<unsigned int>(e) + n);
    }

    inline static constexpr TickOrder RenderSystemTickOrder = TickOrder::TICK_DEFAULT;

    inline static constexpr TickOrder InputTickOrder        = TickOrder::TICK_INPUT;

    //inline static constexpr TickOrder EditorTickOrder       = TickOrder::TICK_UI + 1;
}