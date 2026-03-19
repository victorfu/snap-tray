#ifndef SNAPTRAY_IUPDATESERVICE_H
#define SNAPTRAY_IUPDATESERVICE_H

#include "update/InstallSourceDetector.h"

#include <QString>
#include <utility>

enum class UpdatePlatform {
    MacOS,
    Windows,
    Other
};

enum class UpdateServiceKind {
    Sparkle,
    WinSparkle,
    ExternalManaged,
    Unsupported
};

struct UpdateCheckResult {
    enum class Status {
        Started,
        Unavailable,
        Failed
    };

    Status status = Status::Unavailable;
    QString message;

    static UpdateCheckResult started()
    {
        return {Status::Started, QString()};
    }

    static UpdateCheckResult unavailable(QString message)
    {
        return {Status::Unavailable, std::move(message)};
    }

    static UpdateCheckResult failed(QString message)
    {
        return {Status::Failed, std::move(message)};
    }

    bool isStarted() const { return status == Status::Started; }
};

class IUpdateService
{
public:
    virtual ~IUpdateService() = default;

    virtual UpdateServiceKind kind() const = 0;
    virtual InstallSource installSource() const = 0;
    virtual bool isExternallyManaged() const = 0;
    virtual QString managementMessage() const = 0;
    virtual bool initialize(QString* errorMessage = nullptr) = 0;
    virtual void startAutomaticChecks() = 0;
    virtual void syncSettings(bool autoCheckEnabled, int checkIntervalHours) = 0;
    virtual UpdateCheckResult checkForUpdatesInteractive() = 0;
};

UpdatePlatform currentUpdatePlatform();
QString updateManagementMessageForSource(InstallSource source);

#endif // SNAPTRAY_IUPDATESERVICE_H
