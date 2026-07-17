#include "qml/QmlOverlayManager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQuickView>
#include <QTimer>

#include <cstdlib>
#include <memory>

namespace {

bool* g_managerDestroyed = nullptr;

void verifyManagerSurvivedStaticTeardown()
{
    if (g_managerDestroyed && *g_managerDestroyed) {
        std::_Exit(EXIT_FAILURE);
    }
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // Register before constructing the singleton so this runs after any
    // destructor registered for a function-local static manager.
    if (std::atexit(verifyManagerSurvivedStaticTeardown) != 0) {
        return EXIT_FAILURE;
    }

    g_managerDestroyed = new bool(false);

    auto& manager = SnapTray::QmlOverlayManager::instance();
    QObject::connect(&manager, &QObject::destroyed, []() {
        *g_managerDestroyed = true;
    });

    if (!manager.engine()) {
        return EXIT_FAILURE;
    }

    std::unique_ptr<QQuickView> view;
    if (!QGuiApplication::screens().isEmpty()) {
        view.reset(manager.createScreenOverlay());
        if (!view) {
            return EXIT_FAILURE;
        }

        view->resize(32, 32);
        view->show();
    }

    QTimer::singleShot(50, &app, &QCoreApplication::quit);
    const int result = app.exec();

    if (view) {
        view->close();
    }
    view.reset();
    return result;
}
