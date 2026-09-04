#pragma once

#include <cstdint>

#include <EASTL/string.h>
#include <imgui.h>

#include "UI/Bus/MaterialEditBus.h"

namespace Editor
{
    //! The first of the two material editing surfaces: this one edits the MATERIAL, so a
    //! change here reaches every object referencing it. The second is the material slot in
    //! Component View, which edits one object's override. Splitting them across two windows
    //! is what makes "did I just change everyone, or only this one" unambiguous by
    //! construction, with no UI hint needed to explain it.
    //!
    //! Free-floating and non-modal, deliberately NOT part of the dockspace layout: editing a
    //! material has to run alongside the scene it is changing, and a docked panel would cost
    //! an existing one its space. Docking still works if the user wants to park it somewhere.
    //!
    //! One window, one material at a time -- opening another retargets this one.
    class MaterialWindow final : public MaterialEditBus::Handler
    {
    public:
        MaterialWindow();
        ~MaterialWindow() override;

        void Draw();

        // MaterialEditBus
        void OpenMaterialEditor(Spark::Material::MaterialHandle handle) override;

    private:
        void DrawTitleBar(float width, const eastl::string& name, ImU32 swatch);
        void DrawPreviewPanel(uint32_t handleId, const ImVec2& size);
        void DrawPropertyPanel(uint32_t handleId, const ImVec2& size);
        void DrawFooter(float width, const eastl::string& path, bool canSave);

        Spark::Material::MaterialHandle m_target{Spark::Material::NullMaterial};
        bool                            m_open      = false;
        bool                            m_focusNext = false;

        //! Edited since the last write to disk. Editing goes straight through to the
        //! material, so this is not "unflushed changes" -- it means the material and the
        //! `.smat` behind it have drifted apart. Save / Save As are what close that gap,
        //! and so are what clear this.
        bool                            m_dirty = false;

        //! Which preview shape the (not yet implemented) preview would show.
        int                             m_previewShape = 0;
    };
}
