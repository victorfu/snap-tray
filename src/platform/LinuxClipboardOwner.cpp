#include "platform/LinuxClipboardOwner.h"

#include <QBuffer>
#include <QClipboard>
#include <QDataStream>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImageReader>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMimeData>
#include <QProcess>
#include <QTimer>
#include <QUuid>
#include <memory>

namespace SnapTray {
namespace {
constexpr quint64 kMaxPngBytes = 256 * 1024 * 1024;
constexpr qint64 kMaxDecodedPixels = 128 * 1024 * 1024;
constexpr int kTokenBytes = 16;
constexpr char kAccepted = 1;

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

    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    const QString name = QStringLiteral("snaptray-clipboard-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!server.listen(name)) return false;
    const QByteArray token = QUuid::createUuid().toRfc4122();
    QProcess owner;
    owner.setProgram(launchPath);
    owner.setArguments({QLatin1String(kClipboardOwnerArgument), name, QString::fromLatin1(token.toHex())});
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
    writer << quint64(png.size());
    if (!writeBytes(*socket, header, deadline, timeoutMs)
        || !writeBytes(*socket, png, deadline, timeoutMs)
        || !readBytes(*socket, 1, received, deadline, timeoutMs)
        || received.at(0) != kAccepted) return false;
    // Commit only after the child confirms ownership. A disconnected parent
    // during setup must not leave an unacknowledged owner running forever.
    if (!writeBytes(*socket, QByteArray(1, kAccepted), deadline, timeoutMs)) return false;
    socket->disconnectFromServer();
    return true;
}

int runLinuxClipboardOwner(const QStringList& arguments)
{
    if (arguments.size() != 4 || !isLinuxClipboardOwnerRequest(arguments)) return 1;
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
    QByteArray png;
    if (!readBytes(socket, qint64(length), png, deadline, kClipboardHandoffTimeoutMs)) return 1;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::ReadOnly)) return 1;
    QImageReader reader(&buffer, "PNG");
    const QSize size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > kMaxDecodedPixels) return 1;
    const QImage image = reader.read();
    if (image.isNull()) return 1;
    auto* clipboard = QGuiApplication::clipboard();
    if (!clipboard) return 1;
    auto* mime = new QMimeData;
    mime->setData(QStringLiteral("image/png"), png);
    mime->setImageData(image);
    clipboard->setMimeData(mime);
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
