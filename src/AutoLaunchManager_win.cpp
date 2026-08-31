#include "AutoLaunchManager.h"

#include "settings/AutoLaunchSettingsManager.h"
#include "settings/AutoLaunchSyncPolicy.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

#include <windows.h>
#include <appmodel.h>
#include <roapi.h>
#include <unknwn.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>

#include <utility>

namespace {

constexpr auto kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto kAppName = "SnapTray";
constexpr auto kStartupTaskId = L"SnapTrayStartup";

namespace WindowsApplicationModel = winrt::Windows::ApplicationModel;
namespace WindowsFoundation = winrt::Windows::Foundation;
using StartupTask = WindowsApplicationModel::StartupTask;

class ThreadWinRtApartment
{
public:
    ThreadWinRtApartment()
        : m_result(RoInitialize(RO_INIT_SINGLETHREADED))
    {
        if (FAILED(m_result) && m_result != RPC_E_CHANGED_MODE) {
            winrt::check_hresult(m_result);
        }
    }

    ~ThreadWinRtApartment()
    {
        if (SUCCEEDED(m_result)) {
            RoUninitialize();
        }
    }

private:
    HRESULT m_result = E_FAIL;
};

void ensureWinRtApartment()
{
    thread_local ThreadWinRtApartment apartment;
    (void)apartment;
}

QString currentExecutablePath()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

QString canonicalStartupCommand()
{
    return QStringLiteral("\"%1\"").arg(currentExecutablePath());
}

QString startupEntryValue()
{
    QSettings settings(kRunKey, QSettings::NativeFormat);
    return settings.value(kAppName).toString().trimmed();
}

bool startupEntryExists()
{
    QSettings settings(kRunKey, QSettings::NativeFormat);
    return settings.contains(kAppName);
}

SnapTray::AutoLaunchSyncState startupEntryState()
{
    return SnapTray::classifyWindowsAutoLaunchEntry(
        startupEntryExists(),
        startupEntryValue(),
        currentExecutablePath());
}

bool isPackagedApp()
{
    UINT32 length = 0;
    const LONG result = GetCurrentPackageFullName(&length, nullptr);
    return result == ERROR_INSUFFICIENT_BUFFER;
}

AutoLaunchStatus registryStatus()
{
    const auto state = startupEntryState();
    if (state == SnapTray::AutoLaunchSyncState::EnabledCurrentCanonical
        || state == SnapTray::AutoLaunchSyncState::EnabledCurrentLegacy) {
        return {AutoLaunchState::Enabled, {}};
    }

    if (state == SnapTray::AutoLaunchSyncState::EnabledOther) {
        return {
            AutoLaunchState::Unavailable,
            QStringLiteral("Another startup command is already registered for SnapTray.")
        };
    }

    return {AutoLaunchState::Disabled, {}};
}

AutoLaunchStatus startupTaskStatus(WindowsApplicationModel::StartupTaskState state)
{
    using WindowsApplicationModel::StartupTaskState;

    switch (state) {
    case StartupTaskState::Disabled:
        return {AutoLaunchState::Disabled, {}};
    case StartupTaskState::Enabled:
        return {AutoLaunchState::Enabled, {}};
    case StartupTaskState::DisabledByUser:
        return {AutoLaunchState::DisabledByUser, {}};
    case StartupTaskState::DisabledByPolicy:
        return {AutoLaunchState::DisabledByPolicy, {}};
    case StartupTaskState::EnabledByPolicy:
        return {AutoLaunchState::EnabledByPolicy, {}};
    }

    return {
        AutoLaunchState::Unavailable,
        QStringLiteral("Windows returned an unknown startup task state.")
    };
}

QString winRtErrorMessage(const winrt::hresult_error& error)
{
    const auto message = error.message();
    if (!message.empty()) {
        return QString::fromWCharArray(message.c_str());
    }

    return QStringLiteral("Windows error 0x%1")
        .arg(static_cast<quint32>(error.code()), 8, 16, QLatin1Char('0'));
}

QString asyncFailureMessage(WindowsFoundation::AsyncStatus status)
{
    if (status == WindowsFoundation::AsyncStatus::Canceled) {
        return QStringLiteral("The Windows startup task request was canceled.");
    }

    return QStringLiteral("The Windows startup task request failed.");
}

AutoLaunchStatus readStartupTaskStatus(const StartupTask& task)
{
    try {
        return startupTaskStatus(task.State());
    } catch (const winrt::hresult_error& error) {
        return {AutoLaunchState::Unavailable, winRtErrorMessage(error)};
    }
}

using StartupTaskCallback = std::function<void(StartupTask, QString)>;

void getPackagedStartupTask(StartupTaskCallback callback)
{
    try {
        ensureWinRtApartment();
        auto operation = StartupTask::GetAsync(kStartupTaskId);
        operation.Completed(
            [callback = std::move(callback)](
                const WindowsFoundation::IAsyncOperation<StartupTask>& asyncOperation,
                WindowsFoundation::AsyncStatus asyncStatus) mutable {
                if (asyncStatus != WindowsFoundation::AsyncStatus::Completed) {
                    callback(nullptr, asyncFailureMessage(asyncStatus));
                    return;
                }

                try {
                    auto task = asyncOperation.GetResults();
                    if (!task) {
                        callback(
                            nullptr,
                            QStringLiteral("The SnapTray startup task is missing from the package manifest."));
                        return;
                    }
                    callback(task, {});
                } catch (const winrt::hresult_error& error) {
                    callback(nullptr, winRtErrorMessage(error));
                }
            });
    } catch (const winrt::hresult_error& error) {
        callback(nullptr, winRtErrorMessage(error));
    }
}

void requestPackagedStartupChange(
    const StartupTask& task,
    bool enabled,
    AutoLaunchManager::StatusCallback callback)
{
    AutoLaunchStatus currentStatus = readStartupTaskStatus(task);
    if (currentStatus.state == AutoLaunchState::Unavailable) {
        callback(std::move(currentStatus));
        return;
    }

    if (enabled) {
        if (currentStatus.state != AutoLaunchState::Disabled) {
            callback(std::move(currentStatus));
            return;
        }

        try {
            ensureWinRtApartment();
            auto operation = task.RequestEnableAsync();
            operation.Completed(
                [task, callback = std::move(callback)](
                    const WindowsFoundation::IAsyncOperation<WindowsApplicationModel::StartupTaskState>& asyncOperation,
                    WindowsFoundation::AsyncStatus asyncStatus) mutable {
                    if (asyncStatus != WindowsFoundation::AsyncStatus::Completed) {
                        AutoLaunchStatus status = readStartupTaskStatus(task);
                        status.errorMessage = asyncFailureMessage(asyncStatus);
                        callback(std::move(status));
                        return;
                    }

                    try {
                        callback(startupTaskStatus(asyncOperation.GetResults()));
                    } catch (const winrt::hresult_error& error) {
                        AutoLaunchStatus status = readStartupTaskStatus(task);
                        status.errorMessage = winRtErrorMessage(error);
                        callback(std::move(status));
                    }
                });
        } catch (const winrt::hresult_error& error) {
            currentStatus.errorMessage = winRtErrorMessage(error);
            callback(std::move(currentStatus));
        }
        return;
    }

    if (currentStatus.state != AutoLaunchState::Enabled) {
        callback(std::move(currentStatus));
        return;
    }

    try {
        task.Disable();
        callback(readStartupTaskStatus(task));
    } catch (const winrt::hresult_error& error) {
        currentStatus.errorMessage = winRtErrorMessage(error);
        callback(std::move(currentStatus));
    }
}

} // namespace

bool AutoLaunchManager::isEnabled()
{
    return !isPackagedApp() && registryStatus().isEnabled();
}

bool AutoLaunchManager::setEnabled(bool enabled)
{
    if (isPackagedApp() || startupEntryState() == SnapTray::AutoLaunchSyncState::EnabledOther) {
        return false;
    }

    QSettings settings(kRunKey, QSettings::NativeFormat);

    if (enabled) {
        settings.setValue(kAppName, canonicalStartupCommand());
    } else {
        settings.remove(kAppName);
    }

    settings.sync();
    return settings.status() == QSettings::NoError;
}

bool AutoLaunchManager::syncWithPreference()
{
    if (isPackagedApp()) {
        // The manifest StartupTask is authoritative for MSIX. It is queried
        // asynchronously by SettingsBackend and must never be overwritten from
        // a cached application preference.
        return false;
    }

    const auto state = startupEntryState();
    const auto plan = SnapTray::buildAutoLaunchStartupSyncPlan(std::nullopt, state);
    if (plan.shouldApplyChange) {
        (void)setEnabled(plan.targetEnabled);
    }

    const AutoLaunchStatus actualStatus = registryStatus();
    if (actualStatus.state != AutoLaunchState::Unavailable) {
        AutoLaunchSettingsManager::instance().savePreferredEnabled(actualStatus.isEnabled());
    }
    return actualStatus.isEnabled();
}

void AutoLaunchManager::queryStatus(StatusCallback callback)
{
    if (!callback) {
        return;
    }

    if (!isPackagedApp()) {
        callback(registryStatus());
        return;
    }

    getPackagedStartupTask(
        [callback = std::move(callback)](StartupTask task, const QString& errorMessage) mutable {
            if (!task) {
                callback({AutoLaunchState::Unavailable, errorMessage});
                return;
            }
            callback(readStartupTaskStatus(task));
        });
}

void AutoLaunchManager::setEnabledAsync(bool enabled, StatusCallback callback)
{
    if (!callback) {
        return;
    }

    if (!isPackagedApp()) {
        AutoLaunchStatus status = registryStatus();
        if (!setEnabled(enabled)) {
            status.errorMessage = QStringLiteral("Failed to update the Windows startup registry.");
            callback(std::move(status));
            return;
        }
        callback(registryStatus());
        return;
    }

    getPackagedStartupTask(
        [enabled, callback = std::move(callback)](
            StartupTask task,
            const QString& errorMessage) mutable {
            if (!task) {
                callback({AutoLaunchState::Unavailable, errorMessage});
                return;
            }
            requestPackagedStartupChange(task, enabled, std::move(callback));
        });
}
