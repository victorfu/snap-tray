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

    static UniqueSaveResult saveImageUnique(const QImage& image,
                                            const UniqueSaveSpec& spec,
                                            const QByteArray& explicitFormat = {});

    static bool saveImageAtomically(const QImage& image,
                                    const QString& filePath,
                                    const QByteArray& explicitFormat = QByteArray(),
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
                                                     const UniqueSaveHooks& hooks);
    static QByteArray resolveFormat(const QString& filePath,
                                    const QByteArray& explicitFormat,
                                    Error* error);
    static void setError(Error* error, const QString& stage, const QString& message);
};

#endif // IMAGESAVEUTILS_H
