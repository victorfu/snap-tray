#include "MainApplication.h"
#include "AutoLaunchManager.h"
#include "SingleInstanceGuard.h"
#include "cli/CLIHandler.h"
#include "platform/PlatformCapabilities.h"
#include "platform/QtQuickBackendPolicy.h"
#include "settings/LanguageManager.h"
#include "settings/Settings.h"
#include "version.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QOperatingSystemVersion>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

// Import static QML plugin so the linker includes SnapTrayQml types.
Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

namespace {

bool shouldBypassRuntimeGuardForMetadataCommand(const QStringList& arguments)
{
    return arguments.size() >= 2 &&
        (arguments.at(1) == QStringLiteral("--help") ||
         arguments.at(1) == QStringLiteral("-h") ||
         arguments.at(1) == QStringLiteral("--version") ||
         arguments.at(1) == QStringLiteral("-v"));
}

int reportUnsupportedRuntime(const QString& message)
{
    QTextStream err(stderr);
    err << "Error: " << message << "\n";
    return 1;
}

} // namespace

int main(int argc, char* argv[])
{
    SnapTray::applyQtQuickGraphicsBackendPolicy(
        SnapTray::selectQtQuickGraphicsBackendPolicy(QOperatingSystemVersion::current()));

    // Check for CLI arguments before creating QApplication
    QStringList arguments;
    for (int i = 0; i < argc; ++i) {
        arguments.append(QString::fromLocal8Bit(argv[i]));
    }

    if (shouldBypassRuntimeGuardForMetadataCommand(arguments)) {
        QCoreApplication app(argc, argv);
        app.setApplicationName(SnapTray::kApplicationName);
        app.setOrganizationName(SnapTray::kOrganizationName);
        app.setApplicationVersion(SNAPTRAY_VERSION);

        SnapTray::CLI::CLIHandler cliHandler;
        auto result = cliHandler.process(arguments);

        QTextStream out(stdout);
        QTextStream err(stderr);
        if (result.isSuccess()) {
            out << result.message << "\n";
        }
        else {
            err << "Error: " << result.message << "\n";
        }

        return static_cast<int>(result.code);
    }

    // CLI mode: process command and exit
    if (SnapTray::CLI::CLIHandler::hasArguments(arguments)) {
        // Use QApplication for full GUI compatibility (clipboard, screen capture)
        QApplication app(argc, argv);
        app.setApplicationName(SnapTray::kApplicationName);
        app.setOrganizationName(SnapTray::kOrganizationName);
        app.setApplicationVersion(SNAPTRAY_VERSION);

        const auto capabilities = SnapTray::currentPlatformCapabilities();
        if (!capabilities.isRuntimeSupported &&
            !shouldBypassRuntimeGuardForMetadataCommand(arguments)) {
            return reportUnsupportedRuntime(capabilities.unsupportedRuntimeMessage);
        }

        // Ensure settings migration/cleanup runs in CLI mode as well.
        (void)SnapTray::getSettings();

        SnapTray::CLI::CLIHandler cliHandler;
        auto result = cliHandler.process(arguments);

        // Output result
        QTextStream out(stdout);
        QTextStream err(stderr);

        if (!result.data.isEmpty()) {
            // Binary data (--raw)
            QFile outFile;
            if (outFile.open(stdout, QIODevice::WriteOnly)) {
                outFile.write(result.data);
            }
        }
        else if (!result.message.isEmpty()) {
            if (result.isSuccess()) {
                out << result.message << "\n";
            }
            else {
                err << "Error: " << result.message << "\n";
            }
        }

        // Run brief event loop to allow clipboard operations to complete
        // This is necessary on macOS where clipboard uses pasteboard promises
        // 250ms gives sufficient time for the pasteboard to persist the data
        QTimer::singleShot(250, &app, &QApplication::quit);
        app.exec();

        return static_cast<int>(result.code);
    }

    // GUI mode: standard application startup
    QApplication app(argc, argv);

    // Critical: Don't quit when last window closes (we're a tray app)
    app.setQuitOnLastWindowClosed(false);

    // Set application metadata
    app.setApplicationName(SnapTray::kApplicationName);
    app.setOrganizationName(SnapTray::kOrganizationName);
    app.setApplicationVersion(SNAPTRAY_VERSION);

    const auto capabilities = SnapTray::currentPlatformCapabilities();
    if (!capabilities.isRuntimeSupported) {
        return reportUnsupportedRuntime(capabilities.unsupportedRuntimeMessage);
    }

    // Run settings migration/cleanup during startup.
    (void)SnapTray::getSettings();

    // Load translations before any UI creation
    auto& langManager = LanguageManager::instance();
    const QString savedLanguage = langManager.loadLanguage();
    if (!langManager.loadTranslation(savedLanguage)) {
        qWarning() << "Failed to load translation for language:" << savedLanguage
                   << "- falling back to English";
    }

    // Single instance check
    SingleInstanceGuard guard(SNAPTRAY_APP_BUNDLE_ID);
    if (!guard.tryLock()) {
        // Another instance is already running, notify it and exit
        qDebug() << "Another instance of SnapTray is already running. Exiting.";
        guard.sendActivateMessage();
        return 0;
    }

    AutoLaunchManager::syncWithPreference();

    MainApplication mainApp;

    QObject::connect(
        &guard, &SingleInstanceGuard::activateRequested, &mainApp, &MainApplication::activate);

    // Connect CLI command handler
    QObject::connect(
        &guard,
        &SingleInstanceGuard::commandReceived,
        &mainApp,
        &MainApplication::handleCLICommand);

    mainApp.initialize();

    return app.exec();
}
