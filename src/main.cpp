#include <QApplication>
#include <QDebug>
#include "MainApplication.h"
#include "SingleInstanceGuard.h"
#include "settings/Settings.h"
#include "version.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Critical: Don't quit when last window closes (we're a tray app)
    app.setQuitOnLastWindowClosed(false);

    // Set application metadata
    app.setApplicationName(SnapTray::kApplicationName);
    app.setOrganizationName(SnapTray::kOrganizationName);
    app.setApplicationVersion(SNAPTRAY_VERSION);

    // Single instance check
    SingleInstanceGuard guard("com.victorfu.snaptray");
    if (!guard.tryLock()) {
        // Another instance is already running, notify it and exit
        qDebug() << "Another instance of SnapTray is already running. Exiting.";
        guard.sendActivateMessage();
        return 0;
    }

    MainApplication mainApp;

    QObject::connect(&guard, &SingleInstanceGuard::activateRequested,
                     &mainApp, &MainApplication::activate);

    mainApp.initialize();

    return app.exec();
}
