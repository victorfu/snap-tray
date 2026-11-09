#include <QGuiApplication>
#include <QClipboard>
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include "platform/LinuxClipboardOwner.h"

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    const auto args = app.arguments();
    if (SnapTray::isLinuxClipboardOwnerRequest(args)) {
        const int result = SnapTray::runLinuxClipboardOwner(args);
        const QString marker = qEnvironmentVariable("SNAPTRAY_CLIPBOARD_PROBE_EXIT_FILE");
        if (!marker.isEmpty()) {
            QFile file(marker);
            if (file.open(QIODevice::WriteOnly)) file.write(QByteArray::number(result));
        }
        return result;
    }
    if (args.size() == 3 && args.at(1) == "--copy") {
        QImage image(16, 16, QImage::Format_RGB32);
        image.fill(QColor(args.at(2)));
        const QString appImage = qEnvironmentVariable("APPIMAGE").trimmed();
        const QString launchPath = appImage.isEmpty() ? app.applicationFilePath() : appImage;
        return SnapTray::copyImageToLinuxClipboard(image, launchPath) ? 0 : 1;
    }
    if (args.size() == 2 && args.at(1) == "--read") {
        const auto image = app.clipboard()->image();
        if (image.isNull()) return 1;
        QTextStream(stdout) << image.pixelColor(3, 3).name() << Qt::endl;
        return 0;
    }
    if (args.size() == 2 && args.at(1) == "--manager") {
        const auto image = app.clipboard()->image();
        if (image.isNull()) return 1;
        app.clipboard()->setImage(image);
        QGuiApplication::sync();
        QTextStream(stdout) << "owned" << Qt::endl;
        QTimer::singleShot(8000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    return 1;
}
