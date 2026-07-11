#pragma once
#include "ITab.h"
#include "../sensors/SensorAggregator.h"

namespace ui {

// The only tab in iteration 1: a real-time hardware dashboard with metric cards
// and 60-second rolling graphs. Reads from a SensorAggregator that's populated on
// a background thread — this tab never touches Win32/bridge APIs directly.
class HardwareMonitorTab : public ITab {
public:
    explicit HardwareMonitorTab(sensors::SensorAggregator& aggregator) : aggregator_(aggregator) {}

    const char* GetTitle() const override { return "Hardware Monitor"; }
    const char* GetIcon() const override { return "HW"; }
    void OnRender(float deltaTimeSeconds) override;

private:
    void RenderMetricCards(const sensors::CombinedSample& sample);
    void RenderFanTable(const sensors::CombinedSample& sample);
    void RenderGraphs();

    sensors::SensorAggregator& aggregator_;
};

} // namespace ui
