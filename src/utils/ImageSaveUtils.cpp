#include "utils/ImageSaveUtils.h"

#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <QDir>
#include <QUuid>

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

ImageSaveUtils::UniqueSaveResult ImageSaveUtils::saveImageUnique(
    const QImage& image, const UniqueSaveSpec& spec, const QByteArray& explicitFormat)
{
    return saveImageUniqueWithHooks(image, spec, explicitFormat, {});
}

ImageSaveUtils::UniqueSaveResult ImageSaveUtils::saveImageUniqueWithHooks(
    const QImage& image, const UniqueSaveSpec& spec, const QByteArray& explicitFormat,
    const UniqueSaveHooks& hooks)
{
    UniqueSaveResult result;
    if (image.isNull()) {
        setError(&result.error, QStringLiteral("write"), QStringLiteral("Image is null"));
        return result;
    }
    if (spec.outputDir.isEmpty() || !QDir().mkpath(spec.outputDir)) {
        setError(&result.error, QStringLiteral("open"), QStringLiteral("Unable to create output directory"));
        return result;
    }
    const QDir dir(QDir(spec.outputDir).absolutePath());
    auto context = spec.context;
    context.outputDir = dir.path();
    if (!context.timestamp.isValid())
        context.timestamp = QDateTime::currentDateTime();
    const auto initial = FilenameTemplateEngine::renderFilename(spec.filenameTemplate, context);
    result.renderWarning = initial.error;
    if (initial.filename.isEmpty()) {
        setError(&result.error, QStringLiteral("open"), initial.error);
        return result;
    }
    result.filePath = dir.filePath(initial.filename);
    const QByteArray format = resolveFormat(result.filePath, explicitFormat, &result.error);
    if (format.isEmpty())
        return result;

    struct TemporaryPath {
        QString path;
        ~TemporaryPath() { if (!path.isEmpty()) QFile::remove(path); }
    } temporary;
    {
        // Exclusive creation keeps the staging path safe from collisions while
        // using normal output permissions (including umask and directory ACLs).
        QFile file(dir.filePath(QStringLiteral(".snaptray-save-")
                                + QUuid::createUuid().toString(QUuid::Id128)));
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            setError(&result.error, QStringLiteral("open"), file.errorString());
            return result;
        }
        temporary.path = file.fileName();
        QImageWriter writer(&file, format);
        if (!writer.write(image)) {
            setError(&result.error, QStringLiteral("write"), writer.errorString());
            return result;
        }
        if (!file.flush()) {
            setError(&result.error, QStringLiteral("write"), file.errorString());
            return result;
        }
        // Destruction closes the native handle as well, including on Windows.
    }

    constexpr int kNumberedAttempts = 100;
    for (int attempt = 0; attempt <= kNumberedAttempts + 1; ++attempt) {
        const QString uuid = attempt > kNumberedAttempts
            ? (hooks.uuidSuffix.isEmpty() ? QUuid::createUuid().toString(QUuid::Id128).left(8)
                                         : hooks.uuidSuffix)
            : QString();
        const QString candidate = FilenameTemplateEngine::collisionFilename(
            spec.filenameTemplate, context, initial.filename, attempt, uuid);
        if (candidate.isEmpty()) {
            setError(&result.error, QStringLiteral("commit"),
                     QStringLiteral("Filename length limit is too small for a collision suffix"));
            return result;
        }
        result.filePath = dir.filePath(candidate);
        const auto published = hooks.publish
            ? hooks.publish(temporary.path, result.filePath)
            : SnapTray::publishFileNoReplace(temporary.path, result.filePath);
        if (published.status == SnapTray::FilePublishStatus::Published) {
            result.success = true;
            return result;
        }
        if (published.status == SnapTray::FilePublishStatus::Failed) {
            setError(&result.error, QStringLiteral("commit"), published.error);
            return result;
        }
    }
    setError(&result.error, QStringLiteral("commit"), QStringLiteral("Unable to allocate a unique filename"));
    return result;
}

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
                                          Error* error,
                                          QScreen* sourceScreen)
{
    if (pixmap.isNull()) {
        setError(error, QStringLiteral("write"), QStringLiteral("Pixmap is null"));
        return false;
    }

    Q_UNUSED(sourceScreen);

    QImage image = pixmap.toImage();
    if (!image.colorSpace().isValid()) {
        const QColorSpace sRgb(QColorSpace::SRgb);
        if (sRgb.isValid()) {
            image.setColorSpace(sRgb);
        }
    }

    return saveImageAtomically(image, filePath, explicitFormat, error);
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
