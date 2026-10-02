#include "SceneTextures.h"

#include <EASTL/algorithm.h>

#include <Math/Bit.h>

namespace Spark::Render::SceneTextures
{
    namespace HZB
    {
        const RHI::AttachmentId& ClosestName()
        {
            static const RHI::AttachmentId s_name("HZBClosest");
            return s_name;
        }

        const RHI::AttachmentId& FurthestName()
        {
            static const RHI::AttachmentId s_name("HZBFurthest");
            return s_name;
        }

        uint32_t MipCount(const Math::Vector2Int& renderSize)
        {
            const Math::Vector2Int size = MipSize(renderSize, 0);
            uint32_t count = 1;
            for (int32_t side = eastl::max(size.x, size.y); side > 1; side >>= 1)
            {
                ++count;
            }
            return count;
        }

        Math::Vector2Int MipSize(const Math::Vector2Int& renderSize, uint32_t mip)
        {
            // Half the render size, rounded up so an odd side keeps its last pixel column.
            const uint32_t width  = NextPowerOfTwo(static_cast<uint32_t>(eastl::max((renderSize.x + 1) / 2, 1)));
            const uint32_t height = NextPowerOfTwo(static_cast<uint32_t>(eastl::max((renderSize.y + 1) / 2, 1)));
            return Math::Vector2Int(
                static_cast<int32_t>(eastl::max(width >> mip, 1u)),
                static_cast<int32_t>(eastl::max(height >> mip, 1u)));
        }

        Math::Vector2 UvFactor(const Math::Vector2Int& renderSize)
        {
            const Math::Vector2Int size = MipSize(renderSize, 0);
            return Math::Vector2(
                static_cast<float>(renderSize.x) / static_cast<float>(size.x * 2),
                static_cast<float>(renderSize.y) / static_cast<float>(size.y * 2));
        }
    }
}
