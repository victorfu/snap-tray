#ifndef AUTOLAUNCHMANAGER_H
#define AUTOLAUNCHMANAGER_H

#include <functional>

#include <QString>

enum class AutoLaunchState {
    Disabled,
    Enabled,
    DisabledByUser,
    DisabledByPolicy,
    EnabledByPolicy,
    Unavailable
};

struct AutoLaunchStatus
{
    AutoLaunchState state = AutoLaunchState::Unavailable;
    QString errorMessage;

    bool isEnabled() const
    {
        return state == AutoLaunchState::Enabled
            || state == AutoLaunchState::EnabledByPolicy;
    }

    bool canChange() const
    {
        return state == AutoLaunchState::Disabled
            || state == AutoLaunchState::Enabled;
    }
};

class AutoLaunchManager
{
public:
    using StatusCallback = std::function<void(AutoLaunchStatus)>;

    static bool isEnabled();
    static bool setEnabled(bool enabled);
    static bool syncWithPreference();
    static void queryStatus(StatusCallback callback);
    static void setEnabledAsync(bool enabled, StatusCallback callback);
};

#endif // AUTOLAUNCHMANAGER_H
