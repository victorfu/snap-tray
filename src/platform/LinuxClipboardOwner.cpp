#include "platform/LinuxClipboardOwner.h"

#include "platform/ClipboardImageFile.h"
#include <QBuffer>
#include <QClipboard>
#include <QDataStream>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QFile>
#include <QImageReader>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMimeData>
#include <QProcess>
#include <QTimer>
#include <QUuid>
#include <memory>
#ifdef Q_OS_LINUX
#include <QtGui/qguiapplication_platform.h>
#include <X11/Xlib.h>
#endif

namespace SnapTray {
namespace {
constexpr quint64 kMaxPngBytes = 256 * 1024 * 1024;
constexpr qint64 kMaxDecodedPixels = 128 * 1024 * 1024;
constexpr int kTokenBytes = 16;
constexpr char kAccepted = 1;
constexpr char kReadyToPublish = 2;

int remaining(const QElapsedTimer& timer, int timeoutMs)
{
    return qMax(0, timeoutMs - int(timer.elapsed()));
}

bool readBytes(QLocalSocket& socket, qint64 size, QByteArray& bytes,
               const QElapsedTimer& timer, int timeoutMs)
{
    bytes.clear();
    while (bytes.size() < size) {
        const auto chunk = socket.read(size - bytes.size());
        if (!chunk.isEmpty()) {
            bytes.append(chunk);
        } else if (remaining(timer, timeoutMs) == 0
                   || !socket.waitForReadyRead(remaining(timer, timeoutMs))) {
            return false;
        }
    }
    return true;
}

bool writeBytes(QLocalSocket& socket, const QByteArray& bytes,
                const QElapsedTimer& timer, int timeoutMs)
{
    if (socket.write(bytes) != bytes.size()) return false;
    while (socket.bytesToWrite() > 0) {
        if (remaining(timer, timeoutMs) == 0
            || !socket.waitForBytesWritten(remaining(timer, timeoutMs))) return false;
    }
    return true;
}

bool handoff(const QByteArray& payload, const QString& mode, const QString& launchPath, int timeoutMs,
             bool* publicationStarted = nullptr, quint32 expectedOwner = 0)
{
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    const QString name = QStringLiteral("snaptray-clipboard-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!server.listen(name)) return false;
    const QByteArray token = QUuid::createUuid().toRfc4122();
    QProcess owner;
    owner.setProgram(launchPath);
    QStringList args{QLatin1String(kClipboardOwnerArgument), name, QString::fromLatin1(token.toHex())};
    if (!mode.isEmpty())
        args << mode;
    if (expectedOwner)
        args << QString::number(expectedOwner);
    owner.setArguments(args);
    // Detached children must not keep shell pipes open after the CLI exits.
    owner.setStandardInputFile(QProcess::nullDevice());
    owner.setStandardOutputFile(QProcess::nullDevice());
    owner.setStandardErrorFile(QProcess::nullDevice());
    QElapsedTimer deadline;
    deadline.start();
    if (!owner.startDetached()) return false;
    if (!server.hasPendingConnections()
        && !server.waitForNewConnection(remaining(deadline, timeoutMs))) return false;
    std::unique_ptr<QLocalSocket> socket(server.nextPendingConnection());
    if (!socket) return false;
    QByteArray received;
    if (!readBytes(*socket, kTokenBytes, received, deadline, timeoutMs) || received != token) return false;
    QByteArray header;
    QDataStream writer(&header, QIODevice::WriteOnly);
    writer << quint64(payload.size());
    if (!writeBytes(*socket, header, deadline, timeoutMs)
        || !writeBytes(*socket, payload, deadline, timeoutMs)) return false;
    if (publicationStarted) {
        // The child has prepared the snapshot. Until we acknowledge,
        // it cannot publish the URL, so earlier failures can discard the PNG.
        if (!readBytes(*socket, 1, received, deadline, timeoutMs)
            || received.at(0) != kReadyToPublish) return false;
        *publicationStarted = true;
        if (!writeBytes(*socket, QByteArray(1, kReadyToPublish), deadline, timeoutMs)) return false;
    }
    if (!readBytes(*socket, 1, received, deadline, timeoutMs)
        || received.at(0) != kAccepted) return false;
    // Commit only after the child confirms ownership. A disconnected parent
    // during setup must not leave an unacknowledged owner running forever.
    if (!writeBytes(*socket, QByteArray(1, kAccepted), deadline, timeoutMs)) return false;
    socket->disconnectFromServer();
    return true;
}
}

std::optional<quint32> linuxClipboardOwnerWindow()
{
#ifdef Q_OS_LINUX
    auto* x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!x11 || !x11->display())
        return std::nullopt;
    auto* display = x11->display();
    const auto selection = XInternAtom(display, "CLIPBOARD", False);
    return static_cast<quint32>(XGetSelectionOwner(display, selection));
#else
    return std::nullopt;
#endif
}

bool isLinuxClipboardOwnerRequest(const QStringList& arguments)
{
    return arguments.size() >= 2 && arguments.at(1) == QLatin1String(kClipboardOwnerArgument);
}

bool copyImageToLinuxClipboard(const QImage& image, const QString& launchPath, int timeoutMs)
{
    if (image.isNull() || launchPath.isEmpty() || timeoutMs <= 0
        || qint64(image.width()) * image.height() > kMaxDecodedPixels) return false;
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")
        || png.isEmpty() || quint64(png.size()) > kMaxPngBytes) return false;

    return handoff(png, {}, launchPath, timeoutMs);
}

bool copyFileToLinuxClipboard(const QString& path, bool includePixels, const QString& launchPath,
                              bool& publicationStarted, quint32 expectedOwner)
{
    publicationStarted = false;
    return handoff(path.toUtf8(), includePixels ? QStringLiteral("image-file") : QStringLiteral("file"),
                   launchPath, kClipboardHandoffTimeoutMs, &publicationStarted, expectedOwner);
}

int runLinuxClipboardOwner(const QStringList& arguments)
{
    if ((arguments.size() < 4 || arguments.size() > 6) || !isLinuxClipboardOwnerRequest(arguments))
        return 1;
    const QString mode = arguments.value(4);
    if (!mode.isEmpty() && mode != QStringLiteral("image-file") && mode != QStringLiteral("file"))
        return 1;
    bool validOwner = false;
    const quint32 expectedOwner = arguments.value(5).toUInt(&validOwner);
    if (arguments.size() == 6 && (!validOwner || !expectedOwner || mode.isEmpty())) return 1;
    const QByteArray token = QByteArray::fromHex(arguments.at(3).toLatin1());
    if (token.size() != kTokenBytes || arguments.at(3).size() != kTokenBytes * 2) return 1;
    if (QGuiApplication::platformName() != QStringLiteral("xcb")) return 1;
    QGuiApplication::setQuitOnLastWindowClosed(false);
    QElapsedTimer deadline;
    deadline.start();
    QLocalSocket socket;
    socket.connectToServer(arguments.at(2));
    if (!socket.waitForConnected(kClipboardHandoffTimeoutMs)
        || !writeBytes(socket, token, deadline, kClipboardHandoffTimeoutMs)) return 1;
    QByteArray bytes;
    if (!readBytes(socket, sizeof(quint64), bytes, deadline, kClipboardHandoffTimeoutMs)) return 1;
    QDataStream header(bytes);
    quint64 length = 0;
    header >> length;
    if (header.status() != QDataStream::Ok || length == 0 || length > kMaxPngBytes) return 1;
    QByteArray payload;
    if (!readBytes(socket, qint64(length), payload, deadline, kClipboardHandoffTimeoutMs)) return 1;
    auto* clipboard = QGuiApplication::clipboard();
    if (!clipboard) return 1;
    auto mime = std::make_unique<QMimeData>();
    if (!mode.isEmpty()) {
        const QString path = QString::fromUtf8(payload);
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > kClipboardImageQuota)
            return 1;
        const QUrl url = QUrl::fromLocalFile(path);
        mime->setUrls({url});
        mime->setData(QStringLiteral("x-special/gnome-copied-files"), "copy\n" + url.toEncoded());
        if (mode == QStringLiteral("image-file")) {
            const auto png = file.readAll();
            if (png.size() != file.size()) return 1;
            mime->setData(QStringLiteral("image/png"), png);
        }
    } else {
        QBuffer buffer(&payload);
        if (!buffer.open(QIODevice::ReadOnly)) return 1;
        QImageReader reader(&buffer, "PNG");
        const QSize size = reader.size();
        if (!size.isValid() || qint64(size.width()) * size.height() > kMaxDecodedPixels) return 1;
        const QImage image = reader.read();
        if (image.isNull()) return 1;
        mime->setData(QStringLiteral("image/png"), payload);
        mime->setImageData(image);
    }
    if (!mode.isEmpty()
        && (!writeBytes(socket, QByteArray(1, kReadyToPublish), deadline, kClipboardHandoffTimeoutMs)
            || !readBytes(socket, 1, bytes, deadline, kClipboardHandoffTimeoutMs)
            || bytes.at(0) != kReadyToPublish)) return 1;
    // A normal GUI exit transfers only the selection it still owns. The
    // previous owner may have been replaced while this process was starting.
    if (expectedOwner && linuxClipboardOwnerWindow() != expectedOwner) return 1;
    clipboard->setMimeData(mime.release());
    QGuiApplication::sync();
    if (!clipboard->ownsClipboard()
        || !writeBytes(socket, QByteArray(1, kAccepted), deadline, kClipboardHandoffTimeoutMs)
        || !readBytes(socket, 1, bytes, deadline, kClipboardHandoffTimeoutMs)
        || bytes.at(0) != kAccepted) return 1;
    socket.disconnectFromServer();
    QObject::connect(clipboard, &QClipboard::changed, QCoreApplication::instance(),
        [clipboard](QClipboard::Mode mode) {
            if (mode == QClipboard::Clipboard && !clipboard->ownsClipboard()) QCoreApplication::quit();
        });
    // Also covers ownership lost while the commit was being received.
    QTimer::singleShot(0, QCoreApplication::instance(), [clipboard] {
        if (!clipboard->ownsClipboard()) QCoreApplication::quit();
    });
    return QCoreApplication::exec();
}
} // namespace SnapTray
