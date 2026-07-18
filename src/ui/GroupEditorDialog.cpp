#include "GroupEditorDialog.h"
#include "../platform/ComScope.h"
#include "../platform/StringConvert.h"
#include "../startup/ShortcutStartupControl.h"
#include <imgui.h>
#include <shobjidl.h>
#include <cstring>
#include <optional>

namespace ui {

namespace {

using platform::Utf8ToWide;
using platform::WideToUtf8;

constexpr const char* kPopupId = "Grupo de lanzamiento###GroupEditorDialog";

void CopyToBuffer(char* buffer, size_t bufferSize, const std::string& value) {
    std::strncpy(buffer, value.c_str(), bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
}

// Same rare, user-triggered COM usage as AddStartupEntryDialog's file
// picker -- bracketed locally, not part of any per-cycle ComScope
// pairing. Unlike that one, accepts any file (no SetFileTypes() filter):
// a launch-group entry can be a .exe, a .lnk, or anything else the OS
// can execute (e.g. Minecraft launches through Java, not a bare .exe).
std::optional<std::wstring> PickAnyFile(HWND owner) {
    platform::ComScope comScope;

    std::optional<std::wstring> result;
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog,
                                   reinterpret_cast<void**>(&dialog));
    if (SUCCEEDED(hr) && dialog) {
        if (SUCCEEDED(dialog->Show(owner))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item)) && item) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    result = std::wstring(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }

    return result;
}

bool HasExtension(const std::wstring& path, const wchar_t* ext) {
    size_t extLen = wcslen(ext);
    if (path.size() < extLen) {
        return false;
    }
    return _wcsnicmp(path.c_str() + (path.size() - extLen), ext, extLen) == 0;
}

// Resolves a picked path to the exe that will actually run: a .lnk is
// resolved to its target via the same IShellLinkW logic StartupTab's
// Startup-folder scan uses; anything else (a .exe, or any other
// executable format the user picked) is used as-is.
std::wstring ResolveEntryExePath(const std::wstring& pickedPath) {
    if (!HasExtension(pickedPath, L".lnk")) {
        return pickedPath;
    }
    platform::ComScope comScope;
    std::optional<std::wstring> target = startup::ShortcutStartupControl::ResolveShortcutTarget(pickedPath);
    return target.value_or(L"");
}

} // namespace

void GroupEditorDialog::OpenForCreate() {
    isOpen_ = true;
    isEditMode_ = false;
    editingGroupId_.clear();
    entries_.clear();
    CopyToBuffer(nameBuffer_, sizeof(nameBuffer_), "");
    openRequested_ = true;
}

void GroupEditorDialog::OpenForEdit(const groups::LaunchGroup& group) {
    isOpen_ = true;
    isEditMode_ = true;
    editingGroupId_ = group.id;
    entries_ = group.entries;
    CopyToBuffer(nameBuffer_, sizeof(nameBuffer_), group.name);
    openRequested_ = true;
}

void GroupEditorDialog::Render(groups::GroupManager& manager) {
    if (openRequested_) {
        openRequested_ = false;
        ImGui::OpenPopup(kPopupId);
    }

    if (!isOpen_) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::InputText("Nombre", nameBuffer_, sizeof(nameBuffer_));

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::TextUnformatted("Apps del grupo:");

    std::optional<size_t> pendingRemoveIndex;
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (RenderEntryRow(i)) {
            pendingRemoveIndex = i;
        }
    }
    if (pendingRemoveIndex.has_value()) {
        entries_.erase(entries_.begin() + static_cast<long>(*pendingRemoveIndex));
    }

    if (ImGui::SmallButton("+ Anadir app")) {
        std::optional<std::wstring> picked = PickAnyFile(nullptr);
        if (picked.has_value()) {
            groups::LaunchEntry entry;
            entry.path = WideToUtf8(*picked);
            entry.resolvedExePath = WideToUtf8(ResolveEntryExePath(*picked));
            entries_.push_back(std::move(entry));
        }
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    bool canSave = nameBuffer_[0] != '\0' && !entries_.empty();
    ImGui::BeginDisabled(!canSave);
    if (ImGui::Button("Guardar")) {
        if (isEditMode_) {
            manager.UpdateGroup(editingGroupId_, nameBuffer_, entries_);
        } else {
            manager.CreateGroup(nameBuffer_, entries_);
        }
        isOpen_ = false;
        justSaved_ = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancelar")) {
        isOpen_ = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

bool GroupEditorDialog::RenderEntryRow(size_t index) {
    ImGui::PushID(static_cast<int>(index));
    groups::LaunchEntry& entry = entries_[index];

    bool resolved = !entry.resolvedExePath.empty();
    ImGui::TextWrapped("%s", resolved ? entry.resolvedExePath.c_str() : "(no se pudo resolver el destino)");

    char argsBuffer[256];
    CopyToBuffer(argsBuffer, sizeof(argsBuffer), entry.args);
    ImGui::SetNextItemWidth(300.0f);
    if (ImGui::InputText("Argumentos", argsBuffer, sizeof(argsBuffer))) {
        entry.args = argsBuffer;
    }

    ImGui::SameLine();
    bool removeClicked = ImGui::SmallButton("Quitar");

    ImGui::Separator();
    ImGui::PopID();
    return removeClicked;
}

bool GroupEditorDialog::ConsumeJustSaved() {
    bool result = justSaved_;
    justSaved_ = false;
    return result;
}

} // namespace ui
