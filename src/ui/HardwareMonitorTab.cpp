#include "HardwareMonitorTab.h"
#include "Theme.h"
#include <imgui.h>
#include <implot.h>
#include <algorithm>
#include <array>
#include <string>
#include <cstdio>

namespace ui {

namespace {

constexpr float kMetricCardWidth = 200.0f;
constexpr float kMetricCardHeight = 132.0f;

// Renders a card, then places the next card on the same line only if it still
// fits within the window's content region — the standard ImGui wrapping idiom —
// so an arbitrary number of drives/metrics wraps instead of overflowing.
// `percent` in [0, 100] draws a thin accent progress bar below the value;
// pass -1 to omit it (e.g. for uptime or network throughput, which aren't a
// bounded 0-100 quantity).
//
// Relies only on the theme's automatic ItemSpacing between the label/value/bar
// — an earlier version added manual Dummy() gaps on top of that automatic
// spacing, which double-counted the vertical space and overflowed the card
// (forcing an internal scrollbar). NoScrollbar is set explicitly as a
// belt-and-suspenders guard: content should now always fit, but if it doesn't,
// it clips instead of showing a scrollbar.
void MetricCard(const char* label, const char* value, float percent = -1.0f) {
    ImGui::BeginChild(label, ImVec2(kMetricCardWidth, kMetricCardHeight),
                        ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextDisabled("%s", label);

    if (Theme::g_fonts.value) {
        ImGui::PushFont(Theme::g_fonts.value);
    }
    ImGui::TextUnformatted(value);
    if (Theme::g_fonts.value) {
        ImGui::PopFont();
    }

    if (percent >= 0.0f) {
        float clamped = percent < 0.0f ? 0.0f : (percent > 100.0f ? 100.0f : percent);
        ImGui::ProgressBar(clamped / 100.0f, ImVec2(-1, 6), "");
    }

    ImGui::EndChild();

    float windowVisibleX2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    float lastItemX2 = ImGui::GetItemRectMax().x;
    float nextItemX2 = lastItemX2 + ImGui::GetStyle().ItemSpacing.x + kMetricCardWidth;
    if (nextItemX2 < windowVisibleX2) {
        ImGui::SameLine();
    }
}

std::string FormatBytes(uint64_t bytes) {
    constexpr double kGB = 1024.0 * 1024.0 * 1024.0;
    char buf[64];
    snprintf(buf, sizeof(buf), "%.1f GB", static_cast<double>(bytes) / kGB);
    return buf;
}

std::string FormatUptime(uint64_t uptimeMs) {
    uint64_t totalSeconds = uptimeMs / 1000;
    uint64_t days = totalSeconds / 86400;
    uint64_t hours = (totalSeconds % 86400) / 3600;
    uint64_t minutes = (totalSeconds % 3600) / 60;
    char buf[64];
    if (days > 0) {
        snprintf(buf, sizeof(buf), "%llud %lluh %llum", (unsigned long long)days,
                  (unsigned long long)hours, (unsigned long long)minutes);
    } else {
        snprintf(buf, sizeof(buf), "%lluh %llum", (unsigned long long)hours,
                  (unsigned long long)minutes);
    }
    return buf;
}

// Highest value currently in either buffer — used to size the Y axis so it
// always has headroom above the live data instead of relying on ImPlot's
// autofit, which visibly lagged behind fast-changing series (network) and
// could clip the current value or dip below zero on padding.
float ComputeSeriesMax(const RingBuffer<float, 60>& a, const RingBuffer<float, 60>* b) {
    float maxValue = 0.0f;
    for (size_t i = 0; i < a.Size(); ++i) {
        maxValue = std::max(maxValue, a[i]);
    }
    if (b) {
        for (size_t i = 0; i < b->Size(); ++i) {
            maxValue = std::max(maxValue, (*b)[i]);
        }
    }
    return maxValue;
}

// Renders one plot inside its own bordered card (same visual language as the
// metric cards above) at an explicit size — callers arrange these in a grid
// with margins rather than letting a single plot stretch edge-to-edge or
// blend into its neighbors. The Y axis is always pinned to [0, yMax] (never
// autofit) so it never shows a negative range and never clips the current
// value.
void PlotHistory(const char* plotTitle, const char* seriesA, ui::RingBuffer<float, 60>& a,
                  const char* seriesB, ui::RingBuffer<float, 60>* b, const char* yAxisLabel,
                  float width, float height, float yMax) {
    std::string frameId = std::string(plotTitle) + "##frame";
    ImGui::BeginChild(frameId.c_str(), ImVec2(width, height), ImGuiChildFlags_Borders);

    if (ImPlot::BeginPlot(plotTitle, ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Seconds ago", yAxisLabel);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, 59, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, yMax, ImPlotCond_Always);

        // X coordinates are computed per-series from that series' own sample
        // count. Reusing series A's x array for series B was a real bug: if A
        // was empty (e.g. CPU temp unavailable) while B had data (GPU temp),
        // every one of B's points landed at x=0 instead of spread across the
        // last 60s — a degenerate vertical stack that reads as "nothing
        // plotted" even though the data was there.
        std::array<float, 60> xsA{};
        std::array<float, 60> ysA{};
        size_t n = a.Size();
        for (size_t i = 0; i < n; ++i) {
            xsA[i] = static_cast<float>(n - 1 - i);
            ysA[i] = a[i];
        }
        if (n > 0) {
            ImPlot::PlotLine(seriesA, xsA.data(), ysA.data(), static_cast<int>(n),
                              ImPlotSpec{ImPlotProp_LineWeight, 2.5f});
        }

        if (b) {
            std::array<float, 60> xsB{};
            std::array<float, 60> ysB{};
            size_t nb = b->Size();
            for (size_t i = 0; i < nb; ++i) {
                xsB[i] = static_cast<float>(nb - 1 - i);
                ysB[i] = (*b)[i];
            }
            if (nb > 0) {
                ImPlot::PlotLine(seriesB, xsB.data(), ysB.data(), static_cast<int>(nb),
                                  ImPlotSpec{ImPlotProp_LineWeight, 2.5f});
            }
        }

        ImPlot::EndPlot();
    }

    ImGui::EndChild();
}

} // namespace

void HardwareMonitorTab::OnRender(float /*deltaTimeSeconds*/) {
    sensors::CombinedSample sample = aggregator_.GetLatest();

    RenderMetricCards(sample);
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    RenderFanTable(sample);
    ImGui::Dummy(ImVec2(0.0f, 16.0f));
    RenderGraphs();
}

void HardwareMonitorTab::RenderMetricCards(const sensors::CombinedSample& sample) {
    char buf[64];

    snprintf(buf, sizeof(buf), "%.0f%%", sample.native.cpuUsagePercent);
    MetricCard("CPU Usage", buf, sample.native.cpuUsagePercent);

    if (sample.bridge && (sample.bridge->sensorsAvailable & ipc::kCpuTempAvailable)) {
        snprintf(buf, sizeof(buf), "%.0f C", sample.bridge->cpuTempC);
    } else {
        snprintf(buf, sizeof(buf), "N/A");
    }
    MetricCard("CPU Temp", buf);

    snprintf(buf, sizeof(buf), "%.0f%%", sample.native.ramUsagePercent);
    MetricCard("RAM Usage", buf, sample.native.ramUsagePercent);

    if (sample.bridge && (sample.bridge->sensorsAvailable & ipc::kGpuUsageAvailable)) {
        snprintf(buf, sizeof(buf), "%.0f%%", sample.bridge->gpuUsagePercent);
        MetricCard("GPU Usage", buf, sample.bridge->gpuUsagePercent);
    } else {
        MetricCard("GPU Usage", "N/A");
    }

    if (sample.bridge && (sample.bridge->sensorsAvailable & ipc::kGpuTempAvailable)) {
        snprintf(buf, sizeof(buf), "%.0f C", sample.bridge->gpuTempC);
    } else {
        snprintf(buf, sizeof(buf), "N/A");
    }
    MetricCard("GPU Temp", buf);

    if (sample.bridge && (sample.bridge->sensorsAvailable & ipc::kVramAvailable) &&
        sample.bridge->vramTotalMB > 0.0f) {
        snprintf(buf, sizeof(buf), "%.0f / %.0f MB", sample.bridge->vramUsedMB,
                  sample.bridge->vramTotalMB);
        float percent = 100.0f * sample.bridge->vramUsedMB / sample.bridge->vramTotalMB;
        MetricCard("VRAM", buf, percent);
    } else {
        MetricCard("VRAM", "N/A");
    }

    snprintf(buf, sizeof(buf), "%.0f / %.0f KB/s", sample.native.networkUpKBps,
              sample.native.networkDownKBps);
    MetricCard("Net Up/Down", buf);

    MetricCard("Uptime", FormatUptime(sample.native.uptimeMs).c_str());

    for (const auto& drive : sample.native.drives) {
        // Drive roots are always ASCII (e.g. L"C:\\"), safe to narrow directly.
        char driveLetter = drive.rootPath.empty() ? '?' : static_cast<char>(drive.rootPath[0]);
        std::string label = std::string("Disk ") + driveLetter + ":";
        std::string value = FormatBytes(drive.freeBytes) + " free";
        float usedPercent = drive.totalBytes > 0
                                 ? 100.0f * static_cast<float>(drive.totalBytes - drive.freeBytes) /
                                       static_cast<float>(drive.totalBytes)
                                 : -1.0f;
        MetricCard(label.c_str(), value.c_str(), usedPercent);
    }

    ImGui::NewLine();
}

void HardwareMonitorTab::RenderFanTable(const sensors::CombinedSample& sample) {
    bool hasFans = sample.bridge && (sample.bridge->sensorsAvailable & ipc::kFansAvailable) &&
                   sample.bridge->fanCount > 0;

    // Framed like the charts below (bordered card, same visual language) so
    // "Fan Speed" reads as its own titled panel rather than a bare table.
    ImGui::BeginChild("FanSpeedCard", ImVec2(-1, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    if (Theme::g_fonts.value) {
        ImGui::PushFont(Theme::g_fonts.value);
    }
    ImGui::TextUnformatted("Fan Speed");
    if (Theme::g_fonts.value) {
        ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    if (!hasFans) {
        ImGui::TextDisabled("No fan sensors detected.");
    } else {
        constexpr ImGuiTableFlags kFanTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                                     ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("FanTable", 2, kFanTableFlags)) {
            ImGui::TableSetupColumn("Fan");
            ImGui::TableSetupColumn("Speed (RPM)");
            ImGui::TableHeadersRow();
            for (uint32_t i = 0; i < sample.bridge->fanCount; ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(sample.bridge->fanLabel[i]);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.0f", sample.bridge->fanRpm[i]);
            }
            ImGui::EndTable();
        }
    }

    ImGui::EndChild();
}

void HardwareMonitorTab::RenderGraphs() {
    constexpr float kGap = 16.0f;
    constexpr float kPlotHeight = 200.0f;
    float availWidth = ImGui::GetContentRegionAvail().x;
    float plotWidth = (availWidth - kGap) * 0.5f;

    auto cpuUsage = aggregator_.GetCpuUsageHistory();
    auto ramUsage = aggregator_.GetRamUsageHistory();
    auto cpuTemp = aggregator_.GetCpuTempHistory();
    auto gpuTemp = aggregator_.GetGpuTempHistory();
    auto gpuUsage = aggregator_.GetGpuUsageHistory();
    auto gpuVram = aggregator_.GetGpuVramHistory();
    auto netUp = aggregator_.GetNetUpHistory();
    auto netDown = aggregator_.GetNetDownHistory();

    // Percent-based series have a known, fixed 0-100 scale. Temperature and
    // network have no natural upper bound, so their axis follows the live
    // data with headroom (+15%/+30%) and a floor so the scale doesn't look
    // broken when values are small (e.g. an idle network link).
    constexpr float kPercentMax = 100.0f;
    float tempMax = std::max(90.0f, ComputeSeriesMax(cpuTemp, &gpuTemp) * 1.15f);
    float netMax = std::max(50.0f, ComputeSeriesMax(netUp, &netDown) * 1.3f);

    PlotHistory("CPU / RAM Usage", "CPU %", cpuUsage, "RAM %", &ramUsage, "%", plotWidth, kPlotHeight,
                kPercentMax);
    ImGui::SameLine(0.0f, kGap);
    PlotHistory("CPU / GPU Temperature", "CPU Temp", cpuTemp, "GPU Temp", &gpuTemp, "C", plotWidth,
                kPlotHeight, tempMax);

    PlotHistory("GPU Usage / VRAM", "GPU %", gpuUsage, "VRAM %", &gpuVram, "%", plotWidth, kPlotHeight,
                kPercentMax);
    ImGui::SameLine(0.0f, kGap);
    PlotHistory("Network Up/Down", "Up KB/s", netUp, "Down KB/s", &netDown, "KB/s", plotWidth,
                kPlotHeight, netMax);
}

} // namespace ui
