#include "PassScopes.h"

#include <Pass/Component/PassComponents.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Component/Component.h>
#include <RHI/Resource/Buffer/Buffer.h>

#include <Resource/Model/VertexLayout.h>

#include <Binding/Instance/InstanceBinding.h>
#include <Geometry/MeshGeometry.h>

namespace Spark::Render
{
    namespace
    {
        constexpr RHI::AttachmentStage DepthStages =
            RHI::AttachmentStage::EarlyFragmentTest | RHI::AttachmentStage::LateFragmentTest;
    }

    // ============================================================
    // Attachment / ShaderAttachment
    // ============================================================

    Attachment& Attachment::Format(RHI::Format format)
    {
        auto* image = RHIExecuteContext::Current()->TryGet<ImagePassAttachment>(m_handle);
        ASSERT(image != nullptr, "Format() is for image attachments.");
        image->m_viewDescriptor.m_overrideFormat = format;
        return *this;
    }

    Attachment& Attachment::View(const RHI::ImageViewDescriptor& view)
    {
        auto* image = RHIExecuteContext::Current()->TryGet<ImagePassAttachment>(m_handle);
        ASSERT(image != nullptr, "An image view on an attachment of a buffer.");
        image->m_viewDescriptor = view;
        return *this;
    }

    Attachment& Attachment::View(const RHI::BufferViewDescriptor& view)
    {
        auto* buffer = RHIExecuteContext::Current()->TryGet<BufferPassAttachment>(m_handle);
        ASSERT(buffer != nullptr, "A buffer view on an attachment of an image.");
        buffer->m_viewDescriptor = view;
        return *this;
    }

    Attachment& Attachment::From(eastl::string_view passName)
    {
        auto& rhiContext  = *RHIExecuteContext::Current();
        auto& passContext = *PassExecuteContext::Current();

        const auto* image    = rhiContext.TryGet<ImagePassAttachment>(m_handle);
        const Pass  user     = image ? image->m_pass : rhiContext.Get<BufferPassAttachment>(m_handle).m_pass;
        const char* userName = passContext.Get<PassName>(user).m_name.GetCStr();
        const ObjectName name(passName);

        const bool readsPreviousFrame = rhiContext.Has<PreviousFrameTag>(m_handle);
        ASSERT(!readsPreviousFrame,
            "Pass {}: .From({}) on a ReadPreviousImage, which reads what last frame left at its end.",
            userName, name.GetCStr());
        ASSERT(!rhiContext.Has<FromPass>(m_handle),
            "Pass {}: .From({}) on an access that already has a .From.", userName, name.GetCStr());

        // Among the passes declared before this one: .From reaches back for a version that has
        // been written over since. What a pass declared later leaves needs none.
        Pass from = NullPass;
        for (const Pass pass : passContext.GetPassesInDeclOrder())
        {
            if (pass == user)
            {
                break;
            }
            const auto* declared = passContext.TryGet<PassName>(pass);
            if (declared != nullptr && declared->m_name == name)
            {
                from = pass;
                break;
            }
        }
        ASSERT(from != NullPass,
            "Pass {}: .From({}) names no pass declared before this one.", userName, name.GetCStr());

        if (from != NullPass && !readsPreviousFrame)
        {
            rhiContext.AddOrReplace<FromPass>(m_handle, FromPass{ from });
        }
        return *this;
    }

    ShaderAttachment& ShaderAttachment::Format(RHI::Format format)
    {
        auto* image = RHIExecuteContext::Current()->TryGet<ImagePassAttachment>(GetHandle());
        ASSERT(image != nullptr, "Format() is for image attachments.");
        const bool writes = CheckBitsAny(image->m_access, RHI::AttachmentAccess::Write);
        image->m_viewDescriptor.m_overrideFormat    = format;
        image->m_viewDescriptor.m_overrideBindFlags =
            writes ? RHI::ImageBindFlags::ShaderReadWrite : RHI::ImageBindFlags::ShaderRead;
        return *this;
    }

    ShaderAttachment& ShaderAttachment::Bind(const RHI::InputName& input)
    {
        m_builder->BindShaderInput(GetHandle(), input);
        return *this;
    }

    ShaderAttachment& ShaderAttachment::BindIndex(const RHI::InputName& input)
    {
        m_builder->BindShaderInputIndex(GetHandle(), input);
        return *this;
    }

    ShaderAttachment& ShaderAttachment::BindValid(const RHI::InputName& input)
    {
        m_builder->BindPreviousFrameValid(GetHandle(), input);
        return *this;
    }

    ShaderAttachment& ShaderAttachment::IndirectArgumentsOf(RHIHandle view, uint64_t byteOffset)
    {
        m_builder->SetIndirectArgumentsOfView(GetHandle(), view, byteOffset);
        return *this;
    }

    ShaderAttachment& ShaderAttachment::Stage(RHI::AttachmentStage stage)
    {
        ASSERT(!m_fixedStage, "A compute pass's shader accesses are always in the compute stage.");
        auto& rhiContext = *RHIExecuteContext::Current();
        const RHIHandle handle = m_attachment.GetHandle();
        if (auto* image = rhiContext.TryGet<ImagePassAttachment>(handle))
        {
            image->m_stage = stage;
        }
        else
        {
            rhiContext.Get<BufferPassAttachment>(handle).m_stage = stage;
        }
        return *this;
    }

    // ============================================================
    // RenderScope
    // ============================================================

    Attachment RenderScope::RenderTarget(const RHI::AttachmentId& name, const RHI::AttachmentLoadStoreAction& action)
    {
        return Attachment(m_builder->AddScopeAttachment(m_scope, &m_colorCount, name,
            RHI::AttachmentUsage::RenderTarget, RHI::AttachmentAccess::Write,
            RHI::AttachmentStage::ColorAttachmentOutput, &action));
    }

    Attachment RenderScope::DepthWrite(const RHI::AttachmentId& name, const RHI::AttachmentLoadStoreAction& action)
    {
        return Attachment(m_builder->AddScopeAttachment(m_scope, &m_colorCount, name,
            RHI::AttachmentUsage::DepthStencil, RHI::AttachmentAccess::Write, DepthStages, &action));
    }

    Attachment RenderScope::DepthRead(const RHI::AttachmentId& name)
    {
        const RHI::AttachmentLoadStoreAction action;
        return Attachment(m_builder->AddScopeAttachment(m_scope, &m_colorCount, name,
            RHI::AttachmentUsage::DepthStencil, RHI::AttachmentAccess::Read, DepthStages, &action));
    }

    Attachment RenderScope::Resolve(const RHI::AttachmentId& name, const Attachment& source)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        ASSERT(rhiContext.Has<ColorAttachmentIndex>(source.GetHandle())
                && rhiContext.Get<ScopeAttachment>(source.GetHandle()).m_scope == m_scope,
            "A Resolve's source must be a RenderTarget of the same Scope.");

        // The resolve writes every pixel, so nothing is loaded.
        RHI::AttachmentLoadStoreAction action;
        action.m_loadAction  = RHI::AttachmentLoadAction::DontCare;
        action.m_storeAction = RHI::AttachmentStoreAction::Store;
        const RHIHandle handle = m_builder->AddScopeAttachment(m_scope, &m_colorCount, name,
            RHI::AttachmentUsage::Resolve, RHI::AttachmentAccess::Write,
            RHI::AttachmentStage::ColorAttachmentOutput, &action);
        rhiContext.Add<ResolveSource>(handle, ResolveSource{ source.GetHandle() });
        return Attachment(handle);
    }

    ShaderAttachment RenderScope::ReadImage(const RHI::AttachmentId& name)
    {
        return ShaderAttachment(*m_builder, m_builder->AddScopeAttachment(m_scope, &m_colorCount, name,
            RHI::AttachmentUsage::Shader, RHI::AttachmentAccess::Read,
            RHI::AttachmentStage::Uninitialized, nullptr), false);
    }

    ShaderAttachment RenderScope::ReadWriteImage(const RHI::AttachmentId& name)
    {
        return ShaderAttachment(*m_builder, m_builder->AddScopeAttachment(m_scope, &m_colorCount, name,
            RHI::AttachmentUsage::Shader, RHI::AttachmentAccess::ReadWrite,
            RHI::AttachmentStage::Uninitialized, nullptr), false);
    }

    ShaderAttachment RenderScope::ReadBuffer(const RHI::AttachmentId& name)
    {
        return ShaderAttachment(*m_builder, m_builder->AddScopeBufferAttachment(m_scope, name,
            RHI::AttachmentUsage::Shader, RHI::AttachmentAccess::Read, RHI::AttachmentStage::Uninitialized), false);
    }

    ShaderAttachment RenderScope::ReadWriteBuffer(const RHI::AttachmentId& name)
    {
        return ShaderAttachment(*m_builder, m_builder->AddScopeBufferAttachment(m_scope, name,
            RHI::AttachmentUsage::Shader, RHI::AttachmentAccess::ReadWrite, RHI::AttachmentStage::Uninitialized), false);
    }

    Attachment RenderScope::IndirectArguments(const RHI::AttachmentId& name)
    {
        return Attachment(m_builder->AddScopeBufferAttachment(m_scope, name,
            RHI::AttachmentUsage::Indirect, RHI::AttachmentAccess::Read, RHI::AttachmentStage::DrawIndirect));
    }

    Attachment RenderScope::InputAssembly(const RHI::AttachmentId& name)
    {
        return Attachment(m_builder->AddScopeBufferAttachment(m_scope, name,
            RHI::AttachmentUsage::InputAssembly, RHI::AttachmentAccess::Read, RHI::AttachmentStage::VertexInput));
    }

    void RenderScope::Draw(const RHI::DrawArguments& arguments, uint32_t instanceCount)
    {
        RHI::DrawItem item;
        item.m_drawArguments    = arguments;
        item.m_drawInstanceArgs = RHI::DrawInstanceArguments(instanceCount, 0);
        RHIExecuteContext::Current()->Add<RHI::DrawItem>(m_builder->AddScopeItem(m_scope), item);
    }

    void RenderScope::Draw(const RHI::DrawArguments& arguments, eastl::span<const Attachment> vertices,
        uint32_t instanceCount)
    {
        DrawStreams(arguments, vertices, NullHandle, RHI::IndexFormat::Unknown, instanceCount);
    }

    void RenderScope::Draw(const RHI::DrawArguments& arguments, eastl::span<const Attachment> vertices,
        const IndexStream& indices, uint32_t instanceCount)
    {
        DrawStreams(arguments, vertices, indices.m_buffer.GetHandle(), indices.m_format, instanceCount);
    }

    void RenderScope::DrawStreams(const RHI::DrawArguments& arguments, eastl::span<const Attachment> vertices,
        RHIHandle indices, RHI::IndexFormat indexFormat, uint32_t instanceCount)
    {
        eastl::fixed_vector<RHIHandle, RHI::Limits::Pipeline::StreamCountMax, true> handles;
        for (const Attachment& vertex : vertices)
        {
            handles.push_back(vertex.GetHandle());
        }

        RHI::DrawItem item;
        item.m_drawArguments    = arguments;
        item.m_drawInstanceArgs = RHI::DrawInstanceArguments(instanceCount, 0);
        m_builder->AddScopeDraw(m_scope, item, eastl::span<const RHIHandle>(handles.data(), handles.size()),
            indices, indexFormat);
    }

    void RenderScope::DrawIndirect(const RHI::AttachmentId& argumentsName, const RHI::AttachmentId& countName)
    {
        RHI::DrawItem item;
        if (!UseIndirectDrawGeometry(item))
        {
            return;
        }
        const RHIHandle arguments = IndirectArguments(argumentsName).GetHandle();
        const RHIHandle count     = IndirectArguments(countName).GetHandle();
        m_builder->AddScopeDrawIndirect(m_scope, item, arguments, count);
    }

    void RenderScope::DrawIndirect(const RHI::AttachmentId& argumentsName)
    {
        RHI::DrawItem item;
        if (!UseIndirectDrawGeometry(item))
        {
            return;
        }
        m_builder->AddScopeDrawIndirect(m_scope, item, IndirectArguments(argumentsName).GetHandle(), NullHandle);
    }

    bool RenderScope::UseIndirectDrawGeometry(RHI::DrawItem& outItem)
    {
        auto& rhiContext = *RHIExecuteContext::Current();

        auto findBuffer = [&](auto view) -> const RHI::Buffer*
        {
            const RHI::Buffer* found = nullptr;
            view.each([&](RHIHandle, const RHI::Components::Buffer& buffer) { found = buffer.m_buffer.get(); });
            return found;
        };

        // Every mesh is a part of one native buffer; the stream that hands a record's first
        // instance to the vertex shader is another. Nothing to draw through until both exist.
        const RHI::Buffer* geometry = findBuffer(
            rhiContext.GetView<MeshGeometryBaseBufferTag, RHI::Components::Buffer>(Exclude<DeadTag>));
        const RHI::Buffer* instanceIds = findBuffer(
            rhiContext.GetView<InstanceIDBufferTag, RHI::Components::Buffer>(Exclude<DeadTag>));
        if (geometry == nullptr || instanceIds == nullptr)
        {
            return false;
        }

        // What the draw reads of them: each mesh's part, and the ID stream.
        Uses<MeshGeometrySegmentTag>();
        Uses<InstanceIDBufferTag>();

        const uint64_t geometryByteCount = geometry->GetDescriptor().m_byteCount;
        ASSERT(geometryByteCount <= UINT32_MAX,
            "[RenderScope] The geometry pool's native buffer is beyond what a stream's range holds.");

        // The whole native buffer as both streams: each record says which part its draw reads.
        outItem.m_vertexBufferView.SetVertexInputView(0, RHI::VertexInputView(
            *geometry, 0, static_cast<uint32_t>(geometryByteCount), Resource::StandardVertex::Stride));
        outItem.m_vertexBufferView.SetVertexInputView(1, RHI::VertexInputView(
            *instanceIds, 0, static_cast<uint32_t>(instanceIds->GetDescriptor().m_byteCount), sizeof(uint32_t)));
        outItem.m_indexBufferView = RHI::IndexBufferView(
            *geometry, 0, static_cast<uint32_t>(geometryByteCount), RHI::IndexFormat::UINT32);
        return true;
    }

    ShaderAttachment RenderScope::ReadPreviousImage(const RHI::AttachmentId& name)
    {
        ImagePassAttachment a;
        a.m_attachmentId = AttachmentId{ name, 0, 1 };
        a.m_usage        = RHI::AttachmentUsage::Shader;
        a.m_stage        = RHI::AttachmentStage::Uninitialized;
        return ShaderAttachment(*m_builder, m_builder->AddPreviousFrameAttachment(a, m_scope), false);
    }

    // ============================================================
    // ComputeScope
    // ============================================================

    ShaderAttachment ComputeScope::ReadPreviousImage(const RHI::AttachmentId& name)
    {
        ImagePassAttachment a;
        a.m_attachmentId = AttachmentId{ name, 0, 1 };
        a.m_usage        = RHI::AttachmentUsage::Shader;
        a.m_stage        = RHI::AttachmentStage::ComputeShader;
        return ShaderAttachment(*m_builder, m_builder->AddPreviousFrameAttachment(a, m_scope), true);
    }

    ShaderAttachment ComputeScope::ReadImage(const RHI::AttachmentId& name)
    {
        return ShaderAttachment(*m_builder, m_builder->AddScopeAttachment(m_scope, nullptr, name,
            RHI::AttachmentUsage::Shader, RHI::AttachmentAccess::Read,
            RHI::AttachmentStage::ComputeShader, nullptr), true);
    }

    ShaderAttachment ComputeScope::ReadWriteImage(const RHI::AttachmentId& name)
    {
        return ShaderAttachment(*m_builder, m_builder->AddScopeAttachment(m_scope, nullptr, name,
            RHI::AttachmentUsage::Shader, RHI::AttachmentAccess::ReadWrite,
            RHI::AttachmentStage::ComputeShader, nullptr), true);
    }

    ShaderAttachment ComputeScope::WriteImage(const RHI::AttachmentId& name)
    {
        return ShaderAttachment(*m_builder, m_builder->AddScopeAttachment(m_scope, nullptr, name,
            RHI::AttachmentUsage::Shader, RHI::AttachmentAccess::Write,
            RHI::AttachmentStage::ComputeShader, nullptr), true);
    }
    ShaderAttachment ComputeScope::ReadBuffer(const RHI::AttachmentId& name)
    {
        return ShaderAttachment(*m_builder, m_builder->AddScopeBufferAttachment(m_scope, name,
            RHI::AttachmentUsage::Shader, RHI::AttachmentAccess::Read, RHI::AttachmentStage::ComputeShader), true);
    }

    ShaderAttachment ComputeScope::ReadWriteBuffer(const RHI::AttachmentId& name)
    {
        return ShaderAttachment(*m_builder, m_builder->AddScopeBufferAttachment(m_scope, name,
            RHI::AttachmentUsage::Shader, RHI::AttachmentAccess::ReadWrite, RHI::AttachmentStage::ComputeShader), true);
    }

    ShaderAttachment ComputeScope::WriteBuffer(const RHI::AttachmentId& name)
    {
        return ShaderAttachment(*m_builder, m_builder->AddScopeBufferAttachment(m_scope, name,
            RHI::AttachmentUsage::Shader, RHI::AttachmentAccess::Write, RHI::AttachmentStage::ComputeShader), true);
    }

    Attachment ComputeScope::IndirectArguments(const RHI::AttachmentId& name)
    {
        return Attachment(m_builder->AddScopeBufferAttachment(m_scope, name,
            RHI::AttachmentUsage::Indirect, RHI::AttachmentAccess::Read, RHI::AttachmentStage::DrawIndirect));
    }

    // ============================================================
    // CopyScope
    // ============================================================

    Attachment CopyScope::CopySource(const RHI::AttachmentId& name)
    {
        return Attachment(m_builder->AddScopeBufferAttachment(m_scope, name,
            RHI::AttachmentUsage::Copy, RHI::AttachmentAccess::Read, RHI::AttachmentStage::Copy));
    }

    Attachment CopyScope::CopyDestination(const RHI::AttachmentId& name)
    {
        return Attachment(m_builder->AddScopeBufferAttachment(m_scope, name,
            RHI::AttachmentUsage::Copy, RHI::AttachmentAccess::Write, RHI::AttachmentStage::Copy));
    }
}
