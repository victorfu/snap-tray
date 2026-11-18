#ifndef IMAGESAVEUTILS_H
#define IMAGESAVEUTILS_H

#include <QByteArray>
#include <QImage>
#include <QPixmap>
#include <QString>
#include <functional>
#include "utils/FilenameTemplateEngine.h"
#include "platform/AtomicFilePublish.h"

class QScreen;

// Encoder settings for ImageSaveUtils. The defaults leave every format at its
// own default.
struct ImageSaveEncodeOptions {
    // 0-100 for formats with a quality setting (JPEG, WebP); -1 keeps the
    // encoder default. Lossless PNG ignores it.
    int quality = -1;
};

class ImageSaveUtils
{
public:
    struct Error {
        QString message;
        QString stage; // open / format / write / commit
    };

    struct UniqueSaveSpec {
        QString outputDir;
        QString filenameTemplate;
        FilenameTemplateEngine::Context context;
    };
    struct UniqueSaveResult {
        bool success = false;
        QString filePath;
        QString renderWarning;
        Error error;
    };

    using EncodeOptions = ImageSaveEncodeOptions;

    static UniqueSaveResult saveImageUnique(const QImage& image,
                                            const UniqueSaveSpec& spec,
                                            const QByteArray& explicitFormat = {},
                                            const EncodeOptions& encodeOptions = {});

    static bool saveImageAtomically(const QImage& image,
                                    const QString& filePath,
                                    const QByteArray& explicitFormat = QByteArray(),
                                    Error* error = nullptr,
                                    const EncodeOptions& encodeOptions = {});

    // Writes an already encoded image file as it is, with the same
    // all-or-nothing replacement as saveImageAtomically(). The caller answers
    // for `data` matching the file name's format.
    static bool saveEncodedAtomically(const QByteArray& data,
                                      const QString& filePath,
                                      Error* error = nullptr);

    static bool savePixmapAtomically(const QPixmap& pixmap,
                                     const QString& filePath,
                                     const QByteArray& explicitFormat = QByteArray(),
                                     Error* error = nullptr,
                                     QScreen* sourceScreen = nullptr);

private:
    friend class tst_ImageSaveUtils;
    friend struct ImageSaveUtilsTestAccess;
    struct UniqueSaveHooks {
        std::function<SnapTray::FilePublishResult(const QString&, const QString&)> publish;
        QString uuidSuffix;
    };
    static UniqueSaveResult saveImageUniqueWithHooks(const QImage& image,
                                                     const UniqueSaveSpec& spec,
                                                     const QByteArray& explicitFormat,
                                                     const UniqueSaveHooks& hooks,
                                                     const EncodeOptions& encodeOptions = {});
    // Applies `options` to a writer of `format`; PNG keeps its lossless default.
    static void applyEncodeOptions(class QImageWriter& writer,
                                   const QByteArray& format,
                                   const EncodeOptions& options);
    static QByteArray resolveFormat(const QString& filePath,
                                    const QByteArray& explicitFormat,
                                    Error* error);
    static void setError(Error* error, const QString& stage, const QString& message);
};

#endif // IMAGESAVEUTILS_H
