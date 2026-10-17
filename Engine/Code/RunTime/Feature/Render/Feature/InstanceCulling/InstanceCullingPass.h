#pragma once

namespace Spark::Render
{
    class PassContext;

    //! Writes the instances each view sees (InstanceLists): two passes, one per kind of object
    //! a pass selects, the opaque instances for the main views and the shadow casters for the
    //! shadow views. A frustum test is all the culling there is yet.
    //!
    //! Each pass creates its pair of buffers every frame, also in one where no instance has a
    //! slot or no view draws the list: the count is then 0.
    struct InstanceCullingPass
    {
        static void SetUp(PassContext& ctx);
    };
}
