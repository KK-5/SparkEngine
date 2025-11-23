#pragma once

#include <ECS/Entity.h>

namespace Spark
{
    /// @brief The Hierarchy component is used to represent the hierarchical information of an Entity. 
    /// It records the Entity's parent node, previous sibling node, next sibling node, and first child node. 
    /// If you want to directly add this component to an Entity, you must ensure that the information within 
    /// it is correct (for example, sibling nodes must share the same parent node as this node). 
    /// 
    /// If you provide incorrect information, this information will be reported to the Scene for ignoring, 
    /// but the component will not be modified or removed.
    ///
    /// It is recommended to use the interfaces provided in IScene, as they automatically manage 
    /// the Hierarchy component.
    struct Hierarchy
    {
        Entity parent {NullEntity};
        Entity firstChild {NullEntity};
        Entity prevSibling {NullEntity};
        Entity nextSibling {NullEntity};
    };
    
}