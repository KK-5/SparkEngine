#pragma once

#include <cstdint>

#include <EASTL/string.h>
#include <imgui.h>

#include <Resource/Bus/AssetBus.h>
#include <Resource/Material/MaterialState.h>
#include <Resource/Material/StandardPBR.h>

#include "UI/Bus/MaterialEditBus.h"
#include "UI/Bus/SaveAssetDialogBus.h"

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
    class MaterialWindow final : public MaterialEditBus::Handler,
                                 public Spark::Resource::AssetBus::Handler
    {
    public:
        MaterialWindow();
        ~MaterialWindow() override;

        void Draw();

        // MaterialEditBus
        void OpenMaterialEditor(Spark::Material::MaterialHandle handle) override;

        // AssetBus
        void OnAssetSaved(const Spark::Resource::AssetId& id) override;

    private:
        void DrawTitleBar(float width, const eastl::string& name, ImU32 swatch);
        void DrawPreviewPanel(uint32_t handleId, const ImVec2& size);
        void DrawPropertyPanel(uint32_t handleId, const ImVec2& size);
        void DrawFooter(float width, const eastl::string& path, bool canSave);

        //! The edited values as an asset, ready to be written. `existing` is the asset to
        //! put them back into (Save); null builds a throwaway one (Save As).
        Spark::Ptr<Spark::Resource::Asset> AssetToSave(
            uint32_t handleId, const Spark::Ptr<Spark::Resource::Asset>& existing) const;

        //! Everything but the name, which the two callers disagree on. The directory is
        //! the current material's, so a new one lands beside the one being looked at.
        SaveAssetRequest MakeSaveRequest(Spark::Ptr<Spark::Resource::Asset> asset,
                                         const char* title) const;

        void Save();
        void SaveAs();
        void NewMaterial();
        void Revert();

        void TakeSnapshot(uint32_t handleId);

        //! What the dialog is open for, since the two answers to a save differ: Save and
        //! Save As point this material at the file, New opens the material the file makes.
        enum class Pending
        {
            None,
            Save,
            New
        };

        Spark::Material::MaterialHandle m_target{Spark::Material::NullMaterial};
        bool                            m_open      = false;
        bool                            m_focusNext = false;

        //! Edited since the last write to disk -- not "unflushed changes" (editing is
        //! write-through), but "the material and its `.smat` have drifted apart". Save /
        //! Save As close that gap and clear this.
        bool                            m_dirty = false;

        //! What makes the next OnAssetSaved this window's own -- for Save As and New the
        //! only way to learn the id the file was written under.
        Pending                         m_pending = Pending::None;

        //! Where Revert goes back to: the values as they were opened, or as the last save
        //! left them.
        Spark::Resource::StandardPBR    m_snapshotParams;
        Spark::Resource::MaterialState  m_snapshotState;

        //! Which preview shape the (not yet implemented) preview would show.
        int                             m_previewShape = 0;
    };
}
