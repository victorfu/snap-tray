#include "recording/RecordingFileUtils.h"

#include <QByteArray>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace SnapTray::RecordingFileUtils {
namespace {

constexpr qsizetype kCopyBufferSize = 1024 * 1024;

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

bool pathsReferToSameFile(const QString &left, const QString &right)
{
    const QString leftPath = QDir::cleanPath(QFileInfo(left).absoluteFilePath());
    const QString rightPath = QDir::cleanPath(QFileInfo(right).absoluteFilePath());
#ifdef Q_OS_WIN
    return leftPath.compare(rightPath, Qt::CaseInsensitive) == 0;
#else
    return leftPath == rightPath;
#endif
}

} // namespace

bool replaceRecordingFile(const QString &sourcePath,
                          const QString &destinationPath,
                          QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }

    if (sourcePath.isEmpty() || destinationPath.isEmpty()) {
        setError(errorMessage, QStringLiteral("Source and destination paths are required"));
        return false;
    }

    if (pathsReferToSameFile(sourcePath, destinationPath)) {
        if (QFileInfo::exists(sourcePath) && QFileInfo(sourcePath).isFile()) {
            return true;
        }
        setError(errorMessage, QStringLiteral("Source recording does not exist"));
        return false;
    }

    // Open the source before touching the destination. This ordering is the
    // critical guarantee for failed overwrite attempts.
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        setError(errorMessage,
                 QStringLiteral("Failed to open source recording: %1").arg(source.errorString()));
        return false;
    }

    // Preserve the fast same-filesystem move for a destination that does not
    // exist. Existing destinations always take the transactional path below.
    if (!QFileInfo::exists(destinationPath)) {
        source.close();
        if (QFile::rename(sourcePath, destinationPath)) {
            return true;
        }
        if (!source.open(QIODevice::ReadOnly)) {
            setError(errorMessage,
                     QStringLiteral("Failed to reopen source recording: %1").arg(source.errorString()));
            return false;
        }
    }

    QSaveFile destination(destinationPath);
    // Direct-write fallback would truncate an existing recording before all
    // bytes are safely available, defeating the transactional guarantee.
    destination.setDirectWriteFallback(false);
    if (!destination.open(QIODevice::WriteOnly)) {
        setError(errorMessage,
                 QStringLiteral("Failed to prepare destination recording: %1")
                     .arg(destination.errorString()));
        return false;
    }

    QByteArray buffer(kCopyBufferSize, '\0');
    while (!source.atEnd()) {
        const qint64 bytesRead = source.read(buffer.data(), buffer.size());
        if (bytesRead < 0) {
            destination.cancelWriting();
            setError(errorMessage,
                     QStringLiteral("Failed to read source recording: %1").arg(source.errorString()));
            return false;
        }

        qint64 bytesWritten = 0;
        while (bytesWritten < bytesRead) {
            const qint64 result = destination.write(buffer.constData() + bytesWritten,
                                                    bytesRead - bytesWritten);
            if (result <= 0) {
                destination.cancelWriting();
                setError(errorMessage,
                         QStringLiteral("Failed to write destination recording: %1")
                             .arg(destination.errorString()));
                return false;
            }
            bytesWritten += result;
        }
    }

    if (!destination.commit()) {
        setError(errorMessage,
                 QStringLiteral("Failed to commit destination recording: %1")
                     .arg(destination.errorString()));
        return false;
    }

    // The destination is complete and atomically visible. A failed source
    // cleanup leaves a harmless duplicate rather than losing either recording.
    source.close();
    if (!QFile::remove(sourcePath)) {
        qWarning() << "RecordingFileUtils: Destination was committed but source cleanup failed:"
                   << sourcePath;
    }
    return true;
}

} // namespace SnapTray::RecordingFileUtils
