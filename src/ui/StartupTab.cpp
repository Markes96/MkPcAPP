#include "StartupTab.h"
#include "Theme.h"
#include "CardWidgets.h"
#include <imgui.h>
#include <mutex>

namespace ui {

namespace {

constexpr float kCardHeight = 150.0f;
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
        // Held for the whole card loop rather than copied out and back:
        // OnTick only needs this lock briefly, once every ~10 ticks, to swap
        // in a freshly scanned result, so a render-thread hold for one
        // frame is cheap contention, not a bottleneck -- and rendering
        // straight from lastScan_.entries avoids an O(n) deep-copy of every
        // entry's strings twice per frame for no reason.
        std::lock_guard<std::mutex> lock(scanMutex_);

        if (!hasScannedOnce_.load()) {
            ImGui::TextDisabled("Buscando programas de inicio...");
        } else if (lastScan_.entries.empty()) {
            ImGui::TextDisabled("No se ha encontrado ningun programa de terceros en el inicio de Windows.");
        }

        for (size_t i = 0; i < lastScan_.entries.size();) {
            bool removed = false;
            RenderEntryCard(lastScan_.entries[i], removed);
            if (removed) {
                lastScan_.entries.erase(lastScan_.entries.begin() + i);
            } else {
                ++i;
            }
        }
        ImGui::NewLine();
    }

    if (!lastErrorMessage_.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::TextDisabled("%s", lastErrorMessage_.c_str());
    }

    addDialog_.Render(scanner_);

    if (addDialog_.ConsumeJustSaved()) {
        // Immediate rescan (one-off, user-triggered, off the hot path) so
        // the newly added entry shows up right away instead of waiting up
        // to kRescanEveryNTicks ticks for the next background rescan.
        startup::ScanResult freshScan = scanner_.Scan();
        std::lock_guard<std::mutex> lock(scanMutex_);
        lastScan_ = std::move(freshScan);
        hasScannedOnce_.store(true);
    }
}

void StartupTab::RenderEntryCard(startup::StartupEntry& entry, bool& removed) {
    removed = false;

    ImGui::PushID(entry.id.c_str());
    ImGui::BeginGroup();

    ImVec2 cardMin = ImGui::GetCursorScreenPos();
    ImVec2 cardMax = ImVec2(cardMin.x + kCardWidth, cardMin.y + kCardHeight);
    bool isHovered = ImGui::IsMouseHoveringRect(cardMin, cardMax);

    int pushedColors = 0;
    if (isHovered) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(Theme::kAccentHover.x, Theme::kAccentHover.y,
                                                         Theme::kAccentHover.z, 0.10f));
        ImGui::PushStyleColor(ImGuiCol_Border, Theme::kAccentHover);
        pushedColors = 2;
    }

    ImGui::BeginChild(entry.id.c_str(), ImVec2(kCardWidth, kCardHeight), ImGuiChildFlags_Borders,
                       ImGuiWindowFlags_NoScrollbar);

    ImGui::SetCursorPosX((kCardWidth - kIconSize) * 0.5f);
    uint64_t textureId = (entry.targetMissing || entry.resolvedExePath.empty())
                              ? 0
                              : iconCache_.GetOrCreateTexture(entry.resolvedExePath);
    if (textureId != 0) {
        ImGui::Image(textureId, ImVec2(kIconSize, kIconSize));
    } else {
        RenderIconPlaceholder();
    }

    if (Theme::g_fonts.body) {
        ImGui::PushFont(Theme::g_fonts.body);
    }
    CenteredText(entry.displayName.c_str());
    if (Theme::g_fonts.body) {
        ImGui::PopFont();
    }

    CenteredText(SourceBadgeText(entry.source), /*disabled=*/true);
    if (entry.targetMissing) {
        CenteredText("(archivo no encontrado)", /*disabled=*/true);
    }

    ImGui::EndChild();
    if (pushedColors > 0) {
        ImGui::PopStyleColor(pushedColors);
    }

    bool enabledState = entry.enabled;
    if (ImGui::Checkbox("Activado", &enabledState)) {
        if (scanner_.SetEnabled(entry, enabledState)) {
            lastErrorMessage_.clear();
        } else {
            lastErrorMessage_ = "No se pudo cambiar el estado de \"" + entry.displayName + "\".";
        }
    }

    if (entry.deletable) {
        if (ImGui::SmallButton("Quitar")) {
            if (scanner_.DeleteManualEntry(entry)) {
                removed = true;
            } else {
                lastErrorMessage_ = "No se pudo quitar \"" + entry.displayName + "\".";
            }
        }
    }

    ImGui::EndGroup();
    ImGui::PopID();

    float windowVisibleX2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    float lastItemX2 = ImGui::GetItemRectMax().x;
    float nextItemX2 = lastItemX2 + ImGui::GetStyle().ItemSpacing.x + kCardWidth;
    if (nextItemX2 < windowVisibleX2) {
        ImGui::SameLine();
    }
}

} // namespace ui
