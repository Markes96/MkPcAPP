#include "AddStartupEntryDialog.h"
#include "../platform/StringConvert.h"
#include "../platform/ComScope.h"
#include <imgui.h>
#include <shobjidl.h>
#include <cstring>
#include <optional>

namespace ui {

namespace {

using platform::Utf8ToWide;
using platform::WideToUtf8;

constexpr const char* kPopupId = "Anadir programa de inicio###AddStartupEntryDialog";

void CopyToBuffer(char* buffer, size_t bufferSize, const std::string& value) {
    std::strncpy(buffer, value.c_str(), bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
}

// Rare, user-triggered COM usage (native file picker) -- separate from
// StartupScanner::Scan()'s per-cycle ComScope pairing, since this runs on
// the main thread only when the user clicks "Examinar...", not every
// rescan on the background tick thread.
std::optional<std::wstring> PickExecutableFile(HWND owner) {
    platform::ComScope comScope;

    std::optional<std::wstring> result;
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog,
                                   reinterpret_cast<void**>(&dialog));
    if (SUCCEEDED(hr) && dialog) {
        COMDLG_FILTERSPEC filter[] = {{L"Ejecutables (*.exe)", L"*.exe"}};
        dialog->SetFileTypes(1, filter);
        dialog->SetFileTypeIndex(1);

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

std::wstring FileNameWithoutExtension(const std::wstring& path) {
    size_t lastSlash = path.find_last_of(L"\\/");
    std::wstring fileName = (lastSlash == std::wstring::npos) ? path : path.substr(lastSlash + 1);
    size_t lastDot = fileName.find_last_of(L'.');
    return (lastDot == std::wstring::npos) ? fileName : fileName.substr(0, lastDot);
}

} // namespace

void AddStartupEntryDialog::OpenForCreate() {
    isOpen_ = true;
    chosenExePath_.clear();
    errorMessage_.clear();
    CopyToBuffer(nameBuffer_, sizeof(nameBuffer_), "");
    openRequested_ = true;
}

void AddStartupEntryDialog::Render(startup::StartupScanner& scanner) {
    if (openRequested_) {
        openRequested_ = false;
        ImGui::OpenPopup(kPopupId);
    }

    if (!isOpen_) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    std::string pathDisplay =
        chosenExePath_.empty() ? "(ningun archivo seleccionado)" : WideToUtf8(chosenExePath_);
    ImGui::TextWrapped("Ejecutable: %s", pathDisplay.c_str());

    if (ImGui::Button("Examinar...")) {
        std::optional<std::wstring> picked = PickExecutableFile(nullptr);
        if (picked.has_value()) {
            chosenExePath_ = *picked;
            errorMessage_.clear();
            if (nameBuffer_[0] == '\0') {
                CopyToBuffer(nameBuffer_, sizeof(nameBuffer_), WideToUtf8(FileNameWithoutExtension(*picked)));
            }
        }
    }

    ImGui::InputText("Nombre", nameBuffer_, sizeof(nameBuffer_));

    if (!errorMessage_.empty()) {
        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "%s", errorMessage_.c_str());
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    bool canSave = !chosenExePath_.empty() && nameBuffer_[0] != '\0';
    ImGui::BeginDisabled(!canSave);
    if (ImGui::Button("Guardar")) {
        std::wstring displayName = Utf8ToWide(nameBuffer_);
        // scanner.AddManualEntry quotes the path itself before writing the
        // Run value -- pass the raw picked path, not pre-quoted.
        startup::RegistryStartupControl::AddResult result =
            scanner.AddManualEntry(displayName, chosenExePath_);
        switch (result) {
            case startup::RegistryStartupControl::AddResult::Ok:
                isOpen_ = false;
                justSaved_ = true;
                ImGui::CloseCurrentPopup();
                break;
            case startup::RegistryStartupControl::AddResult::DuplicateName:
                errorMessage_ = "Ya existe una entrada con ese nombre.";
                break;
            case startup::RegistryStartupControl::AddResult::Failed:
                errorMessage_ = "No se pudo anadir la entrada.";
                break;
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancelar")) {
        isOpen_ = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

bool AddStartupEntryDialog::ConsumeJustSaved() {
    bool result = justSaved_;
    justSaved_ = false;
    return result;
}

} // namespace ui
