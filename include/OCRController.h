#ifndef OCRCONTROLLER_H
#define OCRCONTROLLER_H

#include <QObject>
#include <QPixmap>
#include <QString>

#ifdef Q_OS_MACOS

class OCRManager;

/**
 * @brief Manages OCR operations for macOS.
 */
class OCRController : public QObject
{
    Q_OBJECT

public:
    explicit OCRController(QObject* parent = nullptr);
    ~OCRController();

    /**
     * @brief Check if OCR is available.
     */
    bool isAvailable() const;

    /**
     * @brief Check if OCR is currently in progress.
     */
    bool isInProgress() const { return m_inProgress; }

    /**
     * @brief Perform OCR on the given region.
     */
    void performOCR(const QPixmap& region);

    /**
     * @brief Cancel any ongoing OCR operation.
     */
    void cancel();

signals:
    void ocrStarted();
    void ocrCompleted(const QString& text);
    void ocrFailed(const QString& error);

private:
    OCRManager* m_ocrManager;
    bool m_inProgress;
};

#endif // Q_OS_MACOS

#endif // OCRCONTROLLER_H
