#ifndef IMAGESAVEUTILS_H
#define IMAGESAVEUTILS_H

#include <QByteArray>
#include <QImage>
#include <QPixmap>
#include <QString>

class ImageSaveUtils
{
public:
    struct Error {
        QString message;
        QString stage; // open / format / write / commit
    };

    static bool saveImageAtomically(const QImage& image,
                                    const QString& filePath,
                                    const QByteArray& explicitFormat = QByteArray(),
                                    Error* error = nullptr);

    static bool savePixmapAtomically(const QPixmap& pixmap,
                                     const QString& filePath,
                                     const QByteArray& explicitFormat = QByteArray(),
                                     Error* error = nullptr);

private:
    static QByteArray resolveFormat(const QString& filePath,
                                    const QByteArray& explicitFormat,
                                    Error* error);
    static void setError(Error* error, const QString& stage, const QString& message);
};

#endif // IMAGESAVEUTILS_H
