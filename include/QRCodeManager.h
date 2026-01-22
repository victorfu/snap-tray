#ifndef QRCODEMANAGER_H
#define QRCODEMANAGER_H

#include <QObject>
#include <QPixmap>
#include <QString>
#include <QImage>
#include <QRect>
#include <functional>

// Decode result structure
struct QRDecodeResult {
    bool success = false;
    QString text;           // Decoded text content
    QString format;         // Barcode format (e.g., "QR_CODE", "EAN_13")
    QString error;          // Error message if failed
    QRect boundingBox;      // Barcode location in image
};

// Encoding options
struct QREncodeOptions {
    int width = 256;
    int height = 256;
    int margin = 2;         // Quiet zone size
    int eccLevel = 1;       // Error correction level (0=L, 1=M, 2=Q, 3=H)
    QColor foreground = Qt::black;
    QColor background = Qt::white;
};

// Callback type for async decoding
using QRDecodeCallback = std::function<void(const QRDecodeResult &result)>;

/**
 * @brief QR Code and barcode manager.
 *
 * Provides encoding and decoding functionality for QR codes and other barcodes
 * using ZXing-CPP library. Similar to OCRManager pattern.
 */
class QRCodeManager : public QObject
{
    Q_OBJECT

public:
    explicit QRCodeManager(QObject *parent = nullptr);
    ~QRCodeManager();

    // ========== Decoding ==========

    /**
     * @brief Decode barcode from QPixmap asynchronously
     * @param pixmap Image to scan
     * @param callback Async result callback
     */
    void decode(const QPixmap &pixmap, const QRDecodeCallback &callback);

    /**
     * @brief Decode barcode synchronously (for simple use cases)
     */
    QRDecodeResult decodeSync(const QPixmap &pixmap);

    /**
     * @brief Decode barcode from file
     */
    void decodeFromFile(const QString &filePath, const QRDecodeCallback &callback);

    // ========== Encoding ==========

    /**
     * @brief Generate QR Code image
     * @param text Text to encode
     * @param options Encoding options
     * @return Generated QR Code image, null QImage if failed
     */
    QImage encode(const QString &text, const QREncodeOptions &options = {});

    /**
     * @brief Check if text can be encoded as QR Code
     */
    static bool canEncode(const QString &text);

    /**
     * @brief Get maximum encodable character count
     */
    static int maxEncodableLength();

    // ========== Utility ==========

    /**
     * @brief Check if QR Code functionality is available
     */
    static bool isAvailable();

    /**
     * @brief Get supported barcode formats
     */
    static QStringList supportedFormats();

signals:
    void decodeComplete(const QRDecodeResult &result);
    void encodeComplete(const QImage &qrCode);
    void error(const QString &message);

private:
    class Private;
    QScopedPointer<Private> d;
};

#endif // QRCODEMANAGER_H
