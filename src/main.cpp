#include <QApplication>
#include "MainApplication.h"
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

    MainApplication mainApp;
    mainApp.initialize();

    return app.exec();
}
