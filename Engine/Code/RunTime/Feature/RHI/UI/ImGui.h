#pragma once

#include <EASTL/vector.h>

#include <imgui.h>

#include <RHI/Base.h>
#include "ImGuiDescriptor.h"

namespace Spark::RHI
{
    class Device;
    class CommandList;
    class CommandQueue;
    class ImageView;

    class ImGui
    {
    public:
        virtual ~ImGui() = default;

        RHI::ResultCode Init(RHI::Device& device, RHI::CommandQueue& commandQueue, const ImGuiDescriptor& desc);

        virtual void Shutdown() = 0;
        virtual void NewFrame() = 0;
        virtual void RenderDrawData(ImDrawData* drawData, RHI::CommandList* commandList) = 0;

        //! Batch-register textures with ImGui by copying SRV descriptors from
        //! the engine heap into ImGui's shader-visible descriptor heap.
        //! Returns one ImTextureID per input ImageView, in the same order.
        virtual void RegisterTextures(
            const eastl::vector<const ImageView*>& imageViews,
            eastl::vector<ImTextureID>& outTextureIds) = 0;

        const ImGuiDescriptor& GetDescriptor() const;

    private:
        virtual RHI::ResultCode InitInternal(RHI::Device& device, RHI::CommandQueue& commandQueue, const ImGuiDescriptor& desc) = 0;

        ImGuiDescriptor m_desc;
    };
}
