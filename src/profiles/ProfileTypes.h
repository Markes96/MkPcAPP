#pragma once
#include <string>

namespace profiles {

enum class PowerPlan { Saver, Balanced, HighPerformance, UltimatePerformance };

template <typename T>
struct Overridable {
    bool apply = false;
    T value{};
};

struct ProfileVariables {
    PowerPlan powerPlan = PowerPlan::Balanced;
    int screenOffTimeoutAcSec = 600;
    int screenOffTimeoutDcSec = 300;
    int sleepTimeoutAcSec = 1800;
    int sleepTimeoutDcSec = 900;
    // Defaults to 0 ("never") rather than mirroring sleepTimeout*, so loading
    // an existing saved profile that predates this field doesn't suddenly
    // start hibernating it.
    int hibernateTimeoutAcSec = 0;
    int hibernateTimeoutDcSec = 0;
    int brightnessPercent = 70;
    Overridable<int> volumePercent;
};

struct Profile {
    std::string id;
    std::string name;
    std::string icon;
    bool isPredefined = false;
    ProfileVariables vars;
};

enum class ApplyResult { Ok, Unsupported, Failed };

struct AppliedVariableResult {
    std::string variableName;
    ApplyResult result;
};

enum class RuleTriggerType { TimeWindow, BatteryBelow, PowerSourceChange };

struct AutomationRule {
    std::string id;
    bool enabled = true;
    RuleTriggerType type = RuleTriggerType::TimeWindow;
    int startMinuteOfDay = 0;    // TimeWindow, 0..1439
    int endMinuteOfDay = 0;      // TimeWindow, 0..1439; if end < start, the window wraps past midnight
    bool weekdaysOnly = false;   // TimeWindow
    int batteryThresholdPercent = 20; // BatteryBelow: fires when battery % is below this
    bool triggerOnAcToBattery = true; // PowerSourceChange: true = fires on AC->battery, false = battery->AC
    std::string targetProfileId;
};

} // namespace profiles
