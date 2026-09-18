#include "platform/LinuxQtQuickSmokeCheck.h"

#include <QDebug>
#include <QEventLoop>
#include <QImage>
#include <QQmlComponent>
#include <QQuickView>
#include <QSGRendererInterface>
#include <QTextStream>
#include <QTimer>

namespace SnapTray {

int runLinuxQtQuickSmokeCheck()
{
    constexpr int kRenderTimeoutMs = 10000;
    const QColor expectedColor(QStringLiteral("#35a7d4"));
    const QUrl sourceUrl(QStringLiteral("qrc:/AppImageSmoke.qml"));

    QQuickView view;
    view.setColor(Qt::transparent);
    QQmlComponent component(view.engine());
    component.setData(R"(
        import QtQuick
        Rectangle { width: 32; height: 32; color: "#35a7d4" }
    )", sourceUrl);
    auto* root = component.create();
    if (!root) {
        qCritical() << "Qt Quick smoke check could not load QML:" << component.errors();
        return 1;
    }
    view.setContent(sourceUrl, &component, root);

    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&loop] {
        qCritical() << "Qt Quick smoke check timed out waiting for a frame.";
        loop.exit(1);
    });

    bool frameChecked = false;
    QObject::connect(&view, &QQuickWindow::frameSwapped, &loop, [&] {
        if (frameChecked) return;
        frameChecked = true;

        // Inspect the renderer actually initialized by Qt, not the requested
        // backend name: silent fallback to the GPU must fail packaging.
        if (view.rendererInterface()->graphicsApi() != QSGRendererInterface::Software) {
            qCritical() << "Qt Quick smoke check did not use the software renderer.";
            loop.exit(1);
            return;
        }
        const QImage frame = view.grabWindow();
        if (frame.isNull() ||
            frame.pixelColor(frame.width() / 2, frame.height() / 2) != expectedColor) {
            qCritical() << "Qt Quick smoke check did not render the expected QML content.";
            loop.exit(1);
            return;
        }
        QTextStream(stdout) << "Qt Quick smoke check passed: software renderer produced a frame.\n";
        loop.exit(0);
    }, Qt::QueuedConnection);

    deadline.start(kRenderTimeoutMs);
    view.show();
    return loop.exec();
}

} // namespace SnapTray
