#include <QApplication>
#include "MainApplication.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Critical: Don't quit when last window closes (we're a tray app)
    app.setQuitOnLastWindowClosed(false);

    // Set application metadata
    app.setApplicationName("SnapTray");
    app.setOrganizationName("MySoft");
    app.setApplicationVersion("1.0.0");

    MainApplication mainApp;
    mainApp.initialize();

    return app.exec();
}
