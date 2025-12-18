#ifndef OCRMANAGER_H
#define OCRMANAGER_H

#include <QObject>
#include <QPixmap>
#include <QString>
#include <functional>

// Callback type for async OCR results
using OCRCallback = std::function<void(bool success, const QString &text, const QString &error)>;

class OCRManager : public QObject
{
    Q_OBJECT

public:
    explicit OCRManager(QObject *parent = nullptr);
    ~OCRManager();

    // Perform OCR on the given pixmap
    // Languages: zh-Hant, zh-Hans, en-US
    // Recognition is async; result delivered via callback
    void recognizeText(const QPixmap &pixmap, const OCRCallback &callback);

    // Check if Vision Framework is available (macOS 10.15+)
    static bool isAvailable();

signals:
    void recognitionComplete(bool success, const QString &text, const QString &error);
};

#endif // OCRMANAGER_H
