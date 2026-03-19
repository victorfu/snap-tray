#include "update/IUpdateService.h"
#include "update/UpdateCoordinator.h"
#include "update/UpdateServiceFactory.h"
#include "update/UpdateSettingsManager.h"
#include "update_config.h"
#include "version.h"

#ifdef Q_OS_WIN

#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>

#include <windows.h>

namespace {

static void notifySuccessfulUpdateCheck()
{
    QMetaObject::invokeMethod(&UpdateCoordinator::instance(),
                              &UpdateCoordinator::recordSuccessfulCheck,
                              Qt::QueuedConnection);
}

class WinSparkleUpdateService final : public IUpdateService
{
public:
    explicit WinSparkleUpdateService(InstallSource source)
        : m_installSource(source)
    {
    }

    ~WinSparkleUpdateService() override
    {
        if (m_cleanup) {
            m_cleanup();
        }

        if (m_module) {
            FreeLibrary(m_module);
        }
    }

    UpdateServiceKind kind() const override { return UpdateServiceKind::WinSparkle; }
    InstallSource installSource() const override { return m_installSource; }
    bool isExternallyManaged() const override { return false; }
    QString managementMessage() const override { return QString(); }

    bool initialize(QString* errorMessage) override
    {
        if (m_initialized) {
            if (errorMessage) {
                *errorMessage = QString();
            }
            return true;
        }

        if (QStringLiteral(SNAPTRAY_WINSPARKLE_APPCAST_URL).trimmed().isEmpty() ||
            QStringLiteral(SNAPTRAY_WINSPARKLE_PUBLIC_KEY).trimmed().isEmpty()) {
            m_lastError = QCoreApplication::translate(
                "WinSparkleUpdateService",
                "WinSparkle is not configured. Set the appcast URL and public key at build time.");
            if (errorMessage) {
                *errorMessage = m_lastError;
            }
            return false;
        }

        const QString dllPath = QDir(QCoreApplication::applicationDirPath())
                                    .filePath(QStringLiteral("WinSparkle.dll"));
        m_module = LoadLibraryW(reinterpret_cast<LPCWSTR>(dllPath.utf16()));
        if (!m_module) {
            m_lastError = QCoreApplication::translate(
                "WinSparkleUpdateService",
                "WinSparkle.dll is missing from the application directory.");
            if (errorMessage) {
                *errorMessage = m_lastError;
            }
            return false;
        }

        if (!resolveFunction("win_sparkle_init", reinterpret_cast<FARPROC*>(&m_init)) ||
            !resolveFunction("win_sparkle_cleanup", reinterpret_cast<FARPROC*>(&m_cleanup)) ||
            !resolveFunction("win_sparkle_check_update_with_ui",
                             reinterpret_cast<FARPROC*>(&m_checkWithUi)) ||
            !resolveFunction("win_sparkle_set_appcast_url",
                             reinterpret_cast<FARPROC*>(&m_setAppcastUrl)) ||
            !resolveFunction("win_sparkle_set_app_details",
                             reinterpret_cast<FARPROC*>(&m_setAppDetails)) ||
            !resolveFunction("win_sparkle_set_eddsa_public_key",
                             reinterpret_cast<FARPROC*>(&m_setEdDsaPublicKey)) ||
            !resolveFunction("win_sparkle_set_automatic_check_for_updates",
                             reinterpret_cast<FARPROC*>(&m_setAutomaticCheck)) ||
            !resolveFunction("win_sparkle_set_update_check_interval",
                             reinterpret_cast<FARPROC*>(&m_setUpdateInterval))) {
            if (errorMessage) {
                *errorMessage = m_lastError;
            }
            return false;
        }

        resolveFunction("win_sparkle_set_can_shutdown_callback",
                        reinterpret_cast<FARPROC*>(&m_setCanShutdownCallback),
                        false);
        resolveFunction("win_sparkle_set_shutdown_request_callback",
                        reinterpret_cast<FARPROC*>(&m_setShutdownRequestCallback),
                        false);
        resolveFunction("win_sparkle_set_did_find_update_callback",
                        reinterpret_cast<FARPROC*>(&m_setDidFindUpdateCallback),
                        false);
        resolveFunction("win_sparkle_set_did_not_find_update_callback",
                        reinterpret_cast<FARPROC*>(&m_setDidNotFindUpdateCallback),
                        false);

        m_setAppcastUrl(SNAPTRAY_WINSPARKLE_APPCAST_URL);
        m_setEdDsaPublicKey(SNAPTRAY_WINSPARKLE_PUBLIC_KEY);
        m_setAppDetails(L"Victor Fu",
                        reinterpret_cast<const wchar_t*>(QStringLiteral(SNAPTRAY_APP_NAME).utf16()),
                        reinterpret_cast<const wchar_t*>(QStringLiteral(SNAPTRAY_VERSION).utf16()));

        if (m_setCanShutdownCallback) {
            m_setCanShutdownCallback(&WinSparkleUpdateService::canShutdownCallback);
        }
        if (m_setShutdownRequestCallback) {
            m_setShutdownRequestCallback(&WinSparkleUpdateService::shutdownRequestCallback);
        }
        if (m_setDidFindUpdateCallback) {
            m_setDidFindUpdateCallback(&WinSparkleUpdateService::didFindUpdateCallback);
        }
        if (m_setDidNotFindUpdateCallback) {
            m_setDidNotFindUpdateCallback(&WinSparkleUpdateService::didNotFindUpdateCallback);
        }

        applyUpdaterSettings();
        m_init();
        m_initialized = true;

        if (errorMessage) {
            *errorMessage = QString();
        }
        return true;
    }

    void startAutomaticChecks() override
    {
        QString errorMessage;
        initialize(&errorMessage);
    }

    void syncSettings(bool autoCheckEnabled, int checkIntervalHours) override
    {
        m_autoCheckEnabled = autoCheckEnabled;
        m_checkIntervalHours = checkIntervalHours;

        if (!m_initialized && m_setAutomaticCheck == nullptr) {
            return;
        }

        applyUpdaterSettings();
    }

    UpdateCheckResult checkForUpdatesInteractive() override
    {
        QString errorMessage;
        if (!initialize(&errorMessage)) {
            return UpdateCheckResult::failed(errorMessage);
        }

        m_checkWithUi();
        return UpdateCheckResult::started();
    }

private:
    using InitFn = void (__cdecl*)();
    using CleanupFn = void (__cdecl*)();
    using CheckWithUiFn = void (__cdecl*)();
    using SetAppcastUrlFn = void (__cdecl*)(const char*);
    using SetAppDetailsFn = void (__cdecl*)(const wchar_t*, const wchar_t*, const wchar_t*);
    using SetEdDsaPublicKeyFn = void (__cdecl*)(const char*);
    using SetAutomaticCheckFn = void (__cdecl*)(int);
    using SetUpdateIntervalFn = void (__cdecl*)(int);
    using CanShutdownCallbackFn = int (__cdecl*)();
    using ShutdownRequestCallbackFn = void (__cdecl*)();
    using SetCanShutdownCallbackFn = void (__cdecl*)(CanShutdownCallbackFn);
    using SetShutdownRequestCallbackFn = void (__cdecl*)(ShutdownRequestCallbackFn);
    using DidFindUpdateCallbackFn = void (__cdecl*)();
    using DidNotFindUpdateCallbackFn = void (__cdecl*)();
    using SetDidFindUpdateCallbackFn = void (__cdecl*)(DidFindUpdateCallbackFn);
    using SetDidNotFindUpdateCallbackFn = void (__cdecl*)(DidNotFindUpdateCallbackFn);

    void applyUpdaterSettings()
    {
        if (m_setAutomaticCheck) {
            m_setAutomaticCheck(m_autoCheckEnabled ? 1 : 0);
        }
        if (m_setUpdateInterval) {
            m_setUpdateInterval(m_checkIntervalHours * 3600);
        }
    }

    bool resolveFunction(const char* name, FARPROC* target, bool required = true)
    {
        *target = GetProcAddress(m_module, name);
        if (*target != nullptr) {
            return true;
        }

        if (required) {
            m_lastError = QCoreApplication::translate(
                              "WinSparkleUpdateService",
                              "WinSparkle.dll is missing required API %1.")
                              .arg(QString::fromLatin1(name));
        }
        return false;
    }

    static int __cdecl canShutdownCallback()
    {
        return UpdateCoordinator::canRequestApplicationShutdown() ? 1 : 0;
    }

    static void __cdecl shutdownRequestCallback()
    {
        UpdateCoordinator::requestApplicationShutdown();
    }

    static void __cdecl didFindUpdateCallback()
    {
        notifySuccessfulUpdateCheck();
    }

    static void __cdecl didNotFindUpdateCallback()
    {
        notifySuccessfulUpdateCheck();
    }

    InstallSource m_installSource;
    bool m_initialized = false;
    bool m_autoCheckEnabled = true;
    int m_checkIntervalHours = UpdateSettingsManager::kDefaultCheckIntervalHours;
    QString m_lastError;
    HMODULE m_module = nullptr;
    InitFn m_init = nullptr;
    CleanupFn m_cleanup = nullptr;
    CheckWithUiFn m_checkWithUi = nullptr;
    SetAppcastUrlFn m_setAppcastUrl = nullptr;
    SetAppDetailsFn m_setAppDetails = nullptr;
    SetEdDsaPublicKeyFn m_setEdDsaPublicKey = nullptr;
    SetAutomaticCheckFn m_setAutomaticCheck = nullptr;
    SetUpdateIntervalFn m_setUpdateInterval = nullptr;
    SetCanShutdownCallbackFn m_setCanShutdownCallback = nullptr;
    SetShutdownRequestCallbackFn m_setShutdownRequestCallback = nullptr;
    SetDidFindUpdateCallbackFn m_setDidFindUpdateCallback = nullptr;
    SetDidNotFindUpdateCallbackFn m_setDidNotFindUpdateCallback = nullptr;
};

} // namespace

std::unique_ptr<IUpdateService> createPlatformUpdateService(UpdateServiceKind kind,
                                                            InstallSource source)
{
    if (kind == UpdateServiceKind::WinSparkle) {
        return std::make_unique<WinSparkleUpdateService>(source);
    }
    return nullptr;
}

#endif // Q_OS_WIN
