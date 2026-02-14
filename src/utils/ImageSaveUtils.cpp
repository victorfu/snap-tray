#include "utils/ImageSaveUtils.h"

#include <QFileInfo>
#include <QImageWriter>
#include <QSaveFile>
#include <QSet>
#include <QStringList>

namespace {

QByteArray normalizeFormat(QByteArray format)
{
    format = format.trimmed().toLower();
    while (!format.isEmpty() && format.startsWith('.')) {
        format.remove(0, 1);
    }
    return format;
}

QByteArray canonicalFormat(QByteArray format)
{
    if (format == "jpg") {
        return "jpeg";
    }
    if (format == "tif") {
        return "tiff";
    }
    return format;
}

bool isSupportedFormat(const QByteArray& format)
{
    static const QSet<QByteArray> supported = []() {
        QSet<QByteArray> values;
        const QList<QByteArray> formats = QImageWriter::supportedImageFormats();
        for (QByteArray f : formats) {
            values.insert(normalizeFormat(f));
        }
        return values;
    }();

    return supported.contains(normalizeFormat(format));
}

QString formatListForError()
{
    QStringList list;
    const QList<QByteArray> formats = QImageWriter::supportedImageFormats();
    list.reserve(formats.size());
    for (const QByteArray& format : formats) {
        list.push_back(QString::fromLatin1(format));
    }
    list.sort();
    return list.join(", ");
}

} // namespace

bool ImageSaveUtils::saveImageAtomically(const QImage& image,
                                         const QString& filePath,
                                         const QByteArray& explicitFormat,
                                         Error* error)
{
    if (image.isNull()) {
        setError(error, QStringLiteral("write"), QStringLiteral("Image is null"));
        return false;
    }

    const QByteArray format = resolveFormat(filePath, explicitFormat, error);
    if (format.isEmpty()) {
        return false;
    }

    QSaveFile saveFile(filePath);
    // Preserve overwrite behavior on filesystems where the target file is writable
    // but the directory disallows creating a temp sibling for atomic rename.
    saveFile.setDirectWriteFallback(true);
    if (!saveFile.open(QIODevice::WriteOnly)) {
        const QString saveError = saveFile.errorString().trimmed();
        setError(error, QStringLiteral("open"),
                 saveError.isEmpty() ? QStringLiteral("Failed to open output file")
                                     : saveError);
        return false;
    }

    QImageWriter writer(&saveFile, format);
    if (!writer.write(image)) {
        saveFile.cancelWriting();
        const QString writeError = writer.errorString().trimmed();
        setError(error, QStringLiteral("write"),
                 writeError.isEmpty() ? QStringLiteral("Failed to encode image")
                                      : writeError);
        return false;
    }

    if (!saveFile.commit()) {
        const QString commitError = saveFile.errorString().trimmed();
        setError(error, QStringLiteral("commit"),
                 commitError.isEmpty() ? QStringLiteral("Failed to commit output file")
                                       : commitError);
        return false;
    }

    return true;
}

bool ImageSaveUtils::savePixmapAtomically(const QPixmap& pixmap,
                                          const QString& filePath,
                                          const QByteArray& explicitFormat,
                                          Error* error)
{
    if (pixmap.isNull()) {
        setError(error, QStringLiteral("write"), QStringLiteral("Pixmap is null"));
        return false;
    }
    return saveImageAtomically(pixmap.toImage(), filePath, explicitFormat, error);
}

QByteArray ImageSaveUtils::resolveFormat(const QString& filePath,
                                         const QByteArray& explicitFormat,
                                         Error* error)
{
    QByteArray format = normalizeFormat(explicitFormat);
    if (format.isEmpty()) {
        format = normalizeFormat(QFileInfo(filePath).suffix().toLatin1());
    }
    if (format.isEmpty()) {
        format = QByteArrayLiteral("png");
    }

    format = canonicalFormat(format);
    if (!isSupportedFormat(format)) {
        setError(error,
                 QStringLiteral("format"),
                 QStringLiteral("Unsupported image format '%1' (supported: %2)")
                     .arg(QString::fromLatin1(format), formatListForError()));
        return QByteArray();
    }

    return format;
}

void ImageSaveUtils::setError(Error* error, const QString& stage, const QString& message)
{
    if (!error) {
        return;
    }
    error->stage = stage;
    error->message = message;
}
