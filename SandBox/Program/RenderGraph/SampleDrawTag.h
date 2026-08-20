#pragma once

namespace Spark::SandBox
{
    //! Classification tag for a sample's drawable, declared via .Accepts<SampleDrawTag>().
    //!
    //! A sample must NOT reuse its own PassTag as the draw tag: DrawItemRouter stamps the
    //! PassTag onto the drawable entity, which already carries the draw tag, and entt
    //! asserts on emplacing a component an entity already owns.
    struct SampleDrawTag {};
}
