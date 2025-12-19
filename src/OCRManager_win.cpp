#include "OCRManager.h"
#include <QDebug>

// Windows stub implementation
// TODO: Implement using Windows.Media.Ocr (UWP) or Tesseract

OCRManager::OCRManager(QObject *parent)
    : QObject(parent)
{
    qDebug() << "OCRManager: Windows implementation not yet available";
}

OCRManager::~OCRManager()
{
}

bool OCRManager::isAvailable()
{
    // TODO: Return true when Windows OCR is implemented
    // Windows 10+ has built-in OCR via Windows.Media.Ocr
    return false;
}

void OCRManager::recognizeText(const QPixmap &pixmap, const OCRCallback &callback)
{
    Q_UNUSED(pixmap);

    QString errorMsg = QStringLiteral("OCR is not yet available on Windows. "
                                       "This feature requires Windows.Media.Ocr or Tesseract integration.");

    if (callback) {
        callback(false, QString(), errorMsg);
    }

    emit recognitionComplete(false, QString(), errorMsg);
}
