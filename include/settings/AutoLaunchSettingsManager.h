#pragma once

#include <optional>

class AutoLaunchSettingsManager
{
public:
    static AutoLaunchSettingsManager& instance();

    std::optional<bool> loadPreferredEnabled() const;
    void savePreferredEnabled(bool enabled);

private:
    AutoLaunchSettingsManager() = default;
    ~AutoLaunchSettingsManager() = default;
    AutoLaunchSettingsManager(const AutoLaunchSettingsManager&) = delete;
    AutoLaunchSettingsManager& operator=(const AutoLaunchSettingsManager&) = delete;

    static constexpr const char* kKeyPreferredEnabled = "general/startOnLogin";
};
