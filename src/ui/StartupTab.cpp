#include "StartupTab.h"
#include "Theme.h"
#include "CardWidgets.h"
#include "../platform/StringConvert.h"
#include <imgui.h>
#include <shellapi.h>
#include <mutex>

namespace ui {

namespace {

constexpr float kIconSize = 32.0f;
// Full rescan every 10 ticks (~10s at the app's 1Hz data-tick rate) rather
// than every tick -- registry/filesystem enumeration plus signature checks
// don't need to run every second. Tune if this is noticeable on real
// hardware with many startup entries.
constexpr uint64_t kRescanEveryNTicks = 10;

const char* SourceBadgeText(startup::StartupSource source) {
    switch (source) {
        case startup::StartupSource::RegistryHkcuRun:
            return "Registro (tu usuario)";
        case startup::StartupSource::RegistryHklmRun:
            return "Registro (todos los usuarios)";
        case startup::StartupSource::StartupFolderUser:
            return "Carpeta Inicio";
        case startup::StartupSource::StartupFolderCommon:
            return "Carpeta Inicio (todos)";
    }
    return "";
}

// Drawn instead of ImGui::Image() when icon extraction failed or the
// entry's target is missing -- degrade visibly rather than showing nothing
// or a broken image, same principle the hardware monitor uses for missing
// sensors.
void RenderIconPlaceholder() {
    ImGui::Dummy(ImVec2(kIconSize, kIconSize));
    ImVec2 boxMin = ImGui::GetItemRectMin();
    ImVec2 boxMax = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRect(boxMin, boxMax, ImGui::GetColorU32(ImGuiCol_Border));
    ImVec2 textSize = ImGui::CalcTextSize("?");
    ImVec2 textPos(boxMin.x + (kIconSize - textSize.x) * 0.5f, boxMin.y + (kIconSize - textSize.y) * 0.5f);
    drawList->AddText(textPos, ImGui::GetColorU32(ImGuiCol_TextDisabled), "?");
}

} // namespace

StartupTab::StartupTab(HWND hwnd, platform::DX11Renderer& renderer) : hwnd_(hwnd), iconCache_(renderer) {}

void StartupTab::OnTick(uint64_t /*tickMs*/) {
    ++tickCounter_;
    if (tickCounter_ % kRescanEveryNTicks != 1) {
        return;
    }

    // Registry/filesystem enumeration + signature checks (no D3D calls) --
    // safe to run on this background thread. Texture creation stays in
    // OnRender only.
    startup::ScanResult freshScan = scanner_.Scan();
    {
        std::lock_guard<std::mutex> lock(scanMutex_);
        lastScan_ = std::move(freshScan);
    }
    hasScannedOnce_.store(true);
}

void StartupTab::OnRender(float /*deltaTimeSeconds*/) {
    if (RenderSectionHeaderAddButton("INICIO")) {
        addDialog_.OpenForCreate();
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    {
        // Held for the whole table loop rather than copied out and back:
        // OnTick only needs this lock briefly, once every ~10 ticks, to
        // swap in a freshly scanned result, so a render-thread hold for one
        // frame is cheap contention, not a bottleneck.
        std::lock_guard<std::mutex> lock(scanMutex_);

        if (!hasScannedOnce_.load()) {
            ImGui::TextDisabled("Buscando programas de inicio...");
        } else if (lastScan_.entries.empty()) {
            ImGui::TextDisabled("No se ha encontrado ningun programa de terceros en el inicio de Windows.");
        } else {
            constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                                     ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("StartupEntriesTable", 5, kTableFlags)) {
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, kIconSize + 8.0f);
                ImGui::TableSetupColumn("Nombre");
                ImGui::TableSetupColumn("Origen", ImGuiTableColumnFlags_WidthFixed, 170.0f);
                ImGui::TableSetupColumn("Activado", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Acciones", ImGuiTableColumnFlags_WidthFixed, 240.0f);
                ImGui::TableHeadersRow();

                for (auto& entry : lastScan_.entries) {
                    RenderEntryRow(entry);
                }

                ImGui::EndTable();
            }
        }
    }

    if (!lastErrorMessage_.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::TextDisabled("%s", lastErrorMessage_.c_str());
    }

    addDialog_.Render(scanner_);
    confirmDeleteDialog_.Render(scanner_);

    if (addDialog_.ConsumeJustSaved() || confirmDeleteDialog_.ConsumeJustDeleted()) {
        // Immediate rescan (one-off, user-triggered, off the hot path) so
        // an add/delete shows up right away instead of waiting up to
        // kRescanEveryNTicks ticks for the next background rescan.
        startup::ScanResult freshScan = scanner_.Scan();
        std::lock_guard<std::mutex> lock(scanMutex_);
        lastScan_ = std::move(freshScan);
        hasScannedOnce_.store(true);
    }
}

void StartupTab::RenderEntryRow(startup::StartupEntry& entry) {
    ImGui::PushID(entry.id.c_str());
    ImGui::TableNextRow();

    bool hasTarget = !entry.targetMissing && !entry.resolvedExePath.empty();

    ImGui::TableSetColumnIndex(0);
    uint64_t textureId = hasTarget ? iconCache_.GetOrCreateTexture(entry.resolvedExePath) : 0;
    if (textureId != 0) {
        ImGui::Image(textureId, ImVec2(kIconSize, kIconSize));
    } else {
        RenderIconPlaceholder();
    }

    ImGui::TableSetColumnIndex(1);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(entry.displayName.c_str());
    if (!hasTarget) {
        ImGui::SameLine();
        ImGui::TextDisabled("(archivo no encontrado)");
    }

    ImGui::TableSetColumnIndex(2);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", SourceBadgeText(entry.source));

    ImGui::TableSetColumnIndex(3);
    bool enabledState = entry.enabled;
    if (ImGui::Checkbox("##enabled", &enabledState)) {
        if (scanner_.SetEnabled(entry, enabledState)) {
            lastErrorMessage_.clear();
        } else {
            lastErrorMessage_ = "No se pudo cambiar el estado de \"" + entry.displayName + "\".";
        }
    }

    ImGui::TableSetColumnIndex(4);

    if (ImGui::SmallButton("i")) {
        infoPopupEntryId_ = entry.id;
        ImGui::OpenPopup("EntryInfoPopup");
        if (hasTarget && infoCache_.find(entry.resolvedExePath) == infoCache_.end()) {
            std::wstring widePath = platform::Utf8ToWide(entry.resolvedExePath);
            EntryInfoCache cacheValue;
            cacheValue.appInfo = startup::ReadAppInfo(widePath);
            cacheValue.signatureInfo = scanner_.GetSignatureInfo(widePath);
            infoCache_[entry.resolvedExePath] = std::move(cacheValue);
        }
    }
    if (infoPopupEntryId_ == entry.id) {
        RenderInfoPopup(entry);
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!hasTarget);
    if (ImGui::SmallButton("Abrir ubicacion")) {
        OpenContainingFolder(entry);
    }
    ImGui::EndDisabled();
    if (!hasTarget && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("El archivo ya no existe.");
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Eliminar")) {
        confirmDeleteDialog_.OpenForEntry(entry);
    }

    ImGui::PopID();
}

void StartupTab::RenderInfoPopup(const startup::StartupEntry& entry) {
    if (ImGui::BeginPopup("EntryInfoPopup")) {
        RenderInfoContent(entry);
        ImGui::EndPopup();
    } else {
        infoPopupEntryId_.clear(); // closed (e.g. clicked outside) -- stop tracking
    }
}

void StartupTab::RenderInfoContent(const startup::StartupEntry& entry) {
    bool hasTarget = !entry.targetMissing && !entry.resolvedExePath.empty();

    ImGui::TextUnformatted("Ruta:");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", hasTarget ? entry.resolvedExePath.c_str() : "Archivo no encontrado");

    if (!hasTarget) {
        return;
    }

    auto it = infoCache_.find(entry.resolvedExePath);
    if (it == infoCache_.end()) {
        ImGui::TextDisabled("No disponible");
        return;
    }
    const startup::AppInfo& appInfo = it->second.appInfo;
    const startup::SignatureInfo& sigInfo = it->second.signatureInfo;

    std::string signerText;
    switch (sigInfo.status) {
        case startup::SignatureStatus::Trusted:
            signerText = sigInfo.signerName.has_value() ? platform::WideToUtf8(*sigInfo.signerName)
                                                          : "(nombre no disponible)";
            break;
        case startup::SignatureStatus::NotSigned:
            signerText = "Sin firmar";
            break;
        case startup::SignatureStatus::VerificationFailed:
            signerText = "No se pudo comprobar la firma";
            break;
    }
    ImGui::Text("Editor: %s", signerText.c_str());

    if (appInfo.fileSizeBytes.has_value()) {
        double megabytes = static_cast<double>(*appInfo.fileSizeBytes) / (1024.0 * 1024.0);
        ImGui::Text("Tamano: %.2f MB", megabytes);
    } else {
        ImGui::TextUnformatted("Tamano: No disponible");
    }

    ImGui::Text("Version: %s", appInfo.productVersion.value_or("No disponible").c_str());
    ImGui::Text("Descripcion: %s", appInfo.fileDescription.value_or("No disponible").c_str());
}

void StartupTab::OpenContainingFolder(const startup::StartupEntry& entry) {
    std::wstring path = platform::Utf8ToWide(entry.resolvedExePath);
    std::wstring args = L"/select,\"" + path + L"\"";
    HINSTANCE result = ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<intptr_t>(result) <= 32) {
        lastErrorMessage_ = "No se pudo abrir la ubicacion de \"" + entry.displayName + "\".";
    } else {
        lastErrorMessage_.clear();
    }
}

} // namespace ui
