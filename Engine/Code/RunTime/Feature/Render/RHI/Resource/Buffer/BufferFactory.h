#include "BufferFactoryDescriptor.h"
#include "Buffer.h"

namespace Spark
{
    //using namespace Spark::RHI;

    template <>
    class IObjectFactory<Render::RHI::Buffer>
    {
    public:
        void Init(const Render::RHI::BufferFactoryDescriptor& descriptor) {}

        void Shutdown();

        template <typename... Args>
        Render::RHI::Buffer* Allocate(Args&&...)
        {
            return nullptr;
        }
    };
}