#include "Asset.h"

#include <Service/Service.h>
#include "AssetManagerInterface.h"


namespace Spark::Resource
{
    void Asset::SetDataReady(eastl::unique_ptr<AssetData> data)
    {
        m_data = eastl::move(data);
        // release 写：m_data 的写入对其它线程的 acquire 读可见
        m_status.store(m_data ? AssetStatus::Ready : AssetStatus::Error,
                       eastl::memory_order_release);
    }

    void Asset::Shutdown()
    {
        if (auto* manager = Service<AssetManager>::Get())
        {
            manager->ReleaseAsset(m_id);
        }
        delete this;
    }
}
