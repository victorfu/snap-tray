#include "QRCodeManager.h"

#include <QDebug>
#include <QImage>
#include <QFile>
#include <QtConcurrent>
#include <QPointer>
#include <memory>

// ZXing-CPP headers
#include "ReadBarcode.h"
#include "MultiFormatWriter.h"
#include "BitMatrix.h"
#include "BarcodeFormat.h"
#include "ReaderOptions.h"
#include "CharacterSet.h"
#include "ImageView.h"

namespace {

// Convert QImage to ZXing ImageView using shared pointer for lifetime management
ZXing::ImageView createImageViewFromShared(const std::shared_ptr<QImage> &imagePtr)
{
    const QImage &image = *imagePtr;

    ZXing::ImageFormat format;
    if (image.format() == QImage::Format_Grayscale8) {
        format = ZXing::ImageFormat::Lum;
    } else {
        // Qt's RGB888 is actually BGR byte order
        format = ZXing::ImageFormat::BGR;
    }

    return ZXing::ImageView(
        image.constBits(),
        image.width(),
        image.height(),
        format,
        image.bytesPerLine()
    );
}

// Convert ZXing format to string
QString formatToString(ZXing::BarcodeFormat format)
{
    switch (format) {
        case ZXing::BarcodeFormat::QRCode: return "QR_CODE";
        case ZXing::BarcodeFormat::DataMatrix: return "DATA_MATRIX";
        case ZXing::BarcodeFormat::Aztec: return "AZTEC";
        case ZXing::BarcodeFormat::PDF417: return "PDF_417";
        case ZXing::BarcodeFormat::EAN8: return "EAN_8";
        case ZXing::BarcodeFormat::EAN13: return "EAN_13";
        case ZXing::BarcodeFormat::UPCA: return "UPC_A";
        case ZXing::BarcodeFormat::UPCE: return "UPC_E";
        case ZXing::BarcodeFormat::Code39: return "CODE_39";
        case ZXing::BarcodeFormat::Code93: return "CODE_93";
        case ZXing::BarcodeFormat::Code128: return "CODE_128";
        case ZXing::BarcodeFormat::ITF: return "ITF";
        case ZXing::BarcodeFormat::Codabar: return "CODABAR";
        default: return "UNKNOWN";
    }
}

// Convert ECC level to ZXing integer (for v2.2.1)
int toZXingEccLevel(int level)
{
    // ZXing v2.2.1 uses integer levels 0-8
    // Map our 0-3 range to appropriate levels
    switch (level) {
        case 0: return 1;  // Low
        case 1: return 3;  // Medium
        case 2: return 5;  // Quartile
        case 3: return 7;  // High
        default: return 3; // Medium
    }
}

int maxEncodableBytesForEccLevel(int eccLevel)
{
    // QR Code Version 40 maximum capacity in binary mode.
    switch (eccLevel) {
        case 0: return 2953; // L
        case 1: return 2331; // M
        case 2: return 1663; // Q
        case 3: return 1273; // H
        default: return 2331;
    }
}

} // anonymous namespace

class QRCodeManager::Private
{
public:
    ZXing::ReaderOptions options;

    Private()
    {
        // Configure reader options for best results
        options.setTryHarder(true);
        options.setTryRotate(true);
        options.setTryInvert(true);
        options.setTryDownscale(true);

        // Support all formats (can be restricted if needed)
        options.setFormats(ZXing::BarcodeFormat::Any);
    }
};

QRCodeManager::QRCodeManager(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
}

QRCodeManager::~QRCodeManager() = default;

bool QRCodeManager::isAvailable()
{
    return true;  // ZXing-CPP doesn't require platform-specific APIs
}

QStringList QRCodeManager::supportedFormats()
{
    return {
        "QR_CODE", "DATA_MATRIX", "AZTEC", "PDF_417",
        "EAN_8", "EAN_13", "UPC_A", "UPC_E",
        "CODE_39", "CODE_93", "CODE_128", "ITF", "CODABAR"
    };
}

void QRCodeManager::decode(const QPixmap &pixmap, const QRDecodeCallback &callback)
{
    if (pixmap.isNull()) {
        if (callback) {
            QRDecodeResult result;
            result.error = tr("Invalid image");
            callback(result);
        }
        return;
    }

    qDebug() << "QRCodeManager::decode: Starting, size:"
             << pixmap.width() << "x" << pixmap.height();

    QPointer<QRCodeManager> weakThis = this;

    // Convert to RGB888 and store in shared pointer for lifetime management
    QImage tempImage = pixmap.toImage();
    if (tempImage.format() != QImage::Format_Grayscale8 &&
        tempImage.format() != QImage::Format_RGB888) {
        tempImage = tempImage.convertToFormat(QImage::Format_RGB888);
    }

    // Create shared pointer to keep image alive throughout async operation
    auto imagePtr = std::make_shared<QImage>(tempImage);

    qDebug() << "QRCodeManager::decode: Converted to format:" << imagePtr->format()
             << "size:" << imagePtr->width() << "x" << imagePtr->height();

    ZXing::ReaderOptions options = d->options;

    // Capture shared pointer by value to extend image lifetime
    // Explicitly discard QFuture - we use callback for result delivery
    (void)QtConcurrent::run([imagePtr, options, callback, weakThis]() {
        QRDecodeResult result;

        qDebug() << "QRCodeManager: Thread started, creating ImageView...";

        try {
            auto imageView = createImageViewFromShared(imagePtr);

            qDebug() << "QRCodeManager: Calling ReadBarcode...";
            auto zxResult = ZXing::ReadBarcode(imageView, options);

            qDebug() << "QRCodeManager: ReadBarcode completed, valid:"
                     << zxResult.isValid();

            if (zxResult.isValid()) {
                result.success = true;
                result.text = QString::fromStdString(zxResult.text());
                result.format = formatToString(zxResult.format());

                // Get bounding box
                auto pos = zxResult.position();
                int minX = std::min({pos.topLeft().x, pos.topRight().x,
                                    pos.bottomLeft().x, pos.bottomRight().x});
                int minY = std::min({pos.topLeft().y, pos.topRight().y,
                                    pos.bottomLeft().y, pos.bottomRight().y});
                int maxX = std::max({pos.topLeft().x, pos.topRight().x,
                                    pos.bottomLeft().x, pos.bottomRight().x});
                int maxY = std::max({pos.topLeft().y, pos.topRight().y,
                                    pos.bottomLeft().y, pos.bottomRight().y});
                result.boundingBox = QRect(minX, minY, maxX - minX, maxY - minY);

                qDebug() << "QRCodeManager: Decoded" << result.format
                         << "content length:" << result.text.length()
                         << "bbox:" << result.boundingBox;
            } else {
                result.error = QObject::tr("No barcode found in image");
                qDebug() << "QRCodeManager: No barcode found";
            }
        } catch (const std::exception &e) {
            result.error = QString::fromStdString(e.what());
            qDebug() << "QRCodeManager: Exception:" << result.error;
        }

        // Return to main thread
        QMetaObject::invokeMethod(qApp, [callback, result, weakThis]() {
            if (callback) {
                callback(result);
            }
            if (weakThis) {
                emit weakThis->decodeComplete(result);
            }
        }, Qt::QueuedConnection);
    });
}

QRDecodeResult QRCodeManager::decodeSync(const QPixmap &pixmap)
{
    QRDecodeResult result;

    if (pixmap.isNull()) {
        result.error = tr("Invalid image");
        return result;
    }

    try {
        QImage image = pixmap.toImage();
        if (image.format() != QImage::Format_Grayscale8 &&
            image.format() != QImage::Format_RGB888) {
            image = image.convertToFormat(QImage::Format_RGB888);
        }

        // Use shared pointer for consistency with async version
        auto imagePtr = std::make_shared<QImage>(image);
        auto imageView = createImageViewFromShared(imagePtr);
        auto zxResult = ZXing::ReadBarcode(imageView, d->options);

        if (zxResult.isValid()) {
            result.success = true;
            result.text = QString::fromStdString(zxResult.text());
            result.format = formatToString(zxResult.format());

            // Get bounding box
            auto pos = zxResult.position();
            int minX = std::min({pos.topLeft().x, pos.topRight().x,
                                pos.bottomLeft().x, pos.bottomRight().x});
            int minY = std::min({pos.topLeft().y, pos.topRight().y,
                                pos.bottomLeft().y, pos.bottomRight().y});
            int maxX = std::max({pos.topLeft().x, pos.topRight().x,
                                pos.bottomLeft().x, pos.bottomRight().x});
            int maxY = std::max({pos.topLeft().y, pos.topRight().y,
                                pos.bottomLeft().y, pos.bottomRight().y});
            result.boundingBox = QRect(minX, minY, maxX - minX, maxY - minY);
        } else {
            result.error = tr("No barcode found");
        }
    } catch (const std::exception &e) {
        result.error = QString::fromStdString(e.what());
    }

    return result;
}

void QRCodeManager::decodeFromFile(const QString &filePath, const QRDecodeCallback &callback)
{
    QImage image(filePath);
    if (image.isNull()) {
        if (callback) {
            QRDecodeResult result;
            result.error = tr("Failed to load image from file");
            callback(result);
        }
        return;
    }

    decode(QPixmap::fromImage(image), callback);
}

QImage QRCodeManager::encode(const QString &text, const QREncodeOptions &options)
{
    if (text.isEmpty()) {
        qDebug() << "QRCodeManager: Cannot encode empty text";
        return QImage();
    }

    try {
        // Create MultiFormatWriter for QR Code
        ZXing::MultiFormatWriter writer(ZXing::BarcodeFormat::QRCode);
        writer.setMargin(options.margin);
        writer.setEccLevel(toZXingEccLevel(options.eccLevel));

        // Generate BitMatrix
        auto matrix = writer.encode(text.toStdString(), options.width, options.height);

        // Convert BitMatrix to QImage
        int width = matrix.width();
        int height = matrix.height();
        QImage qrImage(width, height, QImage::Format_ARGB32);
        qrImage.fill(options.background);

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (matrix.get(x, y)) {
                    qrImage.setPixelColor(x, y, options.foreground);
                }
            }
        }

        qDebug() << "QRCodeManager: Generated QR code" << width << "x" << height;
        emit encodeComplete(qrImage);
        return qrImage;

    } catch (const std::exception &e) {
        qDebug() << "QRCodeManager: Encode failed:" << e.what();
        emit error(QString::fromStdString(e.what()));
        return QImage();
    }
}

bool QRCodeManager::canEncode(const QString &text, int eccLevel)
{
    if (text.isEmpty()) {
        return false;
    }

    try {
        ZXing::MultiFormatWriter writer(ZXing::BarcodeFormat::QRCode);
        writer.setMargin(0);
        writer.setEccLevel(toZXingEccLevel(eccLevel));
        (void)writer.encode(text.toStdString(), 0, 0);
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

int QRCodeManager::maxEncodableLength(int eccLevel)
{
    return maxEncodableBytesForEccLevel(eccLevel);
}
